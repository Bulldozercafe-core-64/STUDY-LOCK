#include <stdio.h>
#include <windows.h>
#include <string.h>
#include <stdlib.h>

#define PASSWORD "12673849"
#define ID_TIMER 1

HWND hBlackWnd, hInputWnd, hEdit, hTimeEdit;
HHOOK hKeyHook;
HINSTANCE hInst;
int remainingSeconds = 0;
int totalSeconds     = 0;
ULONGLONG startTick  = 0;
BOOL isTimeSet       = FALSE;
BOOL isInputVisible  = FALSE;


SYSTEMTIME startWallTime;
HWND  hClockDlg     = NULL;
BOOL  isClockInfoOpen = FALSE;

static WNDPROC origEditProc = NULL;

LRESULT CALLBACK EditSubProc(HWND hWnd, UINT msg, WPARAM wp, LPARAM lp) {
    if (msg == WM_KEYDOWN && wp == VK_RETURN) {
        SendMessage(GetParent(hWnd), WM_KEYDOWN, VK_RETURN, 0);
        return 0;
    }
    return CallWindowProc(origEditProc, hWnd, msg, wp, lp);
}

void SetSecurityOptions(int state) {
    HKEY hKey;
    DWORD value = state;
    if (RegCreateKeyExA(HKEY_CURRENT_USER,
        "Software\\Microsoft\\Windows\\CurrentVersion\\Policies\\Explorer",
        0, NULL, REG_OPTION_NON_VOLATILE, KEY_WRITE, NULL, &hKey, NULL) == ERROR_SUCCESS) {
        RegSetValueExA(hKey, "NoLogoff", 0, REG_DWORD, (const BYTE*)&value, sizeof(DWORD));
        RegCloseKey(hKey);
    }
    if (RegCreateKeyExA(HKEY_LOCAL_MACHINE,
        "SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Policies\\System",
        0, NULL, REG_OPTION_NON_VOLATILE, KEY_WRITE, NULL, &hKey, NULL) == ERROR_SUCCESS) {
        RegSetValueExA(hKey, "HideFastUserSwitching", 0, REG_DWORD, (const BYTE*)&value, sizeof(DWORD));
        RegCloseKey(hKey);
    }
}

// -------------------------------------------------------
// 입력 파싱
// -------------------------------------------------------
int ParseTimeInput(const char *buff) {
    int a = -1, b = -1, c = -1;
    int nodes = sscanf(buff, "%d:%d:%d", &a, &b, &c);
    if (nodes == 3) {
        if (a < 0 || a > 99 || b < 0 || b > 59 || c < 0 || c > 59) return -1;
        return a * 3600 + b * 60 + c;
    } else if (nodes == 2) {
        if (a < 0 || a > 99 || b < 0 || b > 59) return -1;
        return a * 60 + b;
    } else if (nodes == 1) {
        if (a < 0 || a > 59) return -1;
        return a;
    }
    return -1;
}

// -------------------------------------------------------
// 시간 포맷
// -------------------------------------------------------
void FormatTime(int secs, char *out) {
    if (secs < 0) secs = 0;
    if (secs >= 3600) {
        sprintf(out, "%02d:%02d:%02d", secs / 3600, (secs % 3600) / 60, secs % 60);
    } else {
        sprintf(out, "%02d:%02d", secs / 60, secs % 60);
    }
}

void FormatWallTime(SYSTEMTIME *st, char *out) {
    int h = st->wHour;
    int m = st->wMinute;
    const char *ampm = (h < 12) ? "AM" : "PM";
    if (h == 0) h = 12;
    else if (h > 12) h -= 12;
    sprintf(out, "%s %d:%02d", ampm, h, m);
}

void FormatDuration(int secs, char *out) {
    if (secs < 0) secs = 0;
    if (secs >= 3600) {
        int h = secs / 3600;
        int m = (secs % 3600) / 60;
        int s = secs % 60;
        if (m == 0 && s == 0)    sprintf(out, "%d h", h);
        else if (s == 0)         sprintf(out, "%d h %d m", h, m);
        else                     sprintf(out, "%d h %d m %d s", h, m, s);
    } else if (secs >= 60) {
        int m = secs / 60;
        int s = secs % 60;
        if (s == 0) sprintf(out, "%d m", m);
        else        sprintf(out, "%d m %d s", m, s);
    } else {
        sprintf(out, "%d s", secs);
    }
}

void AddSecondsToSysTime(SYSTEMTIME *st, int secs, SYSTEMTIME *out) {
    FILETIME ft;
    SystemTimeToFileTime(st, &ft);
    ULONGLONG ull = ((ULONGLONG)ft.dwHighDateTime << 32) | ft.dwLowDateTime;
    ull += (ULONGLONG)secs * 10000000ULL;
    ft.dwLowDateTime  = (DWORD)(ull & 0xFFFFFFFF);
    ft.dwHighDateTime = (DWORD)(ull >> 32);
    FileTimeToSystemTime(&ft, out);
}

// -------------------------------------------------------
// 키보드 후킹
// -------------------------------------------------------
LRESULT CALLBACK KeyboardProc(int nCode, WPARAM wp, LPARAM lp) {
    if (nCode == HC_ACTION) {
        KBDLLHOOKSTRUCT *p = (KBDLLHOOKSTRUCT *)lp;
        BOOL isNumber   = (p->vkCode >= 0x30 && p->vkCode <= 0x39);
        BOOL isNumpad   = (p->vkCode >= VK_NUMPAD0 && p->vkCode <= VK_NUMPAD9);
        BOOL isControl  = (p->vkCode == VK_BACK  || p->vkCode == VK_RETURN ||
                           p->vkCode == VK_TAB   || p->vkCode == VK_LEFT   ||
                           p->vkCode == VK_RIGHT || p->vkCode == VK_HOME   ||
                           p->vkCode == VK_END   || p->vkCode == VK_ESCAPE);
        BOOL isShift    = (p->vkCode == VK_SHIFT || p->vkCode == VK_LSHIFT || p->vkCode == VK_RSHIFT);
        BOOL isShiftNow = (GetKeyState(VK_SHIFT) & 0x8000);
        BOOL isColon    = (p->vkCode == VK_OEM_1 && isShiftNow);
        if (isNumber || isNumpad || isControl || isShift || isColon)
            return CallNextHookEx(hKeyHook, nCode, wp, lp);
        return 1;
    }
    return CallNextHookEx(hKeyHook, nCode, wp, lp);
}

// -------------------------------------------------------
// 둥근 모서리
// -------------------------------------------------------
void ApplyRoundRgn(HWND hWnd, int w, int h, int r) {
    HRGN hRgn = CreateRoundRectRgn(0, 0, w, h, r, r);
    SetWindowRgn(hWnd, hRgn, TRUE);
}

// -------------------------------------------------------
// Clock Info 팝업
// -------------------------------------------------------
LRESULT CALLBACK ClockDlgProc(HWND hWnd, UINT msg, WPARAM wp, LPARAM lp) {
    static HFONT hFontTitle, hFontInfo, hFontBtn;
    switch (msg) {

    case WM_CREATE: {
        ApplyRoundRgn(hWnd, 360, 230, 22);
        hFontTitle = CreateFont(18, 0, 0, 0, FW_BOLD,   0, 0, 0,
                                DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, "Malgun Gothic");
        hFontInfo  = CreateFont(15, 0, 0, 0, FW_NORMAL, 0, 0, 0,
                                DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, "Malgun Gothic");
        hFontBtn   = CreateFont(15, 0, 0, 0, FW_BOLD,   0, 0, 0,
                                DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, "Malgun Gothic");
        HWND hClose = CreateWindow("BUTTON", "Close",
            WS_CHILD | WS_VISIBLE | BS_FLAT,
            240, 150, 100, 34, hWnd, (HMENU)1, hInst, NULL);
        SendMessage(hClose, WM_SETFONT, (WPARAM)hFontBtn, TRUE);
        break;
    }

    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hWnd, &ps);
        RECT rc; GetClientRect(hWnd, &rc);

        HDC     memDC  = CreateCompatibleDC(hdc);
        HBITMAP memBmp = CreateCompatibleBitmap(hdc, rc.right, rc.bottom);
        HBITMAP oldBmp = (HBITMAP)SelectObject(memDC, memBmp);

        TRIVERTEX vtx[2];
        vtx[0].x = 0;        vtx[0].y = 0;
        vtx[0].Red = 0x0800; vtx[0].Green = 0x1200; vtx[0].Blue = 0x3A00;
        vtx[1].x = rc.right; vtx[1].y = rc.bottom;
        vtx[1].Red = 0x0200; vtx[1].Green = 0x0800; vtx[1].Blue = 0x2200;
        GRADIENT_RECT gr = {0, 1};
        GradientFill(memDC, vtx, 2, &gr, 1, GRADIENT_FILL_RECT_V);

        HPEN hLine   = CreatePen(PS_SOLID, 2, RGB(60, 130, 255));
        HPEN hOldPen = (HPEN)SelectObject(memDC, hLine);
        MoveToEx(memDC, 20, 46, NULL);
        LineTo(memDC, rc.right - 20, 46);
        SelectObject(memDC, hOldPen);
        DeleteObject(hLine);

        SetBkMode(memDC, TRANSPARENT);

        HFONT hOldFont = (HFONT)SelectObject(memDC, hFontTitle);
        SetTextColor(memDC, RGB(160, 200, 255));
        RECT rcTitle = {0, 14, rc.right, 44};
        DrawText(memDC, "Clock Info", -1, &rcTitle, DT_CENTER | DT_VCENTER | DT_SINGLELINE);

        SelectObject(memDC, hFontInfo);
        SetTextColor(memDC, RGB(180, 210, 255));

        char line[128];
        int y = 58, lineH = 20;

        char startStr[32];
        FormatWallTime(&startWallTime, startStr);
        sprintf(line, "Start Time :  %s", startStr);
        RECT rcL = {24, y, rc.right - 20, y + lineH};
        DrawText(memDC, line, -1, &rcL, DT_LEFT | DT_SINGLELINE);
        y += lineH;

        SYSTEMTIME endTime;
        AddSecondsToSysTime(&startWallTime, totalSeconds, &endTime);
        char endStr[32];
        FormatWallTime(&endTime, endStr);
        sprintf(line, "End Time :   %s", endStr);
        rcL.top = y; rcL.bottom = y + lineH;
        DrawText(memDC, line, -1, &rcL, DT_LEFT | DT_SINGLELINE);
        y += lineH;

        char setStr[64];
        FormatDuration(totalSeconds, setStr);
        sprintf(line, "Set Time :   %s", setStr);
        rcL.top = y; rcL.bottom = y + lineH;
        DrawText(memDC, line, -1, &rcL, DT_LEFT | DT_SINGLELINE);
        y += lineH;

        int pastSecs = totalSeconds - remainingSeconds;
        if (pastSecs < 0) pastSecs = 0;
        char pastStr[64];
        FormatDuration(pastSecs, pastStr);
        sprintf(line, "Elapsed Time :  %s", pastStr);
        rcL.top = y; rcL.bottom = y + lineH;
        DrawText(memDC, line, -1, &rcL, DT_LEFT | DT_SINGLELINE);
        y += lineH;

        int leftSecs = remainingSeconds;
        if (leftSecs < 0) leftSecs = 0;
        char leftStr[64];
        FormatDuration(leftSecs, leftStr);
        sprintf(line, "Left Time :  %s", leftStr);
        rcL.top = y; rcL.bottom = y + lineH;
        DrawText(memDC, line, -1, &rcL, DT_LEFT | DT_SINGLELINE);

        SelectObject(memDC, hOldFont);

        BitBlt(hdc, 0, 0, rc.right, rc.bottom, memDC, 0, 0, SRCCOPY);
        SelectObject(memDC, oldBmp);
        DeleteObject(memBmp);
        DeleteDC(memDC);
        EndPaint(hWnd, &ps);
        break;
    }

    case WM_ERASEBKGND: return 1;

    case WM_CTLCOLORBTN: {
        HDC hdcBtn = (HDC)wp;
        SetBkColor(hdcBtn, RGB(20, 35, 80));
        SetTextColor(hdcBtn, RGB(160, 190, 255));
        static HBRUSH hCloseBrush = NULL;
        if (!hCloseBrush) hCloseBrush = CreateSolidBrush(RGB(20, 35, 80));
        return (LRESULT)hCloseBrush;
    }

    case WM_COMMAND:
        if (LOWORD(wp) == 1) {
            isClockInfoOpen = FALSE;
            DestroyWindow(hWnd);
            hClockDlg = NULL;
        }
        break;

    case WM_CLOSE:
        isClockInfoOpen = FALSE;
        DestroyWindow(hWnd);
        hClockDlg = NULL;
        return 0;

    default: return DefWindowProc(hWnd, msg, wp, lp);
    }
    return 0;
}

void ShowClockInfo(void) {
    if (isClockInfoOpen) {
        if (hClockDlg) {
            RECT rcText = {0, 46, 360, 150};
            InvalidateRect(hClockDlg, &rcText, FALSE);
        }
        return;
    }
    isClockInfoOpen = TRUE;
    int sw = GetSystemMetrics(SM_CXSCREEN), sh = GetSystemMetrics(SM_CYSCREEN);
    static BOOL clockClassRegistered = FALSE;
    if (!clockClassRegistered) {
        WNDCLASS cc = {0};
        cc.lpfnWndProc   = ClockDlgProc;
        cc.hInstance     = hInst;
        cc.lpszClassName = "CLOCK_INFO";
        cc.hCursor       = LoadCursor(NULL, IDC_ARROW);
        RegisterClass(&cc);
        clockClassRegistered = TRUE;
    }
    hClockDlg = CreateWindowEx(
        WS_EX_TOPMOST | WS_EX_TOOLWINDOW,
        "CLOCK_INFO", "Clock Info",
        WS_POPUP | WS_CAPTION | WS_VISIBLE,
        (sw - 360) / 2, (sh - 230) / 2, 360, 230,
        hBlackWnd, NULL, hInst, NULL);
    SetForegroundWindow(hClockDlg);
}



// -------------------------------------------------------
// 입력창 (시간 설정 / 비밀번호)
// -------------------------------------------------------
LRESULT CALLBACK InputWndProc(HWND hWnd, UINT msg, WPARAM wp, LPARAM lp) {
    static HFONT hFontLabel, hFontEdit, hFontBtn;

    switch (msg) {

    case WM_CREATE: {
        ApplyRoundRgn(hWnd, 340, 220, 22);
        hFontLabel = CreateFont(17, 0, 0, 0, FW_SEMIBOLD, 0, 0, 0,
                                DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, "Malgun Gothic");
        hFontEdit  = CreateFont(20, 0, 0, 0, FW_NORMAL,   0, 0, 0,
                                DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, "Malgun Gothic");
        hFontBtn   = CreateFont(15, 0, 0, 0, FW_BOLD,     0, 0, 0,
                                DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, "Malgun Gothic");
        if (!isTimeSet) {
            hTimeEdit = CreateWindowEx(WS_EX_CLIENTEDGE, "EDIT", "",
                WS_CHILD | WS_VISIBLE | ES_CENTER,
                85, 70, 170, 36, hWnd, NULL, hInst, NULL);
            SendMessage(hTimeEdit, WM_SETFONT, (WPARAM)hFontEdit, TRUE);
            origEditProc = (WNDPROC)SetWindowLongPtr(hTimeEdit, GWLP_WNDPROC, (LONG_PTR)EditSubProc);
            HWND hBtn = CreateWindow("BUTTON", "START",
                WS_CHILD | WS_VISIBLE | BS_FLAT,
                110, 125, 120, 38, hWnd, (HMENU)10, hInst, NULL);
            SendMessage(hBtn, WM_SETFONT, (WPARAM)hFontBtn, TRUE);
        } else {
            hEdit = CreateWindowEx(WS_EX_CLIENTEDGE, "EDIT", "",
                WS_CHILD | WS_VISIBLE | ES_PASSWORD | ES_CENTER,
                70, 70, 200, 36, hWnd, (HMENU)1, hInst, NULL);
            SendMessage(hEdit, WM_SETFONT, (WPARAM)hFontEdit, TRUE);
            origEditProc = (WNDPROC)SetWindowLongPtr(hEdit, GWLP_WNDPROC, (LONG_PTR)EditSubProc);
            HWND hBtn = CreateWindow("BUTTON", "UNLOCK",
                WS_CHILD | WS_VISIBLE | BS_FLAT,
                110, 125, 120, 38, hWnd, (HMENU)20, hInst, NULL);
            SendMessage(hBtn, WM_SETFONT, (WPARAM)hFontBtn, TRUE);
        }
        break;
    }

    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hWnd, &ps);
        RECT rc; GetClientRect(hWnd, &rc);
        TRIVERTEX vtx[2];
        vtx[0].x = 0;        vtx[0].y = 0;
        vtx[0].Red = 0x0800; vtx[0].Green = 0x1200; vtx[0].Blue = 0x3A00;
        vtx[1].x = rc.right; vtx[1].y = rc.bottom;
        vtx[1].Red = 0x0200; vtx[1].Green = 0x0800; vtx[1].Blue = 0x2200;
        GRADIENT_RECT gr = {0, 1};
        GradientFill(hdc, vtx, 2, &gr, 1, GRADIENT_FILL_RECT_V);
        HPEN hLine   = CreatePen(PS_SOLID, 2, RGB(60, 130, 255));
        HPEN hOldPen = (HPEN)SelectObject(hdc, hLine);
        MoveToEx(hdc, 20, 46, NULL);
        LineTo(hdc, rc.right - 20, 46);
        SelectObject(hdc, hOldPen);
        DeleteObject(hLine);
        SetBkMode(hdc, TRANSPARENT);
        SetTextColor(hdc, RGB(160, 200, 255));
        HFONT hOldFont = (HFONT)SelectObject(hdc, hFontLabel);
        RECT rcTitle = {0, 14, rc.right, 44};
        const char *title = isTimeSet
            ? "ENTER PIN"
            : "SET TIME  ( SS / MM:SS / HH:MM:SS )";
        DrawText(hdc, title, -1, &rcTitle, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        SelectObject(hdc, hOldFont);
        EndPaint(hWnd, &ps);
        break;
    }

    case WM_CTLCOLOREDIT: {
        HDC hdcEdit = (HDC)wp;
        SetBkColor(hdcEdit, RGB(12, 22, 58));
        SetTextColor(hdcEdit, RGB(200, 220, 255));
        static HBRUSH hEditBrush = NULL;
        if (!hEditBrush) hEditBrush = CreateSolidBrush(RGB(12, 22, 58));
        return (LRESULT)hEditBrush;
    }

    case WM_CTLCOLORBTN: {
        HDC hdcBtn = (HDC)wp;
        SetBkColor(hdcBtn, RGB(30, 80, 200));
        SetTextColor(hdcBtn, RGB(255, 255, 255));
        static HBRUSH hBtnBrush = NULL;
        if (!hBtnBrush) hBtnBrush = CreateSolidBrush(RGB(30, 80, 200));
        return (LRESULT)hBtnBrush;
    }

    case WM_KEYDOWN:
        if (wp == VK_RETURN) {
            if (!isTimeSet)
                SendMessage(hWnd, WM_COMMAND, MAKEWPARAM(10, BN_CLICKED), 0);
            else
                SendMessage(hWnd, WM_COMMAND, MAKEWPARAM(20, BN_CLICKED), 0);
        }
        break;

    case WM_COMMAND:
        if (LOWORD(wp) == 10) {
            char buff[32];
            GetWindowText(hTimeEdit, buff, 32);
            int total = ParseTimeInput(buff);

            if (strlen(buff) == 0 ||
                strspn(buff, "0123456789:") != strlen(buff) ||
                buff[0] == ':' ||
                buff[strlen(buff)-1] == ':' ||
                strstr(buff, "::") ||
                total < 0) {
                MessageBox(hWnd,
                    "Invalid format!\n\n"
                    "  S:         30\n"
                    "  M:S:       25:00\n"
                    "  H:M:S:     2:30:00\n\n"
                    "Limit: 99:59:59",
                    "Input Error", MB_ICONWARNING | MB_TOPMOST);
                SetWindowText(hTimeEdit, "");
                SetFocus(hTimeEdit);
                return 0;
            }
            totalSeconds     = total;
            remainingSeconds = total;
            startTick        = GetTickCount64();
            GetLocalTime(&startWallTime);
            isTimeSet        = TRUE;
            isInputVisible   = FALSE;
            ShowWindow(hWnd, SW_HIDE);
            DestroyWindow(hTimeEdit);
        } else if (LOWORD(wp) == 20) {
            char buff[128];
            GetWindowText(hEdit, buff, 128);
            if (strcmp(buff, PASSWORD) == 0) {
                SetSecurityOptions(0);
                SetThreadExecutionState(ES_CONTINUOUS);
                UnhookWindowsHookEx(hKeyHook);
                hKeyHook = NULL;
                PostQuitMessage(0);
            } else {
                MessageBox(hWnd, "Incorrect password!", "Error", MB_ICONERROR | MB_TOPMOST);
                SetWindowText(hEdit, "");
                SetFocus(hEdit);
            }
        }
        break;

    case WM_CLOSE:
        if (isTimeSet) { isInputVisible = FALSE; ShowWindow(hWnd, SW_HIDE); }
        return 0;

    default: return DefWindowProc(hWnd, msg, wp, lp);
    }
    return 0;
}

// -------------------------------------------------------
// 메인 다크 화면
// -------------------------------------------------------
LRESULT CALLBACK BlackWndProc(HWND hWnd, UINT msg, WPARAM wp, LPARAM lp) {
    static HFONT hFontTimer, hFontTimerLg, hFontSub, hFontBtn;
    switch (msg) {

    case WM_CREATE: {
        hFontTimer   = CreateFont(80,  0, 0, 0, FW_BOLD,   0, 0, 0,
                                  DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, "Segoe UI");
        hFontTimerLg = CreateFont(110, 0, 0, 0, FW_BOLD,   0, 0, 0,
                                  DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, "Segoe UI");
        hFontSub     = CreateFont(18,  0, 0, 0, FW_NORMAL, 0, 0, 0,
                                  DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, "Malgun Gothic");
        hFontBtn     = CreateFont(14,  0, 0, 0, FW_BOLD,   0, 0, 0,
                                  DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, "Malgun Gothic");
        int sw = GetSystemMetrics(SM_CXSCREEN), sh = GetSystemMetrics(SM_CYSCREEN);
        HWND hBtn = CreateWindow("BUTTON", "UNLOCK",
            WS_CHILD | WS_VISIBLE | BS_FLAT,
            sw - 130, sh - 65, 100, 38, hWnd, (HMENU)100, hInst, NULL);
        SendMessage(hBtn, WM_SETFONT, (WPARAM)hFontBtn, TRUE);
        SetTimer(hWnd, ID_TIMER, 1, NULL);
        break;
    }


    case WM_TIMER: {
        if (!isTimeSet) {
            InvalidateRect(hWnd, NULL, FALSE);
            break;
        }
        ULONGLONG elapsed = GetTickCount64() - startTick;
        remainingSeconds = totalSeconds - (int)(elapsed / 1000);

        // 시간 종료 시 단 1회만 해제
        if (remainingSeconds <= 0 && hKeyHook != NULL) {
            SetSecurityOptions(0);
            SetThreadExecutionState(ES_CONTINUOUS);
            UnhookWindowsHookEx(hKeyHook);
            hKeyHook = NULL;
            // ★ 비번창이 열려있으면 즉시 파괴
            if (hInputWnd && IsWindow(hInputWnd)) {
                DestroyWindow(hInputWnd);
                hInputWnd = NULL;
                isInputVisible = FALSE;
            }
        }

        InvalidateRect(hWnd, NULL, FALSE);

        // Clock Info 텍스트 영역만 갱신 (버튼 깜빡임 방지)
        if (hClockDlg) {
            RECT rcText = {0, 46, 360, 150};
            InvalidateRect(hClockDlg, &rcText, FALSE);
        }
        break;
    }

    case WM_LBUTTONDOWN: {
        if (!isTimeSet) break;
        int sw = GetSystemMetrics(SM_CXSCREEN), sh = GetSystemMetrics(SM_CYSCREEN);
        int x = (int)(short)LOWORD(lp);
        int y = (int)(short)HIWORD(lp);
        RECT btnRect = {sw - 130, sh - 65, sw - 30, sh - 27};
        POINT pt = {x, y};
        if (!PtInRect(&btnRect, pt))
            ShowClockInfo();
        break;
    }

    case WM_KEYDOWN: {
        if (wp == VK_ESCAPE && remainingSeconds <= 0) {
            SetSecurityOptions(0);
            SetThreadExecutionState(ES_CONTINUOUS);
            if (hKeyHook != NULL) { UnhookWindowsHookEx(hKeyHook); hKeyHook = NULL; }
            KillTimer(hWnd, ID_TIMER);
            PostQuitMessage(0);
        }
        break;
    }

    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hWnd, &ps);
        RECT rcClient; GetClientRect(hWnd, &rcClient);
        int sw = rcClient.right, sh = rcClient.bottom;

        HDC     memDC  = CreateCompatibleDC(hdc);
        HBITMAP memBmp = CreateCompatibleBitmap(hdc, sw, sh);
        HBITMAP oldBmp = (HBITMAP)SelectObject(memDC, memBmp);

        TRIVERTEX vtx[2];
        vtx[0].x = 0;  vtx[0].y = 0;
        vtx[0].Red = 0x0500; vtx[0].Green = 0x0D00; vtx[0].Blue = 0x2800;
        vtx[1].x = sw; vtx[1].y = sh;
        vtx[1].Red = 0x0200; vtx[1].Green = 0x0400; vtx[1].Blue = 0x0E00;
        GRADIENT_RECT gr = {0, 1};
        GradientFill(memDC, vtx, 2, &gr, 1, GRADIENT_FILL_RECT_V);

        int barH = 12;
        HBRUSH hTrack = CreateSolidBrush(RGB(15, 30, 70));
        RECT rcTrack = {0, 0, sw, barH};
        FillRect(memDC, &rcTrack, hTrack);
        DeleteObject(hTrack);

        if (isTimeSet) {
            if (remainingSeconds > 0) {
                ULONGLONG elapsedMs = GetTickCount64() - startTick;
                ULONGLONG totalMs   = (ULONGLONG)totalSeconds * 1000;
                if (elapsedMs > totalMs) elapsedMs = totalMs;
                int fillW = (int)((double)elapsedMs / totalMs * sw);
                if (fillW > 0) {
                    TRIVERTEX bvtx[2];
                    bvtx[0].x = 0;     bvtx[0].y = 0;
                    bvtx[0].Red = 0x1E00; bvtx[0].Green = 0x8000; bvtx[0].Blue = 0xFF00;
                    bvtx[1].x = fillW; bvtx[1].y = barH;
                    bvtx[1].Red = 0x6600; bvtx[1].Green = 0xBB00; bvtx[1].Blue = 0xFF00;
                    GRADIENT_RECT bgr = {0, 1};
                    GradientFill(memDC, bvtx, 2, &bgr, 1, GRADIENT_FILL_RECT_H);
                    HPEN hGlow   = CreatePen(PS_SOLID, 2, RGB(180, 230, 255));
                    HPEN hOldPen = (HPEN)SelectObject(memDC, hGlow);
                    MoveToEx(memDC, fillW, 0, NULL);
                    LineTo(memDC, fillW, barH);
                    SelectObject(memDC, hOldPen);
                    DeleteObject(hGlow);
                }
            } else {
                // 시간 초과: 바 꽉 채움
                TRIVERTEX bvtx[2];
                bvtx[0].x = 0;  bvtx[0].y = 0;
                bvtx[0].Red = 0x1E00; bvtx[0].Green = 0x8000; bvtx[0].Blue = 0xFF00;
                bvtx[1].x = sw; bvtx[1].y = barH;
                bvtx[1].Red = 0x6600; bvtx[1].Green = 0xBB00; bvtx[1].Blue = 0xFF00;
                GRADIENT_RECT bgr = {0, 1};
                GradientFill(memDC, bvtx, 2, &bgr, 1, GRADIENT_FILL_RECT_H);
            }

            // 타이머 텍스트
            char timeStr[20];
            if (remainingSeconds >= 0) {
                FormatTime(remainingSeconds, timeStr);
            } else {
                char temp[16];
                int disp = abs(remainingSeconds);  // ★ disp 선언
                if (disp > 359999) disp = 359999;  // ★ 제한
                FormatTime(disp, temp);
                sprintf(timeStr, "-%s", temp);
            }

            HFONT hUseFont = (abs(remainingSeconds) >= 3600) ? hFontTimer : hFontTimerLg;
            SetBkMode(memDC, TRANSPARENT);
            HFONT hOldFont = (HFONT)SelectObject(memDC, hUseFont);

            SetTextColor(memDC, RGB(0, 40, 100));
            RECT rcShadow = {4, barH + 4, sw + 4, sh + 4};
            DrawText(memDC, timeStr, -1, &rcShadow, DT_CENTER | DT_VCENTER | DT_SINGLELINE);

            SetTextColor(memDC, RGB(220, 235, 255));
            RECT rcTime = {0, barH, sw, sh};
            DrawText(memDC, timeStr, -1, &rcTime, DT_CENTER | DT_VCENTER | DT_SINGLELINE);

            SelectObject(memDC, hFontSub);
            SetTextColor(memDC, RGB(80, 120, 200));
            RECT rcSub = {0, sh / 2 + 80, sw, sh / 2 + 110};
            if (remainingSeconds > 0)
                DrawText(memDC, "FOCUS MODE", -1, &rcSub, DT_CENTER | DT_SINGLELINE);
            else
                DrawText(memDC, "TIME OVER  ( Press ESC to Exit )", -1, &rcSub, DT_CENTER | DT_SINGLELINE);

            SelectObject(memDC, hOldFont);
        }

        // 좌측 하단 현재 시각
        {
            SYSTEMTIME now;
            GetLocalTime(&now);
            char nowStr[32];
            FormatWallTime(&now, nowStr);
            HFONT hTempFont = CreateFont(18, 0, 0, 0, FW_BOLD, 0, 0, 0,
                                         DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, "Malgun Gothic");
            HFONT hOldFont2 = (HFONT)SelectObject(memDC, hTempFont);
            SetBkMode(memDC, TRANSPARENT);
            SetTextColor(memDC, RGB(180, 210, 255));
            RECT rcNow = {30, sh - 65, 200, sh - 27};
            DrawText(memDC, nowStr, -1, &rcNow, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
            SelectObject(memDC, hOldFont2);
            DeleteObject(hTempFont);
        }

        BitBlt(hdc, 0, 0, sw, sh, memDC, 0, 0, SRCCOPY);
        SelectObject(memDC, oldBmp);
        DeleteObject(memBmp);
        DeleteDC(memDC);
        EndPaint(hWnd, &ps);
        break;
    }

    case WM_ERASEBKGND: return 1;

    case WM_CTLCOLORBTN: {
        HDC hdcBtn = (HDC)wp;
        SetBkColor(hdcBtn, RGB(20, 50, 140));
        SetTextColor(hdcBtn, RGB(180, 210, 255));
        static HBRUSH hBtnBrush = NULL;
        if (!hBtnBrush) hBtnBrush = CreateSolidBrush(RGB(20, 50, 140));
        return (LRESULT)hBtnBrush;
    }

    case WM_COMMAND:
        if (LOWORD(wp) == 100) {
            if (!isTimeSet) {
                // 아무것도 안 함
            } else if (remainingSeconds <= 0) {
                // 시간 종료 후 → 즉시 바로 해제
                SetSecurityOptions(0);
                SetThreadExecutionState(ES_CONTINUOUS);
                if (hKeyHook != NULL) { UnhookWindowsHookEx(hKeyHook); hKeyHook = NULL; }
                KillTimer(hWnd, ID_TIMER);
                PostQuitMessage(0);
            } else {
                // 타이머 진행 중 → 비밀번호 창
                isInputVisible = !isInputVisible;
                if (isInputVisible) {
                    DestroyWindow(hInputWnd);
                    int sw2 = GetSystemMetrics(SM_CXSCREEN), sh2 = GetSystemMetrics(SM_CYSCREEN);
                    hInputWnd = CreateWindowEx(
                        WS_EX_TOPMOST | WS_EX_TOOLWINDOW,
                        "IN_GRADIENT", "SYSTEM LOCK",
                        WS_POPUP | WS_CAPTION | WS_VISIBLE,
                        (sw2 - 340) / 2, (sh2 - 220) / 2, 340, 220,
                        hBlackWnd, NULL, hInst, NULL);
                    SetForegroundWindow(hInputWnd);
                } else {
                    ShowWindow(hInputWnd, SW_HIDE);
                }
            }
        }
        break;

    default: return DefWindowProc(hWnd, msg, wp, lp);
    }
    return 0;
}

// -------------------------------------------------------
// WinMain
// -------------------------------------------------------
int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrev, LPSTR lpCmd, int nShow) {
    hInst    = hInstance;
    SetSecurityOptions(1);
    SetThreadExecutionState(ES_CONTINUOUS | ES_DISPLAY_REQUIRED | ES_SYSTEM_REQUIRED);
    hKeyHook = SetWindowsHookEx(WH_KEYBOARD_LL, KeyboardProc, GetModuleHandle(NULL), 0);

    WNDCLASS wc = {0}, ic = {0};
    wc.lpfnWndProc   = BlackWndProc;
    wc.hInstance     = hInstance;
    wc.lpszClassName = "BG_STABLE";
    wc.hCursor       = LoadCursor(NULL, IDC_ARROW);
    RegisterClass(&wc);

    ic.lpfnWndProc   = InputWndProc;
    ic.hInstance     = hInstance;
    ic.lpszClassName = "IN_GRADIENT";
    ic.hCursor       = LoadCursor(NULL, IDC_ARROW);
    RegisterClass(&ic);

    int sw = GetSystemMetrics(SM_CXSCREEN), sh = GetSystemMetrics(SM_CYSCREEN);
    hBlackWnd = CreateWindowEx(WS_EX_TOPMOST, "BG_STABLE", NULL,
        WS_POPUP | WS_VISIBLE | WS_CLIPCHILDREN,
        0, 0, sw, sh, NULL, NULL, hInstance, NULL);
    hInputWnd = CreateWindowEx(WS_EX_TOPMOST | WS_EX_TOOLWINDOW,
        "IN_GRADIENT", "SET TIME",
        WS_POPUP | WS_CAPTION | WS_VISIBLE,
        (sw - 340) / 2, (sh - 220) / 2, 340, 220,
        hBlackWnd, NULL, hInstance, NULL);

    MSG msg;
    while (GetMessage(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }
    return 0;
}
