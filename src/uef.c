/*B-em v2.1 by Tom Walker
  UEF/HQ-UEF tape support*/

#include <allegro.h>
#include <zlib.h>
#include <stdio.h>
#include "b-em.h"

int startchunk;
int blocks=0;
int tapelcount,tapellatch,pps;
int intone=0;
gzFile *uef=NULL;

int inchunk=0,chunkid=0,chunklen=0;
int chunkpos=0,chunkdatabits=8;
float chunkf;
int cswena;
void openuef(char *fn)
{
      int c;
//      printf("OpenUEF %s %08X\n",fn,uef);
      if (uef)
         gzclose(uef);
      uef=gzopen(fn,"rb");
      if (!uef) { /*printf("Fail!\n");*/ return; }
      for (c=0;c<12;c++)
          gzgetc(uef);
      inchunk=chunklen=chunkid=0;
      tapellatch=(1000000/(1200/10))/64;
      tapelcount=0;
      pps=120;
      cswena=0;
//      printf("Tapellatch %i\n",tapellatch);
tapeloaded=1;
//      gzseek(uef,27535,SEEK_SET);
}

int uefpos()
{
        return gztell(uef);
}

void closeuef()
{
//printf("CloseUEF\n");
        if (uef)
        {
                gzclose(uef);
                uef=NULL;
        }
}

void rewindit()
{
        int c;
        gzseek(uef,0,SEEK_SET);
        for (c=0;c<12;c++)
            gzgetc(uef);
        inchunk=chunklen=chunkid=0;
}

int ueffileopen()
{
        if (!uef)
           return 0;
        return 1;
}

int ueftoneon=0;
int infilenames=0;
int uefloop=0;
uint8_t fdat;
int ffound;
void receiveuef(uint8_t val)
{
        ueftoneon--;
        if (infilenames)
        {
                ffound=1;
                fdat=val;
//                rpclog("Dat %02X %c\n",val,(val<33)?'.':val);
        }
        else             receive(val);
}

void polltape()
{
        int c;
        uint32_t templ;
        float *tempf;
        uint8_t temp;
        if (!uef)
           return;
        if (!inchunk)
        {
                startchunk=1;
//                printf("%i ",gztell(uef));
                gzread(uef,&chunkid,2);
                gzread(uef,&chunklen,4);
                if (gzeof(uef))
                {
                        gzseek(uef,12,SEEK_SET);
                        gzread(uef,&chunkid,2);
                        gzread(uef,&chunklen,4);
                        uefloop=1;
                }
                inchunk=1;
                chunkpos=0;
//                printf("Chunk ID %04X len %i\n",chunkid,chunklen);
        }
//        else
//           printf("Chunk %04X\n",chunkid);
        switch (chunkid)
        {
                case 0x000: /*Origin*/
                for (c=0;c<chunklen;c++)
                    gzgetc(uef);
                inchunk=0;
                return;

                case 0x005: /*Target platform*/
                for (c=0;c<chunklen;c++)
                    gzgetc(uef);
                inchunk=0;
                return;

                case 0x100: /*Raw data*/
                if (startchunk)
                {
                        dcdlow();
                        startchunk=0;
                }
                chunklen--;
                if (!chunklen)
                {
                        inchunk=0;
                        blocks++;
                }
                receiveuef(gzgetc(uef));
                return;

                case 0x104: /*Defined data*/
                if (!chunkpos)
                {
                        chunkdatabits=gzgetc(uef);
                        gzgetc(uef);
                        gzgetc(uef);
                        chunklen-=3;
                        chunkpos=1;
                }
                else
                {
                        chunklen--;
                        if (chunklen<=0)
                           inchunk=0;
                        temp=gzgetc(uef);
//                        printf("%i : %i %02X\n",gztell(uef),chunklen,temp);
                        if (chunkdatabits==7) receiveuef(temp&0x7F);
                        else                  receiveuef(temp);
                }
                return;

                case 0x110: /*High tone*/
                ueftoneon=2;
                if (!intone)
                {
                        dcd();
                        intone=gzgetc(uef);
                        intone|=(gzgetc(uef)<<8);
                        intone>>=2;
                        if (!intone) intone=1;
//                        printf("intone %i\n",intone);
                }
                else
                {
                        intone--;
                        if (intone==0)
                        {
                                inchunk=0;
                        }
                }
/*                if (!intone)
                {
                        templ=gzgetc(uef); templ|=(gzgetc(uef)<<8);
//                        printf("High tone %04X\n",templ);
                        if (templ>20)
                        {
                                dcd();
                                intone=6;
                        }
                        else
                           inchunk=0;
                }
                else
                {
                        intone--;
                        if (intone==0)
                        {
                                inchunk=0;
                        }
                }*/
                return;

                case 0x111: /*High tone with dummy byte*/
                ueftoneon=2;
                if (!intone)
                {
                        dcd();
                        intone=3;
                }
                else
                {
                        if (intone==4)
                           dcd();
                        intone--;
                        if (intone==0 && inchunk==2)
                        {
                                inchunk=0;
                                gzgetc(uef); gzgetc(uef);
                                gzgetc(uef); gzgetc(uef);
                        }
                        else if (!intone)
                        {
                                inchunk=2;
                                intone=4;
                                receiveuef(0xAA);
                        }
                }
                return;

                case 0x112: /*Gap*/
                ueftoneon=0;
                if (!intone)
                {
//                        dcd();
                        intone=gzgetc(uef);
                        intone|=(gzgetc(uef)<<8);
                        intone>>=2;
//                        printf("gap intone %i\n",intone);
                        if (!intone) intone=1;
                }
                else
                {
                        intone--;
                        if (intone==0)
                        {
                                inchunk=0;
                        }
                }
                return;
/*                if (!intone)
                {
                        intone=3;
                }
                else
                {
                        intone--;
                        if (intone==0)
                        {
                                inchunk=0;
                                gzgetc(uef); gzgetc(uef);
                        }
                }*/
                return;

                case 0x113: /*Float baud rate*/
                templ=gzgetc(uef);
                templ|=(gzgetc(uef)<<8);
                templ|=(gzgetc(uef)<<16);
                templ|=(gzgetc(uef)<<24);
                tempf=(float *)&templ;
                tapellatch=(1000000/((*tempf)/10))/64;
                pps=(*tempf)/10;
                inchunk=0;
                return;

                case 0x116: /*Float gap*/
                ueftoneon=0;
                if (!chunkpos)
                {
                        templ=gzgetc(uef);
                        templ|=(gzgetc(uef)<<8);
                        templ|=(gzgetc(uef)<<16);
                        templ|=(gzgetc(uef)<<24);
                        tempf=(float *)&templ;
                        chunkf=*tempf;
                        //printf("Gap %f %i\n",chunkf,pps);
                        chunkpos=1;
//                        chunkf=4;
                }
                else
                {
//                        printf("Gap now %f\n",chunkf);
                        chunkf-=((float)1/(float)pps);
                        if (chunkf<=0) inchunk=0;
                }
                return;

                case 0x114: /*Security waves*/
                case 0x115: /*Polarity change*/
//                default:
                for (c=0;c<chunklen;c++)
                    gzgetc(uef);
                inchunk=0;
                return;

                default:
                for (c=0;c<chunklen;c++)
                    gzgetc(uef);
                inchunk=0;
                return;
//116 : float gap
//113 : float baud rate

        }
//        allegro_exit();
//        printf("Bad chunk ID %04X length %i\n",chunkid,chunklen);
//        exit(-1);
}

uint8_t fbuffer[4];

#define getuefbyte()            ffound=0; \
                                while (!ffound && !uefloop) \
                                { \
                                        polltape(); \
                                } \
                                if (uefloop) break;

uint8_t ffilename[16];
void findfilenamesuef()
{
        int temp;
        uint8_t tb;
        int c;
        int fsize=0;
        char s[256];
        uint32_t run,load;
        int offset;
        uint8_t status;
        int skip;
        int binchunk=inchunk,bchunkid=chunkid,bchunklen=chunklen;
        int bchunkpos=chunkpos,bchunkdatabits=chunkdatabits;
        int bintone=intone,bffound=ffound;
        float bchunkf=chunkf;
        uint8_t bdat=fdat;
        if (!uef) return;
        inchunk=0; chunkid=0; chunklen=0;
        chunkpos=0; chunkdatabits=8; intone=0;
        chunkf=0;
        startblit();
        temp=gztell(uef);
        gzseek(uef,12,SEEK_SET);
        uefloop=0;
        infilenames=1;
        while (!uefloop)
        {
                ffound=0;
                while (!ffound && !uefloop)
                {
                        polltape();
                }
                if (uefloop) break;
                fbuffer[0]=fbuffer[1];
                fbuffer[1]=fbuffer[2];
                fbuffer[2]=fbuffer[3];
                fbuffer[3]=fdat;
                if (fdat==0x2A && ueftoneon==1)
                {
                        fbuffer[3]=0;
                        c=0;
                        do
                        {
                                ffound=0;
                                while (!ffound && !uefloop)
                                {
                                        polltape();
                                }
                                if (uefloop) break;
                                ffilename[c++]=fdat;
                        } while (fdat!=0x0 && c<10);
                        if (uefloop) break;
                        c--;
                        while (c<13) ffilename[c++]=32;
                        ffilename[c]=0;

                                getuefbyte();
                                tb=fdat;
                                getuefbyte();
                                load=tb|(fdat<<8);
                                getuefbyte();
                                tb=fdat;
                                getuefbyte();
                                load|=(tb|(fdat<<8))<<16;

                                getuefbyte();
                                tb=fdat;
                                getuefbyte();
                                run=tb|(fdat<<8);
                                getuefbyte();
                                tb=fdat;
                                getuefbyte();
                                run|=(tb|(fdat<<8))<<16;

                                getuefbyte();
                                getuefbyte();

                                getuefbyte();
                                tb=fdat;
                                getuefbyte();
                                skip=tb|(fdat<<8);

                                fsize+=skip;

                                getuefbyte();
                                status=fdat;

                        if (status&0x80)
                        {
                                sprintf(s,"%s Size %04X Load %08X Run %08X",ffilename,fsize,load,run);
                                cataddname(s);
                                fsize=0;
                        }
                        for (c=0;c<skip+8;c++)
                        {
                                getuefbyte();
                        }
                }
        }
        infilenames=0;
        gzseek(uef,temp,SEEK_SET);
        uefloop=0;
        inchunk=binchunk;
        chunkid=bchunkid;
        chunklen=bchunklen;
        chunkpos=bchunkpos;
        chunkdatabits=bchunkdatabits;
        chunkf=bchunkf;
        fdat=bdat;
        ffound=bffound;
        intone=bintone;
        endblit();
}

