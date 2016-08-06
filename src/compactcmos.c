/*B-em v2.2 by Tom Walker
  I2C + CMOS RAM emulation for Master Compact*/
#include <stdio.h>
#include "b-em.h"
#include "model.h"

int i2c_clock = 1, i2c_data = 1;

static int cmos_state = 0;
static int i2c_state = 0;
static uint8_t i2c_byte;
static int i2c_pos;
static int i2c_transmit = -1;

static int lastdata;

#define CMOS 1
#define ARM -1

#define I2C_IDLE             0
#define I2C_RECIEVE          1
#define I2C_TRANSMIT         2
#define I2C_ACKNOWLEDGE      3
#define I2C_TRANSACKNOWLEDGE 4

#define CMOS_IDLE            0
#define CMOS_RECIEVEADDR     1
#define CMOS_RECIEVEDATA     2
#define CMOS_SENDDATA        3

static int cmos_rw;

static uint8_t cmos_addr = 0;
static uint8_t cmos_ram[256];

void compactcmos_load(MODEL m)
{
        FILE *cmosf;
        char fn[512];
        sprintf(fn, "%s%s", exedir, m.cmos);
        cmosf = fopen(fn, "rb");
        if (cmosf)
        {
                fread(cmos_ram, 128, 1, cmosf);
                fclose(cmosf);
        }
        else
           memset(cmos_ram, 0, 128);
}

void compactcmos_save(MODEL m)
{
        FILE *cmosf;
        char fn[512];
        sprintf(fn, "%s%s", exedir, m.cmos);
        cmosf = fopen(fn, "wb");
        fwrite(cmos_ram, 128, 1, cmosf);
        fclose(cmosf);
}

static void cmos_stop()
{
        cmos_state = CMOS_IDLE;
        i2c_transmit = ARM;
}

static void cmos_nextbyte()
{
        i2c_byte = cmos_ram[(cmos_addr++) & 0x7F];
}

static void cmos_write(uint8_t byte)
{
//        bem_debugf("CMOS write - %02X %i %02X\n",byte,cmos_state,cmos_addr&0x7F);
        switch (cmos_state)
        {
                case CMOS_IDLE:
                cmos_rw = byte&1;
//                bem_debugf("cmos_rw %i\n",cmos_rw);
                if (cmos_rw)
                {
                        cmos_state = CMOS_SENDDATA;
                        i2c_transmit = CMOS;
                        i2c_byte = cmos_ram[(cmos_addr++) & 0x7F];
                }
                else
                {
                        cmos_state = CMOS_RECIEVEADDR;
                        i2c_transmit = ARM;
                }
                return;

                case CMOS_RECIEVEADDR:
                cmos_addr = byte;
//                bem_debugf("Set CMOS addr %02X %i\n",byte,cmos_rw);
                if (cmos_rw)
                   cmos_state = CMOS_SENDDATA;
                else
                   cmos_state = CMOS_RECIEVEDATA;
                break;

                case CMOS_RECIEVEDATA:
//        bem_debugf("Rec byte - %02X\n",cmos_ram[(cmos_addr)&0x7F]);
                cmos_ram[(cmos_addr++) & 0x7F] = byte;
                break;

                case CMOS_SENDDATA:
                i2c_byte = cmos_ram[(cmos_addr++) & 0x7F];
                break;
//                closevideo();
//                printf("Send data %02X\n",cmos_addr);
//                exit(-1);
        }
}

void compactcmos_i2cchange(int nuclock, int nudata)
{
//                bem_debugf("cmos_rw %i\n",cmos_rw);
//        printf("I2C %i %i %i %i  %i\n",i2c_clock,nuclock,i2c_data,nudata,i2c_state);
//        log("I2C update clock %i %i data %i %i state %i\n",i2c_clock,nuclock,i2c_data,nudata,i2c_state);
        switch (i2c_state)
        {
                case I2C_IDLE:
                if (i2c_clock && nuclock)
                {
                        if (lastdata && !nudata) /*Start bit*/
                        {
//                                printf("Start bit\n");
//                                bem_debug("Start bit recieved\n");
                                i2c_state = I2C_RECIEVE;
                                i2c_pos = 0;
                        }
                }
                break;

                case I2C_RECIEVE:
                if (!i2c_clock && nuclock)
                {
//                        printf("Reciving %07X %07X\n",(*armregs[15]-8)&0x3FFFFFC,(*armregs[14]-8)&0x3FFFFFC);
                        i2c_byte <<= 1;
                        if (nudata)
                           i2c_byte |= 1;
                        else
                           i2c_byte &= 0xFE;
                        i2c_pos++;
                        if (i2c_pos == 8)
                        {
//                                if (output) //logfile("Complete - byte %02X %07X %07X\n",i2c_byte,(*armregs[15]-8)&0x3FFFFFC,(*armregs[14]-8)&0x3FFFFFC);
                                cmos_write(i2c_byte);
                                i2c_state = I2C_ACKNOWLEDGE;
                        }
                }
                else if (i2c_clock && nuclock && nudata && !lastdata) /*Stop bit*/
                {
//                        bem_debug("Stop bit recieved\n");
                        i2c_state = I2C_IDLE;
                        cmos_stop();
                }
                else if (i2c_clock && nuclock && !nudata && lastdata) /*Start bit*/
                {
//                        bem_debug("Start bit recieved\n");
                        i2c_pos = 0;
                        cmos_state = CMOS_IDLE;
                }
                break;

                case I2C_ACKNOWLEDGE:
                if (!i2c_clock && nuclock)
                {
//                        bem_debug("Acknowledging transfer\n");
                        nudata = 0;
                        i2c_pos = 0;
                        if (i2c_transmit == ARM)
                           i2c_state = I2C_RECIEVE;
                        else
                           i2c_state = I2C_TRANSMIT;
                }
                break;

                case I2C_TRANSACKNOWLEDGE:
                if (!i2c_clock && nuclock)
                {
                        if (nudata) /*It's not acknowledged - must be end of transfer*/
                        {
//                                bem_debug("End of transfer\n");
                                i2c_state = I2C_IDLE;
                                cmos_stop();
                        }
                        else /*Next byte to transfer*/
                        {
                                i2c_state = I2C_TRANSMIT;
                                cmos_nextbyte();
                                i2c_pos = 0;
//                                bem_debugf("Next byte - %02X %02X\n",i2c_byte,cmos_addr);
                        }
                }
                break;

                case I2C_TRANSMIT:
                if (!i2c_clock && nuclock)
                {
                        i2c_data = nudata = i2c_byte & 128;
                        i2c_byte <<= 1;
                        i2c_pos++;
//                        if (output) bem_debugf("Transfering bit at %07X %i %02X\n",(*armregs[15]-8)&0x3FFFFFC,i2c_pos,cmos_addr);
                        if (i2c_pos == 8)
                        {
                                i2c_state = I2C_TRANSACKNOWLEDGE;
//                                bem_debug("Acknowledge mode\n");
                        }
                        i2c_clock = nuclock;
                        return;
                }
                break;

        }
        if (!i2c_clock && nuclock)
           i2c_data = nudata;
        lastdata = nudata;
        i2c_clock = nuclock;
}
