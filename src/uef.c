#include <stdio.h>

typedef struct UEFFILE
{
        char name[20];
        unsigned char *blocks[128];
        unsigned short loadaddr,runaddr;
        int existant;
        int numblocks;
} UEFFILE;

UEFFILE getfile(unsigned char *fn);
UEFFILE files[40];
UEFFILE nofile;
int file=0;
unsigned char lastbyte;
FILE *tapelog;
int tapedelay;
FILE *ueffile;
unsigned short chunkid;
int chunklen,chunkpos;
FILE *uefout;

char temps[4096];

void inituef(char *fn)
{
        char temp[10];
        tapelog=fopen("tapelog.txt","wt");
        chunkid=chunklen=chunkpos=0;
        nofile.existant=0;
        if (chdir("uef"))
        {
                perror("uef");
                exit(-1);
        }
        loadfiles(fn);
        if (chdir(".."))
        {
                perror("..");
                exit(-1);
        }
}

void loadfiles(char *fn)
{
        int c;
        UEFFILE tempf;
        FILE *f=fopen(fn,"rb");
        int block=0;
        char temps[4096];
        char temp[10];
        int len,lenn;
        int blocklen;
        int first=1,last=0;
        unsigned char tempo;
        for (c=0;c<40;c++)
        {
                files[c].existant=files[c].numblocks=0;
        }
        fread(temp,10,1,f);
        printf("%s\n",temp);
        tempo=getc(f);
        printf("Version %i.%03i\n",getc(f),tempo);
        while (!feof(f))
        {
                chunkid=getc(f)|(getc(f)<<8);
                chunklen=getc(f)|(getc(f)<<8)|(getc(f)<<16)|(getc(f)<<24);
                if (feof(f))
                   break;
                switch (chunkid)
                {
                        case 0x0000: //Info
                        fread(temps,chunklen,1,f);
                        printf("%s\n",temps);
                        len=chunklen;
                        break;
                        case 0x0002: //Credits
                        fread(temps,chunklen,1,f);
                        printf("%s\n",temps);
                        len=chunklen;
                        break;
                        case 0x0100: //Tape grab
                        lenn=ftell(f);
                        if (first)
                        {
                                first=0;
                                files[file].existant=1;
                                files[file].numblocks++;
                                while (getc(f)!=0x2A);
                                c=0;
                                while ((temps[c]=getc(f))!=0)
                                {
                                        c++;
                                }
                                strcpy(files[file].name,temps);
                                printf("Filename %s\n",temps);
                                files[file].loadaddr=getc(f)|(getc(f)<<8);
                                printf("Load address %04X\n",files[file].loadaddr);
                                getc(f);
                                getc(f);
                                files[file].runaddr=getc(f)|(getc(f)<<8);
                                printf("Run address %04X\n",files[file].runaddr);
                                getc(f);
                                getc(f);
                                getc(f);
                                getc(f);
                                blocklen=getc(f)|(getc(f)<<8);
                                if (getc(f)&0x80)
                                {
                                        file++;
                                        first=1;
                                }
                                getc(f);
                                getc(f);
                                getc(f);
                                getc(f);
                                getc(f);
                                getc(f);
                                fread(temps,blocklen,1,f);
                                files[file].blocks[block]=(unsigned char *)malloc(256);
                                memcpy(files[file].blocks[block],temps,blocklen);
                                block++;
                                getc(f);
                                getc(f);
                                len=ftell(f)-lenn;
                        }
                        else
                        {
                                files[file].numblocks++;
                                while (getc(f)!=0x2A);
                                while (getc(f)!=0);
                                getc(f);
                                getc(f);
                                getc(f);
                                getc(f);
                                getc(f);
                                getc(f);
                                getc(f);
                                getc(f);
                                getc(f);
                                getc(f);
                                blocklen=getc(f)|(getc(f)<<8);
                                if (getc(f)&0x80)
                                {
                                        first=1;
                                        file++;
                                        block=0;
                                }
                                getc(f);
                                getc(f);
                                getc(f);
                                getc(f);
                                getc(f);
                                getc(f);
                                fread(temps,blocklen,1,f);
                                files[file].blocks[block]=(unsigned char *)malloc(256);
                                memcpy(files[file].blocks[block],temps,blocklen);
                                block++;
                                getc(f);
                                getc(f);
                                len=ftell(f)-lenn;
                        }
                        break;
                        case 0x0110: //Tone
                        getc(f); getc(f);
                        len=2;
                        break;
                        case 0x0112: //Gap
                        getc(f); getc(f);
                        len=2;
                        break;
                        default:
/*                        printf("Unrecognised chunk ID %04X length %i\n",chunkid,chunklen);
                        fclose(f);
                        exit(-1);*/
                        /*skip it*/
                        fseek(f,chunklen,SEEK_CUR);
                        break;
                }
        }
        file=0;
        clrscr();
        c=0;
        while (c<40)
        {
                while (!files[c].existant && c<40)
                      c++;
                if (c==40)
                   break;
                printf("%s load %04X run %04X  %i blocks\n",files[c].name,files[c].loadaddr,files[c].runaddr,files[c].numblocks);
                c++;
        }
        fclose(f);
//        tempf=getfile("ARCADIANS");
//        printf("%s load %04X run %04X  %i blocks\n",tempf.name,tempf.loadaddr,tempf.runaddr,tempf.numblocks);
//        waitkey();
}

UEFFILE getfile(unsigned char *fn)
{
        int oldfile=file;
        int done=0;
        UEFFILE f;
//        printf("Trying to find %s\n",fn);
        while (file!=oldfile || !done)
        {
                done=1;
                if (!files[file].existant)
                {
//                        printf("nofile\n");
                        file++;
                        while (!files[file].existant && file!=oldfile)
                        {
                                file++;
                                if (file==40)
                                   file=0;
                        }
                        if (file==oldfile)
                           return nofile;
                }
                if (!stricmp(files[file].name,fn))
                {
//                        printf("%s %s found\n",files[file].name,fn);
                        f=files[file];
                        file++;
                        return f;
                }
                else
                   printf("%s %s\n",files[file].name,fn);
                file++;
        }
        return nofile;
}

/*void uefbyte()
{
        unsigned char tempo;
        topuef:
        if (chunkpos==chunklen)
        {
                chunkid=getc(ueffile)|(getc(ueffile)<<8);
                chunklen=getc(ueffile)|(getc(ueffile)<<8)|(getc(ueffile)<<16)|(getc(ueffile)<<24);
                chunkpos=0;
                if (feof(ueffile))
                {
                        fclose(ueffile);
                        inituef();
                        chunkpos=chunklen=0;
                        goto topuef;
                }
        }
                switch (chunkid)
                {
                        case 0x0000: //Info
                        fread(temps,chunklen,1,ueffile);
                        chunkpos=chunklen;
                        break;
                        case 0x0100: //Tape grab
                        temps[0]=getc(ueffile);
                        putc(temps[0],uefout);
                        writetoacai(temps[0]);
                        lastbyte=temps[0];
                        chunkpos++;
                        break;
                        case 0x0110: //Tone
                        dcd();
                        tapedelay=getc(ueffile)|(getc(ueffile)<<8);
                        tapedelay>>=3;
                        chunkpos=chunklen;
                        break;
                        case 0x0112: //Gap
                        tapedelay=getc(ueffile)|(getc(ueffile)<<8);
                        chunkpos=chunklen;
                        tapedelay>>=3;
                        break;
                        default:
                        closegfx();
                        printf("Unrecognised chunk ID %04X length %i\n",chunkid,chunklen);
                        exit(-1);
                }
}*/

