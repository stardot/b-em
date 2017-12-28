#ifdef WIN32
#define _WIN32_IE 0x400
#include <windows.h>
#include <windowsx.h>
#include <commctrl.h>
#include "resources.h"

#include <stdio.h>
#include <stdint.h>
#include "mem.h"
#include "win.h"

static HWND hWndROMList = NULL;

static int LVInsertColumn(HWND hWnd, UINT uCol, const LPTSTR pszText, int iAlignment, UINT uWidth) {
    LVCOLUMN lc = {0};
    lc.mask = LVCF_SUBITEM | LVCF_TEXT | LVCF_FMT | LVCF_WIDTH;
    lc.fmt = iAlignment;
    lc.pszText = pszText;
    lc.iSubItem = uCol;
    lc.cx = uWidth;
    return ListView_InsertColumn(hWnd, uCol, &lc);
}

static int LVInsertItem(HWND hWnd, UINT uRow, UINT uCol, const LPTSTR pszText, LPARAM lParam) {
    LVITEM li = {0};
    li.mask = LVIF_TEXT | LVIF_PARAM;
    li.iItem = uRow;
    li.iSubItem = uCol;
    li.pszText = pszText;
    li.lParam = (lParam ? lParam : uRow);
    return ListView_InsertItem(hWnd, &li);
}

static void rom_update(int slot, int row) {
    const uint8_t *detail;
    char *rr, str[30], *name;

    rr = rom_slots[slot].swram ? "RAM" : "";
    ListView_SetItemText(hWndROMList, row, 1, rr);
    if ((detail = mem_romdetail(slot)))
        snprintf(str, sizeof str, "%s %02X", (char *)detail+1, *detail);
    else
        str[0] = 0;
    ListView_SetItemText(hWndROMList, row, 2, str);
    if (!(name = rom_slots[slot].name))
        name = "";
    ListView_SetItemText(hWndROMList, row, 3, name);
}

static void rom_list(void) {
    int row, slot;
    char str[20];
    
    ListView_DeleteAllItems(hWndROMList);
    for (row = 0; row < ROM_NSLOT; row++) {
        slot = ROM_NSLOT - 1 - row;
        snprintf(str, sizeof str, "%02d (%X)", slot, slot);
        LVInsertItem(hWndROMList, row, 0, (LPTSTR)str, 0);
        rom_update(slot, row);
    }
}

static void rom_action(HWND hwnd, void (*call_back)(HWND hwnd, int slot)) {
    int row, slot;

    row = ListView_GetSelectionMark(hWndROMList);
    if (row >= 0 && row < ROM_NSLOT) {
        slot = ROM_NSLOT - 1 - row;
        if (!rom_slots[slot].locked) {
            log_debug("win-romconfig: calling callback for row=%d, slot=%d", row, slot);
            call_back(hwnd, slot);
            rom_update(slot, row);
        }
    }
}

static char rom_path[MAX_PATH];

static void select_rom(HWND hwnd, int slot) {
    char *name;
    OPENFILENAME ofn;

    log_debug("win-romconfig: about to create dialogue");
    rom_path[0] = 0;
    ZeroMemory(&ofn, sizeof(ofn));
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = ghwnd;
    ofn.lpstrFilter = "ROMS (*.rom)\0*.rom\0\0";
    ofn.lpstrFile = rom_path;
    ofn.nMaxFile = sizeof(rom_path);
    ofn.nFilterIndex = 1;
    ofn.lpstrFileTitle = NULL;
    ofn.nMaxFileTitle = 0;
    ofn.lpstrInitialDir = NULL;
    ofn.Flags = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST;
    if (GetOpenFileName(&ofn)) {
        if ((name = strrchr(rom_path, '\\')) || (name = strrchr(rom_path, '/')))
            name++;
        else
            name = rom_path;
        mem_loadrom(slot, name, rom_path, 0);
    } else {
        switch(CommDlgExtendedError()) {
            case CDERR_DIALOGFAILURE:
                log_debug("win-romcfg: CDERR_DIALOGFAILURE");
                break;
            case CDERR_FINDRESFAILURE:
                log_debug("win-romcfg: CDERR_FINDRESFAILURE");
                break;
            case CDERR_INITIALIZATION:
                log_debug("win-romcfg: CDERR_INITIALIZATION");
                break;
            case CDERR_LOADRESFAILURE:
                log_debug("win-romcfg: CDERR_LOADRESFAILURE");
                break;
            case CDERR_LOADSTRFAILURE:
                log_debug("win-romcfg: CDERR_LOADSTRFAILURE");
                break;
            case CDERR_LOCKRESFAILURE:
                log_debug("win-romcfg: CDERR_LOCKRESFAILURE");
                break;
            case CDERR_MEMALLOCFAILURE:
                log_debug("win-romcfg: CDERR_MEMALLOCFAILURE");
                break;
            case CDERR_MEMLOCKFAILURE:
                log_debug("win-romcfg: CDERR_MEMLOCKFAILURE");
                break;
            case CDERR_NOHINSTANCE:
                log_debug("win-romcfg: CDERR_NOHINSTANCE");
                break;
            case CDERR_NOHOOK:
                log_debug("win-romcfg: CDERR_NOHOOK");
                break;
            case CDERR_NOTEMPLATE:
                log_debug("win-romcfg: CDERR_NOTEMPLATE");
                break;
            case CDERR_STRUCTSIZE:
                log_debug("win-romcfg: CDERR_STRUCTSIZE");
                break;
            case FNERR_BUFFERTOOSMALL:
                log_debug("win-romcfg: FNERR_BUFFERTOOSMALL");
                break;
            case FNERR_INVALIDFILENAME:
                log_debug("win-romcfg: FNERR_INVALIDFILENAME");
                break;
            case FNERR_SUBCLASSFAILURE:
                log_debug("win-romcfg: FNERR_SUBCLASSFAILURE");
                break;
            default:
                log_debug("win-romcfg: dialoge error %d", CommDlgExtendedError());
        }
    }
    //
    //if (!getfile(hwnd, "Save State (*.SNP)\0*.SNP\0All files (*.*)\0*.*\0\0", path)) {
}

static void mark_as_ram(HWND hwnd, int slot) {
    rom_slots[slot].swram = 1;
}

static void mark_as_rom(HWND hwnd, int slot) {
    rom_slots[slot].swram = 0;
}

static void clear_rom(HWND hwnd, int slot) {
    mem_clearrom(slot);
}

BOOL CALLBACK ROMConfigDlgProc(HWND hwndDlg, UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
        case WM_INITDIALOG:
            hWndROMList = GetDlgItem(hwndDlg, IDC_ROMLIST);
            ListView_SetExtendedListViewStyle(hWndROMList, LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES);
            LVInsertColumn(hWndROMList, 0, "Bank", LVCFMT_LEFT, 45);
            LVInsertColumn(hWndROMList, 1, "RAM?", LVCFMT_LEFT, 45);
            LVInsertColumn(hWndROMList, 2, "ROM Title", LVCFMT_LEFT, 100);
            LVInsertColumn(hWndROMList, 3, "File name", LVCFMT_LEFT, 200);
            rom_list();
            return TRUE;

        case WM_COMMAND:
            switch (LOWORD(wParam)) {
                case IDOK:
                    log_debug("win-romconfig: IDOK");
                    EndDialog(hwndDlg, TRUE);
                    return TRUE;
                case IDC_SELECT_ROM:
                    log_debug("win-romconfig: IDC_SELECT_ROM");
                    rom_action(hwndDlg, select_rom);
                    break;                    
                case IDC_MARK_ROM:
                    log_debug("win-romconfig: IDC_MARK_ROM");
                    rom_action(hwndDlg, mark_as_rom);
                    break;;
                case IDC_MARK_RAM:
                    log_debug("win-romconfig: IDC_MARK_RAM");
                    rom_action(hwndDlg, mark_as_ram);
                    break;;
                case IDC_CLEAR_ROM:
                    log_debug("win-romconfig: IDC_CLEAR_ROM");
                    rom_action(hwndDlg, clear_rom);
                default:
                    log_debug("win-romcfg: unknown WM_COMMAND %d", LOWORD(wParam));
            }
            break;
    }
    return FALSE;
}

#endif
