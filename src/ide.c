/*B-em v2.1 by Tom Walker
  IDE emulation*/
#include <stdio.h>
#include "b-em.h"

int ideenable;
int idewidth;
int idecallback;
int readflash;
int dumpedread=0;
uint8_t idebuffer2[256];
struct
{
        uint8_t atastat;
        uint8_t error,status;
        int secount,sector,cylinder,head,drive,cylprecomp;
        uint8_t command;
        uint8_t fdisk;
        int pos,pos2;
        int spt,hpc;
} ide;

int idereset=0;
uint16_t idebuffer[256];
uint8_t *idebufferb;
FILE *hdfile[2]={NULL,NULL};
void closeide0(void)
{
        fclose(hdfile[0]);
}

void closeide1(void)
{
        fclose(hdfile[1]);
}

void resetide(void)
{
        ide.pos2=1;
        ide.atastat=0x40;
        idecallback=0;
        if (!hdfile[0])
        {
                hdfile[0]=fopen("hd4.hdf","rb+");
                if (!hdfile[0])
                {
                        hdfile[0]=fopen("hd4.hdf","wb");
                        putc(0,hdfile[0]);
                        fclose(hdfile[0]);
                        hdfile[0]=fopen("hd4.hdf","rb+");
                }
                atexit(closeide0);
        }
        if (!hdfile[1])
        {
                hdfile[1]=fopen("hd5.hdf","rb+");
                if (!hdfile[1])
                {
                        hdfile[1]=fopen("hd5.hdf","wb");
                        putc(0,hdfile[1]);
                        fclose(hdfile[1]);
                        hdfile[1]=fopen("hd5.hdf","rb+");
                }
                atexit(closeide1);
        }
        idebufferb=(uint8_t *)idebuffer;
        ide.spt=63;
        ide.hpc=16;
                ide.atastat=0x40;
                ide.error=0;
                ide.secount=1;
                ide.sector=1;
                ide.head=0;
                ide.cylinder=0;
}

void writeide(uint16_t addr, uint8_t val)
{
        if (!ideenable) return;
//        rpclog("Write IDE %04X %02X %04X\n",addr,val,pc);
        switch (addr&0xF)
        {
                case 0x0:
                idebufferb[ide.pos]=val; ide.pos2=ide.pos+1; ide.pos+=2;
                if (ide.pos>=512)
                {
                        ide.pos=0;
                        ide.atastat=0x80;
                        idecallback=1000;
                }
                return;
                case 0x8:
                idebufferb[ide.pos2]=val;
                return;
                case 0x1:
                ide.cylprecomp=val;
                return;
                case 0x2:
                ide.secount=val;
                return;
                case 0x3:
                ide.sector=val;
                return;
                case 0x4:
                ide.cylinder=(ide.cylinder&0xFF00)|val;
                return;
                case 0x5:
                ide.cylinder=(ide.cylinder&0xFF)|(val<<8);
                return;
                case 0x6:
                ide.head=val&0xF;
                ide.drive=(val>>4)&1;
                return;
                case 0x7: /*Command register*/
                ide.command=val;
                ide.error=0;
                idewidth=0;
//                rpclog("IDE command %02X\n",val);
                switch (val)
                {
                        case 0x10: /*Restore*/
                        case 0x70: /*Seek*/
                        ide.atastat=0x40;
                        idecallback=100;
                        return;
                        case 0x20: /*Read sector*/
/*                        if (ide.secount>1)
                        {
                                error("Read %i sectors from sector %i cylinder %i head %i\n",ide.secount,ide.sector,ide.cylinder,ide.head);
                                exit(-1);
                        }*/
//                        rpclog("Read %i sectors from sector %i cylinder %i head %i\n",ide.secount,ide.sector,ide.cylinder,ide.head);
                        ide.atastat=0x80;
                        idecallback=200;
                        return;
                        case 0x30: /*Write sector*/
/*                        if (ide.secount>1)
                        {
                                error("Write %i sectors to sector %i cylinder %i head %i\n",ide.secount,ide.sector,ide.cylinder,ide.head);
                                exit(-1);
                        }*/
//                        rpclog("Write %i sectors to sector %i cylinder %i head %i\n",ide.secount,ide.sector,ide.cylinder,ide.head);
                        ide.atastat=0x08|0x40;
                        ide.pos=0;
                        return;
                        case 0x40: /*Read verify*/
//                        rpclog("Read verify %i sectors from sector %i cylinder %i head %i\n",ide.secount,ide.sector,ide.cylinder,ide.head);
                        ide.atastat=0x80;
                        idecallback=200;
                        return;
                        case 0x50:
//                        rpclog("Format track %i head %i\n",ide.cylinder,ide.head);
                        ide.atastat=0x08;
//                        idecallback=200;
                        ide.pos=0;
                        return;
                        case 0x91: /*Set parameters*/
                        ide.atastat=0x80;
                        idecallback=200;
                        return;
                        case 0xA1: /*Identify packet device*/
                        case 0xE3: /*Idle*/
                        ide.atastat=0x80;
                        idecallback=200;
                        return;
                        case 0xEC: /*Identify device*/
                        ide.atastat=0x80;
                        idecallback=200;
                        idewidth=1;
                        return;
                }
                rpclog("Bad IDE command %02X\n",val);
                exit(-1);
                return;
        }
//        rpclog("Bad IDE write %04X %02X\n",addr,val);
//        dumpregs();
//        exit(-1);
}

int indexcount=0;

uint8_t readide(uint16_t addr)
{
        uint8_t temp;
        if (!ideenable) return 0xFC;
//        if ((addr&0xF)!=7) rpclog("Read IDE %04X %04X\n",addr,pc);
        switch (addr&0xF)
        {
                case 0x0:
                temp=idebufferb[ide.pos]; ide.pos2=ide.pos+1; ide.pos+=2;
//                printf("IDE read %i\n",ide.pos);
                if (ide.pos>=512)
                {
//                        printf("IDE over!\n");
                        ide.pos=0;
                        ide.atastat=0x40;
                        if (ide.command==0x20)
                        {
//                                printf("But is it continuing? %i\n",ide.secount);
                                ide.secount--;
                                if (ide.secount)
                                {
                                        ide.atastat=0x08;
                                        ide.sector++;
                                        if (ide.sector==(ide.spt+1))
                                        {
                                                ide.sector=1;
                                                ide.head++;
                                                if (ide.head==(ide.hpc))
                                                {
                                                        ide.head=0;
                                                        ide.cylinder++;
                                                }
                                        }
                                        ide.atastat=0x80;
                                        idecallback=200;
//                                        printf("Yes\n");
                                }
                        }
                }
                return temp;
                case 0x8:
                temp=idebufferb[ide.pos2];
                return temp;
                case 0x1:
                return ide.error;
                case 0x2:
                return ide.secount;
                case 0x3:
                return ide.sector;
                case 0x4:
                return ide.cylinder&0xFF;
                case 0x5:
                return ide.cylinder>>8;
                case 0x6:
                return ide.head|(ide.drive<<4);
                case 0x7:
                indexcount++;
                if (indexcount==199)
                {
                        indexcount=0;
                        return ide.atastat|2;
                }
                return ide.atastat;
//                case 0x8:
//                return idebufferb[ide.pos+1];
        }
        return 0xFF;
//        rpclog("Bad IDE read %04X\n",addr);
//        dumpregs();
//        exit(-1);
}

void callbackide(void)
{
        int addr,c;
        if (idereset)
        {
                ide.atastat=0x40;
                ide.error=0;
                ide.secount=1;
                ide.sector=1;
                ide.head=0;
                ide.cylinder=0;
                idereset=0;
//                rpclog("Reset callback\n");
                return;
        }
//        printf("IDECALLBACK %02X\n",ide.command);
        switch (ide.command)
        {
                case 0x10: /*Restore*/
                case 0x70: /*Seek*/
//                rpclog("Restore callback\n");
                ide.atastat=0x40;
//                iomd.statb|=2;
//                updateirqs();
                return;
                case 0x20: /*Read sectors*/
                readflash=1;
                addr=((((ide.cylinder*ide.hpc)+ide.head)*ide.spt)+(ide.sector))*256;
                /*                if (ide.cylinder || ide.head)
                {
                        error("Read from other cylinder/head");
                        exit(-1);
                }*/
                fseek(hdfile[ide.drive],addr,SEEK_SET);
                memset(idebuffer,0,512);
                fread(idebuffer2,256,1,hdfile[ide.drive]);
                for (c=0;c<256;c++) idebufferb[c<<1]=idebuffer2[c];
                ide.pos=0;
                ide.atastat=0x08|0x40;
//                rpclog("Read sector callback %i %i %i offset %08X %i left %i\n",ide.sector,ide.cylinder,ide.head,addr,ide.secount,ide.spt);
//                iomd.statb|=2;
//                updateirqs();
                return;
                case 0x30: /*Write sector*/
                readflash=2;
                addr=((((ide.cylinder*ide.hpc)+ide.head)*ide.spt)+(ide.sector))*256;
//                rpclog("Write sector callback %i %i %i offset %08X %i left %i\n",ide.sector,ide.cylinder,ide.head,addr,ide.secount,ide.spt);
                fseek(hdfile[ide.drive],addr,SEEK_SET);
                for (c=0;c<256;c++) idebuffer2[c]=idebufferb[c<<1];
                fwrite(idebuffer2,256,1,hdfile[ide.drive]);
//                iomd.statb|=2;
//                updateirqs();
                ide.secount--;
                if (ide.secount)
                {
                        ide.atastat=0x08|0x40;
                        ide.pos=0;
                        ide.sector++;
                        if (ide.sector==(ide.spt+1))
                        {
                                ide.sector=1;
                                ide.head++;
                                if (ide.head==(ide.hpc))
                                {
                                        ide.head=0;
                                        ide.cylinder++;
                                }
                        }
                }
                else
                   ide.atastat=0x40;
                return;
                case 0x40: /*Read verify*/
                ide.pos=0;
                ide.atastat=0x40;
//                rpclog("Read verify callback %i %i %i offset %08X %i left\n",ide.sector,ide.cylinder,ide.head,addr,ide.secount);
//                iomd.statb|=2;
//                updateirqs();
                return;
                case 0x50: /*Format track*/
                addr=(((ide.cylinder*ide.hpc)+ide.head)*ide.spt)*256;
//                rpclog("Format cyl %i head %i offset %08X secount %I\n",ide.cylinder,ide.head,addr,ide.secount);
                fseek(hdfile[ide.drive],addr,SEEK_SET);
                memset(idebufferb,0,512);
                for (c=0;c<ide.secount;c++)
                {
                        fwrite(idebuffer,256,1,hdfile[ide.drive]);
                }
                ide.atastat=0x40;
//                iomd.statb|=2;
//                updateirqs();
                return;
                case 0x91: /*Set parameters*/
                ide.spt=ide.secount;
                ide.hpc=ide.head+1;
//                rpclog("%i sectors per track, %i heads per cylinder\n",ide.spt,ide.hpc);
                ide.atastat=0x40;
//                iomd.statb|=2;
//                updateirqs();
                return;
                case 0xA1:
                case 0xE3:
                ide.atastat=0x41;
                ide.error=4;
//                iomd.statb|=2;
//                updateirqs();
                return;
                case 0xEC:
                memset(idebuffer,0,512);
                idebuffer[1]=101; /*Cylinders*/
                idebuffer[3]=16;  /*Heads*/
                idebuffer[6]=63;  /*Sectors*/
                for (addr=10;addr<20;addr++)
                    idebuffer[addr]=0x2020;
                for (addr=23;addr<47;addr++)
                    idebuffer[addr]=0x2020;
                idebufferb[46^1]='v'; /*Firmware version*/
                idebufferb[47^1]='2';
                idebufferb[48^1]='.';
                idebufferb[49^1]='1';
                idebufferb[54^1]='B'; /*Drive model*/
                idebufferb[55^1]='-';
                idebufferb[56^1]='e';
                idebufferb[57^1]='m';
                idebufferb[58^1]=' ';
                idebufferb[59^1]='H';
                idebufferb[60^1]='D';
                idebuffer[50]=0x4000; /*Capabilities*/
                idebuffer[53]=1;
                idebuffer[56]=ide.spt;
                idebuffer[55]=ide.hpc;
                idebuffer[54]=(101*16*63)/(ide.spt*ide.hpc);
                idebuffer[57]=(101*16*63)&0xFFFF;
                idebuffer[58]=(101*16*63)>>16;
                ide.pos=0;
                ide.atastat=0x08;
//                rpclog("ID callback\n");
//                iomd.statb|=2;
//                updateirqs();
                return;
        }
}
/*Read 1F1*/
/*Error &108A1 - parameters not recognised*/
