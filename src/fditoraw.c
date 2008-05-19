#include <stdio.h>
#include "fditoraw.h"

#define printf rpclog
float typetoinches[4]={8,5.25,3.5,3};

unsigned long getml(FILE *f)
{
        unsigned long templ=getc(f)<<24;
        templ|=getc(f)<<16;
        templ|=getc(f)<<8;
        templ|=getc(f);
        return templ;
}

unsigned long getmt(FILE *f)
{
        unsigned long templ=getc(f)<<16;
        templ|=getc(f)<<8;
        templ|=getc(f);
        return templ;
}


FDI *fditoraw_open(FILE *f)
{
        FDI *fdi=(FDI *)malloc(sizeof(FDI));
        int c,d;
        int temp;
        int pulses;

//        fdi->f=fopen(fn,"rb");
        fdi->f=f;
        fread(fdi->header,512,1,fdi->f);

        fdi->tracks=fdi->header[143]+(fdi->header[142]<<8)+1;
        fdi->heads=fdi->header[144]+1;

        for (c=0;c<27;c++)
            printf("%c",fdi->header[c]);
        printf("Creator : ");
        for (c=27;c<59;c++)
            printf("%c",fdi->header[c]);
        c=59;
        while (fdi->header[c]!=0x1A)
        {
                printf("%c",fdi->header[c]);
                c++;
        }
        if (c!=59) printf("\n");

        printf("FDI version %i.%i\n",fdi->header[140],fdi->header[141]);
        printf("%i heads, %i tracks\n",fdi->heads,fdi->tracks);
        printf("Disc is %f inches\n",typetoinches[fdi->header[145]&3]);
        printf("Rotation speed is %i rpm\n",fdi->header[146]+128);

        if (fdi->header[147]&1) printf("Disc is write-protected\n");
        if (fdi->header[147]&2) printf("Image is index-synchronized\n");

        if (fdi->tracks>180)
        {
                printf("Too many tracks! Max is 180\n");
                return fdi;
        }

        temp=512;
        for (c=0;c<fdi->tracks;c++)
        {
                for (d=0;d<fdi->heads;d++)
                {
                        fdi->trackdata[c][d][0]=fdi->header[(c<<fdi->heads)+(d<<1)+152];
                        fdi->trackdata[c][d][1]=fdi->header[(c<<fdi->heads)+(d<<1)+153];
                        if ((fdi->trackdata[c][d][0]&0xC0)==0x80)
                           fdi->tracksize[c][d]=(((fdi->trackdata[c][d][0]&0x3F)<<8)+fdi->trackdata[c][d][1])*256;
                        else
                           fdi->tracksize[c][d]=fdi->trackdata[c][d][1]*256;
                        fdi->trackstart[c][d]=temp;
                        printf("Track %02i Head %i - Type %02X Size %i bytes Starts at %i %08X\n",c,d,fdi->trackdata[c][d][0],fdi->tracksize[c][d],temp,temp);
                        temp+=fdi->tracksize[c][d];
                }
        }
        fseek(fdi->f,fdi->trackstart[0][0],SEEK_SET);
        pulses=getml(fdi->f);
        fdi->hd=(pulses>60000);
        return fdi;
}

int fdi2raw_get_last_track(FDI *fdi)
{
        return fdi->tracks;
}

void fditoraw_close(FDI *fdi)
{
        fclose(fdi->f);
        free(fdi);
}

typedef struct NODE
{
        unsigned short dat;
        int isleaf;
        int parent;
        int l,r;
} NODE;

NODE nodes[32768];
FILE *nf;
unsigned char nodetemp,nodetemp2;
int nodecount,_nodecount=0;
int nextnode;

int getnodedat()
{
        int t;
        if (nodecount==0)
        {
                nodecount=8;
                nodetemp=nodetemp2=getc(nf);
        }
        t=nodetemp&0x80;
        nodetemp<<=1;
        nodecount--;
        return t;
}
void addnode(int lastnode)
{
        int thisnode;
        nodes[nextnode].parent=lastnode;
        if (getnodedat())
        {
                nodes[nextnode].isleaf=1;
                _nodecount++;
                nextnode++;
                return;
        }
        nodes[nextnode].isleaf=0;
        thisnode=nextnode;
        nextnode++;
        nodes[thisnode].l=nextnode;
        addnode(thisnode);
        nodes[thisnode].r=nextnode;
        addnode(thisnode);
}

void addnode16(int num)
{
        if (nodes[num].isleaf)
        {
                nodes[num].dat=getc(nf)<<8;
                nodes[num].dat|=getc(nf);
                _nodecount++;
                return;
        }
        addnode16(nodes[num].l);
        addnode16(nodes[num].r);
}

void createtree(FILE *f)
{
        unsigned char treedat1,treedat2;
        int c;
        treedat1=getc(f);
        treedat2=getc(f);
        printf("Tree header %02X %02X\n",treedat1,treedat2);
        nf=f;
        nodecount=0;
        nextnode=0;
        addnode(-1);
        printf("Tree has %i nodes %i leaves\n",nextnode,_nodecount);
        _nodecount=0;
        addnode16(0);
        printf("%i leaves\n",_nodecount);
}

void decompress(int size, unsigned long *dat)
{
        int c;
        unsigned long v;
        int curnode;
        nodecount=0;
        for (c=0;c<size;c++)
        {
                v=0;
                curnode=0;
                while (!nodes[curnode].isleaf)
                {
                        if (getnodedat()) curnode=nodes[curnode].r;
                        else              curnode=nodes[curnode].l;
                }
                dat[c]=v=nodes[curnode].dat;
                if (!c) printf("First dat %08X\n",v);
        }
}

/*unsigned char decodefm(unsigned short dat)
{
        unsigned char temp;
        temp=0;
        if (dat&0x0001) temp|=1;
        if (dat&0x0004) temp|=2;
        if (dat&0x0010) temp|=4;
        if (dat&0x0040) temp|=8;
        if (dat&0x0100) temp|=16;
        if (dat&0x0400) temp|=32;
        if (dat&0x1000) temp|=64;
        if (dat&0x4000) temp|=128;
        return temp;
}*/

unsigned long _bdat[0x20000];
unsigned char _mfm[0x20000];
unsigned char _data[0x10000];

unsigned char mfmdat;
int bitcount=0;

void addbit(int b)
{
        if (b) _mfm[bitcount/8]|=1<<(7-(bitcount&7));
        bitcount++;
        if (!(bitcount&7)) _mfm[bitcount/8]=0;
}

unsigned long bitsum;
float bittemp;

void fdireadtrack(FDI *fdi, int track, int head, int density)
{
        int temp;
        int pulses;
//        int bitcount=0;
        int pos=0;
        int c;
        int count;
        unsigned short tempdat;
        int thresholds[3]={0x40*2,0x60*2,0x88*2};
//      int hdthresholds[3]={0x13,0x20,0x30};
FILE *f;
printf("Read track %i head %i density %i\n",track,head,density);
        if (head && fdi->heads==1)
        {
                printf("Reading Bside of single sided disc!\n");
                memset(_mfm,0,0x20000);
                return;
        }
                thresholds[0]>>=density;
                thresholds[1]>>=density;
                thresholds[2]>>=density;
        printf("Threshholds are %02X %02X %02X\n",thresholds[0],thresholds[1],thresholds[2]);
        printf("Reading Track %02i Head %i\n",track,head);
        fseek(fdi->f,fdi->trackstart[track][head],SEEK_SET);
        printf("Seeked to %08X\n",ftell(fdi->f));
        pulses=getml(fdi->f);
        printf("%i pulses on this track\n",pulses);
        temp=getmt(fdi->f);
        printf("Average stream %i bytes, type %i\n",temp&0x3FFFFF,temp>>22);
        fseek(fdi->f,fdi->trackstart[track][head]+16,SEEK_SET);
        createtree(fdi->f);
        decompress(pulses,_bdat);
        printf("Taken %i bytes\n",ftell(fdi->f)-(fdi->trackstart[track][head]+16));
//f=fopen("out.bin","wb");
//fwrite(bdat,pulses*4,1,f);
//fclose(f);
bitcount=0;
count=0;

        bitsum=0;
        for (c=1;c<pulses;c++)
            bitsum+=_bdat[c];
        if (density==2) bittemp=4000000.0/(float)bitsum;
        else            bittemp=4000000.0/(float)bitsum;

//printf("%i %f\n",bitsum,bittemp);
        for (c=1;c<pulses;c++)
            _bdat[c]*=bittemp;

        tempdat=0;
        for (c=1;c<pulses;c++)
        {
/*                if (density==2)
                {
                        if (_bdat[c]>=hdthresholds[2]) addbit(0);
                        if (_bdat[c]>=hdthresholds[1]) addbit(0);
                        if (_bdat[c]>=hdthresholds[0]) addbit(0);
                }
                else
                {*/
                        if (_bdat[c]>=thresholds[2]) addbit(0);
                        if (_bdat[c]>=thresholds[1]) addbit(0);
                        if (_bdat[c]>=thresholds[0]) addbit(0);
//                }
/*                if (_bdat[c]>=0xF8) addbit(0);
                if (_bdat[c]>=0xB8) addbit(0);
                if (_bdat[c]>=0x78) addbit(0);*/
                if (_bdat[c]>3) addbit(1);
//                if (count<16)
//                {
//                        if (density==1 && !head) printf("pulse %i %02X %i\n",_bdat[c],_bdat[c],density);
//                        count++;
//                }
        }
        printf("%i bits %i\n",pos,bitcount);
}

/*void findsync()
{
        int fdipos=0;
        int tempi;
        unsigned short fdibuffer;
        for (fdipos=0;fdipos<bitcount;fdipos++)
        {
                tempi=mfm[(fdipos/8)^1]&(1<<(7-(fdipos&7)));
                fdibuffer<<=1;
                fdibuffer|=(tempi?1:0);
                if (fdibuffer==0x4489) printf("Found sync at %i %i\n",fdipos,fdipos/8);
        }
        printf("Finished at %i\n",fdipos);
}*/

FILE *fdif;
/*FDI, buffer, timing (ignore), tracknum, &tracklen, &indexoffset*/
int fdi2raw_loadtrack(FDI *fdi, unsigned short *buffer, unsigned short *timing, int tracknum, int *tracklen, int *indexoffset, int *mr, int density)
{
        bitcount=0;
        if (fdi->heads==1) fdireadtrack(fdi,tracknum,0,density);
        else               fdireadtrack(fdi,tracknum>>1,tracknum&1,density);
//        if (!fdif) fdif=fopen("fdi.dmp","wb");
//        fwrite(_mfm,bitcount/8,1,fdif);
        *tracklen=bitcount;
        memcpy(buffer,_mfm,bitcount/8);
        *indexoffset=500;
        printf("%04X %04X %i\n",buffer[0],_mfm[0],bitcount/8);
        return 1;
}

/*int main()
{
        FDI *f;
        f=fdiopen("lem.fdi");
        fdireadtrack(f,0,0);
        fdiclose(f);
        findsync();
        printf("%i\n",_nodecount);
        return 0;
}*/
