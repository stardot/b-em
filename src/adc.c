/*B-em v2.2 by Tom Walker
  ADC emulation*/

#include <allegro5/allegro.h>
#include <stdio.h>
#include "b-em.h"
#include "adc.h"
#include "via.h"
#include "sysvia.h"

static uint8_t adc_status,adc_high,adc_low,adc_latch;
int adc_time;

uint8_t adc_read(uint16_t addr)
{
        switch (addr & 3)
        {
                case 0:
                return adc_status;
                break;
                case 1:
                return adc_high;
                break;
                case 2:
                return adc_low;
                break;
        }
        return 0x40;
}

void adc_write(uint16_t addr, uint8_t val)
{
        if (!(addr & 3))
        {
                adc_latch  = val;
                adc_time   = 60;
                adc_status = (val & 0xF) | 0x80; /*Busy, converting*/
                sysvia_set_cb1(1);
//                printf("ADC conversion - %02X\n",val);
        }
}

void adc_poll()
{
    uint32_t val = 0x7FFF;
    ALLEGRO_JOYSTICK *joy;
    ALLEGRO_JOYSTICK_STATE jstate;

    if (al_is_joystick_installed()) {
        switch (adc_status & 3) {
            case 0:
                if (al_get_num_joysticks() >= 1) {
                    joy = al_get_joystick(0);
                    al_get_joystick_state(joy, &jstate);
                    val = (128 - jstate.stick[0].axis[0]) * 256;
                }
                break;
            case 1:
                if (al_get_num_joysticks() >= 1) {
                    joy = al_get_joystick(0);
                    al_get_joystick_state(joy, &jstate);
                    val = (128 - jstate.stick[0].axis[1]) * 256;
                }
                break;
            case 2:
                if (al_get_num_joysticks() >= 2) {
                    joy = al_get_joystick(1);
                    al_get_joystick_state(joy, &jstate);
                    val = (128 - jstate.stick[0].axis[0]) * 256;
                }
                break;
            case 3:
                if (al_get_num_joysticks() >= 2) {
                    joy = al_get_joystick(1);
                    al_get_joystick_state(joy, &jstate);
                    val = (128 - jstate.stick[0].axis[1]) * 256;
                }
                break;
        }
        if (val > 0xFFFF)
            val = 0xFFFF;
    }
    adc_status =(adc_status & 0xF) | 0x40; /*Not busy, conversion complete*/
    adc_status|=(val & 0xC000) >> 10;
    adc_high   = val >> 8;
    adc_low    = val & 0xFF;
    sysvia_set_cb1(0);
}

void adc_init()
{
        adc_status = 0x40;            /*Not busy, conversion complete*/
        adc_high = adc_low = adc_latch = 0;
        adc_time = 0;
        al_install_joystick();
}

void adc_savestate(FILE *f)
{
        putc(adc_status,f);
        putc(adc_low,f);
        putc(adc_high,f);
        putc(adc_latch,f);
        putc(adc_time,f);
}

void adc_loadstate(FILE *f)
{
        adc_status = getc(f);
        adc_low    = getc(f);
        adc_high   = getc(f);
        adc_latch  = getc(f);
        adc_time   = getc(f);
}
