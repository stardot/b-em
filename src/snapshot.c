#if 0
/*B-em 0.5 by Tom Walker*/
/*Snapshot loading and saving*/

#include <stdio.h>
#include <allegro.h>

unsigned char aciacr,aciadr,aciasr;
unsigned char ram[65536];
int currentrom;
unsigned char serialreg;
unsigned char adcstatus,adchigh,adclow,adclatch;

unsigned char statusreg;
unsigned char datareg;
unsigned char resultreg;

void savesnapshot(char *fn)
{
        FILE *f;
        int c,d;
        f=fopen(fn,"wb");
        /*Header*/
        /*Add snapshot ID - 0x422D5353 ('B-SS')*/
        putc(0x42,f); putc(0x2D,f); putc(0x53,f); putc(0x53,f);
        putc(0,f); /*Host snapshot*/
        for (c=0;c<3;c++)  /*Unused header bytes*/
                putc(0,f);

        /*6502 Status*/
        save6502status(f);

        /*Video Status*/
        savevideostatus(f);

        /*System VIA*/
        savesysviastatus(f);

        /*User VIA*/
        saveuserviastatus(f);

        /*Keyboard*/
        savekeyboardstatus(f);

        /*Misc*/
        putc(currentrom,f);
        for (c=0;c<15;c++)
                putc(0,f);

        /*RAM*/
        fwrite(ram,32768,1,f);

        /*Serial ULA*/
        putc(serialreg,f);

        /*ACIA*/
        putc(aciacr,f);
        putc(aciasr,f);
        putc(aciadr,f);

        /*ADC*/
        putc(adcstatus,f);
        putc(adchigh,f);
        putc(adclow,f);
        putc(adclatch,f);

        /*FDC*/
        putc(statusreg,f);
        putc(resultreg,f);
        putc(datareg,f);

        fclose(f);
}

void loadsnapshot(char *fn)
{
        FILE *f;
        int c,d;
        char s[80];
        f=fopen(fn,"rb");
        /*Header*/
        for (c=0;c<8;c++)
            getc(f);

        /*6502 Status*/
        load6502status(f);

        /*Video Status*/
        loadvideostatus(f);

        /*System VIA*/
        loadsysviastatus(f);

        /*User VIA*/
        loaduserviastatus(f);

        /*Keyboard*/
        loadkeyboardstatus(f);

        /*Misc*/
        currentrom=getc(f);
        writemem(0xFE30,currentrom); /*Cheap hack*/
        for (c=0;c<15;c++)
                getc(f);

        /*RAM*/
        fread(ram,32768,1,f);

        /*Serial ULA*/
        serialreg=getc(f);

        /*ACIA*/
        aciacr=getc(f);
        aciasr=getc(f);
        aciadr=getc(f);

        /*ADC*/
        adcstatus=getc(f);
        adchigh=getc(f);
        adclow=getc(f);
        adclatch=getc(f);

        /*FDC*/
        statusreg=getc(f);
        resultreg=getc(f);
        datareg=getc(f);

        fclose(f);
}
#endif
