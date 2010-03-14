/*B-em v2.0 by Tom Walker
  CSW cassette support*/
#include <stdio.h>
#include <stdlib.h>
#include <zlib.h>
#include "b-em.h"

int cintone=1,cindat=0,datbits=0,enddat=0;
FILE *cswf=NULL;
char csws[256];
FILE *cswlog;
uint8_t *cswdat=NULL;
int cswpoint;
uint8_t cswhead[0x34];
int tapelcount,tapellatch,pps;
int cswena;
int cswskip=0;
int tapespeed;
int cswloop=1;

void opencsw(char *fn)
{
        int end,c;
        unsigned long destlen=8*1024*1024;
        uint8_t *tempin;
        if (cswf) fclose(cswf);
        if (cswdat) free(cswdat);
        cswena=1;
        /*Allocate buffer*/
        cswdat=malloc(8*1024*1024);
        /*Open file and get size*/
        cswf=fopen(fn,"rb");
        if (!cswf)
        {
                free(cswdat);
                cswdat=NULL;
                return;
        }
        fseek(cswf,-1,SEEK_END);
        end=ftell(cswf);
        fseek(cswf,0,SEEK_SET);
        /*Read header*/
        fread(cswhead,0x34,1,cswf);
        for (c=0;c<cswhead[0x23];c++) getc(cswf);
        /*Allocate temporary memory and read file into memory*/
        end-=ftell(cswf);
        tempin=malloc(end);
        fread(tempin,end,1,cswf);
        fclose(cswf);
//        sprintf(csws,"Decompressing %i %i\n",destlen,end);
//        fputs(csws,cswlog);
        /*Decompress*/
        uncompress(cswdat,&destlen,tempin,end);
        free(tempin);
        /*Reset data pointer*/
        cswpoint=0;
        dcd();
      tapellatch=(1000000/(1200/10))/64;
      tapelcount=0;
      pps=120;
        tapeloaded=1;
}

void closecsw()
{
        if (cswf) fclose(cswf);
        if (cswdat) free(cswdat);
        cswdat=NULL;
        cswf=NULL;
}

int cswtoneon=0;
int ffound,fdat;
int infilenames;
void receivecsw(uint8_t val)
{
        cswtoneon--;
        if (infilenames)
        {
                ffound=1;
                fdat=val;
//                rpclog("Dat %02X %c\n",val,(val<33)?'.':val);
        }
        else             receive(val);
}

void pollcsw()
{
        int c;
        uint8_t dat;
//        sprintf(csws,"CSW poll %08X %i\n",cswdat,cswpoint);
//        fputs(csws,cswlog);
//rpclog("Poll %08X\n",cswdat);
        if (!cswdat) return;
/*        if (!cswf)
        {
                cswf=fopen("3dgran.out","rb");
//                fseek(cswf,0x500,SEEK_SET);
                if (!cswlog) cswlog=fopen("cswlog.txt","wt");
        dcd();
        }*/
//        rpclog("Poll %i\n",c);
        for (c=0;c<10;c++)
        {
                dat=cswdat[cswpoint++];
//                if (cswpoint<1000) rpclog("%i : %02X %i %i %i %i\n",cswpoint,dat,cintone,cindat,datbits,cswskip);
                if (cswpoint>=(8*1024*1024))
                {
                        cswpoint=0;
                        cswloop=1;
//                        rpclog("Loop!\n");
                }
                if (cswskip)
                   cswskip--;
                else if (cintone && dat>0xD) /*Not in tone any more - data start bit*/
                {
//                        rpclog("Entered dat\n");
                        cswpoint++; /*Skip next half of wave*/
                        if (tapespeed) cswskip=6;
                        cintone=0;
                        cindat=1;
//                        sprintf(csws,"Entered data at %i\n",ftell(cswf));
//                        fputs(csws,cswlog);
                        datbits=enddat=0;
                        dcdlow();
//                        rpclog("Start bit\n");
                        return;
                }
                else if (cindat && datbits!=-1 && datbits!=-2)
                {
                        cswpoint++; /*Skip next half of wave*/
                        if (tapespeed) cswskip=6;
                        enddat>>=1;
//                        rpclog("Cindat %02X\n",dat);
                        if (dat<=0xD)
                        {
                                cswpoint+=2;
                                if (tapespeed) cswskip+=6;
                                enddat|=0x80;
                        }
                        datbits++;
                        if (datbits==8)
                        {
//                                rpclog("Received 8 bits %02X %c %i\n",enddat,(enddat<33)?'.':enddat,cswpoint);
                                receivecsw(enddat);
//                                sprintf(csws,"Received 8 bits %02X %c\n",enddat,enddat);
//                                fputs(csws,cswlog);
                                datbits=-2;
                                return;
                        }
                }
                else if (cindat && datbits==-2) /*Deal with stop bit*/
                {
//                        rpclog("Stop bit\n");
                        cswpoint++;
                        if (tapespeed) cswskip=6;
                        if (dat<=0xD)
                        {
                                cswpoint+=2;
                                if (tapespeed) cswskip+=6;
                        }
                        datbits=-1;
                }
                else if (cindat && datbits==-1)
                {
                        if (dat<=0xD) /*Back in tone again*/
                        {
//                                rpclog("Tone\n");
                                dcd();
//                                sprintf(csws,"Entering tone again\n");
//                                fputs(csws,cswlog);
                                cswtoneon=2;
                                cindat=0;
                                cintone=1;
                                datbits=0;
                                return;
                        }
                        else /*Start bit*/
                        {
//                                rpclog("Start bit\n");
                                cswpoint++; /*Skip next half of wave*/
                                if (tapespeed) cswskip+=6;
                                datbits=0;
                                enddat=0;
                        }
                }
        }
}

#define getcswbyte()            ffound=0; \
                                while (!ffound && !cswloop) \
                                { \
                                        pollcsw(); \
                                } \
                                if (cswloop) break;

uint8_t ffilename[16];
void findfilenamescsw()
{
        int temp,temps,tempd,tempi,tempsk,tempspd;
        uint8_t tb;
        int c;
        int fsize=0;
        char s[256];
        uint32_t run,load;
        int offset;
        uint8_t status;
        int skip;
        if (!cswdat) return;
        startblit();
        temp=cswpoint;
        temps=cindat;
        tempi=cintone;
        tempd=datbits;
        tempsk=cswskip;
        tempspd=tapespeed;
        cswpoint=0;

        cindat=cintone=datbits=cswskip=0;
        cintone=1;
        tapespeed=0;

//        gzseek(csw,12,SEEK_SET);
        cswloop=0;
        infilenames=1;
        while (!cswloop)
        {
//                rpclog("Start\n");
                ffound=0;
                while (!ffound && !cswloop)
                {
                        pollcsw();
                }
                if (cswloop) break;
//                rpclog("FDAT %02X cswtoneon %i\n",fdat,cswtoneon);
                if (fdat==0x2A && cswtoneon==1)
                {
                        c=0;
                        do
                        {
                                ffound=0;
                                while (!ffound && !cswloop)
                                {
                                        pollcsw();
                                }
                                if (cswloop) break;
                                ffilename[c++]=fdat;
                        } while (fdat!=0x0 && c<=10);
                        if (cswloop) break;
                        c--;
                        while (c<13) ffilename[c++]=32;
                        ffilename[c]=0;

                                getcswbyte();
                                tb=fdat;
                                getcswbyte();
                                load=tb|(fdat<<8);
                                getcswbyte();
                                tb=fdat;
                                getcswbyte();
                                load|=(tb|(fdat<<8))<<16;

                                getcswbyte();
                                tb=fdat;
                                getcswbyte();
                                run=tb|(fdat<<8);
                                getcswbyte();
                                tb=fdat;
                                getcswbyte();
                                run|=(tb|(fdat<<8))<<16;

                                getcswbyte();
                                getcswbyte();

                                getcswbyte();
                                tb=fdat;
                                getcswbyte();
                                skip=tb|(fdat<<8);

                                fsize+=skip;

                                getcswbyte();
                                status=fdat;

//rpclog("Got block - %08X %08X %02X\n",load,run,status);
                        if (status&0x80)
                        {
                                sprintf(s,"%s Size %04X Load %08X Run %08X",ffilename,fsize,load,run);
                                cataddname(s);
                                fsize=0;
                        }
                        for (c=0;c<skip+8;c++)
                        {
                                getcswbyte();
                        }
                }
        }
        infilenames=0;
        cindat=temps;
        cintone=tempi;
        datbits=tempd;
        cswskip=tempsk;
        tapespeed=tempspd;
        cswpoint=temp;
        cswloop=0;
        endblit();
}

