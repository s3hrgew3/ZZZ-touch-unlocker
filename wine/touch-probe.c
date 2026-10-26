#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define WM_TOUCH 0x0240
#define WM_POINTERUPDATE 0x0245
#define WM_POINTERDOWN 0x0246
#define WM_POINTERUP 0x0247

static LONG n_touch, n_pdown, n_pupd, n_pup, n_lbut;
static int verbose;

#define USAGE \
    "zzz-touch-probe -- does this Wine build deliver WM_TOUCH?\n" \
    "\n" \
    "  zzz-touch-probe [--seconds N] [SECONDS] [--verbose]\n" \
    "\n" \
    "  Opens a topmost fullscreen window, registers it for touch, and counts the\n" \
    "  touch / pointer messages that arrive. Touch the window while it listens.\n" \
    "\n" \
    "  exit 0 = WM_TOUCH delivered, 1 = no WM_TOUCH, 2 = usage\n" \
    "\n" \
    "  A build that stubs RegisterTouchWindow prints: fixme:win:RegisterTouchWindow\n" \
    "  ... stub! and never reports IsTouchWindow=1.\n"
static LRESULT CALLBACK WndProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    switch (m) {
    case WM_TOUCH: InterlockedIncrement(&n_touch); printf("  WM_TOUCH (contacts hi=%u)\n", (unsigned)(w & 0xffff)); break;
    case WM_POINTERDOWN: InterlockedIncrement(&n_pdown); break;
    case WM_POINTERUPDATE: InterlockedIncrement(&n_pupd); break;
    case WM_POINTERUP: InterlockedIncrement(&n_pup); break;
    case WM_LBUTTONDOWN: InterlockedIncrement(&n_lbut); break;
    }
    return DefWindowProcW(h, m, w, l);
}
static void Pump(DWORD ms) {
    DWORD end = GetTickCount() + ms; MSG msg;
    while ((int)(end - GetTickCount()) > 0) {
        while (PeekMessageW(&msg, NULL, 0, 0, PM_REMOVE)) { TranslateMessage(&msg); DispatchMessageW(&msg); }
        Sleep(5);
    }
}
int main(int argc, char** argv) {
    int secs = 8;
    int i;
    for (i = 1; i < argc; ++i) {
        if (!strcmp(argv[i], "--verbose")) verbose = 1;
        else if (!strcmp(argv[i], "--seconds") && i + 1 < argc) secs = atoi(argv[++i]);
        else if (!strncmp(argv[i], "--seconds=", 10)) secs = atoi(argv[i] + 10);
        else if (!strcmp(argv[i], "--help") || !strcmp(argv[i], "-h")) { printf(USAGE); return 2; }
        else if (argv[i][0] == '-') { printf("unknown argument: %s\n", argv[i]); printf(USAGE); return 2; }
        else secs = atoi(argv[i]);
    }
    if (secs < 1) secs = 1;
    WNDCLASSW wc = {0}; wc.lpfnWndProc = WndProc; wc.hInstance = GetModuleHandleW(NULL); wc.lpszClassName = L"zzz-touch-probe";
    wc.hbrBackground = (HBRUSH)GetStockObject(BLACK_BRUSH);
    RegisterClassW(&wc);
    int sw = GetSystemMetrics(SM_CXSCREEN), sh = GetSystemMetrics(SM_CYSCREEN);
    HWND hwnd = CreateWindowExW(WS_EX_TOPMOST, L"zzz-touch-probe", L"zzz-touch-probe", WS_POPUP | WS_VISIBLE,
                                0, 0, sw, sh, NULL, NULL, wc.hInstance, NULL);
    SetWindowPos(hwnd, HWND_TOPMOST, 0, 0, sw, sh, SWP_SHOWWINDOW);
    SetForegroundWindow(hwnd); SetFocus(hwnd); ShowWindow(hwnd, SW_SHOW);
    printf("window %p at 0,0 %dx%d\n", (void*)hwnd, sw, sh);
    printf("RegisterTouchWindow -> %d\n", RegisterTouchWindow(hwnd, 0) ? 1 : 0);
    printf("IsTouchWindow      -> %d (%s)\n", IsTouchWindow(hwnd, NULL) ? 1 : 0,
           IsTouchWindow(hwnd, NULL) ? "registered" : "STUBBED");
    printf("SM_DIGITIZER=%d SM_MAXTOUCHES=%d\n", GetSystemMetrics(94), GetSystemMetrics(95));
    printf("window 0,0 %dx%d (topmost fullscreen)\n", sw, sh);
    printf("listening %d s -- touch the window now\n", secs); fflush(stdout);
    Pump(secs * 1000);
    printf("RESULT WM_TOUCH=%ld POINTERDOWN=%ld POINTERUPDATE=%ld POINTERUP=%ld LBUTTONDOWN=%ld\n",
           (long)n_touch, (long)n_pdown, (long)n_pupd, (long)n_pup, (long)n_lbut);
    if (n_touch > 0) {
        printf("VERDICT: this build delivers WM_TOUCH -- Unity touch input can work\n");
        return 0;
    }
    printf("VERDICT: no WM_TOUCH -- the touch API is stubbed, or nothing touched the window\n");
    return 1;
}
