/*B-em 0.71b by Tom Walker*/
/*This code currently doesn't do anything.
  It is the beginnings of SASI hard disc emulation
  but due to lack of docs is not completed or working
  Anyone want to help?*/
/*FC41 format :
  bit 1 - BUSY
  bit 5 - BUSY (as well)
*/

int timetolive,output;
int sasicallback=0;
unsigned short pc;

struct sasi
{
        unsigned long lba;
        int incommand,params,cparam;
        unsigned char command,data;
        unsigned char p[16];
        int irqena;
} sasi;

void writehdc(unsigned short a, unsigned char v)
{
        output=1;
        timetolive=25000;
        printf("HDC write %04X %02X  %04X\n",a,v,pc);
        switch (a)
        {
                case 0xFC40: /*Data*/
                sasi.data=v;
                return;
                case 0xFC42: /*Select - strobe whatever*/
                if (!sasi.incommand)
                {
                        sasi.command=sasi.data;
                        sasi.incommand=1;
                        switch (sasi.data)
                        {
//                                case 0x01: sasi.params=0; sasicallback=500; return;
                                default: allegro_exit(); printf("Bad command %02X\n",sasi.data); dumpregs(); exit(-1);
                        }
                        return;
                }
                else
                {
                        printf("Param %i = %02X\n",sasi.cparam,sasi.data);
                        sasi.p[sasi.cparam]=sasi.data;
                        if (sasi.cparam==sasi.params)
                        {
                                allegro_exit();
                                printf("Command %02X :\n%02X %02X %02X %02X %02X %02X %02X %02X %02X %02X\n",sasi.command,sasi.p[0],sasi.p[1],sasi.p[2],sasi.p[3],sasi.p[4],sasi.p[5],sasi.p[6],sasi.p[7],sasi.p[8],sasi.p[9]);
                                dumpregs();
                                exit(-1);
                        }
                        sasi.cparam++;
                }
                return;
                case 0xFC43: /*Enable IRQ*/
                sasi.irqena=v;
                return;
        }
        allegro_exit();
        printf("Bad HDC write %04X %02X\n",a,v);
        dumpregs();
        exit(-1);
}

/*FC41 - bit 1 BSY*/
unsigned char readhdc(unsigned short a)
{
        switch (a)
        {
                case 0xFC40:
                return sasi.data;
                break;
                case 0xFC41:
//                if (sasi.incommand) return 2;
                return 0;
        }
        allegro_exit();
        printf("Bad HDC read %04X\n",a);
        dumpregs();
        exit(-1);
}

void pollsasi()
{
        if (!sasi.incommand) return;
        switch (sasi.command)
        {
                case 0x01: /*Rezero*/
                sasi.lba=0;
                sasi.incommand=0;
                return;
        }
}

