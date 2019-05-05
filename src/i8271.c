/*B-em v2.2 by Tom Walker
  8271 FDC emulation*/

#include <stdio.h>
#include <stdlib.h>
#include "b-em.h"
#include "6502.h"
#include "ddnoise.h"
#include "i8271.h"
#include "disc.h"
#include "model.h"

void i8271_callback();
void i8271_data(uint8_t dat);
void i8271_spindown();
void i8271_finishread();
void i8271_notfound();
void i8271_datacrcerror();
void i8271_headercrcerror();
void i8271_writeprotect();
int  i8271_getdata(int last);

static int bytenum;
static int i8271_verify = 0;

// Output Port bit definitions in i8271.drvout
#define SIDESEL   0x20
#define DRIVESEL0 0x40
#define DRIVESEL1 0x80
#define DRIVESEL (DRIVESEL0 | DRIVESEL1)

struct
{
        uint8_t command, params[5];
        int paramnum, paramreq;
        uint8_t status;
        uint8_t result;
        int curtrack[2], cursector;
        int realtrack[2];
        int sectorsleft;
        uint8_t data;
        int phase;
        int written;

        uint8_t drvout;
} i8271;

void i8271_reset()
{
        if (fdc_type == FDC_I8271)
        {
                fdc_callback       = i8271_callback;
                fdc_data           = i8271_data;
                fdc_spindown       = i8271_spindown;
                fdc_finishread     = i8271_finishread;
                fdc_notfound       = i8271_notfound;
                fdc_datacrcerror   = i8271_datacrcerror;
                fdc_headercrcerror = i8271_headercrcerror;
                fdc_writeprotect   = i8271_writeprotect;
                fdc_getdata        = i8271_getdata;
                motorspin = 45000;
        }
        i8271.paramnum = i8271.paramreq = 0;
        i8271.status = 0;
//        printf("Reset 8271\n");
        fdc_time = 0;
        i8271.curtrack[0] = i8271.curtrack[1] = 0;
        i8271.command = 0xFF;
        i8271.realtrack[0] = i8271.realtrack[1] = 0;
}

static void i8271_NMI()
{
        if (i8271.status & 8)
        {
                nmi = 1;
//                printf("NMI\n");
        }
        else                nmi = 0;
}


void i8271_spinup()
{
    if (!motoron) {
        motoron = 1;
        motorspin = 0;
        ddnoise_spinup();
    }
}

void i8271_spindown()
{
    if (motoron) {
        motoron = 0;
        ddnoise_spindown();
    }
    i8271.drvout &= ~DRIVESEL;
}

void i8271_setspindown()
{
    motorspin = 45000;
}

static void short_spindown(void)
{
    motorspin = 15000;
    fdc_time = 0;
}

int params[][2]=
{
        {0x35, 4}, {0x29, 1}, {0x2C, 0}, {0x3D, 1}, {0x3A, 2}, {0x13, 3}, {0x0B, 3}, {0x1B, 3}, {0x1F, 3}, {0x23, 5}, {-1, -1}
};

int i8271_getparams()
{
        int c = 0;
        while (params[c][0] != -1)
        {
                if (params[c][0] == i8271.command)
                   return params[c][1];
                c++;
        }
        return 0;
/*        printf("Unknown 8271 command %02X\n",i8271.command);
        dumpregs();
        exit(-1);*/
}

uint8_t i8271_read(uint16_t addr)
{
//        printf("Read 8271 %04X\n",addr);
        switch (addr & 7)
        {
            case 0: /*Status register*/
                log_debug("i8271: Read status reg %04X %02X\n",pc,i8271.status);
                return i8271.status;
            case 1: /*Result register*/
                log_debug("i8271: Read result reg %04X %02X\n",pc,i8271.result);
                i8271.status &= ~0x18;
                i8271_NMI();
  //              output=1; timetolive=50;
                return i8271.result;
            case 4: /*Data register*/
                //log_debug("i8271: Read data reg %04X %02X\n",pc,i8271.data);
                i8271.status &= ~0xC;
                i8271_NMI();
//                printf("Read data reg %04X %02X\n",pc,i8271.status);
                return i8271.data;
        }
        return 0;
}

#define track0 (i8271.curtrack[curdrive] ? 0 : 2)

void i8271_seek()
{
        int diff = i8271.params[0] - i8271.curtrack[curdrive];
        i8271.realtrack[curdrive] += diff;
        disc_seek(curdrive, i8271.realtrack[curdrive]);
}

void i8271_write(uint16_t addr, uint8_t val)
{
//        printf("Write 8271 %04X %02X\n",addr,val);
        switch (addr&7)
        {
            case 0: /*Command register*/
                if (i8271.status & 0x80) return;
                i8271.command = val & 0x3F;
                log_debug("i8271: command %02X", i8271.command);
                if (i8271.command == 0x17) i8271.command = 0x13;
                // Only commands < ReadDriveStatus actually change the drive select signals
                // We use this later to generate RDY in the ReadDriveStatus command
                if (i8271.command < 0x2C)
                {
                        i8271.drvout &= ~DRIVESEL;
                        i8271.drvout |= val & DRIVESEL;
                }
                curdrive = (val & 0x80) ? 1 : 0;
                i8271.paramnum = 0;
                i8271.paramreq = i8271_getparams();
                i8271.status = 0x80;
                if (!i8271.paramreq)
                {
                        switch (i8271.command)
                        {
                            case 0x2C: /*Read drive status*/
                                i8271.status = 0x10;
                                i8271.result = 0x80 | 8 | track0;
                                if (i8271.drvout & DRIVESEL0) i8271.result |= 0x04;
                                if (i8271.drvout & DRIVESEL1) i8271.result |= 0x40;
//                                printf("Status %02X\n",i8271.result);
                                break;

                            default:
                                i8271.result = 0x18;
                                i8271.status = 0x18;
                                i8271_NMI();
                                fdc_time = 0;
                                break;
//                                printf("Unknown 8271 command %02X 3\n",i8271.command);
//                                dumpregs();
//                                exit(-1);
                        }
                }
                break;
            case 1: /*Parameter register*/
                log_debug("i8271: parameter %02X", val);
                if (i8271.paramnum < 5)
                   i8271.params[i8271.paramnum++] = val;
                if (i8271.paramnum == i8271.paramreq)
                {
                        switch (i8271.command)
                        {
                            case 0x0B: /*Write sector*/
                                i8271.sectorsleft = i8271.params[2] & 31;
                                i8271.cursector = i8271.params[1];
                                i8271_spinup();
                                i8271.phase = 0;
                                if (i8271.curtrack[curdrive] != i8271.params[0]) i8271_seek();
                                else                                             fdc_time = 200;
                                break;
                            case 0x13: /*Read sector*/
                                i8271.sectorsleft = i8271.params[2] & 31;
                                i8271.cursector = i8271.params[1];
                                i8271_spinup();
                                i8271.phase = 0;
                                if (i8271.curtrack[curdrive] != i8271.params[0]) i8271_seek();
                                else                                             fdc_time = 200;
                                break;
                            case 0x1F: /*Verify sector*/
                                i8271.sectorsleft = i8271.params[2] & 31;
                                i8271.cursector = i8271.params[1];
                                i8271_spinup();
                                i8271.phase = 0;
                                if (i8271.curtrack[curdrive] != i8271.params[0]) i8271_seek();
                                else                                             fdc_time = 200;
                                i8271_verify = 1;
                                break;
                            case 0x1B: /*Read ID*/
//                                printf("8271 : Read ID start\n");
                                i8271.sectorsleft = i8271.params[2] & 31;
                                i8271_spinup();
                                i8271.phase = 0;
                                if (i8271.curtrack[curdrive] != i8271.params[0]) i8271_seek();
                                else                                             fdc_time = 200;
                                break;
                            case 0x23: /*Format track*/
                                i8271_spinup();
                                i8271.phase = 0;
                                if (i8271.curtrack[curdrive] != i8271.params[0]) i8271_seek();
                                else                                             fdc_time = 200;
                                break;
                                break;
                            case 0x29: /*Seek*/
//                                fdc_time=10000;
                                i8271_seek();
                                i8271_spinup();
                                break;
                            case 0x35: /*Specify*/
                                i8271.status = 0;
                                break;
                            case 0x3A: /*Write special register*/
                                i8271.status = 0;
//                                printf("Write special %02X\n",i8271.params[0]);
                                switch (i8271.params[0])
                                {
                                    case 0x12: i8271.curtrack[0] = val; /*printf("Write real track now %i\n",val);*/ break;
                                    case 0x17: break; /*Mode register*/
                                    case 0x1A: i8271.curtrack[1] = val; /*printf("Write real track now %i\n",val);*/ break;
                                    case 0x23: i8271.drvout = i8271.params[1]; break;
                                    default:
                                        i8271.result = 0x18;
                                        i8271.status = 0x18;
                                        i8271_NMI();
                                        fdc_time = 0;
                                        break;
//                                        default:
//                                        printf("8271 Write bad special register %02X\n",i8271.params[0]);
//                                        dumpregs();
//                                        exit(-1);
                                }
                                break;
                            case 0x3D: /*Read special register*/
                                i8271.status = 0x10;
                                i8271.result = 0;
                                switch (i8271.params[0])
                                {
                                        case 0x06: i8271.result = 0; break;
                                        case 0x12: i8271.result = i8271.curtrack[0]; break;
                                        case 0x1A: i8271.result = i8271.curtrack[1]; break;
                                        case 0x23: i8271.result = i8271.drvout; break;
                                        default:
                                        i8271.result = 0x18;
                                        i8271.status = 0x18;
                                        i8271_NMI();
                                        fdc_time = 0;
                                        break;
//                                        default:
//                                        printf("8271 Read bad special register %02X\n",i8271.params[0]);
//                                        dumpregs();
//                                        exit(-1);
                                }
                                break;

                                default:
                                i8271.result = 0x18;
                                i8271.status = 0x18;
                                i8271_NMI();
                                fdc_time = 0;
                                break;
//                                printf("Unknown 8271 command %02X 2\n",i8271.command);
//                                dumpregs();
//                                exit(-1);
                        }
                }
                break;
            case 2: /*Reset register*/
                log_debug("i8271: reset %02X", val);
                if (val & 1) i8271_reset();

                break;
            case 4: /*Data register*/
                i8271.data = val;
                i8271.written = 1;
                i8271.status &= ~0xC;
                i8271_NMI();
                break;
        }
}

void i8271_callback()
{
        fdc_time = 0;
//        printf("Callback 8271 - command %02X\n",i8271.command);
        switch (i8271.command)
        {
            case 0x0B: /*Write*/
                if (!i8271.phase)
                {
                        i8271.curtrack[curdrive] = i8271.params[0];
                        disc_writesector(curdrive, i8271.cursector, i8271.params[0], (i8271.drvout & SIDESEL) ? 1 : 0, 0);
                        i8271.phase = 1;

                        i8271.status = 0x8C;
                        i8271.result = 0;
                        i8271_NMI();
                        return;
                }
                i8271.sectorsleft--;
                if (!i8271.sectorsleft)
                {
                        i8271.status = 0x18;
                        i8271.result = 0;
                        i8271_NMI();
                        i8271_setspindown();
                        i8271_verify=0;
                        return;
                }
                i8271.cursector++;
                disc_writesector(curdrive, i8271.cursector, i8271.params[0], (i8271.drvout & SIDESEL) ? 1 : 0, 0);
                bytenum = 0;
                i8271.status = 0x8C;
                i8271.result = 0;
                i8271_NMI();
                break;

            case 0x13: /*Read*/
            case 0x1F: /*Verify*/
                if (!i8271.phase)
                {
//                        printf("Seek to %i\n",i8271.params[0]);
                        i8271.curtrack[curdrive] = i8271.params[0];
//                        i8271.realtrack+=diff;
//                        disc_seek(0,i8271.realtrack);
//                        printf("Re-seeking - track now %i %i\n",i8271.curtrack,i8271.realtrack);
                        disc_readsector(curdrive, i8271.cursector, i8271.params[0], (i8271.drvout & SIDESEL) ? 1 : 0, 0);
                        i8271.phase = 1;
                        return;
                }
                i8271.sectorsleft--;
                if (!i8271.sectorsleft)
                {
                        i8271.status = 0x18;
                        i8271.result = 0;
                        i8271_NMI();
                        i8271_setspindown();
                        i8271_verify=0;
                        return;
                }
                i8271.cursector++;
                disc_readsector(curdrive, i8271.cursector, i8271.params[0], (i8271.drvout & SIDESEL) ? 1 : 0, 0);
                bytenum = 0;
                break;

            case 0x1B: /*Read ID*/
//                printf("Read ID callback %i\n",i8271.phase);
                if (!i8271.phase)
                {
                        i8271.curtrack[curdrive] = i8271.params[0];
//                        i8271.realtrack+=diff;
//                        disc_seek(0,i8271.realtrack);
                        disc_readaddress(curdrive, i8271.params[0], (i8271.drvout & SIDESEL) ? 1 : 0, 0);
                        i8271.phase = 1;
                        return;
                }
//                printf("Read ID track %i %i\n",i8271.params[0],i8271.sectorsleft);
                i8271.sectorsleft--;
                if (!i8271.sectorsleft)
                {
                        i8271.status = 0x18;
                        i8271.result = 0;
                        i8271_NMI();
//                        printf("8271 : ID read done!\n");
                        i8271_setspindown();
                        return;
                }
                i8271.cursector++;
                disc_readaddress(curdrive, i8271.params[0], (i8271.drvout & SIDESEL) ? 1 : 0, 0);
                bytenum = 0;
                break;

            case 0x23: /*Format*/
                if (!i8271.phase)
                {
                        i8271.curtrack[curdrive] = i8271.params[0];
                        disc_writesector(curdrive, i8271.cursector, i8271.params[0], (i8271.drvout & SIDESEL) ? 1 : 0, 0);
                        i8271.phase = 1;

                        i8271.status = 0x8C;
                        i8271.result = 0;
                        i8271_NMI();
                        return;
                }
                if (i8271.phase == 2)
                {
                        i8271.status = 0x18;
                        i8271.result = 0;
                        i8271_NMI();
                        i8271_setspindown();
                        i8271_verify=0;
                        return;
                }
                disc_format(curdrive, i8271.params[0], (i8271.drvout & SIDESEL) ? 1 : 0, 0);
                i8271.phase = 2;
                break;

            case 0x29: /*Seek*/
                i8271.curtrack[curdrive] = i8271.params[0];
//                i8271.realtrack+=diff;
                i8271.status = 0x18;
                i8271.result = 0;
                i8271_NMI();
//                disc_seek(0,i8271.realtrack);
//                printf("Seek done!\n");
                i8271_setspindown();
                break;

            case 0xFF: break;

                default: break;
                printf("Unknown 8271 command %02X 3\n", i8271.command);
                dumpregs();
                exit(-1);
        }
}

void i8271_data(uint8_t dat)
{
        if (i8271_verify) return;
        i8271.data = dat;
        i8271.status = 0x8C;
        i8271.result = 0;
        i8271_NMI();
//        printf("%02X : Data %02X\n",bytenum,dat);
        bytenum++;
}

void i8271_finishread()
{
        fdc_time = 200;
}

void i8271_notfound()
{
        i8271.result = 0x18;
        i8271.status = 0x18;
        i8271_NMI();
        short_spindown();
//        printf("Not found 8271\n");
}

void i8271_datacrcerror()
{
        i8271.result = 0x0E;
        i8271.status = 0x18;
        i8271_NMI();
        short_spindown();
//        printf("CRCdat 8271\n");
}

void i8271_headercrcerror()
{
        i8271.result = 0x0C;
        i8271.status = 0x18;
        i8271_NMI();
        short_spindown();
//        printf("CRChead 8271\n");
}

int i8271_getdata(int last)
{
//        printf("Disc get data %i\n",bytenum);
        bytenum++;
        if (!i8271.written) return -1;
        if (!last)
        {
                i8271.status = 0x8C;
                i8271.result = 0;
                i8271_NMI();
        }
        i8271.written = 0;
        return i8271.data;
}

void i8271_writeprotect()
{
        i8271.result = 0x12;
        i8271.status = 0x18;
        i8271_NMI();
        short_spindown();
}
