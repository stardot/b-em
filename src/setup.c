#include <stdio.h>

FILE *f;

int main()
{
        int mode,val;
        char temp[40];
        printf("1. 320x200 (MODEX)\n");
        printf("2. 320x240 (MODEX)\n");
        printf("3. 320x300 (MODEX)\n");
        printf("4. 320x400 (MODEX)\n");
        printf("5. 400x300 (VESA2)\n");
        printf("6. 640x400 (VESA2)\n");
        printf("7. 640x480 (VESA2)\n");
        printf("8. 800x600 (VESA2)\n");
        printf("Please choose a mode : ");
        scanf("%i",&mode);
        f=fopen("b-em.cfg","w");
        fputs("FRAMESKIP=1\n",f);
        sprintf(temp,"MODE=%i\n",mode-1);
        fputs(temp,f);
        printf("Please enter the name of the disc image to start with : ");
        scanf("%s",temp);
        fputs(temp,f);
        sprintf(temp,"\n");
        fputs(temp,f);
        printf("0. No UEF support\n");
        printf("1. UEF support\n");
        selectuef:
        printf("Please choose : ");
        mode=getch();
        printf("%c",mode);
        if ((mode!='0')&&(mode!='1'))
        {
                printf("\nBad choice\n");
                goto selectuef;
        }
        if (mode=='1')
        {
                printf("\nPlease enter the name of the UEF file to start with : ");
                scanf("%s",temp);
                fputs(temp,f);
                sprintf(temp,"\n");
                fputs(temp,f);
        }
        else
           fputs("No UEF support\n",f);
        printf("\n0. Sound off\n");
        printf("1. Sound on\n");
        selectsound:
        printf("Please choose : ");
        val=getch();
        printf("%c",mode);
        if ((val!='0')&&(val!='1'))
        {
                printf("\nBad choice\n");
                goto selectsound;
        }
        sprintf(temp,"%c\n",val);
        fputs(temp,f);
        printf("\n0. Scanline mode off\n");
        printf("1. Scanline mode on\n");
        selectscan:
        printf("Please choose : ");
        val=getch();
        printf("%c",mode);
        if ((val!='0')&&(val!='1'))
        {
                printf("\nBad choice\n");
                goto selectscan;
        }
        sprintf(temp,"%c\n",val);
        fputs(temp,f);
        sprintf(temp,"%c\n",mode);
        fputs(temp,f);
        fclose(f);
        return 0;
}
