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

void compactcmos_load(MODEL m) {
    FILE *cmosf;
    ALLEGRO_PATH *path;
    const char *cpath;

    memset(cmos_ram, 0, 128);
    if ((path = find_cfg_file(m.cmos, ".bin"))) {
        cpath = al_path_cstr(path, ALLEGRO_NATIVE_PATH_SEP);
        if ((cmosf = fopen(cpath, "rb"))) {
            if (fread(cmos_ram, 128, 1, cmosf) != 1)
                log_warn("compactcmos: cmos file %s read incompletely, some values will be zero", cpath);
            fclose(cmosf);
            log_debug("compactcmos: loaded from %s", cpath);
        }
        else
            log_warn("compactcmos: unable to load CMOS file '%s': %s", cpath, strerror(errno));
        al_destroy_path(path);
    }
    else
        log_error("compactcmos: unable to find CMOS file %s", m.cmos);
}

void compactcmos_save(MODEL m) {
    FILE *cmosf;
    ALLEGRO_PATH *path;
    const char *cpath;

    if ((path = find_cfg_dest(m.cmos, ".bin"))) {
        cpath = al_path_cstr(path, ALLEGRO_NATIVE_PATH_SEP);
        if ((cmosf = fopen(cpath, "wb"))) {
            log_debug("compactcmos: saving to %s", cpath);
            fwrite(cmos_ram, 128, 1, cmosf);
            fclose(cmosf);
        }
        else
            log_error("compactcmos: unable to save CMOS file '%s': %s", cpath, strerror(errno));
        al_destroy_path(path);
    }
    else
        log_error("compactcmos: unable to save CMOS file %s: no suitable destination", m.cmos);
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
//        log_debug("CMOS write - %02X %i %02X\n",byte,cmos_state,cmos_addr&0x7F);
        switch (cmos_state)
        {
                case CMOS_IDLE:
                cmos_rw = byte&1;
//                log_debug("cmos_rw %i\n",cmos_rw);
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
//                log_debug("Set CMOS addr %02X %i\n",byte,cmos_rw);
                if (cmos_rw)
                   cmos_state = CMOS_SENDDATA;
                else
                   cmos_state = CMOS_RECIEVEDATA;
                break;

                case CMOS_RECIEVEDATA:
//        log_debug("Rec byte - %02X\n",cmos_ram[(cmos_addr)&0x7F]);
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
//                log_debug("cmos_rw %i\n",cmos_rw);
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
//                                log_debug("Start bit recieved\n");
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
//                        log_debug("Stop bit recieved\n");
                        i2c_state = I2C_IDLE;
                        cmos_stop();
                }
                else if (i2c_clock && nuclock && !nudata && lastdata) /*Start bit*/
                {
//                        log_debug("Start bit recieved\n");
                        i2c_pos = 0;
                        cmos_state = CMOS_IDLE;
                }
                break;

                case I2C_ACKNOWLEDGE:
                if (!i2c_clock && nuclock)
                {
//                        log_debug("Acknowledging transfer\n");
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
//                                log_debug("End of transfer\n");
                                i2c_state = I2C_IDLE;
                                cmos_stop();
                        }
                        else /*Next byte to transfer*/
                        {
                                i2c_state = I2C_TRANSMIT;
                                cmos_nextbyte();
                                i2c_pos = 0;
//                                log_debug("Next byte - %02X %02X\n",i2c_byte,cmos_addr);
                        }
                }
                break;

            case I2C_TRANSMIT:
                if (!i2c_clock && nuclock)
                {
                        i2c_data = nudata = i2c_byte & 128;
                        i2c_byte <<= 1;
                        i2c_pos++;
//                        if (output) log_debug("Transfering bit at %07X %i %02X\n",(*armregs[15]-8)&0x3FFFFFC,i2c_pos,cmos_addr);
                        if (i2c_pos == 8)
                        {
                                i2c_state = I2C_TRANSACKNOWLEDGE;
//                                log_debug("Acknowledge mode\n");
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
