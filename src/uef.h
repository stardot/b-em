#ifndef _UEF_H
#define _UEF_H

#define UEFMode_Callback        0
#define UEFMode_Poll            1

/* some defines related to the status byte - these may change! */

#define UEF_MMASK                       (3 << 16)
#define UEF_STARTBIT            (2 << 8)
#define UEF_STOPBIT                     (1 << 8)
#define UEF_BYTEMASK            0xff

/* some macros for reading parts of the status byte */

#define UEF_HTONE                       (0 << 16)
#define UEF_DATA                        (1 << 16)
#define UEF_GAP                         (2 << 16)

#define UEFRES_TYPE(x)          (x&UEF_MMASK)
#define UEFRES_BYTE(x)          (x&UEF_BYTEMASK)
#define UEFRES_10BIT(x)         (((x&UEF_BYTEMASK) << 1) | ((x&UEF_STARTBIT) ? 1 : 0) | ((x&UEF_STOPBIT) ? 0x200 : 0))
#define UEFRES_STARTBIT(x)      (x&UEF_STARTBIT)
#define UEFRES_STOPBIT(x)       (x&UEF_STOPBIT)

/* some possible return states */
#define UEF_OK  0

#define UEF_SETMODE_INVALID     -1

#define UEF_OPEN_NOTUEF         -1
#define UEF_OPEN_NOTTAPE        -2
#define UEF_OPEN_NOFILE         -3
#define UEF_OPEN_MEMERR         -4

#ifdef UEF_DLL

#define UEFEXDECL __declspec( dllexport )
#define UEFDECL UEFEXDECL

#else

#define UEFEXDECL extern
#define UEFDECL

#endif

#ifndef _WINDOWS

#define BOOL int
#define TRUE 0
#define FALSE -1

#endif

#ifdef  __cplusplus
extern "C" {
#endif

#ifndef UEF_DLL
extern BOOL uef_libinit(void);
extern BOOL uef_libclose(void);
#endif

typedef int UEFFILE;
UEFEXDECL int uef_errno;

/* setup */
UEFEXDECL int uef_setmode(int mde);
UEFEXDECL void uef_setclock(int beats);
UEFEXDECL void uef_zeroclock(UEFFILE file);
UEFEXDECL int uef_setcallback(void (* func)(UEFFILE file, int event));
UEFEXDECL int uef_getlowesttime(UEFFILE file, int time);

/* poll mode */
UEFEXDECL int uef_getdata(UEFFILE file, int time);

/* callback mode */
UEFEXDECL void uef_keepalive(UEFFILE file);
UEFEXDECL void uef_keepalive_all(void);

/* pause / unpause */
UEFEXDECL void uef_pause(UEFFILE file);
UEFEXDECL void uef_unpause(UEFFILE file);

/* open & close */
UEFEXDECL UEFFILE uef_open(char *name);
UEFEXDECL void uef_close(UEFFILE file);

/* overran count management */
UEFEXDECL void uef_resetoverrun(UEFFILE file);
UEFEXDECL int uef_getoverrun(UEFFILE file);

#ifdef  __cplusplus
}
#endif

#endif