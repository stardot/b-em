#include "b-em.h"
#include "music4000.h"
#include "sound.h"

#define NUM_BLOCKS 8

static uint8_t block = 0;
static uint8_t matrix[NUM_BLOCKS];

void music4000_shift(int value) {
    if (value == 0)
        block = 0;
    else if (block < NUM_BLOCKS)
        block++;
    //log_debug("m4000: block is now #%d", block);
    //count1++;
        
}

uint8_t music4000_read(void) {
    return matrix[block];
}

static void do_note(int note, int vel, int onoff) {
    uint8_t key_num, key_block, key_mask;

    if (note >= 36 && note < 98) {
        if (vel == 0)
            onoff = 0;
        key_num = note - 36;
        key_block = key_num/8;
        key_mask = 0x01 << (key_num - (key_block * 8));
        log_debug("m4000: note=%d, key=%d, block=%d, mask=%02X", note, key_num, key_block, key_mask);
        if (onoff)
            matrix[key_block] &= ~key_mask;
        else
            matrix[key_block] |= key_mask;
    } else
        log_debug("m4000: note %d off keyboard", note);
}

void music4000_note_on(int note, int vel) {
    log_debug("m4000: note on, note=%d, vel=%d", note, vel);
    do_note(note, vel, 1);
}

void music4000_note_off(int note, int vel) {
    log_debug("m4000: note off, note=%d, vel=%d", note, vel);
    do_note(note, vel, 0);
}

void music4000_reset(void) {
    int i;

    for (i = 0; i < NUM_BLOCKS; i++)
        matrix[i] = 0xff;
}
