/*           ██████████            █████████  ████      ████
             ██        ██          ██         ██  ██  ██  ██
             ██        ██          ██         ██    ██    ██
             ██████████     █████  █████      ██          ██
             ██        ██          ██         ██          ██
             ██        ██          ██         ██          ██
             ██████████            █████████  ██          ██

                     BBC Model B Emulator Version 0.3


              All of this code is (C)opyright Tom Walker 1999
         You may use SMALL sections from this program (ie 20 lines)
       If you want to use larger sections, you must contact the author

              If you don't agree with this, don't use B-Em

*/

/*Snapshot loading and saving*/

#include <stdio.h>
#include "gfx.h"
#include "6502.h"
#include "mem.h"
#include "vias.h"
#include "video.h"
#include "acai.h"

unsigned char serialreg;
unsigned char adcstatus,adchigh,adclow,adclatch;

unsigned char statusreg;
unsigned char datareg;
unsigned char resultreg;

void savesnapshot()
{
        FILE *f;
        int c,d;
        f=fopen("snap0000.snp","wb");
        /*Header*/
        /*Add snapshot ID - 0x422D5353 ('B-SS')*/
        putc(0x42,f); putc(0x2D,f); putc(0x53,f); putc(0x53,f);
        putc(0,f); /*Host snapshot*/
        for (c=0;c<3;c++)  /*Unused header bytes*/
                putc(0,f);

        /*6502 Status*/
        putc(a,f); /*Accumulator*/
        putc(x,f); /*X Reg*/
        putc(y,f); /*Y Reg*/
        putc(s,f); /*Stack Reg*/
        putc(p,f); /*P Reg*/
        putc(pc&0xFF,f); /*PCL*/
        putc(pc>>8,f); /*PCH*/
        putc(intStatus,f); /*irqStatus*/

        /*Tube Status (unused)*/
        for (c=0;c<8;c++)
                putc(0,f);

        /*Video Status*/
        putc(CRTC_HorizontalTotal,f); /*CRTC regs*/
        putc(CRTC_HorizontalDisplayed,f);
        putc(CRTC_HorizontalSyncPos,f);
        putc(CRTC_SyncWidth,f);
        putc(CRTC_VerticalTotal,f);
        putc(CRTC_VerticalTotalAdjust,f);
        putc(CRTC_VerticalDisplayed,f);
        putc(CRTC_VerticalSyncPos,f);
        putc(CRTC_InterlaceAndDelay,f);
        putc(CRTC_ScanLinesPerChar,f);
        putc(CRTC_CursorStart,f);
        putc(CRTC_CursorEnd,f);
        putc(CRTC_ScreenStartHigh,f);
        putc(CRTC_ScreenStartLow,f);
        putc(CRTC_CursorPosHigh,f);
        putc(CRTC_CursorPosLow,f);
        putc(CRTC_LightPenHigh,f);
        putc(CRTC_LightPenLow,f);
        for (c=0;c<16;c++) /*ULA Palette*/
                putc(VideoULA_Palette[c],f);
        putc(VideoULA_ControlReg,f); /*ULA Control reg*/
        putc(CRTCControlReg,f);      /*CRTC Control reg*/
        for (c=0;c<12;c++) /*Unused*/
                putc(0,f);

        /*System VIA*/
        putc(SysVIA.ora,f);
        putc(SysVIA.orb,f);
        putc(SysVIA.ira,f);
        putc(SysVIA.irb,f);
        putc(SysVIA.ddra,f);
        putc(SysVIA.ddrb,f);
        putc(SysVIA.acr,f);
        putc(SysVIA.pcr,f);
        putc(SysVIA.ifr,f);
        putc(SysVIA.ier,f);
        fwrite(&SysVIA.timer1c,4,1,f);
        fwrite(&SysVIA.timer1l,4,1,f);
        fwrite(&SysVIA.timer2c,4,1,f);
        fwrite(&SysVIA.timer2l,4,1,f);
        fwrite(&SysVIA.timer1hasshot,4,1,f);
        fwrite(&SysVIA.timer2hasshot,4,1,f);
        putc(IC32State,f);
        for (c=0;c<13;c++)
                putc(0,f);

        /*User VIA*/
        putc(UserVIA.ora,f);
        putc(UserVIA.orb,f);
        putc(UserVIA.ira,f);
        putc(UserVIA.irb,f);
        putc(UserVIA.ddra,f);
        putc(UserVIA.ddrb,f);
        putc(UserVIA.acr,f);
        putc(UserVIA.pcr,f);
        putc(UserVIA.ifr,f);
        putc(UserVIA.ier,f);
        fwrite(&UserVIA.timer1c,4,1,f);
        fwrite(&UserVIA.timer1l,4,1,f);
        fwrite(&UserVIA.timer2c,4,1,f);
        fwrite(&UserVIA.timer2l,4,1,f);
        fwrite(&UserVIA.timer1hasshot,4,1,f);
        fwrite(&UserVIA.timer2hasshot,4,1,f);
        putc(UIC32State,f);
        for (c=0;c<13;c++)
                putc(0,f);

        /*Keyboard*/
        for (c=0;c<10;c++)
                for (d=0;d<8;d++)
                        putc(bbckey[c][d],f);
        for (c=0;c<16;c++)
                putc(0,f);

        /*Misc*/
        putc(currom,f);
        for (c=0;c<15;c++)
                putc(0,f);

        /*RAM*/
        for (c=0;c<32768;c++)
                putc(ram[c],f);

        /*Serial ULA*/
        putc(serialreg,f);

        /*ACIA*/
        putc(acaicr,f);
        putc(acaisr,f);
        putc(acaidr,f);

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

void loadsnapshot()
{
        FILE *f;
        int c,d;
        f=fopen("snap0000.snp","rb");
        /*Header*/
        for (c=0;c<8;c++)
            getc(f);

        /*6502 Status*/
        a=getc(f); /*Accumulator*/
        x=getc(f); /*X Reg*/
        y=getc(f); /*Y Reg*/
        s=getc(f); /*Stack Reg*/
        p=getc(f); /*P Reg*/
        pc=getc(f)|(getc(f)<<8); /*PC*/
        intStatus=getc(f); /*irqStatus*/

        /*Tube Status (unused)*/
        for (c=0;c<8;c++)
                getc(f);

        /*Video Status*/
        CRTC_HorizontalTotal=getc(f); /*CRTC regs*/
        CRTC_HorizontalDisplayed=getc(f);
        CRTC_HorizontalSyncPos=getc(f);
        CRTC_SyncWidth=getc(f);
        CRTC_VerticalTotal=getc(f);
        CRTC_VerticalTotalAdjust=getc(f);
        CRTC_VerticalDisplayed=getc(f);
        CRTC_VerticalSyncPos=getc(f);
        CRTC_InterlaceAndDelay=getc(f);
        CRTC_ScanLinesPerChar=getc(f);
        CRTC_CursorStart=getc(f);
        CRTC_CursorEnd=getc(f);
        CRTC_ScreenStartHigh=getc(f);
        CRTC_ScreenStartLow=getc(f);
        CRTC_CursorPosHigh=getc(f);
        CRTC_CursorPosLow=getc(f);
        CRTC_LightPenHigh=getc(f);
        CRTC_LightPenLow=getc(f);
        for (c=0;c<16;c++) /*ULA Palette*/
                VideoULA_Palette[c]=getc(f);
        VideoULA_ControlReg=getc(f); /*ULA Control reg*/
                if (VideoULA_ControlReg&2)
                   VideoState.IsTeletext=1;
                else
                   VideoState.IsTeletext=0;
        CRTCControlReg=getc(f);      /*CRTC Control reg*/
        for (c=0;c<12;c++) /*Unused*/
                getc(f);

        /*System VIA*/
        SysVIA.ora=getc(f);
        SysVIA.orb=getc(f);
        SysVIA.ira=getc(f);
        SysVIA.irb=getc(f);
        SysVIA.ddra=getc(f);
        SysVIA.ddrb=getc(f);
        SysVIA.acr=getc(f);
        SysVIA.pcr=getc(f);
        SysVIA.ifr=getc(f);
        SysVIA.ier=getc(f);
        fread(&SysVIA.timer1c,4,1,f);
        fread(&SysVIA.timer1l,4,1,f);
        fread(&SysVIA.timer2c,4,1,f);
        fread(&SysVIA.timer2l,4,1,f);
        fread(&SysVIA.timer1hasshot,4,1,f);
        fread(&SysVIA.timer2hasshot,4,1,f);
        IC32State=getc(f);
        for (c=0;c<13;c++)
                getc(f);

        /*User VIA*/
        UserVIA.ora=getc(f);
        UserVIA.orb=getc(f);
        UserVIA.ira=getc(f);
        UserVIA.irb=getc(f);
        UserVIA.ddra=getc(f);
        UserVIA.ddrb=getc(f);
        UserVIA.acr=getc(f);
        UserVIA.pcr=getc(f);
        UserVIA.ifr=getc(f);
        UserVIA.ier=getc(f);
        fread(&UserVIA.timer1c,4,1,f);
        fread(&UserVIA.timer1l,4,1,f);
        fread(&UserVIA.timer2c,4,1,f);
        fread(&UserVIA.timer2l,4,1,f);
        fread(&UserVIA.timer1hasshot,4,1,f);
        fread(&UserVIA.timer2hasshot,4,1,f);
        UIC32State=getc(f);
        for (c=0;c<13;c++)
                getc(f);

        /*Keyboard*/
        for (c=0;c<10;c++)
                for (d=0;d<8;d++)
                        bbckey[c][d]=getc(f);
        for (c=0;c<16;c++)
                getc(f);

        /*Misc*/
        currom=getc(f);
        for (c=0;c<15;c++)
                getc(f);

        /*RAM*/
        for (c=0;c<32768;c++)
                ram[c]=getc(f);

        /*Serial ULA*/
        serialreg=getc(f);
        updateserialreg();

        /*ACIA*/
        acaicr=getc(f);
        acaisr=getc(f);
        acaidr=getc(f);

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
