/*B-em 0.7 by Tom Walker*/
/*UEF handling*/

#include <zlib.h>
#include <stdio.h>

#define HIGHTONE 0x40
int curtapebyte=0,curblock=0;
unsigned char receivebyte;
int tapeon;
int intone=0;
gzFile *uef;
char uefname[260];
int inchunk=0,chunkid=0,chunklen=0;

void openuef(char *fn)
{
      int c;
      char tempname[260];
      memcpy(tempname,fn,260);
      if (uef)
         gzclose(uef);
      uef=gzopen(tempname,"rb");
      for (c=0;c<12;c++)
          gzgetc(uef);
}

void rewindit()
{
        int c;
        gzseek(uef,0,SEEK_SET);
        for (c=0;c<12;c++)
            gzgetc(uef);
        inchunk=0;
}

int ueffileopen()
{
        if (!uef)
           return 0;
        return 1;
}

int startchunk=0;

void polltape()
{
      int c;
      if (!uef)
         return;
      if (!inchunk)
      {
            gzread(uef,&chunkid,2);
            printf("Chunk %03X\n",chunkid);
            gzread(uef,&chunklen,4);
            if (gzeof(uef))
            {
                  gzseek(uef,12,SEEK_SET);
                  gzread(uef,&chunkid,2);
                  gzread(uef,&chunklen,4);
            }
            inchunk=1;
            startchunk=1;
      }
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
/*                if (chunklen==1)
                {
                        gzgetc(uef);
                        inchunk=0;
                        return;
                }
                else
                {*/
                        dcdlow();
                        startchunk=0;
//                }

//                chunklen--;
//                printf("Chunklen %i\n",chunklen);
            }
            curtapebyte=chunklen;
            chunklen--;
            if (!chunklen)
            {
                inchunk=0;
                curblock++;
            }
            receive(gzgetc(uef));
            return;

            case 0x110: /*High tone*/
            if (!intone)
            {
                  dcd();
                  intone=11;
            }
            else
            {
//                  if (intone==4) dcdlow();
                  intone--;
                  if (intone==0)
                  {
                        inchunk=0;
                        gzgetc(uef); gzgetc(uef);
                  }
            }
            return;

            case 0x112: /*Gap*/
/*            inchunk=0;
            gzgetc(uef); gzgetc(uef);
            return;*/
            if (!intone)
            {
//                  printf("GAP\n");
                  dcd();
                  intone=50;
            }
            else
            {
                  intone--;
                  if (intone==0)
                  {
                        inchunk=0;
                        gzgetc(uef); gzgetc(uef);
                  }
            }
            return;
      }
      allegro_exit();
      printf("Bad chunk ID %04X length %i\n",chunkid,chunklen);
      exit(-1);
}

void closeuef()
{
        if (uef)
           gzclose(uef);
}
