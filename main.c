#include "prose_code.h"

/* ── Global editor state (owned by main.c) ── */
EditorState g_editor;

/* ═══════════════════════════════════════════════════════════════
 * APPLICATION ICON — Generated programmatically
 * ═══════════════════════════════════════════════════════════════ */

HICON create_app_icon(void) {
    int size = 32;
    HDC hdc = GetDC(NULL);
    HDC mem = CreateCompatibleDC(hdc);
    HBITMAP bmp = CreateCompatibleBitmap(hdc, size, size);
    HBITMAP old_bmp = (HBITMAP)SelectObject(mem, bmp);

    /* Background */
    fill_rect(mem, 0, 0, size, size, CLR_BG_DARK);

    /* Draw a pen shape */
    HPEN pen = CreatePen(PS_SOLID, 2, CLR_LAVENDER);
    HPEN old_pen = (HPEN)SelectObject(mem, pen);
    MoveToEx(mem, 8, 24, NULL);
    LineTo(mem, 24, 8);
    MoveToEx(mem, 24, 8, NULL);
    LineTo(mem, 28, 4);

    /* Pen nib */
    HPEN pen2 = CreatePen(PS_SOLID, 1, CLR_PEACH);
    SelectObject(mem, pen2);
    MoveToEx(mem, 6, 26, NULL);
    LineTo(mem, 10, 22);

    SelectObject(mem, old_pen);
    DeleteObject(pen);
    DeleteObject(pen2);

    /* Create icon from bitmap */
    HBITMAP mask = CreateCompatibleBitmap(hdc, size, size);
    HDC maskDC = CreateCompatibleDC(hdc);
    HBITMAP old_mask_bmp = (HBITMAP)SelectObject(maskDC, mask);
    fill_rect(maskDC, 0, 0, size, size, RGB(0, 0, 0));

    /* Deselect bitmaps before using in ICONINFO */
    SelectObject(mem, old_bmp);
    SelectObject(maskDC, old_mask_bmp);

    ICONINFO ii = {0};
    ii.fIcon = TRUE;
    ii.hbmMask = mask;
    ii.hbmColor = bmp;
    HICON icon = CreateIconIndirect(&ii);

    DeleteDC(maskDC);
    DeleteObject(mask);
    DeleteDC(mem);
    DeleteObject(bmp);
    ReleaseDC(NULL, hdc);

    return icon;
}

/* ═══════════════════════════════════════════════════════════════
 * ENTRY POINT
 * ═══════════════════════════════════════════════════════════════ */

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE hPrev, LPWSTR lpCmdLine, int nCmdShow) {
    (void)hPrev; (void)lpCmdLine;

    /* Enable Per-Monitor DPI Awareness V2 (Win10 1703+) */
    {
        typedef BOOL (WINAPI *SetProcDpiCtx)(DPI_AWARENESS_CONTEXT);
        HMODULE u32 = GetModuleHandleW(L"user32.dll");
        SetProcDpiCtx fn = (SetProcDpiCtx)GetProcAddress(u32, "SetProcessDpiAwarenessContext");
        if (fn) {
            fn(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
        } else {
            /* Fallback for older Windows */
            typedef BOOL (WINAPI *SetProcDpiAware)(void);
            SetProcDpiAware fn2 = (SetProcDpiAware)GetProcAddress(u32, "SetProcessDPIAware");
            if (fn2) fn2();
        }
    }

    /* Initialize COM for potential future use */
    HRESULT co_hr = CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);

    /* Initialize theme */
    g_theme = THEME_DARK;
    g_theme_index = 0;

    /* Initialize arena allocator */
    arena_init(&g_frame_arena, ARENA_SIZE);
    kw_table_init();

    /* Initialize spell checker */
    spell_init();

    /* Enable visual styles */
    INITCOMMONCONTROLSEX icc = { sizeof(icc), ICC_WIN95_CLASSES };
    InitCommonControlsEx(&icc);

    /* Compute DPI scale factor */
    {
        HDC screen = GetDC(NULL);
        int dpi = GetDeviceCaps(screen, LOGPIXELSX);
        ReleaseDC(NULL, screen);
        g_dpi_scale = (float)dpi / 96.0f;
    }

    /* Register window class */
    HICON icon = create_app_icon();

    WNDCLASSEXW wc = {0};
    wc.cbSize = sizeof(wc);
    wc.style = CS_HREDRAW | CS_VREDRAW | CS_DBLCLKS;
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInstance;
    wc.hIcon = icon;
    wc.hCursor = LoadCursor(NULL, IDC_IBEAM);
    wc.lpszClassName = L"ProseCodeEditor";
    wc.hbrBackground = NULL;
    RegisterClassExW(&wc);

    /* Create window — borderless for custom chrome */
    int sw = GetSystemMetrics(SM_CXSCREEN);
    int sh = GetSystemMetrics(SM_CYSCREEN);
    int ww = (int)(sw * 0.65);
    int wh = (int)(sh * 0.75);

    HWND hwnd = CreateWindowExW(
        WS_EX_ACCEPTFILES,
        L"ProseCodeEditor",
        L"Prose_Code",
        WS_POPUP | WS_THICKFRAME | WS_MINIMIZEBOX | WS_MAXIMIZEBOX | WS_CLIPCHILDREN,
        (sw - ww) / 2, (sh - wh) / 2, ww, wh,
        NULL, NULL, hInstance, NULL);

    g_editor.hwnd = hwnd;

    /* Re-check DPI for the actual window's monitor (may differ from primary) */
    {
        typedef UINT (WINAPI *GetDpiForWindowFn)(HWND);
        GetDpiForWindowFn pGetDpiForWindow = (GetDpiForWindowFn)GetProcAddress(
            GetModuleHandleW(L"user32.dll"), "GetDpiForWindow");
        if (pGetDpiForWindow) {
            UINT wdpi = pGetDpiForWindow(hwnd);
            if (wdpi > 0) g_dpi_scale = (float)wdpi / 96.0f;
        }
    }

    /* Dark mode for title bar (use current theme setting) */
    BOOL dark = g_theme.is_dark;
    DwmSetWindowAttribute(hwnd, 20, &dark, sizeof(dark));

    ShowWindow(hwnd, nCmdShow);
    UpdateWindow(hwnd);

    /* Check for crash recovery (autosave shadows from previous session) */
    autosave_recover();

    /* Handle command line: open file if specified */
    if (lpCmdLine && lpCmdLine[0]) {
        /* Strip quotes */
        wchar_t path[MAX_PATH];
        safe_wcscpy(path, MAX_PATH, lpCmdLine);
        if (path[0] == L'"') {
            memmove(path, path + 1, wcslen(path) * sizeof(wchar_t));
            wchar_t *q = wcschr(path, L'"');
            if (q) *q = 0;
        }
        Document *doc = g_editor.tabs[0];
        load_file(doc, path);
    }

    /* Message loop */
    MSG msg;
    while (GetMessageW(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    /* Cleanup — autosave shadows already removed in WM_CLOSE */
    for (int i = 0; i < g_editor.tab_count; i++) doc_free(g_editor.tabs[i]);
    if (g_editor.hdc_back) {
        /* Restore original bitmap before deleting ours (mirrors WM_SIZE cleanup) */
        SelectObject(g_editor.hdc_back, g_editor.bmp_back_old);
        DeleteObject(g_editor.bmp_back);
        DeleteDC(g_editor.hdc_back);
    }
    DeleteObject(g_editor.font_main);
    DeleteObject(g_editor.font_bold);
    DeleteObject(g_editor.font_italic);
    DeleteObject(g_editor.font_ui);
    DeleteObject(g_editor.font_ui_small);
    DeleteObject(g_editor.font_title);
    DeleteObject(g_editor.font_stats_hero);
    free(g_editor.search.match_positions);
    free(g_frame_arena.base);

    if (g_spell_checker) g_spell_checker->lpVtbl->Release(g_spell_checker);
    spell_cache_free();
    DestroyIcon(icon);
    if (SUCCEEDED(co_hr)) CoUninitialize();
    return (int)msg.wParam;
}
