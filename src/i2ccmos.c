/*B-em v2.1 by Tom Walker
  I2C + CMOS RAM emulation for Master Compact*/
#include <stdio.h>
#include "b-em.h"

int cmosstate=0;
int i2cstate=0;
static int lastdata;
uint8_t i2cbyte;
int i2cclock=1,i2cdata=1,i2cpos;
int i2ctransmit=-1;

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

int cmosrw;
FILE *cmosf;
uint8_t cmosaddr=0;
uint8_t cmosram[256];

void cmosgettime();

void loadcompactcmos(MODEL m)
{
        char fn[512];
        sprintf(fn,"%s%s",exedir,m.cmos);
        cmosf=fopen(fn,"rb");
        if (cmosf)
        {
                fread(cmosram,128,1,cmosf);
                fclose(cmosf);
        }
        else
           memset(cmosram,0,128);
}

void savecompactcmos(MODEL m)
{
        char fn[512];
        sprintf(fn,"%s%s",exedir,m.cmos);
        cmosf=fopen(fn,"wb");
        fwrite(cmosram,128,1,cmosf);
        fclose(cmosf);
}

void cmosstop()
{
        cmosstate=CMOS_IDLE;
        i2ctransmit=ARM;
}

void cmosnextbyte()
{
//        cmosgettime();
        i2cbyte=cmosram[(cmosaddr++)&0x7F];
}
uint8_t cmosgetbyte()
{
        return cmosram[(cmosaddr++)&0x7F];
}

void cmoswrite(uint8_t byte)
{
//        rpclog("CMOS write - %02X %i %02X\n",byte,cmosstate,cmosaddr&0x7F);
        switch (cmosstate)
        {
                case CMOS_IDLE:
                cmosrw=byte&1;
//                rpclog("CMOSRW %i\n",cmosrw);
                if (cmosrw)
                {
                        cmosstate=CMOS_SENDDATA;
                        i2ctransmit=CMOS;
                        i2cbyte=cmosram[((cmosaddr++))&0x7F];
                }
                else
                {
                        cmosstate=CMOS_RECIEVEADDR;
                        i2ctransmit=ARM;
                }
                return;

                case CMOS_RECIEVEADDR:
                cmosaddr=byte;
//                rpclog("Set CMOS addr %02X %i\n",byte,cmosrw);
                if (cmosrw)
                   cmosstate=CMOS_SENDDATA;
                else
                   cmosstate=CMOS_RECIEVEDATA;
                break;

                case CMOS_RECIEVEDATA:
//        rpclog("Rec byte - %02X\n",cmosram[(cmosaddr)&0x7F]);
                cmosram[((cmosaddr++))&0x7F]=byte;
                break;

                case CMOS_SENDDATA:
                i2cbyte=cmosram[((cmosaddr++))&0x7F];
                break;
//                closevideo();
//                printf("Send data %02X\n",cmosaddr);
//                exit(-1);
        }
}

void cmosi2cchange(int nuclock, int nudata)
{
//                rpclog("CMOSRW %i\n",cmosrw);
//        printf("I2C %i %i %i %i  %i\n",i2cclock,nuclock,i2cdata,nudata,i2cstate);
//        log("I2C update clock %i %i data %i %i state %i\n",i2cclock,nuclock,i2cdata,nudata,i2cstate);
        switch (i2cstate)
        {
                case I2C_IDLE:
                if (i2cclock && nuclock)
                {
                        if (lastdata && !nudata) /*Start bit*/
                        {
//                                printf("Start bit\n");
//                                rpclog("Start bit recieved\n");
                                i2cstate=I2C_RECIEVE;
                                i2cpos=0;
                        }
                }
                break;

                case I2C_RECIEVE:
                if (!i2cclock && nuclock)
                {
//                        printf("Reciving %07X %07X\n",(*armregs[15]-8)&0x3FFFFFC,(*armregs[14]-8)&0x3FFFFFC);
                        i2cbyte<<=1;
                        if (nudata)
                           i2cbyte|=1;
                        else
                           i2cbyte&=0xFE;
                        i2cpos++;
                        if (i2cpos==8)
                        {

//                                if (output) //logfile("Complete - byte %02X %07X %07X\n",i2cbyte,(*armregs[15]-8)&0x3FFFFFC,(*armregs[14]-8)&0x3FFFFFC);
                                cmoswrite(i2cbyte);
                                i2cstate=I2C_ACKNOWLEDGE;
                        }
                }
                else if (i2cclock && nuclock && nudata && !lastdata) /*Stop bit*/
                {
//                        rpclog("Stop bit recieved\n");
                        i2cstate=I2C_IDLE;
                        cmosstop();
                }
                else if (i2cclock && nuclock && !nudata && lastdata) /*Start bit*/
                {
//                        rpclog("Start bit recieved\n");
                        i2cpos=0;
                        cmosstate=CMOS_IDLE;
                }
                break;

                case I2C_ACKNOWLEDGE:
                if (!i2cclock && nuclock)
                {
//                        rpclog("Acknowledging transfer\n");
                        nudata=0;
                        i2cpos=0;
                        if (i2ctransmit==ARM)
                           i2cstate=I2C_RECIEVE;
                        else
                           i2cstate=I2C_TRANSMIT;
                }
                break;

                case I2C_TRANSACKNOWLEDGE:
                if (!i2cclock && nuclock)
                {
                        if (nudata) /*It's not acknowledged - must be end of transfer*/
                        {
//                                rpclog("End of transfer\n");
                                i2cstate=I2C_IDLE;
                                cmosstop();
                        }
                        else /*Next byte to transfer*/
                        {
                                i2cstate=I2C_TRANSMIT;
                                cmosnextbyte();
                                i2cpos=0;
//                                rpclog("Next byte - %02X %02X\n",i2cbyte,cmosaddr);
                        }
                }
                break;

                case I2C_TRANSMIT:
                if (!i2cclock && nuclock)
                {
                        i2cdata=nudata=i2cbyte&128;
                        i2cbyte<<=1;
                        i2cpos++;
//                        if (output) rpclog("Transfering bit at %07X %i %02X\n",(*armregs[15]-8)&0x3FFFFFC,i2cpos,cmosaddr);
                        if (i2cpos==8)
                        {
                                i2cstate=I2C_TRANSACKNOWLEDGE;
//                                rpclog("Acknowledge mode\n");
                        }
                        i2cclock=nuclock;
                        return;
                }
                break;

        }
        if (!i2cclock && nuclock)
           i2cdata=nudata;
        lastdata=nudata;
        i2cclock=nuclock;
}
