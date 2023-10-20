/* - overhaul by Diminished */

/* ISSUES OUTSTANDING
 * [ ] "Catalogue Tape" not entirely reliable;
 *     also no 300 baud option here
 * [ ] marginal tape noise issue, where if a 300 baud bit suffers
 *     from ambiguity, some of its cycles are discarded in the interest
 *     of resynchronisation; this means that none of these cycles
 *     make it to the tape noise layer.
 *     Can we fix this? Does it matter? -- no, prob not
 * [ ] saving to tape
 * [ ] uef_read_float() is lazy
 * [ ] fuzz test input formats
 */

#include "b-em.h"
#include "led.h"
#include "tape.h"
#include "serial.h"
#include "tapenoise.h"
#include "uef.h"
#include "csw.h"
#include "sysacia.h"
#include "tibet.h"

#include <ctype.h>
#include <zlib.h>

/*#define CHECK_POLL_TIMING*/
/*#define PRINT_OUT_BITS*/
#define PRINT_FRAMING_CHANGES

int tapelcount,tapellatch,tapeledcount;

bool tape_loaded = false;
bool fasttape = false;
ALLEGRO_PATH *tape_fn = NULL;

bool tibet_ena;
bool csw_ena;

static void tibet_load(const char *fn);
static void tibetz_load(const char *fn);

static struct
{
        char *ext;
        void (*load)(const char *fn);
        void (*close)();
}
loaders[]=
{
/* overhaul v2: strange mixed-metaphor here (terminated array & numeric limit) */
#define TAPE_NUM_LOADERS 4  /* overhaul v2 */
        {"UEF",    uef_load,    uef_close},
        {"CSW",    csw_load,    csw_close},
        {"TIBET",  tibet_load,  tibet_close},  /* overhaul */
        {"TIBETZ", tibetz_load, tibet_close},  /* overhaul */
        /*{0,0,0}*/
        {NULL, NULL, NULL} /* overhaul v2 */
};

#define SERIAL_STATE_AWAITING_START 0
#define SERIAL_STATE_BITS           1
#define SERIAL_STATE_PARITY         2
#define SERIAL_STATE_AWAITING_STOP  3

#define TAPE_FILE_MAXLEN            (16 * 1024 * 1024)
#define TAPE_MAX_DECOMPRESSED_LEN   (32 * 1024 * 1024)

static void tape_poll_4800th (ACIA *acia);
static int tape_poll_1200th (uint8_t ctrl_reg,
                             serial_state_t *ser,
                             uint8_t *frame_ready_out,
                             uint8_t *frame_out,
                             char *tape_noise_1200th_out, /* only used for tape noise -- needs to know about leader */
                             uint8_t *status_reg_bits_to_set_out,
                             uint8_t *status_reg_bits_to_clear_out,
                             uint8_t *raise_dcd_out);
static uint8_t check_parity (uint8_t frame,
                             uint8_t parity_bit,
                             char parity_mode);
static uint8_t handle_error (serial_state_t *ser, int e);
static void serial_get_framing (uint8_t ctrl_reg, serial_framing_t *f);
static int tape_read_1200th (serial_state_t *ser, char *bit_out);
static uint8_t tape_peek_eof (serial_state_t *ser);
static int tape_rewind_2 (serial_state_t *ser);
static int tibet_load_file (const char *fn, uint8_t decompress, tibet_t *t) ;;
static void load_successful (void);
static void tibet_load_2 (const char *fn, uint8_t decompress);
static uint8_t execute_dcd_logic (uint8_t bit, uint32_t *start_wait_count_1200ths, uint8_t awaiting_start_bit) ;

static int tape_loader;
/* all of our state lives here: */
serial_state_t tape_serial; /* exported */

void tape_load(ALLEGRO_PATH *fn)
{
        int c = 0;
        const char *p, *cpath;

        if (!fn) return;
        p = al_get_path_extension(fn);
        if (!p || !*p) return;
        if (*p == '.')
            p++;
        cpath = al_path_cstr(fn, ALLEGRO_NATIVE_PATH_SEP);
        log_info("tape: Loading %s %s", cpath, p);
        while (loaders[c].ext)
        {
                if (!strcasecmp(p, loaders[c].ext))
                {
                        tape_loader = c;
                        loaders[c].load(cpath);
                        return;
                }
                c++;
        }
        tape_loaded = 0;
}

void tape_close()
{
    if (tape_loaded && (tape_loader < TAPE_NUM_LOADERS)) {
        loaders[tape_loader].close();
    }
    tape_loaded = 0;
}

static uint16_t newdat;

/*#define CHECK_POLL_TIMING*/

/* define this to get messages on poll timing; should be 1/4800 s (208 uS) */
#ifdef CHECK_POLL_TIMING
#include <sys/time.h>
static uint64_t poll_timing_us_prev = 0;
static uint32_t poll_timing_num_calls = 0;
#endif

/* called every 1/4800 now (more with with fasttape) */
void tape_poll(void) {

    (tape_serial.poll_phase)++;

    if (motor) {
        tape_poll_4800th(&sysacia);
    } else {
        /* Hmm. Motor control off means DCD line goes low?
           Is this the correct behaviour? */
        sysacia.dcd_line_current_state = 0;
    }
    
    if (tape_serial.poll_phase >= 4) {
        tape_serial.poll_phase = 0;
    }
    
    /* Tape noise needs to continue after the motor switches off,
       to flush out any remaining audio in the ring buffer, so
       we call tapenoise_poll() outside of tape_poll_4800th().
       Check poll_phase == 0 so that we only do this every 1/1200th. */
    if ( sound_tape && (tape_serial.poll_phase == 0) ) {
        tapenoise_poll();
    }


#ifdef CHECK_POLL_TIMING
    /* timing check thing */
    struct timeval t;
    uint64_t us, elapsed;
    
    gettimeofday(&t, NULL);
    us = (t.tv_sec * 1000000) + t.tv_usec;
    if (10000 == poll_timing_num_calls) {
        elapsed = us - poll_timing_us_prev;
        /* overhaul v2: changed, moving average now */
        printf("tape poll timing: %llu microseconds\n", elapsed / 10000);
        poll_timing_num_calls = 0;
        poll_timing_us_prev = us;
    } else {
        poll_timing_num_calls++;
    }
#endif

}

/* this is the rx_hook callback for the sysacia. It's called
   automatically and was used to pass bits to the tape noise
   implementation; but TIBET at least doesn't use it now. */
   
void tape_receive (ACIA *acia, uint8_t data) {
//printf("tape_receive(&%02x)\n", data);
    newdat = data | 0x100;
}

   /*
   This needs to be long enough so that Video's Revenge doesn't
   incorrectly double-blip DCD (it doesn't double-blip it on hardware).
   The silence inserted into the leader sections in Video's Revenge
   will reset start_wait_count_1200ths back to zero, but this needs to happen
   just before it was about to blip DCD, rather than just after it's done it.
   This is the lowest number I could find that stops DCD being blipped twice.
   */
   
#define TAPE_DCD_WAIT 245

/* called every s/1200 */
static uint8_t execute_dcd_logic (uint8_t bit, uint32_t *start_wait_count_1200ths, uint8_t awaiting_start_bit) {
    uint8_t out;
    out = 0;
    if (awaiting_start_bit && ((bit == '1') || (bit == 'L'))) {
        if (TAPE_DCD_WAIT == *start_wait_count_1200ths) {
            out = 1;
//(*start_wait_count_1200ths) = TAPE_DCD_WAIT - 50; // repeat
        }
        if (*start_wait_count_1200ths < 1000000) { /* prevent possible overflow & wrap */
            (*start_wait_count_1200ths)++;
        }
    } else {
        *start_wait_count_1200ths = 0;
    }
    return out;
}

#define SQUAWKPROT_1200THS_SINCE_SILENT 100

/* We are called every 1/1200th of a second.
 * 
 * If a frame is available, frame_ready_out will be set to 1,
 * and the frame's value will be available in value_out_frame.
 * Whether this is the case or not, the 1/1200s of data that
 * was read will be available in atom_pair_out, which will be
 * required to implement the tape noise.
   
 * We also get sysacia status reg bits and DCD information. */

static int tape_poll_1200th (uint8_t ctrl_reg,
                             serial_state_t *ser,
                             uint8_t *frame_ready_out,
                             uint8_t *frame_out,
                             char *tape_noise_1200th_out, /* only used for tape noise -- needs to know about leader */
                             uint8_t *status_reg_bits_to_set_out,
                             uint8_t *status_reg_bits_to_clear_out,
                             uint8_t *raise_dcd_out) {

    int e;
    uint8_t start_bit_eligible;
    uint8_t n, bit;
    char tone_1200th;
    
    *tape_noise_1200th_out = '?';
    *frame_ready_out = 0;
    *frame_out = 0;
    *status_reg_bits_to_set_out = 0;
    *raise_dcd_out = 0;
    
    /* default to 1 for now; may get reset later */
    start_bit_eligible = 1;
    
    if (tape_peek_eof (ser)) {
        return TAPE_E_EOF;
    }
    
    /* reset some stuff */
    if (SERIAL_STATE_AWAITING_START == ser->state) {
    
        ser->bits_count = 0;
        ser->frame = 0;
        ser->parity_error = 0;
        
        // FIXME: need to know how the ACIA behaves if its
        // registers are messed with *while* a frame is
        // being received. This call should really be
        // under the TIBET_STATE_BITS state, and
        // the variables num_data_bits, num_stop_bits, parity
        // and baud300 shouldn't need to be static; they should
        // be re-evaluated each time. Under this regime we wouldn't
        // be able to pass ctrl_reg into poll_2(); we'd have to
        // query it inside this function, which would complicate
        // tape cataloguing slightly, since that sends a fake
        // value to force 8N1.
        
        serial_get_framing (ctrl_reg, &(ser->framing));
        
#ifdef PRINT_FRAMING_CHANGES
        if ( memcmp (&(ser->framing), &(ser->prev_framing), sizeof(serial_framing_t)) ) {
            log_info("tape: framing: %u%c%u @ %u -> %u%c%u @ %u\n",
                   ser->prev_framing.num_data_bits,
                   ser->prev_framing.parity,
                   ser->prev_framing.num_stop_bits,
                   ser->prev_framing.baud300 ? 300 : 1200,
                   ser->framing.num_data_bits,
                   ser->framing.parity,
                   ser->framing.num_stop_bits,
                   ser->framing.baud300 ? 300 : 1200);
        }
        ser->prev_framing = ser->framing;
#endif

    }
    
    if ( ! ser->framing.baud300 ) {
      /* careful; potentially nasty things could happen if
       * 300 baud is switched to 1200 baud by the 6502
       * in the middle of a bit. If we select 1200 baud,
       * make sure bit_phase is reset to zero; any existing
       * 300 baud cycles will just be thrown out.
       * 
       * FIXME: find out what the actual ACIA does in this
       * circumstance. */
       ser->bit_phase = 0;
    }
    
    /* read 1/1200 seconds of tone from the selected tape engine */
    e = tape_read_1200th (ser, &tone_1200th);
    if (TAPE_E_OK != e) { return e; }

    /* handle some conditions concerning leader: */
    if (    (SERIAL_STATE_AWAITING_START == ser->state) ) {
        /* perform leader detection if not already done (CSW, specifically),
         * to assist the tape noise generator: */    
        if (    (ser->start_wait_count_1200ths > 60) /* 15 bits of 300-baud ones */
             && ('1' == tone_1200th)) {
            tone_1200th = 'L';
        }
        /* If enabled, prevent "squawks" being mis-detected as
         * the start of genuine MOS blocks.
         * 
         * A real BBC micro does not actually offer this protection,
         * but I found at least one instance of a cycle burst
         * messing up the load of a title. This check will make sure
         * a certain duration of continuous tone has been detected
         * before a start bit may be recognised by the tape system.
         */
#ifdef SQUAWKPROT_1200THS_SINCE_SILENT
        if ('S' == tone_1200th) {
            ser->num_1200ths_since_silence = 0;
            /* prevent possible overflow incrementing this: */
        } else if (ser->num_1200ths_since_silence < SQUAWKPROT_1200THS_SINCE_SILENT) {
            (ser->num_1200ths_since_silence)++;
        }
        start_bit_eligible = (SQUAWKPROT_1200THS_SINCE_SILENT == ser->num_1200ths_since_silence);
#endif
    }
    
    /* append this 1200th from the tape back-end to the bit_cycles list */
    ser->bit_cycles[ser->bit_phase] = tone_1200th;
    
    /* handle 300 baud */
    ser->bit_phase += (ser->framing.baud300 ? 1 : 4);
    
    *raise_dcd_out = execute_dcd_logic (tone_1200th,
                                        &(ser->start_wait_count_1200ths), // modified
                                        (SERIAL_STATE_AWAITING_START == ser->state));
    
    /* always return this for the tape noise generator,
     * regardless of whether we have a bit or a byte: */
    *tape_noise_1200th_out = tone_1200th;
    
    /* is the bit ready yet? one call for 1200 baud, four for 300 */
    if (ser->bit_phase < 4) {
        return TAPE_E_OK;
    }
    
    /* Bit is ready (hopefully).
     * 
     * As per the TIBET spec, we *must* resynchronise
     * if we encounter an ambiguous 300-baud bit.
     */
    if (ser->framing.baud300) {
        for (n=1; n < 4; n++) {
            uint8_t z0, zn;
            z0 = ('0' == ser->bit_cycles[0]);
            zn = ('0' == ser->bit_cycles[n]);
            if (z0 != zn) {
                /* ambiguous 300-baud bit; resynchronise */
                /*log_warn("tape: ambiguous 300-baud bit");*/
                ser->bit_phase = 4 - n;
                memmove(ser->bit_cycles, ser->bit_cycles + n, 4 - n);
                return TAPE_E_OK;
            }
        }
    }
    
    /* Bit is okay. Regardless of whether we are using 1200
     * baud or 300 baud, we take the value from ser->bit_cycles[0] */
    
    ser->bit_phase = 0; /* reset this, start new bit next time */
    bit = ser->bit_cycles[0];
    
    if (SERIAL_STATE_AWAITING_START == ser->state) {
        if (start_bit_eligible && ('0' == bit)) {

            // start the frame:
            ser->state = SERIAL_STATE_BITS;
                /*ser->bit_phase = 0;*/
        }
    } else if (SERIAL_STATE_BITS == ser->state) {
        //~ e = tape_read_bit (ser, atom_pair_out);
        //~ if (TAPE_E_OK != e) { return e; }
        ser->frame = (ser->frame >> 1) & 0x7f;
        /* silence ('S') technically counts as a 0 as well,
           but we'll just ignore that, since the serial ULA
           is supposed not to pass this through (but sometimes
           it does, at certain volume levels:
           youtube.com/watch?v=5OmfWRPufQ0
        */
        ser->frame |= (('0' == bit) ? 0 : 0x80);
        
        (ser->bits_count)++;
        
        if (ser->bits_count == ser->framing.num_data_bits) {
            if (ser->framing.num_data_bits == 7) {
                ser->frame = (ser->frame >> 1) & 0x7f;
            }
            if ('N' == ser->framing.parity) {
                ser->state = SERIAL_STATE_AWAITING_STOP;
            } else {
                ser->state = SERIAL_STATE_PARITY;
            }
            /*ser->bit_phase = 0;*/
        }
    } else if (SERIAL_STATE_PARITY == ser->state) {
        //~ e = tape_read_bit (ser, atom_pair_out);
        //~ if (TAPE_E_OK != e) { return e; }
        ser->parity_error = ! check_parity (ser->frame, bit != '0', ser->framing.parity);
        ser->state = SERIAL_STATE_AWAITING_STOP;
        /*ser->bit_phase = 0;*/
    } else if (SERIAL_STATE_AWAITING_STOP == ser->state) {
        /* ACIA just ignores the second stop bit,
           so there's no need for ..._AWAITING_STOP_2
           or whatever */
        //~ e = tape_read_bit (ser, atom_pair_out);
        //~ if (TAPE_E_OK != e) { return e; }
        
        *status_reg_bits_to_clear_out = 0x50; // clear framing & parity errors
    
        *status_reg_bits_to_set_out |= (('0' == bit) ? 0x10 : 0); // bad stop bit (framing error)?
        *status_reg_bits_to_set_out |= (ser->parity_error ? 0x40 : 0); // parity error?
        
        // set "receive data register full" flag, data is ready:
        *status_reg_bits_to_set_out |= 1;
        
        /* FIXME: we set this even though it has nothing to do with
           receiving data. 0x2 is the *transmit* register empty bit?
           maybe we should leave this alone? */
        *status_reg_bits_to_set_out |= 2; /* transmit register empty */
        
        // finally
        *frame_ready_out = 1;
        *frame_out = ser->frame;
        
        ser->state = SERIAL_STATE_AWAITING_START;
        /*ser->bit_phase = 0;*/
    } else {
        log_error("tape: BUG: illegal state (%u) in tape_poll_2\n", ser->state);
        return TAPE_E_BUG;
    }
    
    //~ *raise_dcd_out = execute_dcd_logic (*atom_pair_out,
                                        //~ &(ser->start_wait_count), // modified
                                        //~ (SERIAL_STATE_AWAITING_START == ser->state));
    
#ifdef PRINT_OUT_BITS
    char pb, cb;
    pb = ser->prev_1200s;
    cb = *bit_out;
    if ((cb=='1') || (cb=='0')) { cb = 1; }
    else if (cb=='L') { cb = 2; }
    else if (cb=='S') { cb = 3; }
    else { cb = 4; }
    if ((pb=='1') || (pb=='0')) { pb = 1; }
    else if (pb=='L') { pb = 2; }
    else if (pb=='S') { pb = 3; }
    else { pb = 4; }
    //if ((cb==4)||(pb==4)) { printf("wha?\n"); }
    if (cb!=pb) { printf("cb=%d, pb=%d\n", cb, pb); }
    putchar(*bit_out);
#endif /* PRINT_OUT_BITS */

    //~ ser->prev_1200s = *atom_pair_out;
    
    return TAPE_E_OK;

}


static uint8_t check_parity (uint8_t frame,
                             uint8_t parity_bit,
                             char parity_mode) { // 'E' or 'O'
  uint8_t num_ones, i;
  num_ones = 0;
  /* include bit 7 even if in 7-bit mode, it'll be zero in 7-bit mode so it doesn't matter */
  for (i=0; i < 8; i++) {
    num_ones += (frame & 1);
    frame >>= 1;
  }
  num_ones += (parity_bit ? 1 : 0);
  return (parity_mode == 'E') ^ (1 == (num_ones & 1));
}



static void serial_get_framing (uint8_t ctrl_reg, serial_framing_t *f) {

    f->baud300 = ((ctrl_reg & 3) == 2);

    if ((ctrl_reg & 0x1c) == 0) {
        f->num_data_bits = 7;
        f->num_stop_bits = 2;
        f->parity = 'E';
    } else if ((ctrl_reg & 0x1c) == 4) {
        f->num_data_bits = 7;
        f->num_stop_bits = 2;
        f->parity = 'O';
    } else if ((ctrl_reg & 0x1c) == 8) {
        f->num_data_bits = 7;
        f->num_stop_bits = 1;
        f->parity = 'E';
    } else if ((ctrl_reg & 0x1c) == 0xC) {
        f->num_data_bits = 7;
        f->num_stop_bits = 1;
        f->parity = 'O';
    } else if ((ctrl_reg & 0x1c) == 0x10) {
        f->num_data_bits = 8;
        f->num_stop_bits = 2;
        f->parity = 'N';
    } else if ((ctrl_reg & 0x1c) == 0x14) {
        f->num_data_bits = 8;
        f->num_stop_bits = 1;
        f->parity = 'N';
    } else if ((ctrl_reg & 0x1c) == 0x18) {
        f->num_data_bits = 8;
        f->num_stop_bits = 1;
        f->parity = 'E';
    } else if ((ctrl_reg & 0x1c) == 0x1C) {
        f->num_data_bits = 8;
        f->num_stop_bits = 1;
        f->parity = 'O';
    }
    
}

/* for tape catalogue */
int tape_serial_clone_and_rewind (serial_state_t *out, serial_state_t *in) {
    int e;
    e = TAPE_E_OK;
    memcpy(out, in, sizeof(serial_state_t));
    if (TAPE_FILETYPE_TIBET == in->filetype) {
        e = tibet_clone (&(out->fmt.tibet), &(in->fmt.tibet));
        if (TAPE_E_OK != e) { return e; }
        e = tibet_rewind(&(out->fmt.tibet));
    } else if (TAPE_FILETYPE_CSW == in->filetype) {
        e = csw_clone (&(out->fmt.csw), &(in->fmt.csw));
        if (TAPE_E_OK != e) { return e; }
        csw_rewind (&(out->fmt.csw));
    } else if (TAPE_FILETYPE_UEF == in->filetype) {
        e = uef_clone (&(out->fmt.uef), &(in->fmt.uef));
        if (TAPE_E_OK != e) { return e; }
        uef_rewind (&(out->fmt.uef));
    } else {
        log_warn("tape: BUG: unknown internal filetype %d", in->filetype);
        return TAPE_E_BUG;
    }
    return e;
}



/* definite room for improvement here: */
void findfilenames_new (void) {

    int e;
    int32_t n;
    char fn[11];
    char s[256];
    uint32_t load, exec;
    uint32_t file_len;
    serial_state_t serclone;
    
    load = 0;
    exec = 0;
    file_len = 0;
    
    if (TAPE_FILETYPE_INVALID == tape_serial.filetype) {
      return; /* no tape! */
    }
    
    /* get a clone of the live tape state,
       so we don't interfere with any ongoing cassette operation: */
    
    e = tape_serial_clone_and_rewind (&serclone, &tape_serial);
    if (TAPE_E_OK != e) { return; }
    
    while (TAPE_E_OK == e) {
    
        int32_t limit;
        uint16_t blknum;
        uint16_t blklen;
        uint8_t final;
        uint8_t empty;
    
        memset(fn, 0, 11);
        
        limit = 29; /* initial byte limit, will be updated once block len is known */
        
        blknum = 0;
        blklen = 0;
        final = 0;
        empty = 0;
        load = 0;
        exec = 0;
        memset (s, 0, 256);
        
        /* process one block: */
        for (n=0; (n < limit) && (TAPE_E_OK == e); n++) {
        
            uint8_t ready, value, dummy2, dummy3, dummy4;
            char dummy1;
            uint8_t k;
            
            ready = 0;
            value = 0;
            
            /* assume 8N1/1200.
             * 
             * MOS always uses 8N1; there is no concept of
             * a "filename" without 8N1. However, the baud rate
             * could be 300 rather than 1200. The easiest thing
             * to do here might be to try both baud rates, and
             * just pick the one that produces the best outcome.
             * 
             * For UEF chunks &100, &102 and &104 we could bypass
             * tape_poll_2() and rifle through the UEF
             * file itself; this would be baud-agnostic. It wouldn't
             * help with chunk &114, nor with CSW or TIBET, though. */

            e = tape_poll_1200th (0x14, /* 8N1 */
                                  &serclone,
                                  &ready,
                                  &value,
                                  &dummy1,
                                  &dummy2,
                                  &dummy3,
                                  &dummy4);
            if (TAPE_E_OK != e) { break; }
            
//~ printf("ready = %u, value = %x\n", ready, value);
            
            if ( ! ready ) { /* waiting for start bit */
                n--;
            } else if (0 == n) { /* 0 */
                /* need sync byte */
                if ('*' != value) {
                    n--; /* try again */
                }
            } else if (n<12) { /* 1-11 */
                if ((11==n) && (value!=0)) {
                    /* no NULL terminator found; abort, resync */
                    n=-1;
                } else {
                    fn[n-1] = (char) value;
                    /* handle terminator */
                    if (0 == value) {
                        n = 11;
                    /* sanitise non-printable characters */
                    } else if ( ! isprint(value) ) {
                        fn[n-1] = '?';
                    }
                }
            } else if (n<16) { /* 12-15 */
                load = (load >> 8) & 0xffffff;
                load |= (((uint32_t) value) << 24);
            } else if (n<20) { /* 16-19 */
                exec = (exec >> 8) & 0xffffff;
                exec |= (((uint32_t) value) << 24);
            } else if (n<22) { /* 20-21 */
                blknum = (blknum >> 8) & 0xff;
                blknum |= (((uint16_t) value) << 8);
            } else if (n<24) { /* 22-23 */
                blklen = (blklen >> 8) & 0xff;
                blklen |= (((uint16_t) value) << 8);
            } else if (n<25) { /* 24 */
                if (blklen < 0x100) {
                    final = 1;
                }
                if (0x80 & value) {
                    /* 'F' flag */
                    final = 1;
                }
                if (0x40 & value) {
                    /* 'E' flag */
                    empty = 1;
                }
            } else if (n<29) { /* 25-28 */
                /* As a sanity check, make sure that the next file address bytes
                   are all zero. If they're not, we'll skip this, because it's
                   likely not actually a file. Some protection schemes will
                   obviously break this.
                   (Ideally we'd check the header CRC instead) */
                if (value != 0) {
                    file_len = 0;
                    break; /* abort */
                }
                if (28 == n) {
                    /* add block length to file total */
                    file_len += (uint32_t) blklen;
                    if (final) {
                        for (k=0; k < 10; k++) {
                            /* for compatibility with what CSW does */
                            if (fn[k] == '\0') { fn[k] = ' '; }
                        }
                        sprintf(s, "%s Size %04X Load %08X Run %08X", fn, file_len, load, exec);
                        cataddname(s);
                        file_len = 0;
                    }
                    /* skip remainder of block: HCRC2 + data + DCRC2 (but not if file empty) */
                    //limit = n + 4 + 2 + blklen + 2;
                    limit += 2;
                    if ( ! empty ) {
                        limit += (blklen + 2);
                    }
                }
            } else {
                /* ... skip byte ... */
            }
            
        } /* next byte in block */
        
    } /* next block */
    
    tape_serial_finish(&serclone);
    
}


static uint8_t tape_peek_eof (serial_state_t *ser) {
    if (TAPE_FILETYPE_CSW == ser->filetype) {
        return csw_peek_eof(&(ser->fmt.csw));
    } else if (TAPE_FILETYPE_TIBET == ser->filetype) {
        return 0xff & tibet_peek_eof(&(ser->fmt.tibet));
    } else if (TAPE_FILETYPE_UEF == ser->filetype) {
        return uef_peek_eof(&(ser->fmt.uef));
    }
    return 1;
}


#include "6502.h"

static int tape_read_1200th (serial_state_t *ser, char *value_out) {
    
    int e;
    uef_meta_t meta[UEF_MAX_METADATA];
    uint32_t meta_len;
    uint32_t m;
    
    if (TAPE_FILETYPE_CSW == ser->filetype) {
        e = csw_read_1200th (&(ser->fmt.csw), value_out);
    } else if (TAPE_FILETYPE_TIBET == ser->filetype) {
        /* The TIBET reference decoder has a built-in facility
         * for decoding 300 baud; however, it is not used here,
         * so zero is always passed to it. We always request
         * 1/1200th tones from the TIBET back-end (and all other
         * back-ends). 300 baud decoding is now done by b-em itself,
         * regardless of the back-end in use. */
        e = tibet_read_bit (&(ser->fmt.tibet),
                            0, /* always 1/1200th tones */
                            value_out);
    } else if (TAPE_FILETYPE_UEF == ser->filetype) {
        meta_len = 0;
        e = uef_read_1200th (&(ser->fmt.uef),
                             value_out,
                             meta,
                             &meta_len);
        /* TODO: Any UEF metadata chunks that punctuate actual data
         * chunks are accumulated on meta by the above call. Currently
         * this includes chunks &115 (phase change), &120 (position marker),
         * &130 (tape set info), and &131 (start of tape side). At
         * present, nothing is done with these chunks and they are
         * simply destroyed again here. For now we'll just log them */
        for (m=0; m < meta_len; m++) {
            if (meta[m].type != 0x117) { /* only baud rate is currently used */
                log_info("uef: unused metadata, chunk &%x\n", meta[m].type);
            }
        }
        uef_metadata_list_finish (meta, meta_len);
    } else {
        log_warn("tape: BUG: unknown internal filetype %d", ser->filetype);
        return TAPE_E_BUG;
    }
    
    return e;

}



static void tape_poll_4800th (ACIA *acia) {

    uint8_t frame_ready, value;
    uint8_t status_reg_bits_to_set;
    uint8_t status_reg_bits_to_clear;
    uint8_t raise_dcd;
    char tapenoise_1200th;
    int e;
    
    frame_ready = 0;
    value = 0;
    status_reg_bits_to_set = 0;
    raise_dcd = 0;
    
    /* overhaul v2: now called every 1/4800th sec, or 1/9600th with fasttape */
      
    /* DCD line reset, after 1/4800s (although status register still reads DCD high) */
    acia->dcd_line_current_state = 0;
    
    /* return 3/4 of the time after handling the DCD line, unless fast tape is
       selected, in which case we poll the tape back-end on every invocation */
    if ( ( ! fasttape ) && ( tape_serial.poll_phase < 4 ) ) {
        return;
    }
    
    /* gives us a frame only if ready, but always returns us a tapenoise_1200th. */
    e = tape_poll_1200th (acia->control_reg,
                          &tape_serial,
                          &frame_ready,
                          &value,
                          &tapenoise_1200th, /* 1/1200 second; usually one bit, but not if 300 baud */
                          &status_reg_bits_to_set,
                          &status_reg_bits_to_clear,
                          &raise_dcd);
    if (TAPE_E_OK != e) {
        handle_error (&tape_serial, e);
        return;
    }

    if (frame_ready) {
        if (acia->status_reg & 1) {
          /* RECEIVER OVERRUN
             acia_receive() should implement this, but currently it doesn't
             seem to. We'll implement it here instead:
             - previous data has not been read out, so this is a
               receiver overrun condition.
             - (a call to legacy acia_read() will (correctly) clear this bit
               as currently implemented.)
             - the data sheet also states that if we get a receiver overrun,
               the shift register contents are chucked out, and the status
               register is not messed with beyond setting the overrun bit,
               so we *don't* call acia_receive(). */
          log_warn("tape: receiver overrun");
          // FIXME: should this generate an interrupt??
          acia->status_reg |= 0x20;
        } else {
          /* so ONLY update everything if bit 0 was clear */
          acia->status_reg &= ~status_reg_bits_to_clear;
          acia->status_reg |= status_reg_bits_to_set; // re-populate bits 4 and 6 appropriately
          acia_receive (acia, value); // for ACIA
        }
    }
    
    if (raise_dcd) {
        acia_dcdhigh(acia);
    }

    if ( sound_tape ) {
        tapenoise_send_1200 (tapenoise_1200th,
                             &(tape_serial.tapenoise_no_emsgs));
    }
    
}

//#define TAPE_LOOP

static uint8_t handle_error (serial_state_t *ser, int e) {
    if (TAPE_E_EOF == e) {
#ifdef TAPE_LOOP /* overhaul v2 */
        log_warn("tape: tape finished; rewinding (TAPE_LOOP)\n");
        e = tape_rewind_2(ser);
#else
        if ( ! ser->tape_finished_no_emsgs ) {
            log_warn("tape: tape finished\n");
        }
        ser->tape_finished_no_emsgs = 1;
#endif
    } else if (TAPE_E_OK != e) {
        log_warn("tape: tape error; code %d\n", e);
        tape_serial_finish(ser); /* overhaul v2 */
        tibet_ena = 0;
        csw_ena = 0;
        tape_loaded = 0;
        return 1;
    }
    return 0;
}


void tape_rewind(void) {
    tape_rewind_2 (&tape_serial);
}


static int tape_rewind_2 (serial_state_t *ser) {
    int e;
    e = TAPE_E_OK;
    if (TAPE_FILETYPE_CSW == ser->filetype) {
        csw_rewind(&(ser->fmt.csw));
    } else if (TAPE_FILETYPE_TIBET == ser->filetype) {
        e = tibet_rewind(&(ser->fmt.tibet));
    } else {
        uef_rewind(&(ser->fmt.uef));
    }
    ser->tape_finished_no_emsgs = 0; /* overhaul v2: re-enable "tape finished" msg */
    return e;
}


void tape_serial_init (serial_state_t *ser, uint8_t filetype) {

    tape_serial_finish(ser);
    ser->filetype = filetype;
    
    ser->framing.num_data_bits = 8;
    ser->framing.num_stop_bits = 1;
    ser->framing.parity = 'N';
    
#ifdef PRINT_FRAMING_CHANGES
    ser->prev_framing.num_data_bits = 8;
    ser->prev_framing.num_stop_bits = 1;
    ser->prev_framing.parity = 'N';
#endif

}

void tape_serial_finish (serial_state_t *ser) {
    if (TAPE_FILETYPE_UEF == ser->filetype) {
        uef_finish(&(ser->fmt.uef));
    } else if (TAPE_FILETYPE_CSW == ser->filetype) {
        csw_finish(&(ser->fmt.csw));
    } else if (TAPE_FILETYPE_TIBET == ser->filetype) {
        tibet_finish(&(ser->fmt.tibet));
    }
    memset(ser, 0, sizeof(serial_state_t));
}


#define UNZIP_CHUNK 256
#define DECOMP_DELTA (100 * 1024)

int tape_decompress (uint8_t **out, uint32_t *len_out, uint8_t *in, uint32_t len_in) {

    /* if this returns E_OK, then 'in' will have been invalidated;
     * otherwise, 'in' is still valid, and must be freed by the caller. */

    uint8_t buf[UNZIP_CHUNK];
    size_t alloc;
    z_stream strm;
    uint32_t pos;
    
    *out = NULL;
    strm.zalloc   = Z_NULL;
    strm.zfree    = Z_NULL;
    strm.opaque   = Z_NULL;
    strm.next_in  = (unsigned char *) in;
    strm.avail_in = len_in;
    
    /* OR 32 allows us to decompress both zlib (for CSW)
       and gzip (for UEF and TIBETZ): */
    if ( inflateInit2 (&strm, 15 | 32) < 0 ) {
        log_warn("tape: could not decompress data; zlib init failed.");
        return TAPE_E_ZLIB_INIT;
    }
    
    alloc = 0;
    pos = 0;
    
    do {

        int zerr;
        uint32_t piece_len;
        uint32_t newsize;
        uint8_t *p;
        
        strm.avail_out = UNZIP_CHUNK;
        strm.next_out  = buf;
        
        zerr = inflate (&strm, Z_NO_FLUSH);
        
        switch (zerr) {
            case Z_OK:
            case Z_STREAM_END:
            case Z_BUF_ERROR:
                break;
            default:
                inflateEnd (&strm);
                log_warn ("tape: could not decompress data; zlib code %d", zerr);
                if (NULL != *out) { free(*out); }
                *out = NULL;
                return TAPE_E_ZLIB_DECOMPRESS;
        }
        
        piece_len = (UNZIP_CHUNK - strm.avail_out);
        
        p = NULL;
        
        if ((piece_len + pos) >= alloc) {
            newsize = piece_len + pos + DECOMP_DELTA;
            /* prevent shenanigans */
            if (newsize >= TAPE_MAX_DECOMPRESSED_LEN) {
                log_warn ("tape: decompressed size is too large\n");
                inflateEnd (&strm);
                if (*out != NULL) { free(*out); }
                *out = NULL;
                return TAPE_E_DECOMPRESSED_TOO_LARGE;
            }
            p = realloc (*out, newsize);
            if (NULL == p)  {
                log_warn ("tape: could not decompress data; realloc failed\n");
                inflateEnd (&strm);
                if (*out != NULL) { free(*out); }
                *out = NULL;
                return TAPE_E_MALLOC;
            }
            *out = p;
            alloc = newsize;
        }
        
        memcpy (*out + pos, buf, piece_len);
        pos += piece_len;
        
    } while (strm.avail_out == 0);
    
    inflateEnd (&strm);
    
    *len_out = pos;
    
    return TAPE_E_OK;
    
}


int tape_load_file (const char *fn, uint8_t decompress, uint8_t **buf_out, uint32_t *len_out) {
    
    FILE *f;
    uint32_t pos, alloc;
    uint8_t *buf;
    int e;
    uint8_t *buf2;
    uint32_t buf2_len;
    
    e = TAPE_E_OK;
    
    buf = NULL;

    if (NULL == (f = fopen (fn, "rb"))) {
        log_warn("tape: Unable to open file '%s': %s", fn, strerror(errno));
        return TAPE_E_FOPEN;
    }
    
    pos = 0;
    alloc = 0;
    
#define TAPE_FILE_ALLOC_DELTA (1024 * 1024)
    
    while ( ! feof(f) && ! ferror(f) ) {
        uint32_t chunk;
        uint32_t newsize;
        long num_read;
        uint8_t *p;
        chunk = 1024;
        /* ask for 1024 bytes */
        if ((pos + chunk) >= TAPE_FILE_MAXLEN) {
            log_warn("tape: File is too large: '%s' (max. %d)", fn, TAPE_FILE_MAXLEN);
            e = TAPE_E_FILE_TOO_LARGE;
            break;
        }
        if ((pos + chunk) >= alloc) {
            newsize = pos + chunk + TAPE_FILE_ALLOC_DELTA;
            p = realloc(buf, newsize);
            if (NULL == p) {
                log_warn("tape: Failed to grow file buffer: '%s'", fn);
                e = TAPE_E_MALLOC;
                break;
            }
            alloc = newsize;
            buf = p;
        }
        num_read = fread (buf+pos, 1, chunk, f);
        if (ferror(f) || (num_read < 0)) {
            log_warn("tape: Stream error reading file '%s': %s", fn, strerror(errno));
            e = TAPE_E_FREAD;
            break;
        }
        pos += (uint32_t) (0x7fffffff & num_read);
    }
    
    fclose(f);
    
    if (TAPE_E_OK != e) {
        free(buf);
        return e;
    }
    
    if (decompress) {
        e = tape_decompress (&buf2, &buf2_len, buf, pos & 0x7fffffff);
        free(buf);
        if (TAPE_E_OK != e) { return e; }
        log_info("tape_decompress: %u -> %u\n", (uint32_t) (pos & 0x7fffffff), buf2_len);
        pos = buf2_len;
        buf = buf2;
    }
    
    *buf_out = buf;
    *len_out = pos;
    
    return TAPE_E_OK;
    
}




static void tibet_load(const char *fn) {
    tibet_load_2(fn, 0);
}

static void tibetz_load(const char *fn) {
    tibet_load_2(fn, 1);
}

void uef_load(const char *fn) {
    int e;
    tape_serial_init (&tape_serial, TAPE_FILETYPE_UEF);
    e = uef_load_file (fn, &(tape_serial.fmt.uef));
    if (TAPE_E_OK != e) {
        log_warn("tape: could not load UEF file (code %d): '%s'", e, fn);
    } else {
        load_successful();
    }
    //tibet_ena = 1;       /* exported */
}

static void tibet_load_2 (const char *fn, uint8_t decompress) {
    int e;
    tape_serial_init (&tape_serial, TAPE_FILETYPE_TIBET);
    e = tibet_load_file (fn, decompress, &(tape_serial.fmt.tibet));
    if (TAPE_E_OK != e) {
        log_warn("tape: could not load TIBET file (code %d): '%s'", e, fn);
    } else {
        load_successful();
    }
    tibet_ena = 1;       /* exported */
}

void csw_load (const char *fn) {
    int e;
    tape_serial_init (&tape_serial, TAPE_FILETYPE_CSW);
    e = csw_load_file (fn, &(tape_serial.fmt.csw));
    if (TAPE_E_OK != e) {
        log_warn("tape: could not load CSW file (code %d): '%s'", e, fn);
    } else {
        load_successful();
    }
}

void tibet_close(void) {
    tape_serial_finish(&tape_serial);
}

void uef_close(void) {
    tape_serial_finish(&tape_serial);
}

void csw_close(void) {
    tape_serial_finish(&tape_serial);
}

static void load_successful (void) {

    /* Note: 1201 here is technically 1201.9.
       The precise value is unimportant, as long as
       tapellatch evaluates to the correct value of 13.
       Using 1202 gives 12.999, which rounds down to 12,
       so that's no good; 1201 it is. */
    tapellatch  = 1000000 / (1201 * 64);
                    
    tapelcount  = 0;
    tape_loaded = 1;
    
}


static int tibet_load_file (const char *fn, uint8_t decompress, tibet_t *t) {

    int e;
    char *buf;
    uint32_t len;
    
    len = 0;
    buf = NULL;

    e = tape_load_file (fn, decompress, (uint8_t **) &buf, &len);
    if (TAPE_E_OK != e) { return e; }

    e = tibet_decode (buf, len, t);
    if (TAPE_E_OK != e) {
        log_warn("tape: error (code %d) decoding TIBET file '%s'", e, fn);
        free(buf);
        return e;
    }
    
    free(buf);
    buf = NULL;
    
    return TAPE_E_OK;
    
}

uint32_t tape_read_u32 (uint8_t *in) {
    uint32_t u;
    u =   (  (uint32_t)in[0])
        | ((((uint32_t)in[1]) << 8) & 0xff00)
        | ((((uint32_t)in[2]) << 16) & 0xff0000)
        | ((((uint32_t)in[3]) << 24) & 0xff000000);
    return u;
}

uint32_t tape_read_u24 (uint8_t *in) {
    uint32_t u;
    u =   (  (uint32_t)in[0])
        | ((((uint32_t)in[1]) << 8) & 0xff00)
        | ((((uint32_t)in[2]) << 16) & 0xff0000);
    return u;
}

uint16_t tape_read_u16 (uint8_t *in) {
    uint16_t u;
    u =   (  (uint16_t)in[0])
        | ((((uint16_t)in[1]) << 8) & 0xff00);
    return u;
}
