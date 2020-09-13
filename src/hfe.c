/*
  HFE disc support

  Based on Rev.1.1 - 06/20/2012 of the format specification.
  Documentation: https://hxc2001.com/download/floppy_drive_emulator/SDCard_HxC_Floppy_Emulator_HFE_file_format.pdf

  The design of this module is quite messy because B-EM lacks a clear
  separation between the representation of the media (i.e. the image
  file format) and the FDC implementation.  As a result, the image
  file format handlers (i.e. this file, fdi.c, sdf-acc.c) are also
  required to do things (for example recognizing address marks) that
  are actually the FDC's job.


  TO-DO list:

  Implement the functions not yet implemented (i.e., remove the calls
  to abort()).

  Discard track data when the motor "spins down" so that the emulator
  can pick up changes to the HFE file (though this may also require us
  to re-read the header as well).
*/
#undef _NDEBUG
#include <inttypes.h>
#include <limits.h>
#include <stdbool.h>

#include "b-em.h"
#include "disc.h"
#include "hfe.h"

#define UCHAR_BIT CHAR_BIT

/* HFE_FMT_MODE...: constants from the HFE file format.
 * These represent the floppy interface mode.
 */
#define HFE_FMT_MODE_HFE_FMT_IBMPC_DD_FLOPPYMODE      0x00
#define HFE_FMT_MODE_IBMPC_HD_FLOPPYMODE              0x01
#define HFE_FMT_MODE_ATARIST_DD_FLOPPYMODE            0x02
#define HFE_FMT_MODE_ATARIST_HD_FLOPPYMODE            0x03
#define HFE_FMT_MODE_AMIGA_DD_FLOPPYMODE              0x04
#define HFE_FMT_MODE_AMIGA_HD_FLOPPYMODE              0x05
#define HFE_FMT_MODE_CPC_DD_FLOPPYMODE                0x06
#define HFE_FMT_MODE_GENERIC_SHUGGART_DD_FLOPPYMODE   0x07
#define HFE_FMT_MODE_IBMPC_ED_FLOPPYMODE              0x08
#define HFE_FMT_MODE_MSX2_DD_FLOPPYMODE               0x09
#define HFE_FMT_MODE_C64_DD_FLOPPYMODE                0x0A
#define HFE_FMT_MODE_EMU_SHUGART_FLOPPYMODE           0x0B
#define HFE_FMT_MODE_S950_DD_FLOPPYMODE               0x0C
#define HFE_FMT_MODE_S950_HD_FLOPPYMODE               0x0D
#define HFE_FMT_MODE_DISABLE_FLOPPYMODE               0xFE


/* HFE_FMT_ENC...: constants from the HFE file format.
 * These represent the encoding of bits in the track.
 * In other words, FM or MFM.
 */
#define HFE_FMT_ENC_ISOIBM_MFM_ENCODING              0x00
#define HFE_FMT_ENC_AMIGA_MFM_ENCODING		     0x01
#define HFE_FMT_ENC_ISOIBM_FM_ENCODING		     0x02
#define HFE_FMT_ENC_EMU_FM_ENCODING		     0x03
#define HFE_FMT_ENC_UNKNOWN_ENCODING		     0xFF


/* HFE_OPCODE...: constants from the HFE file format.
 * These are opcodes from the HFE (v3 and on) file format.
 *
 * These masks are for the in-memory bit ordering, which for each byte
 * is the reverse of the order they are stored in the HFE file..
 */
#define HFE_OPCODE_MASK       0xF0
#define HFE_OPCODE_NOP        0xF0
#define HFE_OPCODE_SETINDEX   0xF1
#define HFE_OPCODE_SETBITRATE 0xF2
#define HFE_OPCODE_SKIPBITS   0xF3
#define HFE_OPCODE_RAND       0xF4


/* struct picfileformatheader is the in-memory representation of the
 * header of the HFE file.  It looks superficially similar to the
 * on-disc layout, but we perform the I/O byte-wise in order to avoid
 * struct packing issues.
 */
struct picfileformatheader
{
  unsigned char HEADERSIGNATURE[8];
  unsigned char formatrevision;
  unsigned char number_of_track;
  unsigned char number_of_side;
  unsigned char track_encoding;
  unsigned short bitRate;
  unsigned short floppyRPM;
  unsigned char floppyinterfacemode;
  unsigned char v1_dnu_v3_write_protected; // in v1, unused, in v3, write_protected
  unsigned short track_list_offset;
  unsigned char write_allowed;
  unsigned char single_step;
  unsigned char track0s0_altencoding;
  unsigned char track0s0_encoding;
  unsigned char track0s1_altencoding;
  unsigned char track0s1_encoding;
};

enum ReadOpType
  {
   /* No read operation is in progress. */
   OP_NOT_READING,

   /* readaddress in progress. */
   OP_READ_JUST_ADDR,

   /* readsector in progress, we're looking for the sector address. */
   OP_READ_ADDR_FOR_SECTOR,

   /* readsector in progress, we just read a matching sector address,
      hence read in sector data after we see the data address mark. */
   OP_READ_SECTOR,

   /* writesector and format are not implemented. */
  };


enum
  {
   ADDRESS_MARK_SECTOR_ID = 0xFE,
   ADDRESS_MARK_CONTROL_REC = 0xF8,
   ADDRESS_MARK_DATA_REC = 0xFB
  };

enum { SECTOR_ACCEPT_ANY = -1 };
enum { NO_TRACK = -1 };
typedef void (*scan_setup_fn)(int);

struct sector_address
{
  int track;
  int side;
  /* when specifying a sector to look for, sector is set to either the
     "wanted" value or to SECTOR_ACCEPT_ANY. */
  int sector;
};

/* hfe_poll_state contains the state information used by the state
   machine in hfe_poll.  Really a lot of this could be refactored such
   that fdi.c and hfe.c use the same implementation for decoding track
   data.  But factoring that out of fdi.c is a larger job than I want
   to take on for my first B-EM patch.
 */
struct hfe_poll_state
{
  /* current_op indicates whether or not an operation is in progress. */
  enum ReadOpType current_op;

  /* current_op_name gives a human-readable name for the current operation
     and is not owned by this struct (i.e. should not be freed when an
     instance of this struct is freed. */
  const char *current_op_name;

  /* When current_op == OP_READ_ADDR_FOR_SECTOR or OP_READ_JUST_ADDR,
     target contains the sector address we're looking for. */
  struct sector_address target;

  /* When true, motor is spun up. */
  bool motor_running;

  /* When true, mfm_mode indicates that we're doing I/O on an
     MFM-encoded track.  This affects how we decode address marks
     (e.g. because there are signalling bytes only present on MFM
     tracks) but it also affects how we interpret the stream of bits
     coming out of the HFE file (since in FM mode, alternate bits in
     the HFE file are ignored.

     mfm_mode is (or should be) set from function parameters in
     hfe_readsector, hfe_readaddress. If they were implemented, also
     hfe_format, hfe_writesector.

     Alternate bits are not significant (& are ignored) when decodng
     an FM-encoded track in HFE, but this is a property of the file
     format, not the mode of the FDC.  Therefore we deal with that in
     hfe_copy_bits(), not hfe_poll().
  */
  bool mfm_mode;

  /* our current bit position within the track data. */
  long track_bit_pos;

  /* The number of complete revolutions of the media since we started the current operation. */
  int revolutions_this_op;

  /* bits_avail_to_decode indicates how many bits in shift_register
     are available for decoding.  bits_avail_to_decode simply counts
     bits and we perform a decode when we have 16 bits (i.e. 8 data
     bits).
   */
  int bits_avail_to_decode;

  /* When bytes_to_read > 0, we read |bits_avail_to_decode| bits to
     obtain a byte.  That byte is passed to fdc_data.  When
     bytes_to_read==0, hfe_poll causes us to advance our position in
     the track data (as if the disc was spinning) but not to offer
     data to the FDC.
  */
  size_t bytes_to_read;

  /* Current CRC value.  We only update this when reading data and
     not, for example, when we're searching for a sync or an address
     mark. */
  unsigned short crc;

  /* number of bytes in the sector we are going to read */
  unsigned int sector_bytes_to_read;

  /* We shift bits read from the disk into the least significant bit
     of shift_register.  We use this to identify address marks without
     needing to know before we see one where the byte boundaries
     are. */
  uint64_t shift_register;

  /* When we shift bits off the top of shift_register, we put them in
     shift_register_prevbits.  These don't play a role in the matching
     process, but we check this to verify that the address mark
     pattern we matched was preceded by sync bytes. */
  uint64_t shift_register_prevbits;

  /* When we're looking for an address mark, scan_value contains the
     bit sequence we're searching for and scan_mask contains a bitmask
     which tells us which bits in scan_value must match. */
  uint64_t scan_value;
  uint64_t scan_mask;

};


struct hfe_info
{
  /* Data concerning the image file */
  struct picfileformatheader header;
  int hfe_version;		/* supported versions: 1, 3 */

  int current_track;
  unsigned char *track_data;
  size_t track_data_bytes;

  /* Data concerning the I/O state machine */
  struct hfe_poll_state state;
};

struct track_data_pos
{
  unsigned long pos;
  unsigned long len;
};

enum { HFE_DRIVES = 2 };
enum { OP_REV_LIMIT = 3 };
enum { SECTOR_ADDR_BYTES = 6 };

static FILE *hfe_f[HFE_DRIVES];
static struct hfe_info  *hfe_info[HFE_DRIVES];
static int hfe_selected_drive;

static unsigned short le_word(const unsigned char** pp)
{
  const unsigned char *p = *pp;
  unsigned short val = p[0] | (p[1] << 8u);
  (*pp) += 2;
  return val;
}

static struct picfileformatheader hfe_deserialize_header(const unsigned char *d)
{
  struct picfileformatheader h;
#define nextbyte() (*d++)
#define nextshort() le_word(&d)

  memcpy(h.HEADERSIGNATURE, d, sizeof(h.HEADERSIGNATURE));
  d += sizeof(h.HEADERSIGNATURE);

  /* 0x00 ... 0x07 is HEADERSIGNATURE, above. */
  /* 0x08 */ h.formatrevision = nextbyte();
  /* 0x09 */ h.number_of_track = nextbyte();
  /* 0x0A */ h.number_of_side = nextbyte();
  /* 0x0B */ h.track_encoding = nextbyte();
  /* 0x0C */ h.bitRate = nextshort();
  /* 0x0D - second byte of bitRate */
  /* 0x0E */ h.floppyRPM = nextshort();
  /* 0x0F - second byte of floppyRPM */
  /* 0x10  */ h.floppyinterfacemode = nextbyte();
  /* 0x11  */ h.v1_dnu_v3_write_protected = nextbyte();
  /* 0x12  */ h.track_list_offset = nextshort();
  /* 0x13 - second byte of track_list_offset */
  /* 0x14 */ h.write_allowed = nextbyte();
  /* 0x15 */ h.single_step = nextbyte();
  /* 0x16 */ h.track0s0_altencoding = nextbyte();
  /* 0x17 */ h.track0s0_encoding = nextbyte();
  /* 0x18 */ h.track0s1_altencoding = nextbyte();
  /* 0x19 */ h.track0s1_encoding = nextbyte();
  return h;
}

static bool hfe_decode_header(FILE *f, const char *file_name, struct hfe_info *pinfo)
{
  unsigned char buf[512];
  size_t nread;
  nread = fread(buf, 1, sizeof(buf), f);
  if (nread != sizeof(buf))
    {
      log_warn("hfe: unable to read header from HFE file '%s': %s",
	       file_name, strerror(errno));
      return false;
    }
  pinfo->header = hfe_deserialize_header(buf);
  if (0 == memcmp(pinfo->header.HEADERSIGNATURE, "HXCPICFE", 8))
    {
      pinfo->hfe_version = 1;
    }
  else if (0 == memcmp(pinfo->header.HEADERSIGNATURE, "HXCHFEV3", 8))
    {
      pinfo->hfe_version = 3;
    }
  else
    {
      log_warn("hfe: invalid/unsupported header '%*s'",
	       8, pinfo->header.HEADERSIGNATURE);
      return false;
    }
  if (pinfo->header.formatrevision != 0)
    {
      log_warn("hfe: unsupported HFE format %s, revision %d",
	       pinfo->header.HEADERSIGNATURE, pinfo->header.formatrevision);
      return false;
    }
  if (0 == pinfo->header.number_of_track)
    {
      log_warn("hfe: file has only %u tracks", pinfo->header.number_of_track);
      return false;
    }
  return true;
}

/* Reset state for a new operation.  On return, hfe_info[d].state is
   still not correctly initialised since state.target is not correctly
   set.  The caller must do that when this function returns. */
static void start_op(int drive, bool mfm, enum ReadOpType op_type, const char *op_name)
{
  struct hfe_poll_state *p = &hfe_info[drive]->state;
  if (!p->motor_running)
    {
      log_warn("hfe: drive %d: began %s with motor not running", drive, op_name);
    }
  p->current_op = op_type;
  p->current_op_name = op_name;
  p->target.sector = p->target.side = p->target.track = 0;
  p->mfm_mode = mfm;
  p->revolutions_this_op = 0;
  p->bytes_to_read = 0;
  p->bits_avail_to_decode = 0;
  p->shift_register = p->shift_register_prevbits = 0;
  p->scan_value = p->scan_mask = 0;
  hfe_info[drive]->state.revolutions_this_op = 0;
}

static void start_sector_op(int drive, bool mfm, enum ReadOpType op_type,
			    struct sector_address addr, const char *op_name,
			    scan_setup_fn scan_setup)
{
  start_op(drive, mfm, op_type, op_name);
  hfe_info[drive]->state.target = addr;
  scan_setup(drive);
}

static void clear_op_state(struct hfe_poll_state* state)
{
  state->revolutions_this_op = 0;
  state->bytes_to_read = 0;
  state->current_op = OP_NOT_READING;
  state->current_op_name = "(no operation)";
}

/* abandon current operation. */
static void abandon_op(int drive)
{
  if (hfe_info[drive])
    {
      struct hfe_poll_state* state = &hfe_info[drive]->state;
      if (state->current_op != OP_NOT_READING)
	{
	  fdc_notfound();
	}
      clear_op_state(state);
    }
}

static void abandon_op_notfound(int drive)
{
  log_warn("hfe: drive %d reporting sector not found "
	   "for track %d, sector %d (operation: %s)",
	   drive,
	   hfe_info[drive]->state.target.track,
	   hfe_info[drive]->state.target.sector,
	   hfe_info[drive]->state.current_op_name);
  abandon_op(drive);
}

static void abandon_op_badclock(int drive)
{
  log_warn("hfe: drive %d reporting bad clock data at bit position %ld of track %d",
	   drive, hfe_info[drive]->state.track_bit_pos, hfe_info[drive]->current_track);
  abandon_op(drive);
}

void hfe_init()
{
  printf("hfe: init\n");
  hfe_selected_drive = 0;
  int d;
  for (d = 0; d < HFE_DRIVES; ++d)
    {
      hfe_f[d] = NULL;
      hfe_info[d] = NULL;
    }
}


static void hfe_close(int drive)
{
  printf("hfe: drive %d: close\n", drive);
  if (hfe_f[drive])
    {
      fclose(hfe_f[drive]);
      hfe_f[drive] = NULL;
    }
  if (hfe_info[drive])
    {
      if (hfe_info[drive]->state.current_op != OP_NOT_READING)
	{
	  log_warn("hfe: drive %d: closing with in-progress operation (%s)",
		   drive, hfe_info[drive]->state.current_op_name);
	  clear_op_state(&hfe_info[drive]->state);
	}
      hfe_info[drive]->current_track = NO_TRACK;
      if (hfe_info[drive]->track_data)
	{
	  free(hfe_info[drive]->track_data);
	  hfe_info[drive]->track_data = NULL;

	}
      hfe_info[drive]->track_data_bytes = 0;
      free(hfe_info[drive]);
      hfe_info[drive] = NULL;
    }
}

static void hfe_undiagnosed_failure(int drive)
{
  /* Our caller wasn't able to report the nature of the failure to its caller. */
  log_error("hfe: failed on drive %d, FDC may now be in an incorrect state",
	    drive);
  disc_abort(drive);
}


/* read_at_pos reads |len| bytes from position |pos| in file |f|.
 *
 * If there is an error, false is returned and |*err| is set to the errno value.
 * If there is a short read, false is returnes and |*err| is 0.
 *   In the short-read case there is no way for the caller to tell how many
 *   bytes at |buf| are valid.
 * If there is no error, true is returned.
 */
static bool hfe_read_at_pos(FILE *f, size_t pos, size_t len, void *buf, int *err)
{
  size_t bytes_read;
  if (0 != fseek(f, pos, SEEK_SET))
    {
      *err = errno;
      return false;
    }
  bytes_read = fread(buf, 1, len, f);
  if (bytes_read != len)
    {
      *err = 0;
      return false;
    }
  return true;
}


static bool decode_lut_entry(const unsigned char *p, struct track_data_pos* out, int *err)
{
  enum
  {
   offset_unit_size = 512
  };
  const unsigned long track_offset = le_word(&p); /* units: 512-byte blocks */
  unsigned long track_len = le_word(&p);	  /* units: bytes */
  if (track_len & 0x1FF)
    {
      const unsigned long bump = 0x200uL;
      unsigned long tl = track_len & (~0x1FFu);
      if (ULONG_MAX - bump < track_len)
	{
	  log_error("hfe: track length %lu is too large", track_len);
	  *err = EOVERFLOW;
	  return false;
	}
      track_len = tl + bump;
    }
  if (ULONG_MAX / offset_unit_size < track_offset)
    {
      log_error("hfe: track offset %lu is too large", track_offset);
      *err = EOVERFLOW;
      return false;
    }
  out->pos = track_offset * offset_unit_size;
  out->len = track_len;
  return true;
}

static bool hfe_locate_track_data(int drive, int track, struct track_data_pos* where, int *err)
{
  unsigned char lutbuf[4];
  const long pos = 512 + track * 4L; /* XXX: should use header.track_list_offset? */
  log_debug("hfe: LUT entry for track %d is at offset %lu", track, pos);
  if (!hfe_read_at_pos(hfe_f[drive], pos, sizeof(lutbuf), lutbuf, err))
    return false;
  if (!decode_lut_entry(lutbuf, where, err))
    return false;
  log_debug("hfe: LUT track data for track %d tells is the track occupies %lu bytes at %lu", track, where->len, where->pos);
  return true;
}

static void hfe_reverse_bit_order(unsigned char *buf, size_t len)
{
  size_t i;
  for (i = 0; i < len; ++i)
    {
      const unsigned char in = buf[i];
      int out = 0;
      if (in & 0x80)  out |= 0x01;
      if (in & 0x40)  out |= 0x02;
      if (in & 0x20)  out |= 0x04;
      if (in & 0x10)  out |= 0x08;
      if (in & 0x08)  out |= 0x10;
      if (in & 0x04)  out |= 0x20;
      if (in & 0x02)  out |= 0x40;
      if (in & 0x01)  out |= 0x80;
      buf[i] = out;
    }
}


void hfe_warn_of_premature_stream_end(const char* opcode)
{
  log_warn("hfe: track data stream ends in the middle of an HFE v3 %s instruction", opcode);
}

static bool hfe_is_hfe3_opcode(unsigned char val)
{
  return (val & HFE_OPCODE_MASK) == HFE_OPCODE_MASK;
}

unsigned char hfe_random_byte(void)
{
  return (rand() >> 5) & 0xFF;
}

static size_t hfe_copy_bits(int version, int encoding,
			    int drive, int track,
			    const unsigned char *src, size_t in_bytes,
			    unsigned char *dest)
{
  /* I don't know how HFE_FMT_ENC_EMU_FM_ENCODING differs from
     HFE_FMT_ENC_ISOIBM_FM_ENCODING */
  int take_this_bit = (encoding != HFE_FMT_ENC_ISOIBM_MFM_ENCODING);
  int got_bits = 0;
  unsigned char out = 0;
  bool hfe3 = version == 3;
  size_t in_offset = 0;
  size_t out_bytes = 0;

  while (in_offset < in_bytes)
    {
      int skipbits = 0;
      unsigned char in = src[in_offset++];
      if (hfe3 && hfe_is_hfe3_opcode(in))
	{
	  switch (in)
	    {
	    case HFE_OPCODE_NOP:
	      continue;		// just consume the opcode.

	    case HFE_OPCODE_SETINDEX:
	      /* For now, we ignore this (i.e. we consume the opcode
		 but do nothing about it).

		 It's not clear how we would need to use it.  I don't
		 know what the interface to the rest of B-EM would be
		 used to report this information anyway.

		 In a physical floppy, detection of the index mark
		 tells us we've seen the whole track (and e.g. allows
		 us to know when to give up searching for a sector in
		 the track data.  But we have a finite amount of input
		 data anyway, so we won't loop forever even if we
		 don't know where in the bitsteam the index mark is.
	      */
	      continue;

	    case HFE_OPCODE_SETBITRATE:
	      /* We only care about the sector contents, so ignore the
		 change in bit rate. */
	      if (in_offset >= in_bytes)
		{
		  hfe_warn_of_premature_stream_end("SETBITRATE");
		  continue;
		}
	      ++in_offset;	/* consume the bit-rate byte */
	      continue;

	    case HFE_OPCODE_SKIPBITS:
	      {
		if (in_offset >= in_bytes)
		  {
		    hfe_warn_of_premature_stream_end("SKIPBITS");
		    continue;
		  }
		skipbits = src[in_offset++];
		if (in_offset >= in_bytes)
		  {
		    hfe_warn_of_premature_stream_end("SKIPBITS");
		    continue;
		  }
		in = src[in_offset++];	/* the byte in which to skip some bits. */
	      }
	      break;

	    case HFE_OPCODE_RAND:
	      in = hfe_random_byte();
	      break;

	    default:
	      {
		log_warn("hfe: drive %d track %d contains an invalid HFE3 opcode 0x%X; returning a short track",
			 drive, track, (unsigned int)in);
		return out_bytes;
	      }
	    }
	}

      for (int bitnum = 0; bitnum < 8; ++bitnum)
	{
	  if (skipbits > 0)
	    {
	      --skipbits;
	      continue;
	    }
	  if (take_this_bit)
	    {
	      int mask = (1 << (7-bitnum));
	      const int bit = in & mask ? 0x80 : 0;
	      /* the output bit might be a clock bit or it might be
		 data, we worry about that separately. */
	      out = (out >> 1 ) | bit;
	      ++got_bits;
	    }
	  if (encoding == HFE_FMT_ENC_ISOIBM_FM_ENCODING)
	    {
	      take_this_bit = !take_this_bit;
	    }
	}
      if (8 == got_bits)
	{
	  *dest++ = out;
	  ++out_bytes;
	  out = 0;
	  got_bits = 0;
	}
    }
  return out_bytes;
}

unsigned char encoding_of_track(int drive, int side, int track)
{
  const struct picfileformatheader *h = &hfe_info[drive]->header;
  if (track == 0)
    {
      if (side == 0)
	{
	  if (h->track0s0_altencoding == 0)
	    return h->track0s0_encoding;
	}
      else
	{
	  if (h->track0s1_altencoding == 0)
	    return h->track0s1_encoding;
	}
    }
  return h->track_encoding;
}

static bool hfe_read_track_data(int drive, int track, int side, unsigned long pos, unsigned long len,
				unsigned char **result, size_t *bytes_read, int *err)
{
  /* The track data consists of a 256-byte block of data for side 0
     followed by a 256-byte block of data for side 1.  Then, another
     256 byte block of data for side 0, and one for side 1, and so
     on. */
  enum { side_block_size = 256 };
  const unsigned char encoding = encoding_of_track(drive, side, track);
  unsigned long begin;
  unsigned char *out, *in;
  in = malloc(len);
  if (!in)
    return false;
  out = malloc(len / 2);
  if (!out)
    {
      free(in);
      return false;
    }
  if (!hfe_read_at_pos(hfe_f[drive], pos, len, in, err))
    {
      if (!err)
	log_error("hfe: short read on track data for drive %d track %d", drive, track);
      return false;
    }
  hfe_reverse_bit_order(in, len);
  *bytes_read = 0;
  for (begin = (side ? side_block_size : 0);
       begin < len;
       begin += (side_block_size * 2))
    {
      (*bytes_read) += hfe_copy_bits(hfe_info[drive]->hfe_version,
				     encoding, drive, track,
				     in + begin, 256, out);
    }
  *result = out;
  return true;
}


static void hfe_track_load_failed(int drive, int track, int err)
{
  if (err)
    log_error("hfe: failed to load track data for drive %d track %d: %s", drive, track, strerror(err));
  else
    log_error("hfe: failed to load track data for drive %d track %d", drive, track);
}

static void hfe_seek(int drive, int track)
{
  int err = 0;
  const int side = 0;		/* XXX: how are sides selected? */
  struct track_data_pos where;
  unsigned char *trackbits = NULL;
  size_t track_len = 0;

  printf("hfe: drive %d seek to track %d\n", drive, track);
  if (NULL == hfe_f[drive])
    {
      log_warn("hfe: seek on unoccupied drive %d track %d", drive, track);
      return;
    }
  if (track < 0)
    {
      log_warn("hfe: seek on drive %d to negative track %d", drive, track);
      track = 0;
    }
  else if (track >= hfe_info[drive]->header.number_of_track)
    {
      log_warn("hfe: seek on drive %d to track %d, but file only has %d tracks",
	       drive, track, hfe_info[drive]->header.number_of_track);
      /* We checked when we parsed the header that number_of_track > 0 */
      track = hfe_info[drive]->header.number_of_track-1;
    }

  log_debug("hfe: drive %d: seek to track %d", drive, track);
  if (!hfe_locate_track_data(drive, track, &where, &err))
    {
      hfe_track_load_failed(drive, track, err);
      hfe_undiagnosed_failure(drive);
      return;
    }
  log_debug("hfe: drive %d: track %d data: %lu bytes at %lu\n", drive, track, where.len, where.pos);
  if (!hfe_read_track_data(drive, track, side, where.pos, where.len,
			   &trackbits, &track_len, &err))
    {
      hfe_track_load_failed(drive, track, err);
      hfe_undiagnosed_failure(drive);
      return;
    }
  if (hfe_info[drive]->track_data)
    {
      free(hfe_info[drive]->track_data);
      hfe_info[drive]->track_data = NULL;
    }

  hfe_info[drive]->current_track = track;
  hfe_info[drive]->track_data = trackbits;
  hfe_info[drive]->track_data_bytes = track_len;
  printf("hfe: loaded %lu bytes of data for drive %d track %d at %p\n",
	 hfe_info[drive]->track_data_bytes,
	 drive,
	 track,
	 hfe_info[drive]->track_data);
}

static void set_up_for_sector_read(int drive, size_t sector_bytes_to_read)
{
  struct hfe_poll_state *state = &hfe_info[drive]->state;

  /* Whether we're in FM or MFM mode, we want the bottom 16 bits of
     our matcher to match the sector address mark.  The address mark
     takes one of two values, depending on the type of the record.

     0xF8: control record (ADDRESS_MARK_CONTROL_REC)
     0xFB: data record (clocked ADDRESS_MARK_DATA_REC)

     After our scanner matches, we need to pull the actual address
     mark value out of the bottom of state->shift_register.
   */
  if (state->mfm_mode)
    {
      /* Look for the bytes leading up to an address mark, followed by
	 the address mark itself: three bytes of (data=0xA1,
	 clock=0x0A) followed by the address mark byte (0xF8 or 0xFB).

	 When the address mark is 0xFB the clocked value will be 0x554A.
	 When the address mark is 0xF8 the clocked value will be 0x5545.
	 The first clock bit is always 0 because the first data bit is
	 1 in either case.

	 Observing that 0x554A & 0x5545 == 0x5540, we put 0x5540 in
	 the bottom 16 bits of the scan value, with zeroes for the
	 least significant four bits of the mask to ensure we accept
	 either 0x554A or 0x5545.
      */
      state->scan_value = 0x4489448944895540;
      state->scan_mask  = 0xFFFFFFFFFFFFFFF0;
      /* Since all three A1 bytes are matched in the topmost bits of
	 state->scan_value, we expect the previous 2 sync bytes (which
	 have the data value 0, so clock bits are 1) to appear in the
	 bottom 32 bits of state->shift_register_prevbits as
	 0xAAAAAAAA.
      */
    }
  else
    {
      /* We're searching for two bytes (though usually there are more)
	 of FM-encoded 0x00 followed by the address mark.  The address
	 mark is abnormally clocked to distinguish it from data.

	 0xF8 -> 0xF56A
	 0xFB -> 0xF56F

	 But, (0xF56A & 0xF56F) == 0xF56A, so we scan for that.
	 check what we got.  0xA == binary 1010
      */
      state->scan_value = 0x0000AAAAAAAAF56A;
      state->scan_mask  = 0x0000FFFFFFFFFFFA;

      /* Our two required sync bytes are matched in
	 scan_value/scan_mask so there is no need to check
	 state->shift_register_prevbits for FM-encoded tracks. */
    }
  state->bytes_to_read = sector_bytes_to_read;
}

static void set_up_for_sector_id_scan(int drive)
{
  struct hfe_poll_state *state = &hfe_info[drive]->state;
  /* Whether we're in FM or MFM mode, we want the bottom 16 bits of
     our matcher to match the sector ID address mark (0xFE,
     ADDRESS_MARK_SECTOR_ID).

     That should be preceded by tyo bytes of sync data (with clocks,
     that would make 32 bits).  Hence those make 0xAAAAAAAA (the clock
     bits being 1 in MFM mode because the data bits are zero, and 1 in
     FM mode because the clocks are always 1 in FM mode).
   */
  if (state->mfm_mode)
    {
      /* Three bytes of (data=0xA1, clock=0x0A) -> 0x4489
	 Followed by the sector ID address mark (data=0xFE)

	 Because 0xFE only has one zero bit, all the MFM clock bits
	 for it will be zero (to get a 1 clock bit, you have to have
	 two zero data bits in a row).

	 0xF == binary 1111 (clocked: c1c1c1c1)
	 0xE == binary 1110 (clocked: c1c1c1c0)

	 Bits:   cDcD cDcD cDcD cDcD
	 Values: 0101 0101 0101 0100
	 Hex:       5    5    5    4

	 The clocked value of 0xFE is 0x5554.
       */
      state->scan_value = 0x4489448944895554;
      state->scan_mask  = 0xFFFFFFFFFFFFFFFF;
    }
  else
    {
      /*
	 The FM-encoded sequence bit sequence 1111010101111110 has the
	 hex value 0xF57E.  Dividing it into 4 nibbles we can visualise
	 the clock and data bits:

	 Hex  binary  clock  data
	 F    1111     11..   11..
	 5    0101     ..00   ..11

	 that is, for the top nibbles we have clock 1100=0xC and data
	 1111=0xF.

	 Hex  binary  clock  data
	 7    0111     01..   11..
	 E    1110     ..11   ..10

	 that is, for the bottom nibbles we have clock 0111=0x7 and
	 data 1110=0xE.

	 Putting together the nibbles we have data 0xC7, clock 0xFE.
	 That is address mark 1, which introduces the sector ID.
	 Address marks are unusual in that the clock bits are not all
	 set to 1.

	 The address mark is preceded by at least two FM-encoded
	 zero bytes; 0x00 encodes to 0xAAAA.

	 When we recognise this pattern we will have to "back up" by
	 16 bits in order to re-read them as the first byte of the
	 sector id (we don't do a similar thing for MFM as the need to
	 match the three A1 bytes uses up 48 bits of our 64 bit scan
	 capacity).
      */
      state->scan_value = 0xAAAAAAAAF57E;
      state->scan_mask  = 0xFFFFFFFFFFFF;
    }
  state->bytes_to_read = SECTOR_ADDR_BYTES;
  state->bits_avail_to_decode = 0;
}

static void hfe_readsector(int drive, int sector, int track, int side, int density)
{
  printf("hfe: drive %d: readsector track %d side %d sector %d (%s)\n",
	 drive, track, side, sector, (density ? "MFM" : "FM"));
  struct sector_address addr;
  addr.track = track;
  addr.side = side;
  addr.sector = sector;
  start_sector_op(drive, density, OP_READ_ADDR_FOR_SECTOR, addr, "readsector",
  set_up_for_sector_id_scan);
}

static void hfe_writesector(int drive, int sector, int track, int side, int density)
{
  printf("hfe: drive %d: writesector track %d side %d sector %d (%s)\n",
	 drive, track, side, sector, (density ? "MFM" : "FM"));
  /* TODO: implement me. */
  abort();
}

static void hfe_readaddress(int drive, int track, int side, int density)
{
  printf("hfe: drive %d: readaddress track %d side %d (%s)\n",
	 drive, track, side, (density ? "MFM" : "FM"));
  struct sector_address addr;
  addr.track = track;
  addr.side = side;
  addr.sector = SECTOR_ACCEPT_ANY;
  start_sector_op(drive, density, OP_READ_JUST_ADDR, addr, "readaddress",
  set_up_for_sector_id_scan);
}

unsigned long crc_cycle(unsigned long crc)
{
  /* Our polynomial is 0x10000 + (0x810<<1) + 1 = 0x11021 This CRC
     is known as CRC16-CCITT.

     When computing CRCs for use in reading disc tracks
     (i.e. CCITT_CRC16), we initialise the cec_ state to 0xFFFF, as
     required for CRC16-CCIT.

     Some implementations instead initialise to 0xCDB4, but that's
     just because the MFM track format's A1 bytes are not being fed to
     the CRC, so they initialise the CRC calculation as if this had
     already been done. */
  if (crc & 32768)
    return  (((crc ^ 0x0810) & 32767) << 1) + 1;
  else
    return crc << 1;
}

static void crc_byte(struct hfe_poll_state* state, unsigned char value)
{
  state->crc ^= value << 8;
  for(int k = 0; k < 8; k++)
    state->crc = crc_cycle(state->crc);
}

static void crc_reset(int drive)
{
  /* CCITT CRC-16 is initialised with 0xFFFF. */
  hfe_info[drive]->state.crc = 0xFFFF;
}

static void handle_sector_id_byte(int drive, unsigned char value)
{
  struct hfe_poll_state *state = &hfe_info[drive]->state;
  log_warn("hfe: drive %d: handling sector ID byte with bytes_to_read=%lu "
	   "and current_op_name=%s\n",
	   drive, (unsigned long)state->bytes_to_read, state->current_op_name);
  crc_byte(state, value);
  const size_t sector_address_bytes_read = SECTOR_ADDR_BYTES - state->bytes_to_read;
  switch (sector_address_bytes_read)
    {
    case 0:		       /* address mark */
      if (value != 0xFE)
	{
	  /* unexpected address mark. */
	  log_warn("hfe: unexpected sector ID address mark %u\n", (unsigned int)value);
	  set_up_for_sector_id_scan(drive);
	  return;
	}
      printf("hfe: drive %d: read address mark for sector id", drive);
      return;

    case 1: 		       /* track (cylinder) */
      printf("hfe: drive %d: sector id: track is %d\n", drive, value);
      if (value != state->target.track)
	{
	  log_warn("hfe: found sector address for track %u when looking for track %d",
		   (unsigned int)value, state->target.track);
	  set_up_for_sector_id_scan(drive);
	}
      return;

    case 2:		       /* side (head) */
      printf("hfe: drive %d: sector id: side is %d\n", drive, value);
      if (value != state->target.track)
	{
	  log_warn("hfe: found sector address for side %u when looking for side %d",
		   (unsigned int)value, state->target.side);
	  set_up_for_sector_id_scan(drive);
	}
      return;

    case 3:		       /* sector (record) */
      printf("hfe: drive %d: sector id: sector is %d\n", drive, value);
      if (state->target.sector == SECTOR_ACCEPT_ANY ||
	  state->target.sector == value)
	{
	  /* OK, but we defer setting up for actually reading the
	     sector until we have read and verified the CRC.  That
	     also ensures that we will have read the size code,
	     too. */
	}
      else
	{
	  /* This also is normal, but the next sector is not the one
	     we want. */
	  set_up_for_sector_id_scan(drive);
	}
      return;

    case 4:		       /* size code */
      /* The extra 2 bytes here are the CRC. */
      state->sector_bytes_to_read = 2u + (1u << (value + 7u));
      printf("hfe: drive %d: sector id: size code is %d (%lu bytes)\n",
	     drive, value, (unsigned long)(state->sector_bytes_to_read - 2u));
      break;

    case 5:		       /* first CRC byte */
      printf("hfe: drive %d: sector id: first CRC byte is 0x%02X\n",
	     drive, (unsigned int)value);
      break;

    case 6:		       /* second CRC byte */
      printf("hfe: drive %d: sector id: second CRC byte is 0x%02X\n",
	     drive, (unsigned int)value);
      if (state->crc)
	{
	  /* CRC mismatch. */
	  log_warn("hfe: CRC mismatch; CRC found 0x%04X, expected 0x00",
		   (unsigned int)state->crc);
	  set_up_for_sector_id_scan(drive);
	}
      if (state->current_op == OP_READ_ADDR_FOR_SECTOR)
	{
	  /* CRC was correct, and the sector address matched; set up to read the sector. */
	  state->current_op = OP_READ_SECTOR;
	  set_up_for_sector_read(drive, state->sector_bytes_to_read);
	}
      break;

    default:
      printf("hfe: drive %d: sector id: unexpected byte position %lu\n",
	     drive, (unsigned long)sector_address_bytes_read);
      break;
    }
}


static void handle_sector_data_byte(int drive, unsigned char value)
{
  // struct hfe_poll_state *state = &hfe_info[drive].state;
  abort();
}

static bool get_next_bit(int drive, bool *end)
{
  struct hfe_poll_state *state = &hfe_info[drive]->state;
  const unsigned char *trackdata = hfe_info[drive]->track_data;
  const int mask_for_this_bit = 1 << (state->track_bit_pos % UCHAR_BIT);
  const long byte_offset = state->track_bit_pos / UCHAR_BIT;
  const int this_bit = (trackdata[byte_offset] & mask_for_this_bit) ? 1 : 0;
  ++state->track_bit_pos;
  /* Check if we reached the end of the track data. */
  if ((state->track_bit_pos)/UCHAR_BIT >= hfe_info[drive]->track_data_bytes)
    {
      *end = true;
      state->track_bit_pos = 0; /* start again at the beginning. */
    }
  return this_bit;
}

static void handle_badclock(int drive)
{
  struct hfe_poll_state *state = &hfe_info[drive]->state;
  switch (state->current_op)
    {
    case OP_NOT_READING:
      log_error("hfe: drive %d: reported bad clock, but no operation is in progress",
		drive);
      return;

    case OP_READ_JUST_ADDR:
      fdc_finishread();
      break;

    case OP_READ_ADDR_FOR_SECTOR:
    case OP_READ_SECTOR:
      fdc_headercrcerror();
      break;
    }
  abandon_op_badclock(drive);
}

static void hfe_poll_drive(int drive, bool is_selected)
{
  if (NULL == hfe_info[drive] || NULL == hfe_info[drive]->track_data)
    {
      return;
    }
  struct hfe_poll_state *state = &hfe_info[drive]->state;

  /* Valid states for the state machine:
     Idle (current_op == OP_NOT_READING)
     Scanning for an address mark (scan_mask != 0)
     Reading clocked data (bytes_to_read > 0)
  */
  assert(state->current_op == OP_NOT_READING ||
	 state->scan_mask != 0 ||
	 state->bytes_to_read > 0);

  if (!state->motor_running)
    {
      assert(state->current_op == OP_NOT_READING);
      return;
    }

  /* The motor can still be running if there is no operation in
     progress, because an operation completed (or failed) but the motor
     didn't get turned off yet.  Either way, we advance our bit position
     over time, as if there were a physical floppy disc rotating past a
     physical read head. */

  bool end = false;
  const int this_bit = get_next_bit(drive, &end);
  if (!is_selected)
    {
      return;
    }
  if (end)
    {
      /* the 'disc' completed a revolution */
      if (state->current_op == OP_NOT_READING)
	{
	  log_warn("hfe: drive %d has completed a full revolution "
		   "(the motor is still on despite there being no "
		   "current I/O operation)",
		   drive);
	}
      else
	{
	  log_warn("hfe: drive %d has completed a full revolution "
		   "(%d revs so far for this operation, which is %s)",
		   drive, state->revolutions_this_op, state->current_op_name);
	  /* A read operation is still in progress when we reached the
	     end of the track.

	     Perhaps we should clear state->bits_avail_to_decode also.  If
	     we could be confident that the floppy index hole coincides
	     with the beginning of the HFE track data, this would
	     certainly be the right thing to do.
	  */
	  if (++state->revolutions_this_op == OP_REV_LIMIT)
	    {
	      /* We failed to find the data we wanted (e.g. we're
		 reading an unformatted track). */
	      abandon_op_notfound(drive);
	      return;
	    }
	}
      if (state->current_op != OP_NOT_READING)
	{
	}
    }

  /* We're going to shift the top bit of state->shift_register out;
     put it into the bottom bit of shift_register_prevbits. */
  const int doomed_shift_bit = state->shift_register & (1LL << 63);
  state->shift_register_prevbits <<= 1;
  state->shift_register_prevbits |= doomed_shift_bit;

  state->shift_register <<= 1;
  state->shift_register |= this_bit;

  if (state->current_op == OP_NOT_READING)
    {
      return;
    }

  if (state->scan_mask)
    {
      /* If we're scanning for an address mark, we must want to read
	 some data once we've found the address mark. */
      assert(state->bytes_to_read > 0);

      /* We're looking for an address mark. */
      if ((state->shift_register & state->scan_mask) != state->scan_value)
	{
	  /* Not found yet, so keep searching.  In particular, don't
	     start shifting the bits we read into data bytes, since
	     the (emulated) disk head is not reading data bits yet. */
	  return;
	}
      printf("hfe: scanner matched: found %08" PRIx64
	     "which matches value %08" PRIx64
	     "with mask %08" PRIx64 "\n",
	     state->shift_register, state->scan_value, state->scan_mask);
      crc_reset(drive);
      if (state->mfm_mode)
	{
	  /* The fact that the shift register matched tells us
	     that we just read the A1 address mark intro bytes.
	     The CRC is computed over the A1 bytes and the rest of
	     the sector address. */
	  crc_byte(state, 0xA1);
	  crc_byte(state, 0xA1);
	  crc_byte(state, 0xA1);
	}

      /* start decoding data (either the sector ID the sector (record)
	 data itself). */
      state->bits_avail_to_decode = 16;
      state->scan_mask = 0;
      /* We continue on to the de-clocking code, because in all cases
	 we have in the bottom 16 bits of shift_register the encoded
	 address mark itself. */
    }

  if (state->bits_avail_to_decode++ < 16)
    return;

  /*
     An FM or MFM encoded byte occupies 16 bits on the disc, and looks
     like this (in the order bits appear on disc):

     first       last
     cDcDcDcDcDcDcDcD (c are clock bits, D data)

     These will be shifted into the bottom 16 bits of shift_register;
     the least significant bit will therefore be the least significant
     data bit.

     In FM, all clock bits in data should be 1.  In MFM, clock bits
     depend on the values of the neighbouting data bits as follows:

     Prev           Next
     Data | Clock | Data
     -----+-------+-----
        0 |     1 |    0
        0 |     0 |    1
        1 |     0 |    0
        1 |     0 |    1

     For FM the clock bits are "wrong" to identify address marks (so
     that we can distinguish address marks from data).  We only read
     the "wrongly clocked" bit sequences using the shift register, so
     if we get to here we know the clock bits are supposed to be
     correct.

     The bottom 16 bits of shift_register encode 8 bits, so
     shift_register & (1 << 15) is the first clock bit.  Hence
     shift_register & (1 << 16) is the previous data bit.
  */
  unsigned int value = 0;
  unsigned int prev_data_bit = (state->shift_register & (1u << 16)) >> 16;
  unsigned int mask = 1 << 15;
  assert (state->bits_avail_to_decode == 16);
  while (state->bits_avail_to_decode--)
    {
      const bool clock = state->shift_register & mask;
      mask >>= 1;
      const bool data = state->shift_register & mask;
      mask >>= 1;
      /* The clock bit in MFM encoding is data-dependent, but the
	 clock bit in FM is always 1. */
      const bool mfm_expected_clock = (prev_data_bit || data) ? 0 : 1;
      if (clock != (state->mfm_mode ? mfm_expected_clock : true))
	{
	  handle_badclock(drive);
	  return;
	}
      value <<= 1;
      if (data)
	{
	  value |= 1;
	}
    }

  /* If the current op is readaddr or readsector we need to pass the
   * byte we read to fdc_read().  Otherwise (OP_READ_ADDR_FOR_SECTOR)
   * we just try to match the address we're reading against the
   * address we're looking for */
  switch (state->current_op)
    {
    case OP_READ_ADDR_FOR_SECTOR:
    case OP_READ_JUST_ADDR:
      handle_sector_id_byte(drive, value);
      break;
    case OP_READ_SECTOR:
      handle_sector_data_byte(drive, value);
      break;
    case OP_NOT_READING:
      assert(state->current_op != OP_NOT_READING);
      break;
    }
}

static void hfe_poll(void)
{
  int d;
  for (d = 0; d < HFE_DRIVES; ++d)
    {
      const bool is_selected = d==hfe_selected_drive;
      hfe_poll_drive(d, is_selected);
    }
}

static void hfe_format(int drive, int track, int side, int density)
{
  /* TODO: implement me. */
  abort();
}

static void hfe_abort(int drive)
{
  printf("hfe: drive %d: abort\n", drive);
  if (hfe_info[drive])
    {
      clear_op_state(&hfe_info[drive]->state);
    }
}

static void init_hfe_poll_state(struct hfe_poll_state *p)
{
  p->current_op = OP_NOT_READING;
  p->motor_running = false;
  p->current_op_name = "(initialized, no current operation)";
  p->target.sector = p->target.track = p->target.side = 0;
  p->mfm_mode = false;
  p->revolutions_this_op = 0;
  p->track_bit_pos = 0;
  p->bits_avail_to_decode = 0;
  p->bytes_to_read = 0;
  /* When we start using the CRC computation, we initialise it to some
     other value, but that happens elsewhere. */
  p->crc = 0;
  p->shift_register = 0;
  p->scan_value = p->scan_mask = 0;
}

static void init_hfe_info(struct hfe_info *p)
{
  p->current_track = NO_TRACK;
  p->track_data = NULL;
  p->track_data_bytes = 0;
  init_hfe_poll_state(&p->state);
}

static void hfe_spinup(int drive)
{
  printf("hfe: drive %d: spin up\n", drive);
  if (hfe_info[drive]->state.motor_running)
    {
      log_warn("hfe: drive %d: spin up with motor already running", drive);
    }
  hfe_info[drive]->state.motor_running = true;
}

static void hfe_spindown(int drive)
{
  printf("hfe: drive %d: spin down\n", drive);
  if (!hfe_info[drive]->state.motor_running)
    {
      log_warn("hfe: drive %d: spin down with motor already off", drive);
    }
  if (hfe_info[drive]->state.current_op != OP_NOT_READING)
    {
      log_warn("hfe: drive %d: spin down with operation %s still in progress",
	       drive, hfe_info[drive]->state.current_op_name);
      abandon_op(drive);	/* will call fdc_notfound(). */
    }
  hfe_info[drive]->state.motor_running = false;
}

void hfe_load(int drive, const char *fn)
{
  printf("hfe: drive %d: loading file %s\n", drive, fn);
  writeprot[drive] = fwriteprot[drive] = 1;
  hfe_f[drive] = fopen(fn, "rb");
  if (!hfe_f[drive])
    {
      log_warn("hfe: unable to open HFE disc image '%s': %s", fn, strerror(errno));
      return;
    }

  free(hfe_info[drive]);

  hfe_info[drive] = malloc(sizeof(*hfe_info[drive]));
  init_hfe_info(hfe_info[drive]);

  if (!hfe_decode_header(hfe_f[drive], fn, hfe_info[drive]))
    {
      log_warn("hfe: HFE disc image '%s' has an invalid header", fn);
      /* unwind the initialization. */
      free(hfe_info[drive]);
      hfe_info[drive] = NULL;
      fclose(hfe_f[drive]);
      hfe_f[drive] = NULL;
      return;
    }
  drives[drive].close       = hfe_close;
  drives[drive].seek        = hfe_seek;
  drives[drive].readsector  = hfe_readsector;
  drives[drive].writesector = hfe_writesector;
  drives[drive].readaddress = hfe_readaddress;
  drives[drive].poll        = hfe_poll;
  drives[drive].format      = hfe_format;
  drives[drive].abort       = hfe_abort;
  drives[drive].spinup      = hfe_spinup;
  drives[drive].spindown    = hfe_spindown;
}
