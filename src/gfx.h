#include <dpmi.h>
#include <errno.h>

#ifndef TOMGFXHEAD
#define TOMGFXHEAD

#ifdef __cplusplus
extern "C" {
#endif

#ifndef __INLINE__
#define __INLINE__ extern inline
#endif

#define MOUSE
#define FILES
#define TIMER
#define KEYBOARD
#define FIXED

#define ASCID(a,b,c,d) (a+(b<<8)+(c<<16)+(d<<24))
#define ascid(a,b,c,d) (a+(b<<8)+(c<<16)+(d<<24))

struct BMP;
struct RGB;
struct VTABLE;
struct SAMPLE;
struct RGBMAP;

/*Internal structure - the bitmap drawing functions*/
typedef struct VTABLE
{
        void (*putpixel)(struct BMP *b,unsigned int x, unsigned int y, unsigned long col);
        long (*getpixel)(struct BMP *b,unsigned int x, unsigned int y);
        void (*hline)(struct BMP *b,int x1, int y1, int x2, int y2, long col);
        void (*vline)(struct BMP *b,int x1, int y1, int x2, int y2, long col);
        void (*clearall)(struct BMP *b,long col);
        void (*blittoself)(struct BMP *b, int x1, int y1, int x2, int y2, int sizex, int sizey);
        void (*blitfromself)(struct BMP *src, struct BMP *dest, int srcx, int srcy, int destx, int desty, int sizex, int sizey);
        void (*blitfrommem)(struct BMP *src, struct BMP *dest, int srcx, int srcy, int destx, int desty, int sizex, int sizey);
        void (*drawsprite)(struct BMP *src, struct BMP *dest, int destx, int desty, int sizex, int sizey);
} VTABLE;

/*Various defines*/
#define VGA    ASCID('V','G','A',' ')
#define MODEX  ASCID('X','V','G','A')
#define VESA1  ASCID('V','E','S','A')
#define VESA2B ASCID('V','E','2','B')
#define VESA2L ASCID('V','E','2','L')

#define GUS    ASCID('G','U','S',' ')
#define SB     ASCID('S','B',' ',' ')
#define SB2    ASCID('S','B','2',' ')
#define SBPRO  ASCID('S','B','P','R')

#define TRUE 1
#define FALSE 0

#define NULL 0

#define SOLID  ASCID('S','O','L','I')
#define MASKED ASCID('M','A','S','K')
#define XOR ASCID ('X','O','R',' ')
#define MIN(x,y)     (((x) < (y)) ? (x) : (y))
#define MAX(x,y)     (((x) > (y)) ? (x) : (y))
#define ABS(x)       (((x) >= 0) ? (x) : (-(x)))

/*The RGB structure - holds palette values*/
typedef struct RGB
{
        unsigned char r,g,b;
} RGB;

/*The PALETTE structure - 256 RGB's*/
typedef RGB PALETTE[256];

typedef struct RGBMAP
{
        char data[32][32][32];
} RGBMAP;

RGBMAP *rgbmap;

typedef struct COLOURMAP
{
        char data[256][256];
} COLOURMAP;

COLOURMAP *colmap;


int (*colfunc)(int x, int y);

typedef struct DLG
{
        int (*proc)(struct DLG *d, int msg, int c);
        int x,y;
        int sizex,sizey;
        int d1,d2;
        int flags;
        long forcol,bakcol;
        char *string;
        char *dp,*dp2;
} DLG;

#define DOK          0
#define DERROR       1
#define DCLOSE       2
#define DREDRAW      3
#define DTOTALREDRAW 4
#define DCLICK       5
#define DINIT        6
#define DRCLICK      7
#define DRELCLICK    8
#define DKEY         9
#define DUSEDKEY     10
#define DOFFERFOCUS  11
#define DWANTFOCUS   12

#define DCENTRE 0x01
#define DEXIT   0x02
#define DMASKED 0x04
#define DSELECTED 0x08

#define PAN(x)       (((x)>>8) / physx)
#define KEY_PRSC 127
typedef struct GFX_DRIVER        /* creates and manages the screen bitmap */
{
   int  id;                      /* driver ID code */
   char *name;                   /* driver name */
   char *desc;                   /* description (VESA version, etc) */
   int  (*init)(int w, int h, int vw, int vh);
//   void (*exit)(struct BITMAP *b);
   void (*close)();
   void (*vsync)();
   void (*setcol)(char col, struct RGB r);
   int  (*scroll)(int x, int y);
   int  w, h;                    /* physical (not virtual!) screen size */
   int  linear;                  /* true if video memory is linear */
   long bank_size;               /* bank size, in bytes */
   long bank_gran;               /* bank granularity, in bytes */
   long vid_mem;                 /* video memory size, in bytes */
   long vid_phys_base;           /* physical address of video memory */
} GFX_DRIVER;

typedef struct _GFX_DRIVER_INFO  /* info about a graphics driver */
{
   int id;                /* integer ID */
   GFX_DRIVER *driver;           /* the driver structure */
   int autodetect;               /* set to allow autodetection */
} _GFX_DRIVER_INFO;

GFX_DRIVER *currdrv;

#define COLMAP ascid('C','O','L','M')

int _drawmode;
__INLINE__ void setdrawmode(int mode) {_drawmode=mode;}
GFX_DRIVER _drvvga;
GFX_DRIVER _drvmodex;
GFX_DRIVER _drvvesa1;
GFX_DRIVER _drvvesa2b;
GFX_DRIVER _drvvesa2l;

char screendumpname[40];
int do_dialog(DLG *d, int focus);
int dlg_button(DLG *d, int msg, int c);
int dlg_selbox(DLG *d, int msg, int c);
int dlg_checkbox(DLG *d, int msg, int c);
int dlg_goodbutton(DLG *d, int msg, int c);
int dlg_text(DLG *d, int msg, int c);
int dlg_seltext(DLG *d, int msg, int c);
int dlg_clear(DLG *d, int msg, int c);
int dlg_box(DLG *d, int msg, int c);
int dlg_hollowbox(DLG *d, int msg, int c);
int alertbox(char *s1, char *s2, char *s3, char *b1, char *b2, char *b3);

/*Fixed point math*/
typedef long fixed;

/*The BMP structure - this is what you draw onto*/
typedef struct BMP
{
        long x,y;
        int size;
        int colourdepth;
        int isscreenbitmap;
        int planar;
        int x_of,y_of;
        unsigned short seg;
        char *addr;
        VTABLE *vtable;
        char *line[0];
} BMP;

BMP *screen;
PALETTE _pal;
char tomgfxerror[80];

typedef struct DATAOBJ
{
        unsigned long size;
        unsigned long type;
        void *data;
} DATAOBJ;

typedef struct DATAFILE
{
        unsigned long size;
        unsigned long numitems;
        DATAOBJ *objects[0];
} DATAFILE;

#define DATMAGIC ASCID('D','A','T','A')
#define NEWTHING ASCID('N','E','W',' ')

#define BINARYDATA  ASCID('B','I','N','A')
#define PALETTEDATA ASCID('P','A','L','E')
#define FONTDATA    ASCID('F','O','N','T')
#define BMPDATA     ASCID('B','M','P',' ')
#define SMPDATA     ASCID('S','A','M','P')

#define NORMAL ASCID('N','O','R','M')

PALETTE beebpal;
int _linear;

typedef struct FONT8x8
{
        char dat[224][8];
} FONT8x8;

typedef struct FONT8x16
{
        char dat[224][16];
} FONT8x16;

typedef struct FONT
{
        int height;
        union
        {
                FONT8x8 *dat8x8;
                FONT8x16 *dat8x16;
        }dat;
} FONT;

void tomgfxinit();

/*Graphics mode functions and variables*/
int _virginity;
int physx,physy,virtx,virty,splitpos;
int _oldseg,_charmode;
int gfxed,keyboard,timered;
int _colourdepth;
int initgfx(int type, long w, long h, long vw, long vh);
void closegfx();
void vsync();
//void scrollmodex(unsigned int x, unsigned int y);
void scrollvesa(int x, int y);
void scroll(int x, int y);
void splitmodex(unsigned int line);

#define LOOP    1
#define REVERSE 2
#define BIDIR   4

/*Colour functions*/
void colourdepth(int c);
long makecol(unsigned char r, unsigned char g, unsigned char b);
void setcol(char col, RGB p);
void setpal(PALETTE pal);
RGB getcol(char c);
void getpal(PALETTE pal);
void trucolourpal(PALETTE pal);
void fadeout();
void fadein(PALETTE pal);
void fadeto(PALETTE pal);
void createRGBmap(RGBMAP *rgb, PALETTE pal, void (*callback)());
void createcolourmap(COLOURMAP *colourmap, PALETTE pal, RGB (*blend)(PALETTE pal, int x, int y), void (*callback)());
void createlightmap(COLOURMAP *map, PALETTE pal, int r, int g, int b, void (*callback)());
void createtransmap(COLOURMAP *colourmap, PALETTE pal, int transparency, void (*callback)());

/*Drawing functions*/
void drawline(BMP *b,int x1, int y1, int x2, int y2, long col);
void drawaaline(BMP *b,int x1, int y1, int x2, int y2, long col);
void drawbox(BMP *b, int x1, int y1, int x2, int y2, long col);
void circle(BMP *b,int x, int y, long r, long col);
void circlefill(BMP *b,int x, int y, long r, long col);
void elipse(BMP *b, int x, int y, int xr, int yr, long col);
void polygon(BMP *b,int numsides, unsigned short int *points, long col);
void hollowtriangle(BMP *b,int x1, int y1, int x2, int y2, int x3, int y3, long col);
void triangle(BMP *b,int x1, int y1, int x2, int y2, int x3, int y3, long col);
void quad(BMP *b, int x1, int y1, int x2, int y2, int x3, int y3, int x4, int y4, long col);
void flood(BMP *b, int x, int y, long col);
//void rotbox(BMP *b, int x1, int y1, int x2, int y2, fixed angle, long col);
void spline(BMP *b, int *points, long col);
void gouraudtriangle(BMP *b, int x1, int y1, int x2, int y2, int x3, int y3, long col1, long col2, long col3);
void texturedtriangle(BMP *b, int x1, int y1, int x2, int y2, int x3, int y3, BMP *texture);
void maskedtexturedtriangle(BMP *bmp, int x1, int y1, int x2, int y2, int x3, int y3, BMP *texture);
void doline(BMP *b,int x1, int y1, int x2, int y2, long d, void (*proc)());
void docircle(BMP *b,int x, int y, long r, long d, void (*proc)());
void doelipse(BMP *b, int x, int y, int rx, int ry, long d, void (*proc)());

/*Text functions*/
void charmode(int mode);
//void drawchar(BMP *b,unsigned int x, unsigned int y, char c, long col);
//void drawstring(BMP *b,unsigned int x, unsigned int y, char *s, long col);
//void drawstringcentre(BMP *b,unsigned int x, unsigned int y, char *s, long col);
//void stringprintf(BMP *b, unsigned int x, unsigned int y, long col, char *format,...);
//void stringprintfcentre(BMP *b, unsigned int x, unsigned int y, long col, char *format,...);

/*Misc functions*/
int dorand(int max);
char *getfileext(char *filename);

/*Bitmap functions*/
BMP *createbmp(long x, long y);
void destroybmp(BMP *b);

/*Blit functions*/
void blit(BMP *src, BMP *dest, int srcx, int srcy, int destx, int desty, int sizex, int sizey);
void masked_blit(BMP *source, BMP *dest, int srcx, int srcy, int destx, int desty, int sizex, int sizey);
void draw_sprite(BMP *src, BMP *dest, int x, int y);
void draw_sprite_v_flip(BMP *source, BMP *dest, int x, int y);
void draw_sprite_h_flip(BMP *source, BMP *dest, int x, int y);
void draw_sprite_vh_flip(BMP *source, BMP *dest, int x, int y);
void stretched_blit(BMP *src, BMP *dest, int srcx, int srcy, int sizex1,int sizey1, int destx,int desty, int sizex2, int sizey2);
void masked_stretched_blit(BMP *src, BMP *dest, int srcx, int srcy, int sizex1,int sizey1, int destx, int desty, int sizex2, int sizey2);
void colourmapblit(BMP *src, BMP *dest, int srcx, int srcy, int destx, int desty, int sizex, int sizey);

FONT *font;
#ifdef MOUSE
/*Mouse functions and variables*/
BMP *mouse1,*blnk,*mouseback;
int mousex,mousey,mouseb;
int mouseon,showcursor;
int moused;
int initmouse();
void cursoron();
void cursoroff();
void tomcursoron();
void tomcursoroff();
void scaremouse();
void unscaremouse();
void putmouse(int x, int y);
void setlimits(int x1, int y1, int x2, int y2);
#endif

#ifdef FILES
/*File functions*/
BMP *loadtfx(char *f,RGB *pal);
int writetfx(BMP *b,char *f,RGB *pal);
BMP *loadpcx(char *filename, RGB *pal);
int save_pcx(char *filename, BMP *b, PALETTE pal);
BMP *loadbmp(char *fn, RGB *pal);
BMP *loadbitmap(char *filename, PALETTE pal);
#endif

/*Low-level drawing functions*/
__INLINE__ void putpixel(BMP *b, unsigned int x, unsigned int y, long col)
{
        if (!b)
           return;
        if (x < (unsigned int)b->x && y < (unsigned int)b->y)
        {
                if (_drawmode==XOR)
                   b->vtable->putpixel(b,x,y,col^b->vtable->getpixel(b,x,y));
                else if (_drawmode==NORMAL)
                   b->vtable->putpixel(b,x,y,col);
                else if (_colourdepth==8&&colmap)
                   b->vtable->putpixel(b,x,y,colmap->data[col][b->vtable->getpixel(b,x,y)]);
                else if (_colourdepth!=8)
                   b->vtable->putpixel(b,x,y,colfunc(col,b->vtable->getpixel(b,x,y)));
        }
}

__INLINE__ void _putpixel(BMP *b, unsigned int x, unsigned int y, long col)
{
        if (!b)
           return;
        if (x < (unsigned int)b->x && y < (unsigned int)b->y)
        {
                if (_drawmode==NORMAL)
                   b->vtable->putpixel(b,x,y,col);
                else if (_drawmode==XOR)
                   b->vtable->putpixel(b,x,y,col^b->vtable->getpixel(b,x,y));
                else if (_colourdepth==8&&colmap)
                   b->vtable->putpixel(b,x,y,colmap->data[col][b->vtable->getpixel(b,x,y)]);
                else if (_colourdepth!=8)
                   b->vtable->putpixel(b,x,y,colfunc(col,b->vtable->getpixel(b,x,y)));
        }
}

__INLINE__ long getpixel(BMP *b, unsigned int x, unsigned int y)
{
        if (!b)
           return 0;
        if (x < (unsigned int)b->x && y < (unsigned int)b->y)
           return b->vtable->getpixel(b,x,y);
        return 0;
}

__INLINE__ void hline(BMP *b, int x1, int y1, int x2, int y2, long col)
{
        int t;
        if (x1>x2)
        {
                t=x1;
                x1=x2;
                x2=t;
        }
        if (!b)
           return;
        if (x1 < 0)
           x1=0;
        if (x2 < 0)
           return;
        if (x1 > b->x-1)
           return;
        if (x2 > b->x-1)
           x2=b->x-1;
        if (y1 > b->y-1)
           return;
        if (y1 < 0)
           return;
        if (_drawmode==NORMAL)
        {
                if (x1 > x2)
                   b->vtable->hline(b,x2,y1,x1,y2,col);
                else
                   b->vtable->hline(b,x1,y1,x2,y2,col);
        }
        else
           _slohline(b,x1,y1,x2,y2,col);
}

__INLINE__ void vline(BMP *b, int x1, int y1, int x2, int y2, long col)
{
        if (!b)
           return;
        if (y1 < 0)
           y1=0;
        if (y2 < 0)
           y2=0;
        if (y1 > b->y-1)
           y1=b->y-1;
        if (y2 > b->y-1)
           y2=b->y-1;
        if (x1 > b->x-1)
           return;
        if (_drawmode==NORMAL)
        {
                if (y1 > y2)
                   b->vtable->vline(b,x1,y2,x2,y1,col);
                else
                   b->vtable->vline(b,x1,y1,x2,y2,col);
        }
        else
           _slovline(b,x1,y1,x2,y2);
}

__INLINE__ void clearall(BMP *b, long col)
{
        if (b)
           b->vtable->clearall(b,col);
}

#ifdef KEYBOARD
/*Keyboard functions and variables*/
volatile char keys[128];
unsigned int _keybuffer[128];
_go32_dpmi_seginfo old_handler, new_handler;
unsigned int waitkey();
int keypressed();
void clearbuf();
int initkey();
void closekey();
unsigned char shifts;

/*Shifts*/
#define SHIFT_LSHIFT   0x01
#define SHIFT_RSHIFT   0x02
#define SHIFT_CTRL     0x04
#define SHIFT_ALT      0x08
#define SHIFT_SCROLL   0x10
#define SHIFT_NUMLOCK  0x20
#define SHIFT_CAPS     0x40

/*Key scancodes*/
#define KEY_ESC        0x01
#define KEY_1          0x02
#define KEY_2          0x03
#define KEY_3          0x04
#define KEY_4          0x05
#define KEY_5          0x06
#define KEY_6          0x07
#define KEY_7          0x08
#define KEY_8          0x09
#define KEY_9          0x0a
#define KEY_0          0x0b
#define KEY_MINUS      0x0c
#define KEY_EQUALS     0x0d
#define KEY_PLUS       0x0d
#define KEY_BACKSP     0x0e
#define KEY_TAB        0x0f
#define KEY_Q          0x10
#define KEY_W          0x11
#define KEY_E          0x12
#define KEY_R          0x13
#define KEY_T          0x14
#define KEY_Y          0x15
#define KEY_U          0x16
#define KEY_I          0x17
#define KEY_O          0x18
#define KEY_P          0x19
#define KEY_LANGLE     0x1a
#define KEY_RANGLE     0x1b
#define KEY_ENTER      0x1c
#define KEY_CTRL       0x1d
#define KEY_A          0x1e
#define KEY_S          0x1f
#define KEY_D          0x20
#define KEY_F          0x21
#define KEY_G          0x22
#define KEY_H          0x23
#define KEY_J          0x24
#define KEY_K          0x25
#define KEY_L          0x26
#define KEY_SCOLON     0x27
#define KEY_QUOTA      0x28
#define KEY_RQUOTA     0x29
#define KEY_LSHIFT     0x2a
#define KEY_BSLASH     0x2b
#define KEY_Z          0x2c
#define KEY_X          0x2d
#define KEY_C          0x2e
#define KEY_V          0x2f
#define KEY_B          0x30
#define KEY_N          0x31
#define KEY_M          0x32
#define KEY_COMA       0x33
#define KEY_DOT        0x34
#define KEY_SLASH      0x35
#define KEY_RSHIFT     0x36
#define KEY_GREY_STAR  0x37
#define KEY_ALT        0x38
#define KEY_SPACE      0x39
#define KEY_CAPS       0x3a
#define KEY_F1         0x3b
#define KEY_F2         0x3c
#define KEY_F3         0x3d
#define KEY_F4         0x3e
#define KEY_F5         0x3f
#define KEY_F6         0x40
#define KEY_F7         0x41
#define KEY_F8         0x42
#define KEY_F9         0x43
#define KEY_F10        0x44
#define KEY_NUMLOCK    0x45
#define KEY_SCRLOCK    0x46
#define KEY_HOME       0x47
#define KEY_UP         0x48
#define KEY_PGUP       0x49
#define KEY_GREY_MINUS 0x4a
#define KEY_LEFT       0x4b
#define KEY_PAD_5      0x4c
#define KEY_RIGHT      0x4d
#define KEY_GREY_PLUS  0x4e
#define KEY_END        0x4f
#define KEY_DOWN       0x50
#define KEY_PGDN       0x51
#define KEY_INSERT     0x52
#define KEY_DEL        0x53
#define KEY_F11        0x57
#define KEY_F12        0x58

#endif

#ifdef TIMER
/*Timer functions and variables*/
_go32_dpmi_seginfo old_timer, new_timer;
int inittimer();
void closetimer();
int installint(void ((*proc)()), int speed);
void removeint(void ((*proc)()));
void rest(unsigned long int msecs);

#define TIMERS_PER_SECOND     1193181L
#define SECS_TO_TIMER(x)      ((long)(x) * TIMERS_PER_SECOND)
#define MSEC_TO_TIMER(x)      ((long)(x) * (TIMERS_PER_SECOND / 1000))
#define BPS_TO_TIMER(x)       (TIMERS_PER_SECOND / (long)(x))
#define BPM_TO_TIMER(x)       ((60 * TIMERS_PER_SECOND) / (long)(x))

#endif

/*Locking functions*/
#define END_OF_FUNCTION(x)    void x##_end() { }
#define LOCK_DATA(d,s)        _go32_dpmi_lock_data((d), (s))
#define LOCK_CODE(c,s)        _go32_dpmi_lock_code((c), (s))
#define UNLOCK_DATA(d,s)      _unlock_dpmi_data((d), (s))

#define LOCK_VARIABLE(x)      LOCK_DATA((void *)&x, sizeof(x))
#define LOCK_FUNCTION(x)      LOCK_CODE(x, (long)x##_end - (long)x)

#ifdef FIXED
/*Fixed point math functions*/
fixed fsin(fixed x);
fixed fcos(fixed x);
fixed fsqrt(fixed x);

__INLINE__ fixed itofix(int x)
{
   return x << 16;
}

__INLINE__ int fixtoi(fixed x)
{
   return (x >> 16) + ((x & 0x8000) >> 15);
}

__INLINE__ fixed ftofix(double x)
{
   return (long)(x * 65536.0 + (x < 0 ? -0.5 : 0.5));
}


__INLINE__ double fixtof(fixed x)
{
   return (double)x / 65536.0;
}

__INLINE__ fixed fadd(fixed x, fixed y)
{
        return x+y;
}

__INLINE__ fixed fsub(fixed x, fixed y)
{
        return x+y;
}

__INLINE__ fixed fmul(fixed x, fixed y)
{
        fixed edx __attribute__ ((__unused__));
        fixed result;
        asm("
        movl %2,%0
        imull %3
        shrdl $16, %%edx, %0
        shrl $16, %%edx"
        : "=&a" (result),
          "=&d" (edx)

        : "mr" (x),
          "mr" (y)
        : "%cc", "memory"
        );
        return result;
}

__INLINE__ fixed fdiv(fixed x, fixed y)
{
   fixed edx __attribute__ ((__unused__));
   fixed reg __attribute__ ((__unused__));
   fixed result;
   asm (
      "  testl %0, %0 ; "                 /* test sign of x */
      "  js 3f ; "

      "  testl %2, %2 ; "                 /* test sign of y */
      "  jns 4f ; "
      "  negl %2 ; "

      " 0: "                              /* result will be negative */
      "  movl %0, %%edx ; "               /* check the range is ok */
      "  shrl $16, %%edx ; "
      "  shll $16, %0 ; "
      "  cmpl %2, %%edx ; "
      "  jae 1f ; "

      "  divl %2 ; "                      /* do the divide */
      "  testl %0, %0 ; "
      "  jns 2f ; "

      " 1: "
      "  movl %5, %6 ; "                  /* on overflow, set errno */
      "  movl $0x7FFFFFFF, %0 ; "         /* and return MAXINT */

      " 2: "
      "  negl %0 ; "                      /* fix up the sign of the result */
      "  jmp 6f ; "

      "  .p2align 4, 0x90 ; "

      " 3: ; "                            /* x is negative */
      "  negl %0 ; "
      "  testl %2, %2 ; "                 /* test sign of y */
      "  jns 0b ; "
      "  negl %2 ; "

      " 4: "                              /* result will be positive */
      "  movl %0, %%edx ; "               /* check the range is ok */
      "  shrl $16, %%edx ; "
      "  shll $16, %0 ; "
      "  cmpl %2, %%edx ; "
      "  jae 5f ; "

      "  divl %2 ; "                      /* do the divide */
      "  testl %0, %0 ; "
      "  jns 6f ; "

      " 5: "
      "  movl %5, %6 ; "                  /* on overflow, set errno */
      "  movl $0x7FFFFFFF, %0 ; "         /* and return MAXINT */

      " 6: "                              /* finished */

   : "=a" (result),                       /* the result has to go in eax */
     "=&d" (edx),                         /* reliably reserve edx */
     "=r" (reg)                           /* input operand will be clobbered */

   : "0" (x),                             /* x in eax */
     "2" (y),                             /* y in register */
     "i" (ERANGE),
     "m" (errno)

   : "%cc", "memory"                      /* clobbers flags and memory  */
   );

   return result;
}

#endif

/*3D functions*/
fixed _persp_xscale, _persp_yscale, _persp_xoffset, _persp_yoffset;

__INLINE__ void setprojview(int x, int y, int w, int h)
{
   _persp_xscale = itofix(w/2);
   _persp_yscale = itofix(h/2);
   _persp_xoffset = itofix(x + w/2);
   _persp_yoffset = itofix(y + h/2);
}

__INLINE__ void perspproject(fixed x, fixed y, fixed z, fixed *xx, fixed *yy)
{
        *xx=fmul(fdiv(x, z), _persp_xscale) + _persp_xoffset;
        *yy=fmul(fdiv(y, z), _persp_yscale) + _persp_yoffset;
}


int _digcard;
int sizeofbuffer;
int (*newmix)(unsigned long buffer, unsigned long length);
typedef struct SAMPLE
{
        int bits,freq,stereo,length;
        char *data;
} SAMPLE;

/*Module stuff*/
typedef struct MODSAMPLE
{
        SAMPLE *dat;
        int finetune,reppos,replen;
} MODSAMPLE;

typedef struct MODDAT
{
        int ins[4],note[4];
} MODDAT;

typedef struct MODULE
{
        char songname[20];
        int length;
        int patterns[128];
        MODSAMPLE samples[64];
        MODDAT *data[0];
} MODULE;

#ifdef __cplusplus
}
#endif

#endif
