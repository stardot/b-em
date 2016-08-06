/*B-em v2.2 by Tom Walker
  UEF/HQ-UEF tape support*/

#include <allegro.h>
#include <zlib.h>
#include <stdio.h>
#include "b-em.h"
#include "acia.h"
#include "csw.h"
#include "uef.h"
#include "tape.h"

int tapelcount, tapellatch, pps;
gzFile uef_f = NULL;

int uef_toneon = 0;

static int uef_inchunk = 0, uef_chunkid = 0, uef_chunklen = 0;
static int uef_chunkpos = 0, uef_chunkdatabits = 8;
static int uef_startchunk;
static float uef_chunkf;
static int uef_intone = 0;

void uef_load(char *fn)
{
        int c;
//      printf("OpenUEF %s %08X\n",fn,uef);
        if (uef_f)
           gzclose(uef_f);
        uef_f = gzopen(fn, "rb");
        if (!uef_f) { /*printf("Fail!\n");*/ return; }
        for (c = 0; c < 12; c++)
            gzgetc(uef_f);
        uef_inchunk = uef_chunklen = uef_chunkid = 0;
        tapellatch = (1000000 / (1200 / 10)) / 64;
        tapelcount = 0;
        pps = 120;
        csw_ena = 0;
//      printf("Tapellatch %i\n",tapellatch);
        tape_loaded = 1;
//      gzseek(uef,27535,SEEK_SET);
}

void uef_close()
{
//printf("CloseUEF\n");
        if (uef_f)
        {
                gzclose(uef_f);
                uef_f = NULL;
        }
}

int infilenames = 0;
int uefloop = 0;
uint8_t fdat;
int ffound;
static void uef_receive(uint8_t val)
{
        uef_toneon--;
        if (infilenames)
        {
                ffound = 1;
                fdat = val;
//                bem_debugf("Dat %02X %c\n",val,(val<33)?'.':val);
        }
        else
        {
                acia_receive(val);
//                bem_debugf("Dat %02X\n",val);
        }
}

void uef_poll()
{
        int c;
        uint32_t templ;
        float *tempf;
        uint8_t temp;
        if (!uef_f)
           return;
        if (!uef_inchunk)
        {
                uef_startchunk = 1;
//                printf("%i ",gztell(uef));
                gzread(uef_f, &uef_chunkid, 2);
                gzread(uef_f, &uef_chunklen, 4);
                if (gzeof(uef_f))
                {
                        gzseek(uef_f, 12, SEEK_SET);
                        gzread(uef_f, &uef_chunkid, 2);
                        gzread(uef_f, &uef_chunklen, 4);
                        uefloop = 1;
                }
                uef_inchunk = 1;
                uef_chunkpos = 0;
//                printf("Chunk ID %04X len %i\n",uef_chunkid,uef_chunklen);
        }
//        else
//           printf("Chunk %04X\n",uef_chunkid);
        switch (uef_chunkid)
        {
                case 0x000: /*Origin*/
                for (c = 0; c < uef_chunklen; c++)
                    gzgetc(uef_f);
                uef_inchunk = 0;
                return;

                case 0x005: /*Target platform*/
                for (c = 0; c < uef_chunklen; c++)
                    gzgetc(uef_f);
                uef_inchunk = 0;
                return;

                case 0x100: /*Raw data*/
                if (uef_startchunk)
                {
                        dcdlow();
                        uef_startchunk = 0;
                }
                uef_chunklen--;
                if (!uef_chunklen)
                {
                        uef_inchunk = 0;
                }
                uef_receive(gzgetc(uef_f));
                return;

                case 0x104: /*Defined data*/
                if (!uef_chunkpos)
                {
                        uef_chunkdatabits = gzgetc(uef_f);
                        gzgetc(uef_f);
                        gzgetc(uef_f);
                        uef_chunklen -= 3;
                        uef_chunkpos = 1;
                }
                else
                {
                        uef_chunklen--;
                        if (uef_chunklen <= 0)
                           uef_inchunk = 0;
                        temp = gzgetc(uef_f);
//                        printf("%i : %i %02X\n",gztell(uef),uef_chunklen,temp);
                        if (uef_chunkdatabits == 7) uef_receive(temp & 0x7F);
                        else                        uef_receive(temp);
                }
                return;

                case 0x110: /*High tone*/
                uef_toneon = 2;
                if (!uef_intone)
                {
                        dcd();
                        uef_intone = gzgetc(uef_f);
                        uef_intone |= (gzgetc(uef_f) << 8);
                        uef_intone /= 20;
                        if (!uef_intone) uef_intone = 1;
//                        printf("uef_intone %i\n",uef_intone);
                }
                else
                {
                        uef_intone--;
                        if (uef_intone == 0)
                        {
                                uef_inchunk = 0;
                        }
                }
                return;

                case 0x111: /*High tone with dummy byte*/
                uef_toneon = 2;
                if (!uef_intone)
                {
                        dcd();
                        uef_intone = gzgetc(uef_f);
                        uef_intone |= (gzgetc(uef_f)<<8);
                        uef_intone /= 20;
                        if (!uef_intone) uef_intone = 1;
                }
                else
                {
                        uef_intone--;
                        if (uef_intone == 0 && uef_inchunk == 2)
                        {
                                uef_inchunk = 0;
                        }
                        else if (!uef_intone)
                        {
                                uef_inchunk = 2;
                                uef_intone = gzgetc(uef_f);
                                uef_intone |= (gzgetc(uef_f) << 8);
                                uef_intone /= 20;
                                if (!uef_intone) uef_intone = 1;
                                uef_receive(0xAA);
                        }
                }
                return;

                case 0x112: /*Gap*/
                uef_toneon = 0;
                if (!uef_intone)
                {
//                        dcd();
                        uef_intone = gzgetc(uef_f);
                        uef_intone |= (gzgetc(uef_f) << 8);
                        uef_intone /= 20;
//                        printf("gap uef_intone %i\n",uef_intone);
                        if (!uef_intone) uef_intone = 1;
                }
                else
                {
                        uef_intone--;
                        if (uef_intone == 0)
                        {
                                uef_inchunk = 0;
                        }
                }
                return;

                case 0x113: /*Float baud rate*/
                templ = gzgetc(uef_f);
                templ |= (gzgetc(uef_f) << 8);
                templ |= (gzgetc(uef_f) << 16);
                templ |= (gzgetc(uef_f) << 24);
                tempf = (float *)&templ;
                tapellatch = (1000000 / ((*tempf) / 10)) / 64;
                pps = (*tempf) / 10;
                uef_inchunk = 0;
                return;

                case 0x116: /*Float gap*/
                uef_toneon = 0;
                if (!uef_chunkpos)
                {
                        templ = gzgetc(uef_f);
                        templ |= (gzgetc(uef_f) << 8);
                        templ |= (gzgetc(uef_f) << 16);
                        templ |= (gzgetc(uef_f) << 24);
                        tempf = (float *)&templ;
                        uef_chunkf = *tempf;
                        //printf("Gap %f %i\n",uef_chunkf,pps);
                        uef_chunkpos = 1;
//                        uef_chunkf=4;
                }
                else
                {
//                        printf("Gap now %f\n",uef_chunkf);
                        uef_chunkf -= ((float)1 / (float)pps);
                        if (uef_chunkf <= 0) uef_inchunk = 0;
                }
                return;

                case 0x114: /*Security waves*/
                case 0x115: /*Polarity change*/
//                default:
                for (c = 0; c < uef_chunklen; c++)
                    gzgetc(uef_f);
                uef_inchunk = 0;
                return;

                default:
                for (c = 0; c < uef_chunklen; c++)
                    gzgetc(uef_f);
                uef_inchunk = 0;
                return;
//116 : float gap
//113 : float baud rate

        }
//        allegro_exit();
//        printf("Bad chunk ID %04X length %i\n",uef_chunkid,uef_chunklen);
//        exit(-1);
}

uint8_t fbuffer[4];

#define getuefbyte()            ffound = 0; \
                                while (!ffound && !uefloop) \
                                { \
                                        uef_poll(); \
                                } \
                                if (uefloop) break;

uint8_t ffilename[16];
void uef_findfilenames()
{
        int temp;
        uint8_t tb;
        int c;
        int fsize = 0;
        char s[256];
        uint32_t run, load;
        uint8_t status;
        int skip;
        int binchunk  = uef_inchunk,  bchunkid = uef_chunkid, bchunklen = uef_chunklen;
        int bchunkpos = uef_chunkpos, bchunkdatabits = uef_chunkdatabits;
        int bintone   = uef_intone,   bffound = ffound;
        float bchunkf = uef_chunkf;
        uint8_t bdat  = fdat;
        if (!uef_f) return;

        uef_inchunk  = 0; uef_chunkid = 0; uef_chunklen = 0;
        uef_chunkpos = 0; uef_chunkdatabits = 8; uef_intone = 0;
        uef_chunkf   = 0;

        startblit();
        temp=gztell(uef_f);
        gzseek(uef_f, 12, SEEK_SET);
        uefloop = 0;
        infilenames = 1;
        while (!uefloop)
        {
                ffound = 0;
                while (!ffound && !uefloop)
                {
                        uef_poll();
                }
                if (uefloop) break;
                fbuffer[0] = fbuffer[1];
                fbuffer[1] = fbuffer[2];
                fbuffer[2] = fbuffer[3];
                fbuffer[3] = fdat;
                if (fdat == 0x2A && uef_toneon == 1)
                {
                        fbuffer[3] = 0;
                        c = 0;
                        do
                        {
                                ffound = 0;
                                while (!ffound && !uefloop)
                                {
                                        uef_poll();
                                }
                                if (uefloop) break;
                                ffilename[c++] = fdat;
                        } while (fdat != 0x0 && c < 10);
                        if (uefloop) break;
                        c--;
                        while (c < 13) ffilename[c++] = 32;
                        ffilename[c] = 0;

                                getuefbyte();
                                tb = fdat;
                                getuefbyte();
                                load = tb | (fdat << 8);
                                getuefbyte();
                                tb = fdat;
                                getuefbyte();
                                load |= (tb | (fdat << 8)) << 16;

                                getuefbyte();
                                tb = fdat;
                                getuefbyte();
                                run = tb | (fdat << 8);
                                getuefbyte();
                                tb = fdat;
                                getuefbyte();
                                run |= (tb | (fdat << 8)) << 16;

                                getuefbyte();
                                getuefbyte();

                                getuefbyte();
                                tb = fdat;
                                getuefbyte();
                                skip = tb | (fdat << 8);

                                fsize += skip;

                                getuefbyte();
                                status = fdat;

                        if (status & 0x80)
                        {
                                sprintf(s, "%s Size %04X Load %08X Run %08X", ffilename, fsize, load, run);
                                cataddname(s);
                                fsize = 0;
                        }
                        for (c = 0; c <skip + 8; c++)
                        {
                                getuefbyte();
                        }
                }
        }
        infilenames = 0;
        gzseek(uef_f, temp, SEEK_SET);
        uefloop = 0;
        uef_inchunk       = binchunk;
        uef_chunkid       = bchunkid;
        uef_chunklen      = bchunklen;
        uef_chunkpos      = bchunkpos;
        uef_chunkdatabits = bchunkdatabits;
        uef_chunkf        = bchunkf;
        fdat = bdat;
        ffound = bffound;
        uef_intone = bintone;
        endblit();
}

