void Drawteletextscreen();
void initvideo();
void doscreen();
void CRTCWrite(unsigned int Address, unsigned char Value);
int CRTCRead(int Address);
void ULAWrite(int Address, int Value);
int ULARead(int Address);
int VideoTriggerCount;
//int Videodelay;
int scrlenindex;
int videoaddr;
int scanlinedraw;
unsigned char CRTCControlReg;
unsigned char CRTC_HorizontalTotal;      /* R0 */
unsigned char CRTC_HorizontalDisplayed;  /* R1 */
unsigned char CRTC_HorizontalSyncPos;    /* R2 */
unsigned char CRTC_SyncWidth;            /* R3 */
unsigned char CRTC_VerticalTotal;        /* R4 */
unsigned char CRTC_VerticalTotalAdjust;  /* R5 */
unsigned char CRTC_VerticalDisplayed;    /* R6 */
unsigned char CRTC_VerticalSyncPos;      /* R7 */
unsigned char CRTC_InterlaceAndDelay;    /* R8 */
unsigned char CRTC_ScanLinesPerChar;     /* R9 */
unsigned char CRTC_CursorStart;          /* R10 */
unsigned char CRTC_CursorEnd;            /* R11 */
unsigned char CRTC_ScreenStartHigh;      /* R12 */
unsigned char CRTC_ScreenStartLow;       /* R13 */
unsigned char CRTC_CursorPosHigh;        /* R14 */
unsigned char CRTC_CursorPosLow;         /* R15 */
unsigned char CRTC_LightPenHigh;         /* R16 */
unsigned char CRTC_LightPenLow;          /* R17 */
BMP *b;
unsigned char screencheck[32768];
int VideoULA_Palette[16];
unsigned char VideoULA_ControlReg;
/*#define VideoPoll(ncycles) \
                           if (Videodelay==(41943))    \
                           {                         \
                                Videodelay=0;        \
                                doscreen();          \
                           }                         \
                           else                      \
                               Videodelay++;*/
/*                           if (VideoTriggerCount<=TotalCycles) \
                           {                                   \
                                if (Videodelay == 512)         \
                                {                              \
                                        Videodelay = 0;        \
                                        doscreen();            \
                                }                              \
                                else                           \
                                {                              \
                                        Videodelay++;          \
                                }                              \
                           }*/
int stretch;
typedef struct
{
        int Addr;       /* Address of start of next visible character line in beeb memory  - raw */
        int PixmapLine; /* Current line in the pixmap */
        int PreviousFinalPixmapLine; /* The last pixmap line on the previous frame */
        int IsTeletext; /* This frame is a teletext frame - do things differently */
        unsigned char *DataPtr;  /* Pointer into host memory of video data */
        int CharLine;   /* 6845 counts in characters vertically - 0 at the top , incs by 1 - -1 means we are in the bit before the actual display starts */
        int InCharLine; /* Scanline within a character line - counts down*/
        int InCharLineUp; /* Scanline within a character line - counts up*/
        int VSyncState; // Cannot =0 in MSVC $NRM; /* When >0 VSync is high */
} VideoStateT;

VideoStateT VideoState;

