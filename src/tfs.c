/*B-em 1.4 by Tom Walker*/
/*TFS - tape filing system*/

#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <allegro.h>
#include "tfs.h"

int uefena,model;
unsigned char a,x,y,s;
unsigned char *ram,os[16384];
unsigned short pc;
FILE *catalog;
int exeaddr,load,start,end;
void copyfilename(int src, char *dest);
int gotfilelist=0;
char files[24][80];
int numofiles=0;
char xbeebfiles[31][12];
int xbeebloadaddr[31];
int xbeebexeaddr[31];

void loadcatalog()
{
        int c,d,e;
        char temp[80];
        char s1[40],s2[40],s3[40];
        for (c=0;c<31;c++)
            memset(xbeebfiles[c],0,12);
        catalog=fopen("inf/__catalog__","r");
        if (!catalog)
           return;
        for (c=0;c<31;c++)
        {
                for (d=0;d<40;d++)
                    s1[d]=s2[d]=s3[d]=0;
                fgets(temp,80,catalog);
                sscanf(temp,"%s %s %s",s1,s2,s3);
                e=0;
                for (d=0;d<11;d++)
                {
                        if (s1[d])
                        {
                                xbeebfiles[c][e++]=s1[d];
                        }
                        else break;
                }
                xbeebloadaddr[c]=strtol(s2,NULL,16);
                xbeebexeaddr[c]=strtol(s3,NULL,16);
        }
}

void trapos()
{
        if (uefena || model>2) return;
//        diskenabled=1;
//        if (diskenabled)
//        {
//printf("OS TRAPPED\n");
                os[0x327D]=0x92;
                os[0x31B1]=0x2;
//                loadcatalog();
//        }
}

void loadfilelist()
{
        FILE *f=fopen("files.lst","rt");
        char s[40];
        int c=0,d;
        if (!f)
        {
                numofiles=1;
                sprintf(files[0],"No file list - *CAT not supported%c%c",0xA,0xD);
                return;
        }
        while ((c<24)&&fgets(files[c],24,f))
        {
                d=strlen(files[c]);
                files[c][d]=0xD;
                files[c][d+1]=0;
                c++;
                numofiles++;
        }
        fclose(f);
}

int loadfile(char *fn, char *fn2)
{
        int c,length;
        char ss[80],s1[40],s2[40],s3[40];
        char error[30];
        int addr;
        int d;
        unsigned char val;
        FILE *f;
        loaded=1;
        if (chdir("inf"))
        {
                perror("inf");
                printf("File error\n");
                exit(-1);
        }
        strcat(fn2,".inf");
        f=fopen(fn2,"rt");
        if (f)
        {
                fgets(ss,40,f);
                sscanf(ss,"%s %s %s",s1,s2,s3);
                if (!load)
                {
                        load=strtol(s2,NULL,16);
                        load&=0xFFFF;
                }
                exeaddr=strtol(s3,NULL,16);
                exeaddr&=0xFFFF;
                fclose(f);
                f=fopen(fn,"rb");
                if (!f)
                {
                        sprintf(error,"File not found %s\n%c",fn,0xD);
                        ram[0xB0]=0;
                        ram[0xB1]=0;
                        for (c=0;c<strlen(error);c++)
                            ram[0xB0+c]=error[c];
                        pc=0xB0;
                        if (chdir(".."))
                        {
                                perror("..");
                                printf("file not found\n");
                                exit(-1);
                        }
                        return -1;
                }
                fseek(f,-1,SEEK_END);
                length=ftell(f);
                fseek(f,0,SEEK_SET);
                for (c=0;c<length+1;c++)
                    writememl(c+load,getc(f));
                fclose(f);
                if (chdir(".."))
                {
                        perror("..");
                        printf("file really not found\n");
                        exit(-1);
                }
                return 0;
        }
        else
        {
                strcpy(ss,fn);
                if (fn[1]!='.')
                {
                        strcpy(ss,"$.");
                        strcat(ss,fn);
                }
                for (c=0;c<31;c++)
                {
                        if (!strcasecmp(ss,xbeebfiles[c]))
                        {
                                f=fopen(ss,"rb");
                                fseek(f,-1,SEEK_END);
                                length=ftell(f);
                                fseek(f,0,SEEK_SET);
                                for (c=0;c<length+1;c++)
                                    writememl(c+load,getc(f));
                                fclose(f);
                                if (chdir(".."))
                                {
                                        perror("..");
                                        printf("xbeeb file not found\n");
                                        exit(-1);
                                }
                                return 0;
                        }
                }
                sprintf(error,"File not found %s\n%c",fn,0xD);
                ram[0xB0]=0;
                ram[0xB1]=0xFF;
                for (c=0;c<strlen(error);c++)
                    ram[0xB2+c]=error[c];
                pc=0xB0;
                if (chdir(".."))
                {
                        perror("..");
                        printf("even more file not found\n");
                        exit(-1);
                }
                return -1;
        }
        if (chdir(".."))
        {
                perror("..");
                printf("Blarg\n");
                exit(-1);
        }
        return 0;
}

void savefile(char fname[16],char fname2[16],int start,int end)
{
        FILE *f;
        char temp[40];
        if (chdir("inf"))
        {
                perror("inf");
                printf("save file error\n");
                exit(-1);
        }
        strcat(fname2,".inf");
        f=fopen(fname2,"wt");
        sprintf(temp,"%s %X %X",fname,start,end);
        fputs(temp,f);
        fclose(f);
        f=fopen(fname,"wb");
        fwrite(ram+(start&0xFFFF),end-start,1,f);
        fclose(f);
        if (chdir(".."))
        {
                perror("..");
                printf("save file error 2\n");
                exit(-1);
        }
}

int OSFILE()
{
        char fname[16];
        char fname2[16];
        int c;
        int paramblock=(y<<8)|x;
        int fnaddr=ram[paramblock]|(ram[paramblock+1]<<8);
//        printf("OSFILE %02X\n",a);
        if (a==0xFF)
        {
                copyfilename(fnaddr,fname);
                if (ram[(paramblock+6)&0xffff]==0)
                   load=ram[(paramblock+2)&0xffff]|(ram[(paramblock+3)&0xffff]<<8)|(ram[(paramblock+4)&0xffff]<<16)|(ram[(paramblock+5)&0xffff]<<24);
                else
                   load=0;
                for (c=0;c<16;c++)
                    fname2[c]=fname[c];
//                printf("Loading file %s\n",fname);
                if (loadfile(fname,fname2))
                   return 0x7F;
                return 0xFF;
        }
        if (a==0)
        {
                copyfilename(fnaddr,fname);
                load=ram[((paramblock+2) & 0xffff)]+
                                                (ram[((paramblock+3) & 0xffff)] << 8) +
                                                (ram[((paramblock+4) & 0xffff)] << 16) +
                                                (ram[((paramblock+5) & 0xffff)] << 24);
                        exeaddr = ram[((paramblock+6) & 0xffff)] +
                                                (ram[((paramblock+7) & 0xffff)] << 8) +
                                                (ram[((paramblock+8) & 0xffff)] << 16) +
                                                (ram[((paramblock+9) & 0xffff)] << 24);
                        start = ram[((paramblock+10) & 0xffff)] +
                                                (ram[((paramblock+11) & 0xffff)] << 8) +
                                                (ram[((paramblock+12) & 0xffff)] << 16) +
                                                (ram[((paramblock+13) & 0xffff)] << 24);
                        end = ram[(paramblock+14)] +
                                                (ram[((paramblock+15) & 0xffff)] << 8) +
                                                (ram[((paramblock+16) & 0xffff)] << 16) +
                                                (ram[((paramblock+17) & 0xffff)] << 24);
                        for (c=0;c<16;c++)
                            fname2[c]=fname[c];
                        savefile(fname, fname2, start, end);
                        return 0;
       }
       set_gfx_mode(GFX_TEXT,0,0,0,0);
       printf("Error : unknown OSFILE operation %X [%i]\n",a,a);
       exit(-1);
       return 0;
}

int catstatus=0;
char catstring[40];
int catline,catchar;
char catted[]="*CAT\n";
char ran[]="RUN\n";

#define PUSH(val)\
        writememl(0x100+s,val);\
        s--;

int OSFSC()
{
        char fname[16];
        char fname2[16];
        int c;
        int fnaddr=(y<<8)+x;
//        printf("OSFSC %02X\n",a);
        if (catstatus)
           a=5;
        if (a==2||a==3||a==4) /*2= / command, 3=unknown * command, 4=*RUN*/
        {
                copyfilename(fnaddr,fname);
                if (!strcasecmp(fname,"DIR$"))
                   return a;
                for (c=0;c<16;c++)
                    fname2[c]=fname[c];
                load=0;
                if (loadfile(fname,fname2))
                   return 0x7F;
                pc=exeaddr;
                return a;
        }
        if (a==8||a==0) /*6=new filing system, 8=*ENABLE, 0=*OPT*/
           return a;
        if (a==6)
        {
                ram[28]=ram[42]=0;
                ram[29]=ram[43]=0xf;
                return a;
        }
        if (a==5)
        {
                if (catstatus==0)
                {
                        catstatus=1;
                        catline=0;
                        catchar=0;
                }
                catchar++;
                if (catchar==strlen(files[catline])+1)
                {
                        catchar=0;
                        catline++;
                }
                if (catline==numofiles)
                {
                        catstatus=0;
                        return a;
                }
                PUSH((0xF1B1-1) >> 8);
                PUSH((0xF1B1-1) & 0xFF);
                pc=0xFFEE;
                a=files[catline][catchar-1];
                return 255;
        }
       set_gfx_mode(GFX_TEXT,0,0,0,0);
       printf("Error : unknown OSFSC operation %X [%i]\n",a,a);
       exit(-1);
        return 0x80;
}

void copyfilename(int src, char *dest)
{
        int quoted=0,start=0,c=0,done=0;
        if (ram[src]=='"' )
        {
                src++;
                src &= 0xffff;
                quoted=1;
        }
        while (!done)
        {
                if (quoted&&(ram[src+c]=='"'))
                {
                        done=1;
                        break;
                }
                if (ram[src+c]==0xD)
                {
                        done=1;
                        break;
                }
                if (ram[src+c]==' ')
                {
                        done=1;
                        break;
                }
                if (ram[src+c]=='`')
                {
                        done=1;
                        break;
                }
                if ((start+c ) == 16)
                {
                        done=1;
                        break;
                }
                if (ram[src+c]!='.')
                {
                        dest[start+c]=ram[src+c];
                        c++;
                }
                else
                   src++; /*Cheap hack to get directories to work (ish)*/
        }
        dest[start+c]=0;
}
