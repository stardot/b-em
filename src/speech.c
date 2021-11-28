/*
 * Synthetic Speech for B-Em.
 *
 * This modules emulates a TMS5220 speech processor and attached
 * Serial ROM (PHROM) with the contents of the ROM supplied in the
 * file phrom_a.rom
 *
 * It is an adpatation of the TMS5220 emulation from MAME.
 *
 * Copyright Frank Palazzolo, Neill Corlett and Aaron Giles.
 * Adaptation by Steve Fosdick 2021.
 *
 * BSD 3-clause license.
 */

#define DEBUG_FRAME
#include "speech.h"
#include <allegro5/allegro_audio.h>

uint8_t speech_status;

#define TMS5220_PERFECT_INTERPOLATION_HACK

#define FREQ_SPEECH   8000
#define BUFLEN_SPEECH   32

#define MAX_K                   10
#define MAX_SCALE_BITS          6
#define MAX_SCALE               (1<<MAX_SCALE_BITS)
#define MAX_CHIRP_SIZE          52

struct coeffs {
    int             num_k;
    int             energy_bits;
    int             pitch_bits;
    int             kbits[MAX_K];
    unsigned short  energytable[MAX_SCALE];
    unsigned short  pitchtable[MAX_SCALE];
    int             ktable[MAX_K][MAX_SCALE];
    int16_t         chirptable[MAX_CHIRP_SIZE];
    int8_t          interp_coeff[8];
};

static const struct coeffs speech_coeff =
{
    10,
    4,
    6,
    { 5, 5, 4, 4, 4, 4, 4, 3, 3, 3 },
    { 0,  1,  2,  3,  4,  6,  8, 11, 16, 23, 33, 47, 63, 85,114, 0 },
    {   0,  15,  16,  17,  18,  19,  20,  21,
       22,  23,  24,  25,  26,  27,  28,  29,
       30,  31,  32,  33,  34,  35,  36,  37,
       38,  39,  40,  41,  42,  44,  46,  48,
       50,  52,  53,  56,  58,  60,  62,  65,
       68,  70,  72,  76,  78,  80,  84,  86,
       91,  94,  98, 101, 105, 109, 114, 118,
      122, 127, 132, 137, 142, 148, 153, 159 },
    {
        /* K1  */
        { -501, -498, -497, -495, -493, -491, -488, -482,
          -478, -474, -469, -464, -459, -452, -445, -437,
          -412, -380, -339, -288, -227, -158,  -81,   -1,
            80,  157,  226,  287,  337,  379,  411,  436 },
        /* K2  */
        { -328, -303, -274, -244, -211, -175, -138,  -99,
           -59,  -18,   24,   64,  105,  143,  180,  215,
           248,  278,  306,  331,  354,  374,  392,  408,
           422,  435,  445,  455,  463,  470,  476,  506 },
        /* K3  */
        { -441, -387, -333, -279, -225, -171, -117,  -63,
            -9,   45,   98,  152,  206,  260,  314,  368  },
        /* K4  */
        { -328, -273, -217, -161, -106,  -50,    5,   61,
           116,  172,  228,  283,  339,  394,  450,  506  },
        /* K5  */
        { -328, -282, -235, -189, -142,  -96,  -50,   -3,
            43,   90,  136,  182,  229,  275,  322,  368  },
        /* K6  */
        { -256, -212, -168, -123,  -79,  -35,   10,   54,
            98,  143,  187,  232,  276,  320,  365,  409  },
        /* K7  */
        { -308, -260, -212, -164, -117,  -69,  -21,   27,
            75,  122,  170,  218,  266,  314,  361,  409  },
        /* K8  */
        { -256, -161,  -66,   29,  124,  219,  314,  409  },
        /* K9  */
        { -256, -176,  -96,  -15,   65,  146,  226,  307  },
        /* K10 */
        { -205, -132,  -59,   14,   87,  160,  234,  307  },
    },
    /* Chirp table */\
    {   0x00, 0x03, 0x0f, 0x28, 0x4c, 0x6c, 0x71, 0x50,
        0x25, 0x26, 0x4c, 0x44, 0x1a, 0x32, 0x3b, 0x13,
        0x37, 0x1a, 0x25, 0x1f, 0x1d, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00 },
    /* interpolation shift coefficients */\
    { 0, 3, 3, 3, 2, 2, 1, 1 }
};

static ALLEGRO_VOICE *voice;
static ALLEGRO_MIXER *mixer;
static ALLEGRO_AUDIO_STREAM *stream;
static unsigned speech_proc_count;
static uint8_t speech_phrom[16384];
static uint32_t speech_phrom_addr;
static uint8_t speech_phrom_bits;
static uint8_t speech_phrom_byte;
static bool speech_RDB_flag;

#define FIFO_SIZE 16
static uint8_t speech_fifo[FIFO_SIZE];
static uint8_t speech_fifo_head;
static uint8_t speech_fifo_tail;
static uint8_t speech_fifo_count;
static uint8_t speech_fifo_bits_taken;

static uint16_t speech_previous_energy; /* needed for lattice filter to match patent */
static uint8_t speech_subcycle;         /* contains the current subcycle for a given PC: 0 is A' (only used on SPKSLOW mode on 51xx), 1 is A, 2 is B */
static uint8_t speech_subc_reload;      /* contains 1 for normal speech, 0 when SPKSLOW is active */
static uint8_t speech_PC;               /* current parameter counter (what param is being interpolated), ranges from 0 to 12 */
/* NOTE: the interpolation period counts 1,2,3,4,5,6,7,0 for divide by 8,8,8,4,4,2,2,1 */
static uint8_t speech_IP;               /* the current interpolation period */
static bool speech_inhibit;             /* If 1, interpolation is inhibited until the DIV1 period */
static bool speech_uv_zpar;             /* If 1, zero k5 thru k10 coefficients */
static bool speech_zpar;                /* If 1, zero ALL parameters. */
static bool speech_pitch_zero;          /* circuit 412; pitch is forced to zero under certain circumstances */
static uint16_t speech_pitch_count;     /* pitch counter; provides chirp rom address */

static uint8_t speech_IP;               /* the current interpolation period */
static uint8_t speech_new_frame_energy_idx;
static uint8_t speech_new_frame_pitch_idx;
static uint8_t speech_new_frame_k_idx[MAX_K];
static uint8_t speech_PC;               /* current parameter counter (what param is being interpolated), ranges from 0 to 12 */

#ifndef TMS5220_PERFECT_INTERPOLATION_HACK
static int16_t speech_current_energy;
static int16_t speech_current_pitch;
static int16_t speech_current_k[MAX_K];
#else
static int8_t speech_old_frame_energy_idx;
static int8_t speech_old_frame_pitch_idx;
static int8_t speech_old_frame_k_idx[MAX_K];
static bool speech_old_zpar;
static bool speech_old_uv_zpar;
static int32_t speech_current_energy;
static int32_t speech_current_pitch;
static int32_t speech_current_k[MAX_K];
#endif

static bool speech_read_select;
static bool speech_write_select;
static bool speech_io_ready;
static int speech_ready_delay;
static bool speech_uv_zpar;             /* If 1, zero k5 thru k10 coefficients */
static bool speech_zpar;                /* If 1, zero ALL parameters. */

/* these contain global status bits (booleans) */
static bool speech_previous_talk_status;/* this is the OLD value of talk_status (i.e. previous value of m_SPEN|m_TALKD), needed for generating interrupts on a falling talk_status edge */
static bool speech_SPEN;                /* set on speak(or speak external and BL falling edge) command, cleared on stop command, reset command, or buffer out */
static bool speech_DDIS;                /* If 1, DDIS is 1, i.e. Speak External command in progress, writes go to FIFO. */
static bool speech_TALK;                /* set on SPEN & RESETL4(pc12->pc0 transition), cleared on stop command or reset command */
static bool speech_TALKD;               /* TALK(TCON) value, latched every RESETL4 */
static bool speech_buffer_low;          /* If 1, FIFO has less than 8 bytes in it */
static bool speech_buffer_empty;        /* If 1, FIFO is empty */

/* these contain data describing the current and previous voice frames */
static bool speech_OLDE;
static bool speech_OLDP;

static uint8_t speech_new_frame_energy_idx;
static uint8_t speech_new_frame_pitch_idx;
static uint8_t speech_new_frame_k_idx[10];

static int32_t speech_u[11];
static int32_t speech_x[10];
static uint16_t speech_RNG;             /* the random noise generator configuration is: 1 + x + x^3 + x^4 + x^13 TODO: no it isn't */
static int16_t speech_excitation_data;

static void speech_update_ready_state(void)
{
    if (speech_io_ready)
        speech_status &= ~0x80;
    else
        speech_status |= 0x80;
}

static void speech_set_interrupt_state(bool state)
{
    if (state)
        speech_status &= ~0x40;
    else
        speech_status |= 0x40;
}

static void speech_update_fifo_status_and_ints(void)
{
    speech_update_ready_state();

    /* BL is set if neither byte 9 nor 8 of the FIFO are in use; this
     * translates to having fifo_count (which ranges from 0 bytes in
     * use to 16 bytes used) being less than or equal to 8.
     * Victory/Victorba depends on this.
     */
    if (speech_fifo_count <= 8) {
        /* generate an interrupt if necessary; if /BL was inactive
         * and is now active, set int.
         */
        if (!speech_buffer_low)
        {
            speech_buffer_low = true;
            speech_set_interrupt_state(1);
        }
    }
    else
        speech_buffer_low = false;

    /* BE is set if neither byte 15 nor 14 of the FIFO are in use;
     * this translates to having fifo_count equal to exactly 0
    */
    if (speech_fifo_count == 0)
    {
        // generate an interrupt if necessary; if /BE was inactive and is now active, set int.
        if (!speech_buffer_empty)
        {
            speech_buffer_empty = true;
            speech_set_interrupt_state(true);
        }
        if (speech_DDIS) {
            /* /BE being active clears the TALK status via TCON, which
             * in turn clears SPEN, but ONLY if DDIS is set!
             * See patent page 16, gate 232b
             */
            speech_TALK = speech_SPEN = false;
        }
    }
    else
        speech_buffer_empty = false;

    /* generate an interrupt if /TS was active, and is now inactive.
     * also, in this case, regardless if DDIS was set, unset it.
     */
    bool talk_status = speech_SPEN || speech_TALKD;
    if (speech_previous_talk_status && !talk_status) {
        log_debug("speech: Talk status WAS 1, is now 0, unsetting  and firing an interrupt!\n");
        speech_set_interrupt_state(1);
        speech_DDIS = false;
        speech_io_ready = true;
        speech_update_ready_state();
    }
    speech_previous_talk_status = talk_status;
}

static int speech_read_phrom(int count)
{
    int val = 0;
    log_debug("speech: %d bits requested from PHROM", count);
    while (count--) {
        if (speech_phrom_bits == 0) {
            speech_phrom_byte = speech_phrom[speech_phrom_addr++];
            log_debug("speech: read ROM %04X: %02X", speech_phrom_addr-1, speech_phrom_byte);
            speech_phrom_bits = 8;
        }
        val = (val << 1) | (speech_phrom_byte & 1);
        speech_phrom_byte >>= 1;
        --speech_phrom_bits;
    }
    return val;
}

/*
 * extract_bits
 *
 * extract a specific number of bits from the current input stream (FIFO or VSM)
 */

static int speech_extract_bits(int count)
{
    if (speech_DDIS) {
        int val = 0;
        /* From FIFO */
        while (count--) {
            val = (val << 1) | ((speech_fifo[speech_fifo_head] >> speech_fifo_bits_taken) & 1);
            speech_fifo_bits_taken++;
            if (speech_fifo_bits_taken >= 8)
            {
                speech_fifo_count--;
                speech_fifo[speech_fifo_head] = 0; // zero the newly depleted FIFO head byte
                speech_fifo_head = (speech_fifo_head + 1) % FIFO_SIZE;
                speech_fifo_bits_taken = 0;
                speech_update_fifo_status_and_ints();
            }
        }
        return val;
    }
    else
        return speech_read_phrom(count);
}

/*
 * parse_frame
 *
 * parse a new frame's worth of data; returns 0 if not enough bits in buffer
 */

static void speech_parse_frame(void)
{
#ifdef TMS5220_PERFECT_INTERPOLATION_HACK
    speech_old_uv_zpar = speech_uv_zpar;
    speech_old_zpar = speech_zpar;
#endif
    /* Since we're parsing a frame, we must be talking, so clear zpar
     * here.  Also, before we started parsing a frame, the P=0 and E=0
     * latches were both reset by RESETL4, so clear uv_zpar here.
     */
    speech_uv_zpar = speech_zpar = 0;

    /* We actually don't care how many bits are left in the FIFO here;
     * the frame subpart will be processed normally, and any bits
     * extracted 'past the end' of the FIFO will be read as zeroes; the
     * FIFO being emptied will set the /BE latch which will halt speech
     * exactly as if a stop frame had been encountered (instead of
     * whatever partial frame was read).  The same exact circuitry is
     * used for both functions on the real chip, see
     * US patent 4335277 sheet 16, gates 232a (decode stop frame)
     * and 232b (decode /BE plus DDIS (decode disable) which is active
     * during speak external).
     */

    speech_IP = 0;
    speech_update_fifo_status_and_ints();
    if (speech_DDIS && speech_buffer_empty)
        goto ranout;

    /* attempt to extract the energy index */
    speech_new_frame_energy_idx = speech_extract_bits(speech_coeff.energy_bits);
    speech_update_fifo_status_and_ints();
    if (speech_DDIS && speech_buffer_empty)
        goto ranout;
    /* if the energy index is 0 or 15, we're done */
    if ((speech_new_frame_energy_idx == 0) || (speech_new_frame_energy_idx == 15))
        return;

    /* attempt to extract the repeat flag */
    int rep_flag = speech_extract_bits(1);

    /* attempt to extract the pitch */
    speech_new_frame_pitch_idx = speech_extract_bits(speech_coeff.pitch_bits);
    /* if the new frame is unvoiced, be sure to zero out the k5-k10 parameters */
    speech_uv_zpar = (speech_new_frame_pitch_idx == 0);
    speech_update_fifo_status_and_ints();
    if (speech_DDIS && speech_buffer_empty)
        goto ranout;
    /* if this is a repeat frame, just do nothing, it will reuse the
     * old coefficients/
     */
    if (rep_flag)
        return;

    // extract first 4 K coefficients
    for (int i = 0; i < 4; i++) {
        speech_new_frame_k_idx[i] = speech_extract_bits(speech_coeff.kbits[i]);
        speech_update_fifo_status_and_ints();
        if (speech_DDIS && speech_buffer_empty)
            goto ranout;
    }

    /* if the pitch index was zero, we only need 4 K's and the rest
     * of the coefficients are zeroed, but that's done in the generator
     * code
     */
    if (speech_new_frame_pitch_idx == 0)
        return;

    // If we got here, we need the remaining 6 K's
    for (int i = 4; i < speech_coeff.num_k; i++)
    {
        speech_new_frame_k_idx[i] = speech_extract_bits(speech_coeff.kbits[i]);
        speech_update_fifo_status_and_ints();
        if (speech_DDIS && speech_buffer_empty) goto ranout;
    }

    if (speech_DDIS)
        log_debug("speech: Parsed a frame successfully in FIFO - %d bits remaining", (speech_fifo_count*8)-(speech_fifo_bits_taken));
    else
        log_debug("speech: Parsed a frame successfully in ROM");
    return;

ranout:
    log_debug("speech: Ran out of bits on a parse!");
}

/*
 * speech_matrix_multiply -- does the proper multiply and shift
 * a is the k coefficient and is clamped to 10 bits (9 bits plus a sign)
 * b is the running result and is clamped to 14 bits.
 * output is 14 bits, but note the result LSB bit is always 1.
 * Because the low 4 bits of the result are trimmed off before output,
 * this makes almost no difference in the computation.
 */

static int32_t speech_matrix_multiply(int32_t a, int32_t b)
{
    int32_t result;
    while (a>511) { a-=1024; }
    while (a<-512) { a+=1024; }
    while (b>16383) { b-=32768; }
    while (b<-16384) { b+=32768; }
    result = ((a*b)>>9); /** TODO: this isn't technically right to the chip, which truncates the lowest result bit, but it causes glitches otherwise. **/
    if (result>16383)
        log_debug("tms5220: matrix multiplier(1) overflowed! a: %x, b: %x, result: %x", a, b, result);
    if (result<-16384)
        log_debug("tms5220: matrix multiplier(1) underflowed! a: %x, b: %x, result: %x", a, b, result);
    return result;
}

/*
 * speech_lattice_filter -- executes one 'full run' of the lattice filter on
 * a specific byte of excitation data, and specific values of all the
 * current k constants,  and returns the resulting sample.
 */

static int32_t speech_lattice_filter(void)
{
    speech_u[10] = speech_matrix_multiply(speech_previous_energy, (speech_excitation_data<<6));  //Y(11)
    speech_u[9] = speech_u[10] - speech_matrix_multiply(speech_current_k[9], speech_x[9]);
    speech_u[8] = speech_u[9] - speech_matrix_multiply(speech_current_k[8], speech_x[8]);
    speech_u[7] = speech_u[8] - speech_matrix_multiply(speech_current_k[7], speech_x[7]);
    speech_u[6] = speech_u[7] - speech_matrix_multiply(speech_current_k[6], speech_x[6]);
    speech_u[5] = speech_u[6] - speech_matrix_multiply(speech_current_k[5], speech_x[5]);
    speech_u[4] = speech_u[5] - speech_matrix_multiply(speech_current_k[4], speech_x[4]);
    speech_u[3] = speech_u[4] - speech_matrix_multiply(speech_current_k[3], speech_x[3]);
    speech_u[2] = speech_u[3] - speech_matrix_multiply(speech_current_k[2], speech_x[2]);
    speech_u[1] = speech_u[2] - speech_matrix_multiply(speech_current_k[1], speech_x[1]);
    speech_u[0] = speech_u[1] - speech_matrix_multiply(speech_current_k[0], speech_x[0]);
#ifdef DEBUG_LATTICE
    int32_t err = speech_x[9] + speech_matrix_multiply(speech_current_k[9], speech_u[9]); //x_10, real chip doesn't use or calculate this
#endif
    speech_x[9] = speech_x[8] + speech_matrix_multiply(speech_current_k[8], speech_u[8]);
    speech_x[8] = speech_x[7] + speech_matrix_multiply(speech_current_k[7], speech_u[7]);
    speech_x[7] = speech_x[6] + speech_matrix_multiply(speech_current_k[6], speech_u[6]);
    speech_x[6] = speech_x[5] + speech_matrix_multiply(speech_current_k[5], speech_u[5]);
    speech_x[5] = speech_x[4] + speech_matrix_multiply(speech_current_k[4], speech_u[4]);
    speech_x[4] = speech_x[3] + speech_matrix_multiply(speech_current_k[3], speech_u[3]);
    speech_x[3] = speech_x[2] + speech_matrix_multiply(speech_current_k[2], speech_u[2]);
    speech_x[2] = speech_x[1] + speech_matrix_multiply(speech_current_k[1], speech_u[1]);
    speech_x[1] = speech_x[0] + speech_matrix_multiply(speech_current_k[0], speech_u[0]);
    speech_x[0] = speech_u[0];
    speech_previous_energy = speech_current_energy;

#ifdef DEBUG_LATTICE
    log_debug("speech: lattice_filter, V:%04d ", speech_u[10]);
    for (int i = 9; i >= 0; i--)
        log_debug("speech: lattice_filter, Y%d:%04d ", i+1, speech_u[i]);
    log_debug("speech: lattice_filter, E:%04d ", err);
    for (int i = 9; i >= 0; i--)
        log_debug("speech: lattice_filter, b%d:%04d ", i+1, speech_x[i]);
#endif
    return speech_u[0];
}

static void speech_process(int16_t *buffer, unsigned int size)
{
    int buf_count = 0;

    //log_debug("speech: process called with size of %d; IP=%d, PC=%d, subcycle=%d, SPEN=%d, TALK=%d, TALKD=%d", size, speech_IP, speech_PC, speech_subcycle, speech_SPEN, speech_TALK, speech_TALKD);

    /* loop until the buffer is full or we've stopped speaking */
    while (size > 0) {
        if (speech_TALKD) { // speaking
            /* if we're ready for a new frame to be applied, i.e. when
             * IP=0, PC=12, Sub=1 (In reality, the frame was really
             * loaded incrementally during the entire IP=0 PC=x time
             * period, but it doesn't affect anything until IP=0 PC=12
             * happens)
             */
            if ((speech_IP == 0) && (speech_PC == 12) && (speech_subcycle == 1)) {
#ifdef TMS5220_PERFECT_INTERPOLATION_HACK
                /* remember previous frame energy, pitch, and coefficients */
                speech_old_frame_energy_idx = speech_new_frame_energy_idx;
                speech_old_frame_pitch_idx = speech_new_frame_pitch_idx;
                for (int i = 0; i < speech_coeff.num_k; i++)
                    speech_old_frame_k_idx[i] = speech_new_frame_k_idx[i];
#endif
                /* Parse a new frame into the new_target_energy, new_target_pitch and new_target_k[] */
                speech_parse_frame();

                /* if the new frame is a stop frame, unset both TALK
                 * and SPEN (via TCON). TALKD remains active while the
                 * energy is ramping to 0.
                 */
                if (speech_new_frame_energy_idx == 0x0F) {
                    log_debug("speech: got stop frame");
                    speech_TALK = speech_SPEN = false;
                    speech_update_fifo_status_and_ints(); // probably not necessary...
                }

                /* In all cases where interpolation would be inhibited,
                 * set the inhibit flag; otherwise clear it.
                 * Interpolation inhibit cases:
                 *   Old frame was voiced, new is unvoiced
                 *   Old frame was silence/zero energy, new has non-zero energy
                 *   Old frame was unvoiced, new is voiced
                 *   Old frame was unvoiced, new frame is silence/zero
                 *   energy (non-existent on tms51xx rev D and F (present
                 *   and working on tms52xx, present but buggy on tms51xx rev A and B))
                 */
                if ((!speech_OLDP && (speech_new_frame_pitch_idx == 0))
                    || (speech_OLDP && !(speech_new_frame_pitch_idx == 0))
                    || (speech_OLDE && !speech_new_frame_energy_idx == 0)
                    || (speech_OLDP && speech_new_frame_energy_idx == 0) )
                    speech_inhibit = true;
                else // normal frame, normal interpolation
                    speech_inhibit = false;
#ifdef DEBUG_FRAME
                /* Debug info for current parsed frame */
                log_debug("speech: OLDE=%d; NEWE=%d; OLDP=%d; NEWP=%d ", speech_OLDE, speech_new_frame_energy_idx == 0, speech_OLDP, (speech_new_frame_pitch_idx == 0));
                if (!speech_inhibit)
                    log_debug("speech: Processing normal frame");
                else
                    log_debug("speech: Interpolation inhibited");
                log_debug("speech: Current Energy, Pitch and Ks =      %04d,    %04d, %04d, %04d, %04d, %04d, %04d, %04d, %04d, %04d, %04d, %04d",
                          speech_current_energy, speech_current_pitch,
                          speech_current_k[0], speech_current_k[1],
                          speech_current_k[2], speech_current_k[3],
                          speech_current_k[4], speech_current_k[5],
                          speech_current_k[6], speech_current_k[7],
                          speech_current_k[8], speech_current_k[9]);
                log_debug("speech: target Energy(idx), Pitch, and Ks = %04d(%x), %04d, %04d, %04d, %04d, %04d, %04d, %04d, %04d, %04d, %04d, %04d",
                    (speech_coeff.energytable[speech_new_frame_energy_idx] * (1-speech_zpar)),
                    speech_new_frame_energy_idx,
                    (speech_coeff.pitchtable[speech_new_frame_pitch_idx] * (1-speech_zpar)),
                    (speech_coeff.ktable[0][speech_new_frame_k_idx[0]] * (1-speech_zpar)),
                    (speech_coeff.ktable[1][speech_new_frame_k_idx[1]] * (1-speech_zpar)),
                    (speech_coeff.ktable[2][speech_new_frame_k_idx[2]] * (1-speech_zpar)),
                    (speech_coeff.ktable[3][speech_new_frame_k_idx[3]] * (1-speech_zpar)),
                    (speech_coeff.ktable[4][speech_new_frame_k_idx[4]] * (1-speech_uv_zpar)),
                    (speech_coeff.ktable[5][speech_new_frame_k_idx[5]] * (1-speech_uv_zpar)),
                    (speech_coeff.ktable[6][speech_new_frame_k_idx[6]] * (1-speech_uv_zpar)),
                    (speech_coeff.ktable[7][speech_new_frame_k_idx[7]] * (1-speech_uv_zpar)),
                    (speech_coeff.ktable[8][speech_new_frame_k_idx[8]] * (1-speech_uv_zpar)),
                    (speech_coeff.ktable[9][speech_new_frame_k_idx[9]] * (1-speech_uv_zpar)) );
#endif
            }
            else {
                /* Not a new frame, just interpolate the existing frame.
                 *
                 * disable inhibit when reaching the last interp period,
                 * but don't overwrite the inhibit value
                 */
                bool inhibit_state = (speech_inhibit && (speech_IP != 0));
#ifdef TMS5220_PERFECT_INTERPOLATION_HACK
                int samples_per_frame =speech_subc_reload?175:266; // either (13 A cycles + 12 B cycles) * 7 interps for normal SPEAK/SPKEXT, or (13*2 A cycles + 12 B cycles) * 7 interps for SPKSLOW
                int current_sample = (speech_subcycle -speech_subc_reload)+(speech_PC*(3-speech_subc_reload))+((speech_subc_reload?25:38)*((speech_IP-1)&7));
                // reset the current energy, pitch, etc to what it was at frame start
                speech_current_energy = (speech_coeff.energytable[speech_old_frame_energy_idx] * (1-speech_old_zpar));
                speech_current_pitch = (speech_coeff.pitchtable[speech_old_frame_pitch_idx] * (1-speech_old_zpar));
                for (int i = 0; i < speech_coeff.num_k; i++)
                    speech_current_k[i] = (speech_coeff.ktable[i][speech_old_frame_k_idx[i]] * (1-((i<4)?speech_old_zpar:speech_old_uv_zpar)));
                // now adjust each value to be exactly correct for each of the samples per frame
                if (speech_IP != 0) { // if we're still interpolating...
                    speech_current_energy = (speech_current_energy + (((speech_coeff.energytable[speech_new_frame_energy_idx] -speech_current_energy)*(1-inhibit_state))*current_sample)/samples_per_frame)*(1-speech_zpar);
                    speech_current_pitch = (speech_current_pitch + (((speech_coeff.pitchtable[speech_new_frame_pitch_idx] - speech_current_pitch)*(1-inhibit_state))*current_sample)/samples_per_frame)*(1-speech_zpar);
                    for (int i = 0; i < speech_coeff.num_k; i++)
                        speech_current_k[i] = (speech_current_k[i] + (((speech_coeff.ktable[i][speech_new_frame_k_idx[i]] - speech_current_k[i])*(1-inhibit_state))*current_sample)/samples_per_frame)*(1-((i<4)?speech_zpar:speech_uv_zpar));
                }
                else // we're done, play this frame for 1/8 frame.
                {
                    if (speech_subcycle == 2)
                        speech_pitch_zero = false; // this reset happens around the second subcycle during IP=0
                    speech_current_energy = (speech_coeff.energytable[speech_new_frame_energy_idx] * (1-speech_zpar));
                    speech_current_pitch = (speech_coeff.pitchtable[speech_new_frame_pitch_idx] * (1-speech_zpar));
                    for (int i = 0; i < speech_coeff.num_k; i++)
                        speech_current_k[i] = (speech_coeff.ktable[i][speech_new_frame_k_idx[i]] * (1-((i<4)?speech_zpar:speech_uv_zpar)));
                }
#else
                //Updates to parameters only happen on subcycle '2' (B cycle) of PCs.
                if (speech_subcycle == 2) {
                    switch(speech_PC)
                    {
                        case 0: /* PC = 0, B cycle, write updated energy */
                            if (speech_IP==0)
                                speech_pitch_zero = 0; // this reset happens around the second subcycle during IP=0
                            speech_current_energy = (speech_current_energy + (((speech_coeff.energytable[speech_new_frame_energy_idx] - speech_current_energy)*(1-inhibit_state)) >> speech_coeff.interp_coeff[speech_IP]))*(1-speech_zpar);
                            break;
                        case 1: /* PC = 1, B cycle, write updated pitch */
                            speech_current_pitch = (speech_current_pitch + (((speech_coeff.pitchtable[speech_new_frame_pitch_idx] - speech_current_pitch)*(1-inhibit_state)) >> speech_coeff.interp_coeff[speech_IP]))*(1-speech_zpar);
                            break;
                        case 2: case 3: case 4: case 5: case 6: case 7: case 8: case 9: case 10: case 11:
                            /* PC = 2 through 11, B cycle, write updated K1 through K10 */
                            speech_current_k[speech_PC-2] = (speech_current_k[speech_PC-2] + (((speech_coeff.ktable[speech_PC-2][speech_new_frame_k_idx[speech_PC-2]] - speech_current_k[speech_PC-2])*(1-inhibit_state)) >> speech_coeff.interp_coeff[speech_IP]))*(1-(((speech_PC-2)<4)?speech_zpar:speech_uv_zpar));
                            break;
                        case 12: /* PC = 12 */
                            /* we should NEVER reach this point, PC=12 doesn't have a subcycle 2 */
                            break;
                    }
                }
#endif
            }

            // calculate the output
            if (speech_OLDP) {
                // generate unvoiced samples here
                if (speech_RNG & 1)
                    speech_excitation_data = ~0x3F; /* according to the patent it is (either + or -) half of the maximum value in the chirp table, so either 01000000(0x40) or 11000000(0xC0)*/
                else
                    speech_excitation_data = 0x40;
                //log_debug("speech: unvoiced excitation data=%02X (%d)", speech_excitation_data, speech_excitation_data);
            }
            else /* (!old_frame_unvoiced_flag) */
            {
                // generate voiced samples here
                /* US patent 4331836 Figure 14B shows, and logic would
                 * hold, that a pitch based chirp function has a
                 * chirp/peak and then a long chain of zeroes.  The last
                 * entry of the chirp rom is at address 0b110011 (51d),
                 * the 52nd sample, and if the address reaches that
                 * point the ADDRESS incrementer is disabled, forcing
                 * all samples beyond 51d to be == 51d
                 */
                if (speech_pitch_count >= 51)
                    speech_excitation_data = (int8_t)speech_coeff.chirptable[51];
                else /*m_pitch_count < 51*/
                    speech_excitation_data = (int8_t)speech_coeff.chirptable[speech_pitch_count];
                //log_debug("speech: voiced excitation data=%02X (%d)", speech_excitation_data, speech_excitation_data);
            }

            // Update LFSR *20* times every sample (once per T cycle), like patent shows
            for (int i=0; i<20; i++) {
                int bitout = ((speech_RNG >> 12) & 1) ^
                             ((speech_RNG >>  3) & 1) ^
                             ((speech_RNG >>  2) & 1) ^
                             ((speech_RNG >>  0) & 1);
                speech_RNG <<= 1;
                speech_RNG |= bitout;
            }
            int32_t this_sample = speech_lattice_filter(); /* execute lattice filter */
            //log_debug("speech: sample=%d", this_sample);

            /* next, force result to 14 bits (since its possible that
             * the addition at the final (k1) stage of the lattice
             * overflowed)
             */
            while (this_sample > 16383)
                this_sample -= 32768;
            while (this_sample < -16384)
                this_sample += 32768;
            buffer[buf_count] = this_sample << 1;

            // Update all counts

            speech_subcycle++;
            if ((speech_subcycle == 2) && (speech_PC == 12)) { // RESETF3
                /* Circuit 412 in the patent acts a reset, resetting
                 * the pitch counter to 0 if INHIBIT was true during
                 * the most recent frame transition.  The exact time
                 * this occurs is betwen IP=7, PC=12 sub=0, T=t12
                 * and IP = 0, PC=0 sub=0, T=t12, a period of exactly
                 * 20 cycles, which overlaps the time OLDE and OLDP are
                 * updated at IP=7 PC=12 T17 (and hence INHIBIT itself
                 * 2 t-cycles later).  According to testing the pitch
                 * zeroing lasts approximately 2 samples.  We set the
                 * zeroing latch here, and unset it on PC=1 in the
                 * generator.
                 */
                if ((speech_IP == 7) && speech_inhibit)
                    speech_pitch_zero = true;
                if (speech_IP == 7) { // RESETL4
                    // Latch OLDE and OLDP
                    speech_OLDE = speech_new_frame_energy_idx == 0;
                    speech_OLDP = (speech_new_frame_pitch_idx == 0);
                    /* if TALK was clear last frame, halt speech now,
                     * since TALKD (latched from TALK on new frame)
                     * just went inactive.
                     */

                    log_debug("speech: RESETL4, about to update status: IP=%d, PC=%d, subcycle=%d, SPEN=%d, TALK=%d, TALKD=%d", speech_IP, speech_PC, speech_subcycle, speech_SPEN, speech_TALK, speech_TALKD);
                    if ((!speech_TALK) && (!speech_SPEN))
                        log_debug("speech: TALKD = 0 caused by stop frame or buffer empty, halting speech.");

                    speech_TALKD = speech_TALK; // TALKD is latched from TALK
                    speech_update_fifo_status_and_ints(); // to trigger an interrupt if talk_status has changed
                    if ((!speech_TALK) && speech_SPEN)
                        speech_TALK = true; // TALK is only activated if it wasn't already active, if SPEN is active, and if we're in RESETL4 (which we are).

                    log_debug("speech: RESETL4, status updated: IP=%d, PC=%d, subcycle=%d, SPEN=%d, TALK=%d, TALKD=%d", speech_IP, speech_PC, speech_subcycle, speech_SPEN, speech_TALK, speech_TALKD);
                }
                speech_subcycle = speech_subc_reload;
                speech_PC = 0;
                speech_IP++;
                speech_IP &= 0x7;
            }
            else if (speech_subcycle == 3) {
                speech_subcycle = speech_subc_reload;
                speech_PC++;
            }
            speech_pitch_count++;
            if ((speech_pitch_count >= speech_current_pitch) || speech_pitch_zero)
                speech_pitch_count = 0;
            speech_pitch_count &= 0x1FF;
        }
        else { // TALKD == 0
            speech_subcycle++;
            if ((speech_subcycle == 2) && (speech_PC == 12)) { // RESETF3
                if (speech_IP == 7) { // RESETL4
                    speech_TALKD = speech_TALK; // TALKD is latched from TALK
                    speech_update_fifo_status_and_ints(); // probably not necessary
                    if ((!speech_TALK) && speech_SPEN)
                        speech_TALK = true; // TALK is only activated if it wasn't already active, if SPEN is active, and if we're in RESETL4 (which we are).
                }
                speech_subcycle = speech_subc_reload;
                speech_PC = 0;
                speech_IP++;
                speech_IP&=0x7;
            }
            else if (speech_subcycle == 3) {
                speech_subcycle = speech_subc_reload;
                speech_PC++;
            }
            buffer[buf_count] = -1; /* should be just -1; actual chip outputs -1 every idle sample; (cf note in data sheet, p 10, table 4) */
        }
        buf_count++;
        size--;
    }
}

void speech_reset(void)
{
    speech_status = 0xff;

    memset(speech_fifo, 0, sizeof(speech_fifo));
    speech_fifo_head = speech_fifo_tail = speech_fifo_count = speech_fifo_bits_taken = 0;

    /* initialize the chip state */
    /* Note that we do not actually clear IRQ on start-up : IRQ is even raised if speech_buffer_empty or speech_buffer_low are 0 */
    speech_SPEN = speech_DDIS = speech_TALK = speech_TALKD = speech_previous_talk_status = false;
    speech_set_interrupt_state(0);
    speech_update_ready_state();
    speech_buffer_empty = speech_buffer_low = true;

    speech_RDB_flag = false;

    /* initialize the energy/pitch/k states */
#ifdef TMS5220_PERFECT_INTERPOLATION_HACK
    speech_old_frame_energy_idx = speech_old_frame_pitch_idx = 0;
    speech_old_zpar = false;
    for (int i = 0; i < MAX_K; ++i)
        speech_old_frame_k_idx[i] = 0;
#endif
    speech_new_frame_energy_idx = speech_current_energy =  speech_previous_energy = 0;
    speech_new_frame_pitch_idx = speech_current_pitch = 0;
    speech_zpar = speech_uv_zpar = false;
    for (int i = 0; i < MAX_K; ++i) {
        speech_new_frame_k_idx[i] = 0;
        speech_current_k[i] = 0;
        speech_u[i] = 0;
        speech_x[i] = 0;
    }

    /* initialize the sample generators */
    speech_inhibit = true;
    speech_subcycle = speech_pitch_count = speech_PC = 0;
    speech_subc_reload = 1;
    speech_OLDE = speech_OLDP = true;
    speech_IP = 0;
    speech_RNG = 0x1FFF;
    speech_phrom_addr = 0;
    log_debug("speech: speech_status after reset=%02X", speech_status);
}

void speech_close(void)
{
}

void speech_init(void)
{
    ALLEGRO_PATH *dir = al_create_path_for_directory("roms/speech");
    if (dir) {
        ALLEGRO_PATH *file = find_dat_file(dir, "phrom_a", ".rom");
        if (file) {
            const char *cpath = al_path_cstr(file, ALLEGRO_NATIVE_PATH_SEP);
            FILE *fp = fopen(cpath, "rb");
            if (fp) {
                if (fread(speech_phrom, sizeof(speech_phrom), 1, fp)) {
                    fclose(fp);
                    if ((voice = al_create_voice(FREQ_SPEECH, ALLEGRO_AUDIO_DEPTH_INT16, ALLEGRO_CHANNEL_CONF_1))) {
                        log_debug("speech: voice=%p", voice);
                        if ((mixer = al_create_mixer(FREQ_SPEECH, ALLEGRO_AUDIO_DEPTH_INT16, ALLEGRO_CHANNEL_CONF_1))) {
                            log_debug("speech: mixer=%p", mixer);
                            if (al_attach_mixer_to_voice(mixer, voice)) {
                                if ((stream = al_create_audio_stream(32, BUFLEN_SPEECH, FREQ_SPEECH, ALLEGRO_AUDIO_DEPTH_INT16, ALLEGRO_CHANNEL_CONF_1))) {
                                    log_debug("speech: stream=%p", stream);
                                    if (al_attach_audio_stream_to_mixer(stream, mixer)) {
                                        speech_io_ready = true;
                                        speech_reset();
                                    } else
                                        log_error("speech: unable to attach stream to mixer");
                                } else
                                    log_error("speech: unable to create stream=");
                            } else
                                log_error("speech: unable to attach mixer to voice");
                        } else
                            log_error("speech: unable to create mixer");
                    } else
                        log_error("speech: unable to create voice");
                }
                else {
                    log_error("speech: error reading PHROM data from %s: %s", cpath, strerror(errno));
                    fclose(fp);
                }
            }
            else
                log_error("speech: unable to open PHROM file %s: %s", cpath, strerror(errno));
        }
        else
            log_error("speech: PHROM file phrom_a not found");
    }
    else
        log_error("speech: unable to create directory object");
}

static void speech_fifo_add(uint8_t data)
{
    if (speech_fifo_count < FIFO_SIZE) {
        speech_fifo[speech_fifo_tail] = data;
        speech_fifo_tail = (speech_fifo_tail + 1) % FIFO_SIZE;
        speech_fifo_count++;
        log_debug("speech: data_write: Added byte to FIFO (current count=%2d)", speech_fifo_count);
        bool old_buffer_low = speech_buffer_low;
        speech_update_fifo_status_and_ints();

        // if we just unset buffer low with that last write, and SPEN *was* zero (see circuit 251, sheet 12)
        if ((!speech_SPEN) && (old_buffer_low && (!speech_buffer_low))) { // MUST HAVE EDGE DETECT
            log_debug("speech: data_write triggered SPEN to go active!\n");
            // ...then we now have enough bytes to start talking; set zpar and clear out the new frame parameters (it will become old frame just before the first call to parse_frame() )
            speech_zpar = true;
            speech_uv_zpar = true; // zero k4-k10 as well
            speech_OLDE = true; // 'silence/zpar' frames are zero energy
            speech_OLDP = true; // 'silence/zpar' frames are zero pitch
#ifdef TMS5220_PERFECT_INTERPOLATION_HACK
            speech_old_zpar = true; // zero all the old parameters
            speech_old_uv_zpar = true; // zero old k4-k10 as well
#endif
            speech_SPEN = true;
#ifdef FAST_START_HACK
            speech_TALK = true;
#endif
            speech_new_frame_energy_idx = 0;
            speech_new_frame_pitch_idx = 0;
            for (int i = 0; i < 4; i++)
                speech_new_frame_k_idx[i] = 0;
            for (int i = 4; i < 7; i++)
                speech_new_frame_k_idx[i] = 0xF;
            for (int i = 7; i < speech_coeff.num_k; i++)
                speech_new_frame_k_idx[i] = 0x7;
        }
    }
    else {
        log_debug("speech: data_write: Ran out of room in the tms52xx FIFO! this should never happen!");
        // at this point, /READY should remain HIGH/inactive until the FIFO has at least one byte open in it.
    }
}

static void speech_command(uint8_t cmd)
{
    log_debug("speech: process_command called with parameter %02X", cmd);

    /* parse the command */
    switch ((cmd & 0x70) >> 4) {
        case 0x1: /* read byte */
            if (!speech_SPEN && !speech_TALKD) { /* TALKST must be clear for RDBY */
                speech_phrom_addr &= 0x3fff;
                log_debug("speech: read byte command using address %04X", speech_phrom_addr);
                speech_phrom_byte = speech_read_phrom(8);    /* read one byte from speech ROM... */
                log_debug("speech: read byte value=%02X", speech_phrom_byte);
                speech_RDB_flag = true;
            }
            else
                log_debug("speech: Read Byte command received during TALK state, ignoring!");
            break;

        case 0x0: case 0x2: /* set rate (tms5220c and cd2501ecd only), otherwise NOP */
            log_debug("speech: set rate is a NOP command on the TMS5220");
            break;

        case 0x3: /* read and branch */
            if (!speech_SPEN && !speech_TALKD) { /* TALKST must be clear for RB */
                speech_phrom_addr &= 0x3fff;
                log_debug("speech: Read and Branch command received");
                speech_RDB_flag = false;
                speech_phrom_addr = speech_read_phrom(16);
            }
            break;

        case 0x4: /* load address */
            log_debug("speech: Load Address command received\n");
            if (!speech_SPEN && !speech_TALKD) { /* TALKST must be clear for LA */
                /* tms5220 data sheet says that if we load only one
                 * 4-bit nibble, it won't work.  This code does not
                 * care about this.
                 */
                speech_phrom_addr = (speech_phrom_addr >> 4) | ((cmd & 0xf) << 16);
                speech_phrom_bits = 0;
                log_debug("speech: phrom_address=%04X", speech_phrom_addr);
                speech_RDB_flag = false;
            }
            else
                log_debug("speech: Load Address command received during TALK state, ignoring!\n");
            break;

        case 0x5: /* speak */
            speech_phrom_addr &= 0x3fff;
            log_debug("speech: Speak (VSM) command received, begin at adress %04X", speech_phrom_addr);
            speech_SPEN = true;
#ifdef FAST_START_HACK
            speech_TALK = true;
#endif
            speech_DDIS = false; // speak using VSM
            speech_zpar = true; // zero all the parameters
            speech_uv_zpar = true; // zero k4-k10 as well
            speech_OLDE = true; // 'silence/zpar' frames are zero energy
            speech_OLDP = true; // 'silence/zpar' frames are zero pitch
#ifdef TMS5220_PERFECT_INTERPOLATION_HACK
            speech_old_zpar = true; // zero all the old parameters
            speech_old_uv_zpar = true; // zero old k4-k10 as well
#endif
            // following is semi-hack but matches idle state observed on chip
            speech_new_frame_energy_idx = 0;
            speech_new_frame_pitch_idx = 0;
            for (int i = 0; i < 4; i++)
                speech_new_frame_k_idx[i] = 0;
            for (int i = 4; i < 7; i++)
                speech_new_frame_k_idx[i] = 0xF;
            for (int i = 7; i < speech_coeff.num_k; i++)
                speech_new_frame_k_idx[i] = 0x7;
            speech_RDB_flag = false;
            break;

        case 0x6: /* speak external */
            log_debug("speech: Speak External command received");

            /* SPKEXT going active asserts /SPKEE for 2 clocks, which
             * clears the FIFO and its counters
             */
            memset(speech_fifo, 0, sizeof(speech_fifo));
            speech_fifo_head = speech_fifo_tail = speech_fifo_count = speech_fifo_bits_taken = 0;
            // SPEN is enabled when the FIFO passes half full (falling edge of BL signal)
            speech_DDIS = true; // speak using FIFO
            speech_zpar = true; // zero all the parameters
            speech_uv_zpar = true; // zero k4-k10 as well
            speech_OLDE = true; // 'silence/zpar' frames are zero energy
            speech_OLDP = true; // 'silence/zpar' frames are zero pitch
#ifdef TMS5220_PERFECT_INTERPOLATION_HACK
            speech_old_zpar = true; // zero all the old parameters
            speech_old_uv_zpar = true; // zero old k4-k10 as well
#endif
            // following is semi-hack but matches idle state observed on chip
            speech_new_frame_energy_idx = 0;
            speech_new_frame_pitch_idx = 0;
            for (int i = 0; i < 4; i++)
                speech_new_frame_k_idx[i] = 0;
            for (int i = 4; i < 7; i++)
                speech_new_frame_k_idx[i] = 0xF;
            for (int i = 7; i < speech_coeff.num_k; i++)
                speech_new_frame_k_idx[i] = 0x7;
            speech_RDB_flag = false;
            break;

        case 0x7: /* reset */
            log_debug("speech: Reset command received");
            speech_reset();
            break;
    }
    /* update the buffer low state */
    speech_update_fifo_status_and_ints();
}

uint8_t speech_read(void)
{
    if (speech_RDB_flag) {
        log_debug("speech: read returning byte %02X from PHROM", speech_phrom_byte);
        return speech_phrom_byte;
    }
    unsigned flags = 0;
    if (speech_SPEN || speech_TALKD)
        flags |= 0x80;
    if (speech_buffer_low)
        flags |= 0x40;
    if (speech_buffer_empty)
        flags |= 0x20;
    log_debug("speech: read returning flags %02X", flags);
    return flags;
}

void speech_write(uint8_t val)
{
    if (speech_DDIS) // If in speak external mode.
        speech_fifo_add(val);
    else
        speech_command(val);
}

void speech_set_rs(bool state)
{
    speech_read_select = state;
    if (state) {
        log_debug("speech: read select set");
        speech_io_ready = false;
        speech_status |= 0x80;
        speech_ready_delay = 2;
    }
}

void speech_set_ws(bool state)
{
    speech_write_select = state;
    if (state) {
        log_debug("speech: write select set");
        speech_io_ready = false;
        speech_status |= 0x80;
        speech_ready_delay = 2;
    }
}

void speech_poll(void)
{
    if (++speech_proc_count >= 60) {
        if (stream) {
            int16_t *buf = al_get_audio_stream_fragment(stream);
            if (buf) {
                speech_process(buf, BUFLEN_SPEECH);
                al_set_audio_stream_fragment(stream, buf);
                al_set_audio_stream_playing(stream, true);
                speech_proc_count = 0;
            }
        }
    }
    if (speech_ready_delay && --speech_ready_delay == 0) {
        log_debug("speech: poll");
        if (speech_write_select && !speech_read_select && (speech_fifo_count >= FIFO_SIZE) && speech_DDIS) {
            log_debug("speech: FIFO full, re-requesting poll");
            speech_ready_delay = 8;
        }
        else {
            log_debug("speech: asserting I/O ready");
            speech_io_ready = true;
            speech_status &= ~0x80;
        }
    }
}
