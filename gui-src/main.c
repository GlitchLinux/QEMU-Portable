/*
 * QEMU-Portable.exe - Portable QEMU Launcher with Win32 GUI
 *
 * Lightweight native GUI launcher for QEMU x86_64 virtualization.
 * Wizard-style page flow for boot source, firmware, and RAM config.
 *
 * Build (cross-compile from Linux):
 *   x86_64-w64-mingw32-windres resources.rc -o resources.o
 *   x86_64-w64-mingw32-gcc -O2 -o QEMU-Portable.exe main.c resources.o \
 *       -lgdi32 -lcomctl32 -lcomdlg32 -mwindows -municode
 *
 * (c) 2026 Glitch Linux - https://glitchlinux.com
 */

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <commctrl.h>
#include <commdlg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <shlobj.h>

#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "comdlg32.lib")

#define IDI_APP 101

#define COL_BG        RGB(27,  27,  27)
#define COL_BG_ALT    RGB(36,  36,  36)
#define COL_BG_INPUT  RGB(42,  42,  42)
#define COL_ACCENT    RGB(189, 189, 189)
#define COL_FG        RGB(230, 230, 230)
#define COL_FG_DIM    RGB(130, 130, 130)
#define COL_BORDER    RGB(58,  58,  58)
#define COL_DANGER    RGB(255,  85,  85)
#define COL_HIGHLIGHT RGB(30,  30,  30)
#define COL_HOVER     RGB(48,  48,  48)
#define COL_LB_BG     RGB(30,  30,  30)

#define CLIENT_W 460
#define CLIENT_H 460
#define MARGIN    20
#define CONTENT_Y 132  /* Y where page content starts (below icon+title+sep) */
#define BTN_ROW_Y (CLIENT_H - 52)

/* IDs */
enum {
    ID_BTN_PHYSICAL = 1001, ID_BTN_ISO, ID_BTN_IPXE, ID_BTN_COMBO,
    ID_BTN_EXIT, ID_BTN_LAUNCH, ID_BTN_BACK, ID_BTN_BROWSE,
    ID_BTN_KILL, ID_BTN_REFRESH, ID_BTN_SELECT, ID_BTN_MENU,
    ID_BTN_COMBO_PHYS, ID_BTN_COMBO_VIRT, ID_BTN_BROWSE_COMBO_ISO,
    ID_BTN_BROWSE_COMBO_VDISK, ID_BTN_COMBO_SELECT, ID_BTN_COMBO_REFRESH,
    ID_RADIO_BIOS = 1100, ID_RADIO_UEFI, ID_EDIT_RAM,
    ID_DISK_LIST = 1200, ID_EDIT_FILE,
    ID_LBL1 = 1300, ID_LBL2, ID_LBL3, ID_LBL_FW, ID_LBL_RAM, ID_LBL_MB,
    ID_SESSION_INFO = 1400,
    ID_COMBO_DISK_LIST = 1500, ID_EDIT_COMBO_ISO, ID_EDIT_COMBO_VDISK,
    ID_LBL_CONFIRM1, ID_LBL_CONFIRM2, ID_LBL_CONFIRM3
};

/* Pages */
enum {
    PG_MENU, PG_DISK, PG_FILE, PG_CONFIG, PG_RUNNING,
    PG_COMBO_ISO, PG_COMBO_DISKTYPE, PG_COMBO_PHYSICAL, PG_COMBO_VIRTUAL, PG_COMBO_CONFIRM
};

#define MAX_DISKS 32
typedef struct { WCHAR path[64]; WCHAR display[256]; int index; } DiskEntry;

static HINSTANCE g_hInst;
static HWND g_hWnd;
static int g_page = PG_MENU;

/* All controls - created once, shown/hidden per page */
static HWND g_btn[30];  /* indexed by ID - 1001 */
static HWND g_radio[2]; /* BIOS, UEFI */
static HWND g_editRam, g_editFile;
static HWND g_editComboIso, g_editComboVdisk;
static HWND g_diskList, g_comboDiskList;
static HWND g_lbl[10];  /* generic labels */
static HWND g_lblFw, g_lblRam, g_lblMb;
static HWND g_lblConfirm[3];
static HWND g_sessionInfo;

/* State */
static WCHAR g_launcherDir[MAX_PATH], g_qemuBin[MAX_PATH], g_qemuRoot[MAX_PATH];
static WCHAR g_ovmfCode[MAX_PATH], g_ovmfVarsTemplate[MAX_PATH];
static WCHAR g_selectedSource[2048], g_secondaryDisk[2048];
static BOOL g_sourceIsPhysical, g_sourceIsIpxe, g_hasSecondary;
static HANDLE g_hQemuProc;
static DiskEntry g_disks[MAX_DISKS];
static int g_diskCount;

/* GDI */
static HBRUSH g_brBg, g_brBgAlt, g_brInput, g_brLb;
static HFONT g_fNormal, g_fTitle, g_fMono, g_fBtn, g_fSmall;
static HICON g_icon64, g_icon32;

/* Helpers */
#define BTN(id) g_btn[(id) - 1001]
#define CW (CLIENT_W - MARGIN * 2)

static HWND MkBtn(HWND p, int id, const WCHAR *t, int x, int y, int w, int h) {
    HWND hw = CreateWindowW(L"BUTTON", t, WS_CHILD | BS_OWNERDRAW,
        x, y, w, h, p, (HMENU)(INT_PTR)id, g_hInst, NULL);
    if (id >= 1001 && id <= 1030) BTN(id) = hw;
    return hw;
}

static HWND MkLabel(HWND p, int id, const WCHAR *t, int x, int y, int w, int h, HFONT f) {
    HWND hw = CreateWindowW(L"STATIC", t, WS_CHILD | SS_LEFT,
        x, y, w, h, p, (HMENU)(INT_PTR)id, g_hInst, NULL);
    SendMessageW(hw, WM_SETFONT, (WPARAM)f, TRUE);
    return hw;
}

static HWND MkEdit(HWND p, int id, int x, int y, int w, int h) {
    HWND hw = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
        WS_CHILD | ES_AUTOHSCROLL, x, y, w, h, p, (HMENU)(INT_PTR)id, g_hInst, NULL);
    SendMessageW(hw, WM_SETFONT, (WPARAM)g_fMono, TRUE);
    return hw;
}

static HWND MkList(HWND p, int id, int x, int y, int w, int h) {
    HWND hw = CreateWindowExW(0, L"LISTBOX", NULL,
        WS_CHILD | WS_VSCROLL | LBS_NOTIFY | LBS_NOINTEGRALHEIGHT | LBS_OWNERDRAWFIXED | LBS_HASSTRINGS,
        x, y, w, h, p, (HMENU)(INT_PTR)id, g_hInst, NULL);
    SendMessageW(hw, WM_SETFONT, (WPARAM)g_fMono, TRUE);
    return hw;
}

/* ---- Disk enumeration ---- */
static void EnumerateDisks(void) {
    g_diskCount = 0;
    WCHAR sp[MAX_PATH]; GetTempPathW(MAX_PATH, sp); wcscat(sp, L"qp_ld.txt");
    HANDLE hS = CreateFileW(sp, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hS == INVALID_HANDLE_VALUE) return;
    DWORD w; WriteFile(hS, "list disk\r\n", 11, &w, NULL); CloseHandle(hS);

    SECURITY_ATTRIBUTES sa = { sizeof(sa), NULL, TRUE };
    HANDLE hR, hW; CreatePipe(&hR, &hW, &sa, 0);
    SetHandleInformation(hR, HANDLE_FLAG_INHERIT, 0);

    WCHAR cmd[512]; wsprintfW(cmd, L"diskpart /s \"%s\"", sp);
    STARTUPINFOW si; PROCESS_INFORMATION pi;
    ZeroMemory(&si, sizeof(si)); si.cb = sizeof(si);
    si.hStdOutput = hW; si.hStdError = hW;
    si.dwFlags = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW; si.wShowWindow = SW_HIDE;
    if (!CreateProcessW(NULL, cmd, NULL, NULL, TRUE, CREATE_NO_WINDOW, NULL, NULL, &si, &pi))
        { CloseHandle(hR); CloseHandle(hW); DeleteFileW(sp); return; }
    CloseHandle(hW);

    char buf[16384]; int pos = 0; DWORD br;
    while (ReadFile(hR, buf + pos, sizeof(buf) - pos - 1, &br, NULL) && br > 0)
        { pos += br; if (pos >= (int)sizeof(buf) - 1) break; }
    buf[pos] = 0;
    WaitForSingleObject(pi.hProcess, 5000);
    CloseHandle(pi.hProcess); CloseHandle(pi.hThread); CloseHandle(hR); DeleteFileW(sp);

    char *line = strtok(buf, "\r\n");
    while (line && g_diskCount < MAX_DISKS) {
        while (*line == ' ') line++;
        char w1[64]={0}, w2[64]={0}; int consumed=0;
        if (sscanf(line, "%63s %63s %n", w1, w2, &consumed) >= 2) {
            char *ep; long idx = strtol(w2, &ep, 10);
            if (*ep == '\0' && ep != w2 && idx >= 0) {
                const char *tail = line + consumed; while (*tail == ' ') tail++;
                char st[64]={0}, sz[64]={0}, su[16]={0};
                sscanf(tail, "%63s %63s %15s", st, sz, su);
                wsprintfW(g_disks[g_diskCount].path, L"\\\\.\\PhysicalDrive%ld", idx);
                g_disks[g_diskCount].index = (int)idx;
                WCHAR ws[64], wz[64], wu[16];
                MultiByteToWideChar(CP_ACP, 0, st, -1, ws, 64);
                MultiByteToWideChar(CP_ACP, 0, sz, -1, wz, 64);
                MultiByteToWideChar(CP_ACP, 0, su, -1, wu, 16);
                wsprintfW(g_disks[g_diskCount].display, L"  Disk %ld      %s %s      (%s)", idx, wz, wu, ws);
                g_diskCount++;
            }
        }
        line = strtok(NULL, "\r\n");
    }
}

static void PopulateList(HWND hL) {
    if (!hL) return;
    SendMessageW(hL, LB_RESETCONTENT, 0, 0);
    for (int i = 0; i < g_diskCount; i++)
        SendMessageW(hL, LB_ADDSTRING, 0, (LPARAM)g_disks[i].display);
}

/* ---- File open dialog ---- */
static BOOL BrowseImage(HWND owner, WCHAR *out, int outLen) {
    OPENFILENAMEW ofn; WCHAR fp[2048] = {0};
    ZeroMemory(&ofn, sizeof(ofn)); ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = owner;
    ofn.lpstrFilter = L"All Bootable Images\0*.iso;*.img;*.qcow2;*.vhd;*.vhdx;*.vdi\0"
        L"ISO Files\0*.iso\0Disk Images\0*.img;*.qcow2;*.vhd;*.vhdx;*.vdi\0All Files\0*.*\0";
    ofn.lpstrFile = fp; ofn.nMaxFile = 2048;
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;
    ofn.lpstrTitle = L"Select Boot Image";
    if (GetOpenFileNameW(&ofn)) { wcsncpy(out, fp, outLen); return TRUE; }
    return FALSE;
}

/* ---- Launch ---- */
static void LaunchQemu(void) {
    BOOL uefi = (SendMessageW(g_radio[1], BM_GETCHECK, 0, 0) == BST_CHECKED);
    WCHAR rs[32]; GetWindowTextW(g_editRam, rs, 32);
    int ram = _wtoi(rs); if (ram < 128) ram = 2048;
    WCHAR fw[1024]={0}, src[1024]={0}, sec[512]={0};

    if (uefi && GetFileAttributesW(g_ovmfCode) == INVALID_FILE_ATTRIBUTES) {
        MessageBoxW(g_hWnd, L"OVMF_CODE.fd not found - using BIOS.", L"QEMU Portable", MB_OK|MB_ICONWARNING);
        uefi = FALSE;
    }
    if (uefi) {
        WCHAR nd[MAX_PATH]; wsprintfW(nd, L"%s\\nvram", g_launcherDir); CreateDirectoryW(nd, NULL);
        WCHAR nc[MAX_PATH]; wsprintfW(nc, L"%s\\OVMF_VARS_%u%u.fd", nd, GetTickCount(), GetCurrentProcessId());
        CopyFileW(g_ovmfVarsTemplate, nc, FALSE);
        wsprintfW(fw, L"-drive if=pflash,format=raw,readonly=on,file=\"%s\" -drive if=pflash,format=raw,file=\"%s\"", g_ovmfCode, nc);
    }
    if (g_sourceIsIpxe) {
        WCHAR ip[MAX_PATH]; wsprintfW(ip, L"%s\\gLiTcH-iPXE.iso", g_qemuRoot);
        wsprintfW(src, L"-cdrom \"%s\" -boot d", ip);
    } else if (g_sourceIsPhysical) {
        wsprintfW(src, L"-hda \"%s\"", g_selectedSource);
    } else {
        int l = (int)wcslen(g_selectedSource);
        if (l > 4 && _wcsicmp(g_selectedSource + l - 4, L".iso") == 0)
            wsprintfW(src, L"-cdrom \"%s\" -boot d", g_selectedSource);
        else wsprintfW(src, L"-hda \"%s\"", g_selectedSource);
    }
    if (g_hasSecondary && g_secondaryDisk[0]) {
        int l = (int)wcslen(g_selectedSource);
        BOOL iso = (l > 4 && _wcsicmp(g_selectedSource + l - 4, L".iso") == 0);
        wsprintfW(sec, (iso || g_sourceIsIpxe) ? L"-hda \"%s\"" : L"-hdb \"%s\"", g_secondaryDisk);
    }

    WCHAR cmd[4096];
    wsprintfW(cmd, L"\"%s\" -m %d -display gtk %s %s %s", g_qemuBin, ram, fw, src, sec);
    STARTUPINFOW si; PROCESS_INFORMATION pi;
    ZeroMemory(&si, sizeof(si)); si.cb = sizeof(si); ZeroMemory(&pi, sizeof(pi));
    if (!CreateProcessW(NULL, cmd, NULL, NULL, FALSE, CREATE_NO_WINDOW, NULL, g_qemuRoot, &si, &pi)) {
        WCHAR e[512]; wsprintfW(e, L"Failed to launch QEMU (error %lu)", GetLastError());
        MessageBoxW(g_hWnd, e, L"QEMU Portable", MB_OK|MB_ICONERROR); return;
    }
    g_hQemuProc = pi.hProcess; CloseHandle(pi.hThread);
}

static void KillQemu(void) {
    if (g_hQemuProc) { TerminateProcess(g_hQemuProc, 0); WaitForSingleObject(g_hQemuProc, 3000);
        CloseHandle(g_hQemuProc); g_hQemuProc = NULL; }
}

/* ---- Drawing ---- */
static void DrawBtn(LPDRAWITEMSTRUCT di, COLORREF border, COLORREF text) {
    HBRUSH b = CreateSolidBrush(COL_BG_ALT); FillRect(di->hDC, &di->rcItem, b); DeleteObject(b);
    HPEN p = CreatePen(PS_SOLID, 1, border); SelectObject(di->hDC, p);
    SelectObject(di->hDC, GetStockObject(NULL_BRUSH));
    RoundRect(di->hDC, di->rcItem.left, di->rcItem.top, di->rcItem.right, di->rcItem.bottom, 6, 6);
    DeleteObject(p);
    if (di->itemState & ODS_SELECTED) { b = CreateSolidBrush(COL_HOVER); FillRect(di->hDC, &di->rcItem, b); DeleteObject(b); }
    SetBkMode(di->hDC, TRANSPARENT); SetTextColor(di->hDC, text); SelectObject(di->hDC, g_fBtn);
    WCHAR t[256]; GetWindowTextW(di->hwndItem, t, 256);
    DrawTextW(di->hDC, t, -1, &di->rcItem, DT_CENTER|DT_VCENTER|DT_SINGLELINE);
}

static void DrawBtnFill(LPDRAWITEMSTRUCT di, COLORREF bg, COLORREF text) {
    HBRUSH b = CreateSolidBrush(bg); FillRect(di->hDC, &di->rcItem, b); DeleteObject(b);
    HPEN p = CreatePen(PS_SOLID, 1, bg); SelectObject(di->hDC, p);
    SelectObject(di->hDC, GetStockObject(NULL_BRUSH));
    RoundRect(di->hDC, di->rcItem.left, di->rcItem.top, di->rcItem.right, di->rcItem.bottom, 6, 6);
    DeleteObject(p);
    if (di->itemState & ODS_SELECTED) { b = CreateSolidBrush(COL_HOVER); FillRect(di->hDC, &di->rcItem, b); DeleteObject(b); }
    SetBkMode(di->hDC, TRANSPARENT); SetTextColor(di->hDC, text); SelectObject(di->hDC, g_fBtn);
    WCHAR t[256]; GetWindowTextW(di->hwndItem, t, 256);
    DrawTextW(di->hDC, t, -1, &di->rcItem, DT_CENTER|DT_VCENTER|DT_SINGLELINE);
}

/* ---- All HWNDs for hide/show ---- */
#define MAX_CTRLS 64
static HWND g_allCtrls[MAX_CTRLS];
static int g_ctrlCount = 0;
static void RegCtrl(HWND h) { if (g_ctrlCount < MAX_CTRLS) g_allCtrls[g_ctrlCount++] = h; }
static void HideAll(void) { for (int i = 0; i < g_ctrlCount; i++) if (g_allCtrls[i]) ShowWindow(g_allCtrls[i], SW_HIDE); }

/* ---- ShowPage ---- */
static void ShowPage(int pg) {
    g_page = pg;
    HideAll();
    int cw = CW;

    switch (pg) {
    case PG_MENU:
        ShowWindow(BTN(ID_BTN_PHYSICAL), SW_SHOW);
        ShowWindow(BTN(ID_BTN_ISO), SW_SHOW);
        ShowWindow(BTN(ID_BTN_COMBO), SW_SHOW);
        ShowWindow(BTN(ID_BTN_IPXE), SW_SHOW);
        ShowWindow(BTN(ID_BTN_EXIT), SW_SHOW);
        break;

    case PG_DISK:
        ShowWindow(g_lbl[0], SW_SHOW); /* "Select a physical drive:" */
        ShowWindow(g_diskList, SW_SHOW);
        ShowWindow(BTN(ID_BTN_BACK), SW_SHOW);
        ShowWindow(BTN(ID_BTN_REFRESH), SW_SHOW);
        ShowWindow(BTN(ID_BTN_SELECT), SW_SHOW);
        EnumerateDisks(); PopulateList(g_diskList);
        break;

    case PG_FILE:
        ShowWindow(g_lbl[1], SW_SHOW); /* "ISO or disk image:" */
        ShowWindow(g_editFile, SW_SHOW);
        ShowWindow(BTN(ID_BTN_BROWSE), SW_SHOW);
        ShowWindow(g_lblFw, SW_SHOW); ShowWindow(g_radio[0], SW_SHOW); ShowWindow(g_radio[1], SW_SHOW);
        ShowWindow(g_lblRam, SW_SHOW); ShowWindow(g_editRam, SW_SHOW); ShowWindow(g_lblMb, SW_SHOW);
        ShowWindow(BTN(ID_BTN_BACK), SW_SHOW);
        ShowWindow(BTN(ID_BTN_LAUNCH), SW_SHOW);
        break;

    case PG_CONFIG: /* firmware+RAM after disk select or iPXE */
        ShowWindow(g_lblFw, SW_SHOW); ShowWindow(g_radio[0], SW_SHOW); ShowWindow(g_radio[1], SW_SHOW);
        ShowWindow(g_lblRam, SW_SHOW); ShowWindow(g_editRam, SW_SHOW); ShowWindow(g_lblMb, SW_SHOW);
        ShowWindow(BTN(ID_BTN_BACK), SW_SHOW);
        ShowWindow(BTN(ID_BTN_LAUNCH), SW_SHOW);
        break;

    case PG_RUNNING:
        ShowWindow(g_sessionInfo, SW_SHOW);
        ShowWindow(BTN(ID_BTN_MENU), SW_SHOW);
        ShowWindow(BTN(ID_BTN_KILL), SW_SHOW);
        break;

    /* ---- Combo wizard steps ---- */
    case PG_COMBO_ISO:
        ShowWindow(g_lbl[2], SW_SHOW); /* "Step 1: Select boot image" */
        ShowWindow(g_editComboIso, SW_SHOW);
        ShowWindow(BTN(ID_BTN_BROWSE_COMBO_ISO), SW_SHOW);
        ShowWindow(BTN(ID_BTN_BACK), SW_SHOW);
        /* We use "Select" button to proceed to next step */
        ShowWindow(BTN(ID_BTN_SELECT), SW_SHOW);
        break;

    case PG_COMBO_DISKTYPE:
        ShowWindow(g_lbl[3], SW_SHOW); /* "Step 2: Extra disk type" */
        ShowWindow(BTN(ID_BTN_COMBO_PHYS), SW_SHOW);
        ShowWindow(BTN(ID_BTN_COMBO_VIRT), SW_SHOW);
        ShowWindow(BTN(ID_BTN_BACK), SW_SHOW);
        break;

    case PG_COMBO_PHYSICAL:
        ShowWindow(g_lbl[4], SW_SHOW); /* "Step 3: Select physical drive" */
        ShowWindow(g_comboDiskList, SW_SHOW);
        ShowWindow(BTN(ID_BTN_COMBO_REFRESH), SW_SHOW);
        ShowWindow(BTN(ID_BTN_COMBO_SELECT), SW_SHOW);
        ShowWindow(BTN(ID_BTN_BACK), SW_SHOW);
        EnumerateDisks(); PopulateList(g_comboDiskList);
        break;

    case PG_COMBO_VIRTUAL:
        ShowWindow(g_lbl[5], SW_SHOW); /* "Step 3: Select virtual disk" */
        ShowWindow(g_editComboVdisk, SW_SHOW);
        ShowWindow(BTN(ID_BTN_BROWSE_COMBO_VDISK), SW_SHOW);
        ShowWindow(BTN(ID_BTN_SELECT), SW_SHOW); /* reuse as "Next" */
        ShowWindow(BTN(ID_BTN_BACK), SW_SHOW);
        break;

    case PG_COMBO_CONFIRM:
        ShowWindow(g_lblConfirm[0], SW_SHOW);
        ShowWindow(g_lblConfirm[1], SW_SHOW);
        ShowWindow(g_lblConfirm[2], SW_SHOW);
        ShowWindow(g_lblFw, SW_SHOW); ShowWindow(g_radio[0], SW_SHOW); ShowWindow(g_radio[1], SW_SHOW);
        ShowWindow(g_lblRam, SW_SHOW); ShowWindow(g_editRam, SW_SHOW); ShowWindow(g_lblMb, SW_SHOW);
        ShowWindow(BTN(ID_BTN_BACK), SW_SHOW);
        ShowWindow(BTN(ID_BTN_LAUNCH), SW_SHOW);
        /* Update confirm labels */
        { WCHAR t1[512], t2[512];
          const WCHAR *s = wcsrchr(g_selectedSource, L'\\');
          wsprintfW(t1, L"Boot:  %s", s ? s+1 : g_selectedSource);
          const WCHAR *d = wcsrchr(g_secondaryDisk, L'\\');
          if (!d) d = g_secondaryDisk;
          else d++;
          /* For physical drives, show the path as-is */
          if (wcsncmp(g_secondaryDisk, L"\\\\.\\", 4) == 0)
              wsprintfW(t2, L"Disk:  %s", g_secondaryDisk);
          else
              wsprintfW(t2, L"Disk:  %s", d);
          SetWindowTextW(g_lblConfirm[1], t1);
          SetWindowTextW(g_lblConfirm[2], t2);
        }
        break;
    }
    InvalidateRect(g_hWnd, NULL, TRUE);
}

/* ---- WndProc ---- */
static LRESULT CALLBACK WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_CREATE: {
        /* Dark title bar */
        { typedef HRESULT (WINAPI *F)(HWND,DWORD,LPCVOID,DWORD);
          HMODULE m = LoadLibraryW(L"dwmapi.dll");
          if (m) { F f = (F)(void*)GetProcAddress(m, "DwmSetWindowAttribute");
                   if (f) { BOOL d = TRUE; f(hWnd, 20, &d, sizeof(d)); } } }

        g_brBg = CreateSolidBrush(COL_BG); g_brBgAlt = CreateSolidBrush(COL_BG_ALT);
        g_brInput = CreateSolidBrush(COL_BG_INPUT); g_brLb = CreateSolidBrush(COL_LB_BG);

        g_fNormal = CreateFontW(-14,0,0,0,FW_NORMAL,0,0,0,DEFAULT_CHARSET,0,0,CLEARTYPE_QUALITY,0,L"Segoe UI");
        g_fTitle  = CreateFontW(-20,0,0,0,FW_SEMIBOLD,0,0,0,DEFAULT_CHARSET,0,0,CLEARTYPE_QUALITY,0,L"Segoe UI");
        g_fMono   = CreateFontW(-12,0,0,0,FW_NORMAL,0,0,0,DEFAULT_CHARSET,0,0,CLEARTYPE_QUALITY,0,L"Consolas");
        g_fBtn    = CreateFontW(-13,0,0,0,FW_MEDIUM,0,0,0,DEFAULT_CHARSET,0,0,CLEARTYPE_QUALITY,0,L"Segoe UI");
        g_fSmall  = CreateFontW(-11,0,0,0,FW_NORMAL,0,0,0,DEFAULT_CHARSET,0,0,CLEARTYPE_QUALITY,0,L"Segoe UI");

        int cw = CW;
        int bh = 38, bg = 8, my = CONTENT_Y + 14;

        /* Menu buttons */
        RegCtrl(MkBtn(hWnd, ID_BTN_PHYSICAL, L"Boot Physical Drive",       MARGIN, my,               cw, bh));
        RegCtrl(MkBtn(hWnd, ID_BTN_ISO,      L"Boot ISO / Disk Image",     MARGIN, my+(bh+bg),       cw, bh));
        RegCtrl(MkBtn(hWnd, ID_BTN_COMBO,    L"Boot ISO with Extra Disk",  MARGIN, my+(bh+bg)*2,     cw, bh));
        RegCtrl(MkBtn(hWnd, ID_BTN_IPXE,     L"gLiTcH iPXE Netboot",      MARGIN, my+(bh+bg)*3,     cw, bh));
        RegCtrl(MkBtn(hWnd, ID_BTN_EXIT,     L"Exit",                      MARGIN, my+(bh+bg)*4 + 6, cw, 32));

        /* Shared bottom buttons */
        int bw2 = (cw - 8) / 2;
        RegCtrl(MkBtn(hWnd, ID_BTN_BACK,   L"Back",      MARGIN,             BTN_ROW_Y, 100, 34));
        RegCtrl(MkBtn(hWnd, ID_BTN_LAUNCH, L"Launch VM",  MARGIN+cw-120,     BTN_ROW_Y, 120, 34));
        int bw3 = (cw - 12) / 3;
        RegCtrl(MkBtn(hWnd, ID_BTN_REFRESH,L"Refresh",    MARGIN+bw3+6,      BTN_ROW_Y, bw3, 34));
        RegCtrl(MkBtn(hWnd, ID_BTN_SELECT, L"Next",       MARGIN+(bw3+6)*2,  BTN_ROW_Y, bw3, 34));
        RegCtrl(MkBtn(hWnd, ID_BTN_BROWSE, L"Browse",     MARGIN+cw-80,      CONTENT_Y+22, 80, 26));

        /* Running page */
        RegCtrl(MkBtn(hWnd, ID_BTN_MENU, L"Main Menu",       MARGIN,          BTN_ROW_Y, bw2, 38));
        RegCtrl(MkBtn(hWnd, ID_BTN_KILL, L"Kill VM and Exit", MARGIN+bw2+8,   BTN_ROW_Y, bw2, 38));
        g_sessionInfo = MkLabel(hWnd, ID_SESSION_INFO,
            L"QEMU virtual machine is running.\n\nClose this window to keep the VM running,\nor use the button below to terminate it.",
            MARGIN, CONTENT_Y+20, cw, 100, g_fNormal);
        RegCtrl(g_sessionInfo);

        /* Disk page */
        g_lbl[0] = MkLabel(hWnd, ID_LBL1, L"Select a physical drive:", MARGIN, CONTENT_Y, cw, 18, g_fSmall);
        RegCtrl(g_lbl[0]);
        g_diskList = MkList(hWnd, ID_DISK_LIST, MARGIN, CONTENT_Y+22, cw, 210);
        RegCtrl(g_diskList);

        /* File page */
        g_lbl[1] = MkLabel(hWnd, ID_LBL2, L"ISO or disk image (.iso, .img, .qcow2, .vhd, .vhdx, .vdi):",
            MARGIN, CONTENT_Y, cw, 18, g_fSmall);
        RegCtrl(g_lbl[1]);
        g_editFile = MkEdit(hWnd, ID_EDIT_FILE, MARGIN, CONTENT_Y+22, cw-86, 24);
        RegCtrl(g_editFile);

        /* Firmware + RAM (reusable across pages, fixed position) */
        int fwY = CONTENT_Y + 70;
        g_lblFw = MkLabel(hWnd, ID_LBL_FW, L"Firmware", MARGIN, fwY, 70, 18, g_fSmall); RegCtrl(g_lblFw);
        g_radio[0] = CreateWindowW(L"BUTTON", L"BIOS", WS_CHILD|BS_AUTORADIOBUTTON|WS_GROUP,
            MARGIN+70, fwY-1, 70, 20, hWnd, (HMENU)ID_RADIO_BIOS, g_hInst, NULL);
        SendMessageW(g_radio[0], WM_SETFONT, (WPARAM)g_fNormal, TRUE);
        SendMessageW(g_radio[0], BM_SETCHECK, BST_CHECKED, 0);
        RegCtrl(g_radio[0]);
        g_radio[1] = CreateWindowW(L"BUTTON", L"UEFI", WS_CHILD|BS_AUTORADIOBUTTON,
            MARGIN+148, fwY-1, 70, 20, hWnd, (HMENU)ID_RADIO_UEFI, g_hInst, NULL);
        SendMessageW(g_radio[1], WM_SETFONT, (WPARAM)g_fNormal, TRUE);
        RegCtrl(g_radio[1]);
        int ry = fwY + 30;
        g_lblRam = MkLabel(hWnd, ID_LBL_RAM, L"RAM", MARGIN, ry+2, 70, 18, g_fSmall); RegCtrl(g_lblRam);
        g_editRam = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"4096",
            WS_CHILD|ES_NUMBER|ES_AUTOHSCROLL, MARGIN+70, ry, 86, 22, hWnd, (HMENU)ID_EDIT_RAM, g_hInst, NULL);
        SendMessageW(g_editRam, WM_SETFONT, (WPARAM)g_fMono, TRUE);
        RegCtrl(g_editRam);
        g_lblMb = MkLabel(hWnd, ID_LBL_MB, L"MB", MARGIN+160, ry+2, 30, 18, g_fSmall); RegCtrl(g_lblMb);

        /* ---- Combo wizard controls ---- */
        /* Step 1: ISO */
        g_lbl[2] = MkLabel(hWnd, 0, L"Step 1: Select the boot image (ISO or disk image):",
            MARGIN, CONTENT_Y, cw, 18, g_fSmall); RegCtrl(g_lbl[2]);
        g_editComboIso = MkEdit(hWnd, ID_EDIT_COMBO_ISO, MARGIN, CONTENT_Y+22, cw-86, 24); RegCtrl(g_editComboIso);
        RegCtrl(MkBtn(hWnd, ID_BTN_BROWSE_COMBO_ISO, L"Browse", MARGIN+cw-80, CONTENT_Y+22, 80, 26));

        /* Step 2: Disk type */
        g_lbl[3] = MkLabel(hWnd, 0, L"Step 2: What type of extra disk?",
            MARGIN, CONTENT_Y, cw, 18, g_fSmall); RegCtrl(g_lbl[3]);
        RegCtrl(MkBtn(hWnd, ID_BTN_COMBO_PHYS, L"Physical Drive", MARGIN, CONTENT_Y+34, cw, bh));
        RegCtrl(MkBtn(hWnd, ID_BTN_COMBO_VIRT, L"Virtual Disk Image", MARGIN, CONTENT_Y+34+bh+bg, cw, bh));

        /* Step 3a: Physical */
        g_lbl[4] = MkLabel(hWnd, 0, L"Step 3: Select a physical drive:",
            MARGIN, CONTENT_Y, cw, 18, g_fSmall); RegCtrl(g_lbl[4]);
        g_comboDiskList = MkList(hWnd, ID_COMBO_DISK_LIST, MARGIN, CONTENT_Y+22, cw, 190); RegCtrl(g_comboDiskList);
        RegCtrl(MkBtn(hWnd, ID_BTN_COMBO_REFRESH, L"Refresh", MARGIN+bw3+6, BTN_ROW_Y, bw3, 34));
        RegCtrl(MkBtn(hWnd, ID_BTN_COMBO_SELECT, L"Next", MARGIN+(bw3+6)*2, BTN_ROW_Y, bw3, 34));

        /* Step 3b: Virtual */
        g_lbl[5] = MkLabel(hWnd, 0, L"Step 3: Select a virtual disk image:",
            MARGIN, CONTENT_Y, cw, 18, g_fSmall); RegCtrl(g_lbl[5]);
        g_editComboVdisk = MkEdit(hWnd, ID_EDIT_COMBO_VDISK, MARGIN, CONTENT_Y+22, cw-86, 24); RegCtrl(g_editComboVdisk);
        RegCtrl(MkBtn(hWnd, ID_BTN_BROWSE_COMBO_VDISK, L"Browse", MARGIN+cw-80, CONTENT_Y+22, 80, 26));

        /* Confirm page labels */
        g_lblConfirm[0] = MkLabel(hWnd, ID_LBL_CONFIRM1, L"Confirm VM configuration:",
            MARGIN, CONTENT_Y, cw, 18, g_fSmall); RegCtrl(g_lblConfirm[0]);
        g_lblConfirm[1] = MkLabel(hWnd, ID_LBL_CONFIRM2, L"Boot:  -",
            MARGIN, CONTENT_Y+26, cw, 18, g_fMono); RegCtrl(g_lblConfirm[1]);
        g_lblConfirm[2] = MkLabel(hWnd, ID_LBL_CONFIRM3, L"Disk:  -",
            MARGIN, CONTENT_Y+48, cw, 18, g_fMono); RegCtrl(g_lblConfirm[2]);

        ShowPage(PG_MENU);
        break;
    }

    case WM_PAINT: {
        PAINTSTRUCT ps; HDC hdc = BeginPaint(hWnd, &ps);
        FillRect(hdc, &ps.rcPaint, g_brBg);
        if (g_icon64) DrawIconEx(hdc, (CLIENT_W-64)/2, 10, g_icon64, 64, 64, 0, NULL, DI_NORMAL);
        SetBkMode(hdc, TRANSPARENT); SelectObject(hdc, g_fTitle); SetTextColor(hdc, COL_ACCENT);
        RECT tr = {0, 80, CLIENT_W, 104}; DrawTextW(hdc, L"QEMU Portable", -1, &tr, DT_CENTER|DT_SINGLELINE);
        HPEN pen = CreatePen(PS_SOLID, 1, COL_BORDER); SelectObject(hdc, pen);
        MoveToEx(hdc, MARGIN, 118, NULL); LineTo(hdc, CLIENT_W-MARGIN, 118); DeleteObject(pen);
        if (g_page == PG_MENU) {
            SelectObject(hdc, g_fSmall); SetTextColor(hdc, COL_FG_DIM);
            RECT sr = {MARGIN, 124, CLIENT_W-MARGIN, 142};
            DrawTextW(hdc, L"Portable x86_64 virtualization for Windows", -1, &sr, DT_CENTER|DT_SINGLELINE);
        }
        if (g_page == PG_RUNNING) {
            SelectObject(hdc, g_fMono); SetTextColor(hdc, COL_FG_DIM);
            RECT sr = {MARGIN, CLIENT_H-40, CLIENT_W-MARGIN, CLIENT_H-10};
            WCHAR si[512]; wsprintfW(si, L"Source: %s", g_selectedSource);
            DrawTextW(hdc, si, -1, &sr, DT_CENTER|DT_SINGLELINE|DT_END_ELLIPSIS);
        }
        if (g_page == PG_DISK || g_page == PG_COMBO_PHYSICAL) {
            SelectObject(hdc, g_fSmall); SetTextColor(hdc, COL_DANGER);
            RECT wr = {MARGIN, BTN_ROW_Y-20, CLIENT_W-MARGIN, BTN_ROW_Y};
            DrawTextW(hdc, L"Selecting the wrong disk can destroy data.", -1, &wr, DT_LEFT|DT_SINGLELINE);
        }
        EndPaint(hWnd, &ps); break;
    }

    case WM_CTLCOLORSTATIC: { HDC h=(HDC)wParam; SetBkMode(h,TRANSPARENT);
        HWND c=(HWND)lParam;
        if (c==g_sessionInfo) SetTextColor(h,COL_ACCENT);
        else if (c==g_lblConfirm[1]||c==g_lblConfirm[2]) SetTextColor(h,COL_FG);
        else SetTextColor(h,COL_FG_DIM);
        return (LRESULT)g_brBg; }
    case WM_CTLCOLOREDIT: { HDC h=(HDC)wParam; SetTextColor(h,COL_FG); SetBkColor(h,RGB(42,42,42)); return (LRESULT)g_brInput; }
    case WM_CTLCOLORLISTBOX: { HDC h=(HDC)wParam; SetTextColor(h,COL_FG); SetBkColor(h,RGB(30,30,30)); return (LRESULT)g_brLb; }
    case WM_CTLCOLORBTN: { HDC h=(HDC)wParam; SetBkMode(h,TRANSPARENT); SetTextColor(h,COL_FG); return (LRESULT)g_brBg; }

    case WM_MEASUREITEM: { LPMEASUREITEMSTRUCT m=(LPMEASUREITEMSTRUCT)lParam;
        if (m->CtlID==ID_DISK_LIST||m->CtlID==ID_COMBO_DISK_LIST) m->itemHeight=22; return TRUE; }

    case WM_DRAWITEM: {
        LPDRAWITEMSTRUCT di = (LPDRAWITEMSTRUCT)lParam;
        if (di->CtlID == ID_DISK_LIST || di->CtlID == ID_COMBO_DISK_LIST) {
            if ((int)di->itemID < 0) break;
            BOOL sel = (di->itemState & ODS_SELECTED);
            HBRUSH b = CreateSolidBrush(sel ? COL_HIGHLIGHT : COL_LB_BG);
            FillRect(di->hDC, &di->rcItem, b); DeleteObject(b);
            SetBkMode(di->hDC, TRANSPARENT); SetTextColor(di->hDC, sel ? COL_ACCENT : COL_FG);
            SelectObject(di->hDC, g_fMono);
            WCHAR t[256]={0}; SendMessageW(di->hwndItem, LB_GETTEXT, di->itemID, (LPARAM)t);
            RECT r=di->rcItem; r.left+=4; DrawTextW(di->hDC, t, -1, &r, DT_LEFT|DT_VCENTER|DT_SINGLELINE);
            if (di->itemState & ODS_FOCUS) {
                HPEN p=CreatePen(PS_DOT,1,COL_BORDER); SelectObject(di->hDC,p);
                SelectObject(di->hDC,GetStockObject(NULL_BRUSH));
                Rectangle(di->hDC,di->rcItem.left,di->rcItem.top,di->rcItem.right,di->rcItem.bottom);
                DeleteObject(p);
            }
            return TRUE;
        }
        switch (di->CtlID) {
        case ID_BTN_LAUNCH: DrawBtnFill(di, RGB(60,60,60), COL_ACCENT); break;
        case ID_BTN_KILL: DrawBtn(di, COL_DANGER, COL_DANGER); break;
        case ID_BTN_PHYSICAL: case ID_BTN_ISO: case ID_BTN_IPXE: case ID_BTN_COMBO:
        case ID_BTN_SELECT: case ID_BTN_MENU: case ID_BTN_COMBO_PHYS: case ID_BTN_COMBO_VIRT:
        case ID_BTN_COMBO_SELECT:
            DrawBtn(di, COL_ACCENT, COL_ACCENT); break;
        case ID_BTN_EXIT: DrawBtn(di, COL_BORDER, COL_FG_DIM); break;
        default: DrawBtn(di, COL_BORDER, COL_FG); break;
        }
        return TRUE;
    }

    case WM_COMMAND:
        switch (LOWORD(wParam)) {
        case ID_BTN_EXIT: PostQuitMessage(0); break;

        case ID_BTN_PHYSICAL:
            g_sourceIsPhysical=TRUE; g_sourceIsIpxe=FALSE; g_hasSecondary=FALSE;
            ShowPage(PG_DISK); break;

        case ID_BTN_ISO:
            g_sourceIsPhysical=FALSE; g_sourceIsIpxe=FALSE; g_hasSecondary=FALSE;
            g_selectedSource[0]=0; SetWindowTextW(g_editFile, L"");
            ShowPage(PG_FILE); break;

        case ID_BTN_COMBO:
            g_sourceIsPhysical=FALSE; g_sourceIsIpxe=FALSE; g_hasSecondary=TRUE;
            g_selectedSource[0]=0; g_secondaryDisk[0]=0;
            SetWindowTextW(g_editComboIso, L""); SetWindowTextW(g_editComboVdisk, L"");
            ShowPage(PG_COMBO_ISO); break;

        case ID_BTN_IPXE: {
            WCHAR ip[MAX_PATH]; wsprintfW(ip, L"%s\\gLiTcH-iPXE.iso", g_qemuRoot);
            if (GetFileAttributesW(ip)==INVALID_FILE_ATTRIBUTES)
                { MessageBoxW(hWnd, L"gLiTcH-iPXE.iso not found.", L"QEMU Portable", MB_OK|MB_ICONWARNING); break; }
            g_sourceIsPhysical=FALSE; g_sourceIsIpxe=TRUE; g_hasSecondary=FALSE;
            wcscpy(g_selectedSource, L"gLiTcH-iPXE.iso");
            ShowPage(PG_CONFIG); break;
        }

        case ID_BTN_BACK:
            if (g_page==PG_COMBO_DISKTYPE) ShowPage(PG_COMBO_ISO);
            else if (g_page==PG_COMBO_PHYSICAL || g_page==PG_COMBO_VIRTUAL) ShowPage(PG_COMBO_DISKTYPE);
            else if (g_page==PG_COMBO_CONFIRM) ShowPage(PG_COMBO_DISKTYPE);
            else ShowPage(PG_MENU);
            break;

        case ID_BTN_REFRESH: EnumerateDisks(); PopulateList(g_diskList); break;
        case ID_BTN_COMBO_REFRESH: EnumerateDisks(); PopulateList(g_comboDiskList); break;

        /* "Next" / "Select" - context dependent */
        case ID_BTN_SELECT:
            if (g_page == PG_DISK) {
                int s = (int)SendMessageW(g_diskList, LB_GETCURSEL, 0, 0);
                if (s>=0 && s<g_diskCount) { wcscpy(g_selectedSource, g_disks[s].path); ShowPage(PG_CONFIG); }
                else MessageBoxW(hWnd, L"No drive selected.", L"QEMU Portable", MB_OK);
            } else if (g_page == PG_COMBO_ISO) {
                GetWindowTextW(g_editComboIso, g_selectedSource, 2048);
                if (wcslen(g_selectedSource) < 3)
                    { MessageBoxW(hWnd, L"No boot image selected.", L"QEMU Portable", MB_OK); break; }
                ShowPage(PG_COMBO_DISKTYPE);
            } else if (g_page == PG_COMBO_VIRTUAL) {
                GetWindowTextW(g_editComboVdisk, g_secondaryDisk, 2048);
                if (wcslen(g_secondaryDisk) < 3)
                    { MessageBoxW(hWnd, L"No virtual disk selected.", L"QEMU Portable", MB_OK); break; }
                ShowPage(PG_COMBO_CONFIRM);
            }
            break;

        case ID_BTN_COMBO_PHYS: ShowPage(PG_COMBO_PHYSICAL); break;
        case ID_BTN_COMBO_VIRT: ShowPage(PG_COMBO_VIRTUAL); break;

        case ID_BTN_COMBO_SELECT: {
            int s = (int)SendMessageW(g_comboDiskList, LB_GETCURSEL, 0, 0);
            if (s>=0 && s<g_diskCount) { wcscpy(g_secondaryDisk, g_disks[s].path); ShowPage(PG_COMBO_CONFIRM); }
            else MessageBoxW(hWnd, L"No drive selected.", L"QEMU Portable", MB_OK);
            break;
        }

        case ID_BTN_BROWSE:
            if (BrowseImage(hWnd, g_selectedSource, 2048)) SetWindowTextW(g_editFile, g_selectedSource);
            break;
        case ID_BTN_BROWSE_COMBO_ISO:
            if (BrowseImage(hWnd, g_selectedSource, 2048)) SetWindowTextW(g_editComboIso, g_selectedSource);
            break;
        case ID_BTN_BROWSE_COMBO_VDISK:
            if (BrowseImage(hWnd, g_secondaryDisk, 2048)) SetWindowTextW(g_editComboVdisk, g_secondaryDisk);
            break;

        case ID_BTN_LAUNCH:
            if (g_page == PG_FILE) {
                GetWindowTextW(g_editFile, g_selectedSource, 2048);
                if (wcslen(g_selectedSource)<3) { MessageBoxW(hWnd,L"No file selected.",L"QEMU Portable",MB_OK); break; }
            }
            LaunchQemu(); ShowPage(PG_RUNNING); break;

        case ID_BTN_MENU: ShowPage(PG_MENU); break;
        case ID_BTN_KILL: KillQemu(); PostQuitMessage(0); break;

        case ID_DISK_LIST:
            if (HIWORD(wParam)==LBN_DBLCLK) {
                int s=(int)SendMessageW(g_diskList,LB_GETCURSEL,0,0);
                if (s>=0&&s<g_diskCount) { wcscpy(g_selectedSource,g_disks[s].path); ShowPage(PG_CONFIG); }
            } break;
        case ID_COMBO_DISK_LIST:
            if (HIWORD(wParam)==LBN_DBLCLK) {
                int s=(int)SendMessageW(g_comboDiskList,LB_GETCURSEL,0,0);
                if (s>=0&&s<g_diskCount) { wcscpy(g_secondaryDisk,g_disks[s].path); ShowPage(PG_COMBO_CONFIRM); }
            } break;
        }
        break;

    case WM_DESTROY:
        DeleteObject(g_brBg); DeleteObject(g_brBgAlt); DeleteObject(g_brInput); DeleteObject(g_brLb);
        DeleteObject(g_fNormal); DeleteObject(g_fTitle); DeleteObject(g_fMono); DeleteObject(g_fBtn); DeleteObject(g_fSmall);
        PostQuitMessage(0); break;
    default: return DefWindowProcW(hWnd, msg, wParam, lParam);
    }
    return 0;
}

/* ---- Entry ---- */
int WINAPI wWinMain(HINSTANCE hInst, HINSTANCE hPrev, LPWSTR cmdLine, int nShow) {
    (void)hPrev; (void)cmdLine; (void)nShow;
    g_hInst = hInst;
    SetProcessDPIAware();

    GetModuleFileNameW(NULL, g_launcherDir, MAX_PATH);
    WCHAR *sl = wcsrchr(g_launcherDir, L'\\'); if (sl) *sl = 0;
    wsprintfW(g_qemuRoot, L"%s\\qemu", g_launcherDir);
    wsprintfW(g_qemuBin, L"%s\\qemu-system-x86_64.exe", g_qemuRoot);
    wsprintfW(g_ovmfCode, L"%s\\bios\\OVMF_CODE.fd", g_qemuRoot);
    wsprintfW(g_ovmfVarsTemplate, L"%s\\bios\\OVMF_VARS.fd", g_qemuRoot);

    if (GetFileAttributesW(g_qemuBin) == INVALID_FILE_ATTRIBUTES) {
        WCHAR e[1024]; wsprintfW(e, L"qemu-system-x86_64.exe not found.\n\nExpected:\n%s", g_qemuBin);
        MessageBoxW(NULL, e, L"QEMU Portable", MB_OK|MB_ICONERROR); return 1;
    }
    { WCHAR nd[MAX_PATH]; wsprintfW(nd, L"%s\\nvram", g_launcherDir); CreateDirectoryW(nd, NULL); }

    g_icon64 = (HICON)LoadImageW(hInst, MAKEINTRESOURCEW(IDI_APP), IMAGE_ICON, 64, 64, LR_DEFAULTCOLOR);
    g_icon32 = (HICON)LoadImageW(hInst, MAKEINTRESOURCEW(IDI_APP), IMAGE_ICON, 32, 32, LR_DEFAULTCOLOR);
    if (!g_icon64) g_icon64 = LoadIconW(NULL, IDI_APPLICATION);
    if (!g_icon32) g_icon32 = LoadIconW(NULL, IDI_APPLICATION);

    INITCOMMONCONTROLSEX icc = { sizeof(icc), ICC_PROGRESS_CLASS }; InitCommonControlsEx(&icc);
    WNDCLASSEXW wc = {0}; wc.cbSize = sizeof(wc); wc.lpfnWndProc = WndProc; wc.hInstance = hInst;
    wc.hCursor = LoadCursor(NULL, IDC_ARROW); wc.lpszClassName = L"QemuPortable";
    wc.hIcon = g_icon64; wc.hIconSm = g_icon32;
    RegisterClassExW(&wc);

    DWORD style = WS_OVERLAPPED|WS_CAPTION|WS_SYSMENU|WS_MINIMIZEBOX;
    RECT rc = {0, 0, CLIENT_W, CLIENT_H}; AdjustWindowRect(&rc, style, FALSE);
    int ww = rc.right-rc.left, wh = rc.bottom-rc.top;
    int sx = GetSystemMetrics(SM_CXSCREEN), sy = GetSystemMetrics(SM_CYSCREEN);
    g_hWnd = CreateWindowExW(0, L"QemuPortable", L"QEMU Portable", style,
        (sx-ww)/2, (sy-wh)/2, ww, wh, NULL, NULL, hInst, NULL);
    ShowWindow(g_hWnd, SW_SHOW); UpdateWindow(g_hWnd);

    MSG msg; while (GetMessageW(&msg, NULL, 0, 0)) { TranslateMessage(&msg); DispatchMessageW(&msg); }
    return (int)msg.wParam;
}
