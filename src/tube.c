/*B-em v2.2 by Tom Walker
  Tube ULA emulation*/

#include <stdio.h>
#include "b-em.h"
#include "6502.h"
#include "tube.h"

#include "NS32016/32016.h"
#include "NS32016/mem32016.h"
#include "6502tube.h"
#include "65816.h"
#include "arm.h"
#include "x86_tube.h"
#include "z80.h"

#define TUBE6502  1
#define TUBEZ80   2
#define TUBEARM   3
#define TUBEX86   4
#define TUBE65816 5
#define TUBE32016 6

int tube_shift;
int tube_6502_speed=1;
int tubecycles = 0;

int tube_irq=0;
int tube_type=TUBEX86;

static int tube_romin=1;

struct
{
        uint8_t ph1[24],ph2,ph3[2],ph4;
        uint8_t hp1,hp2,hp3[2],hp4;
        uint8_t hstat[4],pstat[4],r1stat;
        int ph1pos,ph3pos,hp3pos;
} tubeula;

void tube_updateints()
{
        tube_irq = 0;
        interrupt &= ~8;

        if ((tubeula.r1stat & 1) && (tubeula.hstat[3] & 128)) interrupt |= 8;

        if ((tubeula.r1stat & 2) && (tubeula.pstat[0] & 128)) tube_irq  |= 1;
        if ((tubeula.r1stat & 4) && (tubeula.pstat[3] & 128)) tube_irq  |= 1;

        if ((tubeula.r1stat & 8) && !(tubeula.r1stat & 16) && ((tubeula.hp3pos > 0) || (tubeula.ph3pos == 0))) tube_irq|=2;
        if ((tubeula.r1stat & 8) &&  (tubeula.r1stat & 16) && ((tubeula.hp3pos > 1) || (tubeula.ph3pos == 0))) tube_irq|=2;
}

uint8_t tube_host_read(uint16_t addr)
{
        uint8_t temp = 0;
        int c;
        if (!tube_exec) return 0xFE;
        switch (addr & 7)
        {
                case 0: /*Reg 1 Stat*/
                temp = (tubeula.hstat[0] & 0xC0) | tubeula.r1stat;
                break;
                case 1: /*Register 1*/
                temp = tubeula.ph1[0];
                for (c = 0; c < 23; c++) tubeula.ph1[c] = tubeula.ph1[c + 1];
                tubeula.ph1pos--;
                tubeula.pstat[0] |= 0x40;
                if (!tubeula.ph1pos) tubeula.hstat[0] &= ~0x80;
                break;
                case 2: /*Register 2 Stat*/
                temp = tubeula.hstat[1];
                break;
                case 3: /*Register 2*/
                temp = tubeula.ph2;
                if (tubeula.hstat[1] & 0x80)
                {
                        tubeula.hstat[1] &= ~0x80;
                        tubeula.pstat[1] |=  0x40;
                }
                break;
                case 4: /*Register 3 Stat*/
                temp = tubeula.hstat[2];
                break;
                case 5: /*Register 3*/
                temp = tubeula.ph3[0];
                if (tubeula.ph3pos > 0)
                {
                        tubeula.ph3[0] = tubeula.ph3[1];
                        tubeula.ph3pos--;
                        tubeula.pstat[2] |= 0x40;
                        if (!tubeula.ph3pos) tubeula.hstat[2] &= ~0x80;
                }
                break;
                case 6: /*Register 4 Stat*/
                temp = tubeula.hstat[3];
                break;
                case 7: /*Register 4*/
                temp = tubeula.ph4;
                if (tubeula.hstat[3] & 0x80)
                {
                        tubeula.hstat[3] &= ~0x80;
                        tubeula.pstat[3] |=  0x40;
                }
                break;
        }
        tube_updateints();
        return temp;
}

void tube_host_write(uint16_t addr, uint8_t val)
{
        if (!tube_exec) return;
        switch (addr & 7)
        {
                case 0: /*Register 1 stat*/
                if (val & 0x80) tubeula.r1stat |=  (val&0x3F);
                else            tubeula.r1stat &= ~(val&0x3F);
                tubeula.hstat[0] = (tubeula.hstat[0] & 0xC0) | (val & 0x3F);
                break;
                case 1: /*Register 1*/
                tubeula.hp1 = val;
                tubeula.pstat[0] |=  0x80;
                tubeula.hstat[0] &= ~0x40;
                break;
                case 3: /*Register 2*/
                tubeula.hp2 = val;
                tubeula.pstat[1] |=  0x80;
                tubeula.hstat[1] &= ~0x40;
                break;
                case 5: /*Register 3*/
                if (tubeula.r1stat & 16)
                {
                        if (tubeula.hp3pos < 2)
                           tubeula.hp3[tubeula.hp3pos++] = val;
                        if (tubeula.hp3pos == 2)
                        {
                                tubeula.pstat[2] |=  0x80;
                                tubeula.hstat[2] &= ~0x40;
                        }
                }
                else
                {
                        tubeula.hp3[0] = val;
                        tubeula.hp3pos = 1;
                        tubeula.pstat[2] |=  0x80;
                        tubeula.hstat[2] &= ~0x40;
                        tube_updateints();
                }
//                printf("Write R3 %i\n",tubeula.hp3pos);
                break;
                case 7: /*Register 4*/
                tubeula.hp4 = val;
                tubeula.pstat[3] |=  0x80;
                tubeula.hstat[3] &= ~0x40;
                break;
        }
        tube_updateints();
}

uint8_t tube_parasite_read(uint32_t addr)
{
        uint8_t temp = 0;
        switch (addr & 7)
        {
                case 0: /*Register 1 stat*/
                if (tube_romin)
                {
                        if (tube_type == TUBE6502 || tube_type == TUBE65816)
                           tube_6502_mapoutrom();
                        tube_romin = 0;
                }
                temp = tubeula.pstat[0] | tubeula.r1stat;
                break;
                case 1: /*Register 1*/
                temp = tubeula.hp1;
                if (tubeula.pstat[0] & 0x80)
                {
                        tubeula.pstat[0] &= ~0x80;
                        tubeula.hstat[0] |=  0x40;
                }
                break;
                case 2: /*Register 2 stat*/
                temp = tubeula.pstat[1];
                break;
                case 3: /*Register 2*/
                temp = tubeula.hp2;
                if (tubeula.pstat[1] & 0x80)
                {
                        tubeula.pstat[1] &= ~0x80;
                        tubeula.hstat[1] |=  0x40;
                }
                break;
                case 4: /*Register 3 stat*/
                temp = tubeula.pstat[2];
                break;
                case 5: /*Register 3*/
                temp = tubeula.hp3[0];
                if (tubeula.hp3pos>0)
                {
                        tubeula.hp3[0] = tubeula.hp3[1];
                        tubeula.hp3pos--;
                        if (!tubeula.hp3pos)
                        {
                                tubeula.hstat[2] |=  0x40;
                                tubeula.pstat[2] &= ~0x80;
                        }
                }
                break;
                case 6: /*Register 4 stat*/
                temp = tubeula.pstat[3];
                break;
                case 7: /*Register 4*/
                temp = tubeula.hp4;
                if (tubeula.pstat[3] & 0x80)
                {
                        tubeula.pstat[3] &= ~0x80;
                        tubeula.hstat[3] |=  0x40;
                }
                break;
        }
        tube_updateints();
        return temp;
}

void tube_parasite_write(uint32_t addr, uint8_t val)
{
        switch (addr & 7)
        {
                case 1: /*Register 1*/
                if (tubeula.ph1pos < 24)
                {
                        tubeula.ph1[tubeula.ph1pos++] = val;
                        tubeula.hstat[0] |= 0x80;
                        if (tubeula.ph1pos == 24) tubeula.pstat[0] &= ~0x40;
                }
                break;
                case 3: /*Register 2*/
                tubeula.ph2 = val;
                tubeula.hstat[1] |=  0x80;
                tubeula.pstat[1] &= ~0x40;
                break;
                case 5: /*Register 3*/
                if (tubeula.r1stat & 16)
                {
                        if (tubeula.ph3pos < 2)
                           tubeula.ph3[tubeula.ph3pos++] = val;
                        if (tubeula.ph3pos == 2)
                        {
                                tubeula.hstat[2] |=  0x80;
                                tubeula.pstat[2] &= ~0x40;
                        }
                }
                else
                {
                        tubeula.ph3[0] = val;
                        tubeula.ph3pos = 1;
                        tubeula.hstat[2] |=  0x80;
                        tubeula.pstat[2] &= ~0x40;
                }
                break;
                case 7: /*Register 4*/
                tubeula.ph4 = val;
                tubeula.hstat[3] |=  0x80;
                tubeula.pstat[3] &= ~0x40;
                break;
        }
        tube_updateints();
}

void tube_6502_init()
{
        tube_type = TUBE6502;
        tube_6502_init_cpu();
        tube_6502_reset();
        tube_readmem = tube_6502_readmem;
        tube_writemem = tube_6502_writemem;
        tube_exec  = tube_6502_exec;
        tube_shift = tube_6502_speed;
}

void tube_updatespeed()
{
        if (tube_type == TUBE6502) tube_shift = tube_6502_speed;
}

void tube_arm_init()
{
        tube_type = TUBEARM;
        arm_init();
        arm_reset();
        tube_readmem = readarmb;
        tube_writemem = writearmb;
        tube_exec  = arm_exec;
        tube_shift = 1;
}

void tube_z80_init()
{
        tube_type = TUBEZ80;
        z80_init();
        z80_reset();
        tube_readmem = tube_z80_readmem;
        tube_writemem = tube_z80_writemem;
        tube_exec  = z80_exec;
        tube_shift = 2;
}

void tube_x86_init()
{
        tube_type = TUBEX86;
        x86_init();
        x86_reset();
        tube_readmem = x86_readmem;
        tube_writemem = x86_writemem;
        tube_exec  = x86_exec;
        tube_shift = 2;
}

void tube_65816_init()
{
        tube_type = TUBE65816;
        w65816_init();
        w65816_reset();
        tube_readmem = readmem65816;
        tube_writemem = writemem65816;
        tube_exec  = w65816_exec;
        tube_shift = 3;
}

void tube_32016_init()
{
        tube_type = TUBE32016;
        n32016_init();
        n32016_reset();
        tube_readmem = read_x8;
        tube_writemem = write_x8;
        tube_exec  = n32016_exec;
        tube_shift = 2;
}

void tube_reset()
{
        tubeula.ph1pos = tubeula.hp3pos = 0;
        tubeula.ph3pos = 1;
        tubeula.r1stat = 0;
        tubeula.hstat[0] = tubeula.hstat[1] = tubeula.hstat[3] = 0x40;
        tubeula.pstat[0] = tubeula.pstat[1] = tubeula.pstat[2] = tubeula.pstat[3] = 0x40;
        tubeula.hstat[2] = 0xC0;
        tube_romin = 1;
}

