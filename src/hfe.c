/*
  HFE disc support

  Based on Rev.1.1 - 06/20/2012 of the format specification.
  Documentation: https://hxc2001.com/download/floppy_drive_emulator/SDCard_HxC_Floppy_Emulator_HFE_file_format.pdf
*/
#include <limits.h>
#include <stdbool.h>

#include "b-em.h"
#include "disc.h"
#include "hfe.h"

struct hfe_info;

static FILE *hfe_f[2];
static struct hfe_info  *hfe_info[2];

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

struct hfe_info
{
  struct picfileformatheader header;
  int hfe_version;		/* supported versions: 1, 3 */
  unsigned char *track_data;
  size_t track_data_bytes;
};

struct track_data_pos
{
  unsigned long pos;
  unsigned long len;
};

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

void hfe_init()
{
  hfe_f[0]  = hfe_f[1]  = NULL;
  hfe_info[0] = hfe_info[1] = NULL;
}


static void hfe_close(int drive)
{
  /* TODO: implement me. */
  abort();
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
      const unsigned char in = buf[len];
      int out = 0;
      if (in & 0x80)  out |= 0x01;
      if (in & 0x40)  out |= 0x02;
      if (in & 0x20)  out |= 0x04;
      if (in & 0x10)  out |= 0x08;
      if (in & 0x08)  out |= 0x10;
      if (in & 0x04)  out |= 0x20;
      if (in & 0x02)  out |= 0x40;
      if (in & 0x01)  out |= 0x80;
      buf[len] = out;
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
  for (begin = (side ? 0 : side_block_size);
       begin < len;
       begin += (side_block_size * 2))
    {
      (*bytes_read) += hfe_copy_bits(hfe_info[drive]->hfe_version,
				     encoding, drive, track,
				     in + begin, 256, out);
    }
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
  if (!hfe_read_track_data(drive, track, side, where.pos, where.len,
			   &trackbits, &track_len, &err))
    {
      hfe_track_load_failed(drive, track, err);
      hfe_undiagnosed_failure(drive);
      return;
    }
}

static void hfe_readsector(int drive, int sector, int track, int side, int density)
{
  /* TODO: implement me. */
  abort();
}

static void hfe_writesector(int drive, int sector, int track, int side, int density)
{
  /* TODO: implement me. */
  abort();
}

static void hfe_readaddress(int drive, int track, int side, int density)
{
  /* TODO: implement me. */
  abort();
}


static void hfe_poll(void)
{
  /* TODO: implement me. */
  abort();
}

static void hfe_format(int drive, int track, int side, int density)
{
  /* TODO: implement me. */
  abort();
}

static void hfe_abort(int drive)
{
  /* TODO: implement me. */
  abort();
}

void hfe_load(int drive, const char *fn)
{
  writeprot[drive] = fwriteprot[drive] = 1;
  hfe_f[drive] = fopen(fn, "rb");
  if (!hfe_f[drive])
    {
      log_warn("hfe: unable to open HFE disc image '%s': %s", fn, strerror(errno));
      return;
    }

  free(hfe_info[drive]);

  hfe_info[drive] = malloc(sizeof(hfe_info[drive]));
  hfe_info[drive]->track_data = NULL;
  hfe_info[drive]->track_data_bytes = 0;

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
}
