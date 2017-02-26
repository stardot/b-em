/*B-em v2.2 by Tom Walker
  Windows tape catalogue window*/

#ifdef WIN32
#include <windows.h>

#include "b-em.h"
#include "csw.h"
#include "uef.h"

#include "resources.h"

void            findfilenamescsw();
void            findfilenamesuef();

HWND            hwndCat;
HWND            cath;
int             catwindowopen = 0;

void clearcatwindow()
{
	int             c;
	if (!catwindowopen)
		return;
	for (c = 100; c >= 0; c--)
		SendMessage(cath, LB_DELETESTRING, c, (LPARAM) NULL);
}

void cataddname(char *s)
{
	if (!catwindowopen)
		return;
	SendMessage(cath, LB_ADDSTRING, (WPARAM) NULL, (LPARAM) s);
}

BOOL CALLBACK catdlgproc(HWND hdlg, UINT message, WPARAM wParam,
    LPARAM lParam)
{
	switch (message) {
	case WM_INITDIALOG:
		cath = GetDlgItem(hdlg, ListBox1);
		catwindowopen = 1;
		if (csw_ena)
			csw_findfilenames();
		else
			uef_findfilenames();
//                SendMessage(h,LB_ADDSTRING,NULL,"test");
		return TRUE;
	case WM_COMMAND:
		switch (LOWORD(wParam)) {
		case IDCANCEL:
		case IDOK:
			return TRUE;
		}
		break;
	case WM_SYSCOMMAND:
		switch (LOWORD(wParam) & 0xFFF0) {
		case SC_CLOSE:
			DestroyWindow(hdlg);
			catwindowopen = 0;
			return TRUE;
		}
		break;
	}
	return FALSE;
}

void showcatalogue(HINSTANCE hinstance, HWND hwnd)
{
	if (!IsWindow(hwndCat)) {
		hwndCat = CreateDialog(hinstance,
		    TEXT("Catalogue"), hwnd, (DLGPROC) catdlgproc);
		ShowWindow(hwndCat, SW_SHOW);
	}
}
#endif
