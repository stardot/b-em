#include <allegro.h>
#include "b-em.h"
#include "music4000.h"

#define NUM_BLOCKS 8

static uint8_t block = 0;
//static uint8_t matrix[NUM_BLOCKS];
//static int count1 = 0;
//static int match = 0;
//uint8_t mask = 0x01;

void m4000_shift(int value) {
    if (value == 0)
        block = 0;
    else if (block < NUM_BLOCKS)
        block++;
    log_debug("m4000: block is now #%d", block);
    //count1++;
        
}

uint8_t m4000_read(void) {
    uint8_t value = 0xff;
    if (block == 0) {
        if (key[KEY_1_PAD])
            value &= ~0x01;
        if (key[KEY_2_PAD])
            value &= ~0x02;
        if (key[KEY_3_PAD])
            value &= ~0x04;
        if (key[KEY_4_PAD])
            value &= ~0x08;
        if (key[KEY_5_PAD])
            value &= ~0x10;
        if (key[KEY_6_PAD])
            value &= ~0x20;
        if (key[KEY_7_PAD])
            value &= ~0x40;
        if (key[KEY_8_PAD])
            value &= ~0x80;
    }
        
    //uint8_t value;
    // = matrix[block];
    /*if (count1++ > 1000) {
        mask << = 1;
        count2++;
        if (count2 > 8) {
            match++;
            if (match > 8)
                match = 0;
            count2 = 0;
            mask = 1;
        }
        count1 = 0;
    }
    * */
    log_debug("m4000: read returns %02X", value);
    return value;
}
