#include "prose_code.h"

/* Convert mouse position to text position */
bpos mouse_to_pos(int mx, int my) {
    Document *doc = current_doc();
    if (!doc) return 0;

    int edit_y = DPI(TITLEBAR_H + MENUBAR_H + TABBAR_H);
    int gw = gutter_width(doc);
    int text_x = gw;
    int lh = g_editor.line_height;
    int cw_px = g_editor.char_width;

    int px = mx - text_x;
    /* Account for horizontal scroll in code mode */
    if (doc->mode == MODE_CODE) px += doc->scroll_x;
    if (px < 0) px = 0;

    if (doc->mode == MODE_PROSE && doc->wc.count > 0) {
        bpos vline = (my - edit_y + doc->scroll_y) / lh;
        if (vline < 0) vline = 0;
        if (vline >= doc->wc.count) vline = doc->wc.count - 1;

        bpos vls = doc->wc.entries[vline].pos;
        bpos vle = wc_visual_line_end(&doc->wc, &doc->gb, &doc->lc, vline);
        bpos vline_len = vle - vls;
        bpos col = pixel_x_to_col(&doc->gb, vls, vline_len, px, cw_px);
        return vls + col;
    }

    bpos line = (my - edit_y + doc->scroll_y) / lh;
    if (line < 0) line = 0;
    if (line >= doc->lc.count) line = doc->lc.count - 1;

    bpos ls = lc_line_start(&doc->lc, line);
    bpos le = lc_line_end(&doc->lc, &doc->gb, line);
    bpos ll = le - ls;
    bpos col = pixel_x_to_col(&doc->gb, ls, ll, px, cw_px);
    return ls + col;
}

/* Scrollbar geometry helper — returns thumb_y and thumb_h for the current scroll state */
int scrollbar_thumb_geometry(int *out_thumb_y, int *out_thumb_h, int *out_edit_y, int *out_edit_h) {
    Document *doc = current_doc();
    if (!doc) return 0;

    int edit_y = DPI(TITLEBAR_H + MENUBAR_H + TABBAR_H);
    int edit_h = g_editor.client_h - edit_y - DPI(STATUSBAR_H);
    int vlines = (int)((doc->mode == MODE_PROSE && doc->wc.count > 0) ? doc->wc.count : doc->lc.count);
    int total_h = vlines * g_editor.line_height;

    if (out_edit_y) *out_edit_y = edit_y;
    if (out_edit_h) *out_edit_h = edit_h;

    if (total_h <= edit_h) return 0; /* no scrollbar needed */

    int thumb_h = (edit_h * edit_h) / total_h;
    if (thumb_h < 30) thumb_h = 30;
    int thumb_y = edit_y + (doc->scroll_y * (edit_h - thumb_h)) / (total_h - edit_h);
    /* Clamp thumb within the scrollbar track */
    if (thumb_y < edit_y) thumb_y = edit_y;
    if (thumb_y + thumb_h > edit_y + edit_h) thumb_y = edit_y + edit_h - thumb_h;

    if (out_thumb_y) *out_thumb_y = thumb_y;
    if (out_thumb_h) *out_thumb_h = thumb_h;
    return 1;
}

/* Word boundary helpers */
bpos word_start(GapBuffer *gb, bpos pos) {
    if (pos <= 0) return 0;
    /* Skip non-word characters first (spaces, punctuation) */
    while (pos > 0) {
        wchar_t c = gb_char_at(gb, pos - 1);
        if (iswalnum(c) || c == L'_') break;
        pos--;
    }
    /* Then skip the word itself */
    while (pos > 0) {
        wchar_t c = gb_char_at(gb, pos - 1);
        if (!iswalnum(c) && c != L'_') break;
        pos--;
    }
    return pos;
}

bpos word_end(GapBuffer *gb, bpos pos) {
    bpos len = gb_length(gb);
    while (pos < len) {
        wchar_t c = gb_char_at(gb, pos);
        if (!iswalnum(c) && c != L'_') break;
        pos++;
    }
    return pos;
}

/* Compute whether position 'up_to' is inside a block comment.
 * Scans from document start, respecting strings and line comments. */
int compute_block_comment_state(GapBuffer *gb, bpos up_to) {
    bpos len = gb_length(gb);
    if (up_to > len) up_to = len;
    if (up_to <= 0) return 0;
    /* Prefer arena to avoid heap alloc/free on every backward scroll frame */
    wchar_t *buf = (wchar_t *)arena_alloc(&g_frame_arena, up_to * sizeof(wchar_t));
    int used_arena = 1;
    if (!buf) {
        buf = (wchar_t *)malloc(up_to * sizeof(wchar_t));
        used_arena = 0;
    }
    if (!buf) return 0;
    gb_copy_range(gb, 0, up_to, buf);
    int in_bc = 0, in_str = 0, in_lc = 0;
    for (bpos i = 0; i < up_to; i++) {
        wchar_t c = buf[i];
        wchar_t cn = (i + 1 < up_to) ? buf[i + 1] : 0;
        if (c == L'\n' || c == L'\r') { in_lc = 0; in_str = 0; continue; }
        if (in_lc) continue;
        if (in_str) { if (c == L'\\') { i++; continue; } if (c == in_str) in_str = 0; continue; }
        if (in_bc) { if (c == L'*' && cn == L'/') { in_bc = 0; i++; } continue; }
        if (c == L'/' && cn == L'/') { in_lc = 1; i++; }
        else if (c == L'/' && cn == L'*') { in_bc = 1; i++; }
        else if (c == L'"' || c == L'\'') { in_str = c; }
    }
    if (!used_arena) free(buf);
    return in_bc;
}

int advance_block_comment_state_for_line(GapBuffer *gb, bpos ls, bpos le, int in_bc) {
    bpos len = le - ls;
    if (len <= 0) return in_bc;

    wchar_t *buf = (wchar_t *)arena_alloc(&g_frame_arena, len * sizeof(wchar_t));
    int used_arena = 1;
    if (!buf) {
        buf = (wchar_t *)malloc(len * sizeof(wchar_t));
        used_arena = 0;
    }
    if (!buf) return in_bc;

    gb_copy_range(gb, ls, len, buf);

    int in_str = 0;
    int in_lc = 0;
    for (bpos i = 0; i < len; i++) {
        wchar_t c = buf[i];
        wchar_t cn = (i + 1 < len) ? buf[i + 1] : 0;
        if (in_lc) continue;
        if (in_str) {
            if (c == L'\\') { i++; continue; }
            if (c == in_str) in_str = 0;
            continue;
        }
        if (in_bc) {
            if (c == L'*' && cn == L'/') { in_bc = 0; i++; }
            continue;
        }
        if (c == L'/' && cn == L'/') { in_lc = 1; i++; }
        else if (c == L'/' && cn == L'*') { in_bc = 1; i++; }
        else if (c == L'"' || c == L'\'') { in_str = c; }
    }

    if (!used_arena) free(buf);
    return in_bc;
}

bpos find_matching_bracket(GapBuffer *gb, bpos pos) {
    bpos len = gb_length(gb);
    if (pos < 0 || pos >= len) return -1;

    wchar_t c = gb_char_at(gb, pos);
    wchar_t match;
    int dir;

    switch (c) {
        case L'(': match = L')'; dir = 1; break;
        case L')': match = L'('; dir = -1; break;
        case L'[': match = L']'; dir = 1; break;
        case L']': match = L'['; dir = -1; break;
        case L'{': match = L'}'; dir = 1; break;
        case L'}': match = L'{'; dir = -1; break;
        default: return -1;
    }

    /* Cache: tokenize each line at most once during the scan */
    bpos cached_line = -1;
    SynToken cached_tokens[2048];

    int depth = 1;
    bpos i = pos + dir;
    while (i >= 0 && i < len && depth > 0) {
        wchar_t ch = gb_char_at(gb, i);
        if (ch == c || ch == match) {
            /* Check if this position is inside a string or comment */
            bpos line = lc_line_of(&current_doc()->lc, i);
            bpos ls = lc_line_start(&current_doc()->lc, line);
            bpos le = lc_line_end(&current_doc()->lc, &current_doc()->gb, line);
            bpos ll = le - ls;
            if (ll > 0 && ll <= 2048) {
                /* Tokenize this line (cached per-line) */
                if (line != cached_line) {
                    wchar_t lbuf[2048];
                    gb_copy_range(gb, ls, ll, lbuf);
                    int bc_state = compute_block_comment_state(gb, ls);
                    tokenize_line_code(lbuf, (int)ll, cached_tokens, bc_state);
                    cached_line = line;
                }
                bpos idx = i - ls;
                if (idx >= 0 && idx < 2048) {
                    SynToken t = cached_tokens[idx];
                    if (t == TOK_STRING || t == TOK_COMMENT) { i += dir; continue; }
                }
            }
            if (ch == c) depth++;
            else if (ch == match) depth--;
        }
        if (depth == 0) return i;
        i += dir;
    }
    return -1;
}

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {

    case WM_CREATE: {
        /* Create fonts */
        g_editor.font_size = FONT_SIZE_DEFAULT;
        g_editor.menu_open = -1;
        g_editor.menu_hover_item = -1;
        g_editor.dropdown_hover = 0;
        g_editor.spellcheck_enabled = 1;  /* spell check on by default */

        g_editor.font_main = CreateFontW(
            -DPI(g_editor.font_size), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_TT_PRECIS, CLIP_DEFAULT_PRECIS,
            CLEARTYPE_QUALITY, FIXED_PITCH | FF_MODERN, L"Consolas");

        g_editor.font_bold = CreateFontW(
            -DPI(g_editor.font_size), 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_TT_PRECIS, CLIP_DEFAULT_PRECIS,
            CLEARTYPE_QUALITY, FIXED_PITCH | FF_MODERN, L"Consolas");

        g_editor.font_italic = CreateFontW(
            -DPI(g_editor.font_size), 0, 0, 0, FW_NORMAL, TRUE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_TT_PRECIS, CLIP_DEFAULT_PRECIS,
            CLEARTYPE_QUALITY, FIXED_PITCH | FF_MODERN, L"Consolas");

        g_editor.font_ui = CreateFontW(
            -DPI(14), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_TT_PRECIS, CLIP_DEFAULT_PRECIS,
            CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_SWISS, L"Segoe UI");

        g_editor.font_ui_small = CreateFontW(
            -DPI(12), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_TT_PRECIS, CLIP_DEFAULT_PRECIS,
            CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_SWISS, L"Segoe UI");

        g_editor.font_title = CreateFontW(
            -DPI(15), 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_TT_PRECIS, CLIP_DEFAULT_PRECIS,
            CLEARTYPE_QUALITY, FIXED_PITCH | FF_MODERN, L"Consolas");

        g_editor.font_stats_hero = CreateFontW(
            -DPI(28), 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_TT_PRECIS, CLIP_DEFAULT_PRECIS,
            CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_SWISS, L"Segoe UI");

        /* Measure character dimensions */
        HDC hdc = GetDC(hwnd);
        HFONT old = (HFONT)SelectObject(hdc, g_editor.font_main);
        TEXTMETRICW tm;
        GetTextMetricsW(hdc, &tm);
        g_editor.char_width = tm.tmAveCharWidth;
        g_editor.line_height = tm.tmHeight + tm.tmExternalLeading + 4;
        SelectObject(hdc, old);
        ReleaseDC(hwnd, hdc);

        /* Create initial tab */
        new_tab();
        g_editor.show_minimap = 1;

        /* Start cursor blink timer (~30fps for smooth fade — 1200ms sine period needs ~30fps) */
        SetTimer(hwnd, TIMER_BLINK, 33, NULL);
        /* Note: TIMER_SMOOTH is started on-demand when scroll target changes */
        g_editor.cursor_visible = 1; g_editor.cursor_last_active = GetTickCount();
        g_editor.session_start_time = GetTickCount();

        /* Start autosave timer — fires every 30 seconds */
        SetTimer(hwnd, TIMER_AUTOSAVE, 30000, NULL);

        return 0;
    }

    case WM_SIZE: {
        g_editor.client_w = LOWORD(lParam);
        g_editor.client_h = HIWORD(lParam);

        /* Recreate back buffer */
        HDC hdc = GetDC(hwnd);
        if (g_editor.hdc_back) {
            /* Restore original bitmap before deleting ours */
            SelectObject(g_editor.hdc_back, g_editor.bmp_back_old);
            DeleteObject(g_editor.bmp_back);
            DeleteDC(g_editor.hdc_back);
        }
        if (g_editor.client_w <= 0 || g_editor.client_h <= 0) {
            g_editor.hdc_back = NULL;
            g_editor.bmp_back = NULL;
            ReleaseDC(hwnd, hdc);
            return 0;
        }
        g_editor.hdc_back = CreateCompatibleDC(hdc);
        g_editor.bmp_back = CreateCompatibleBitmap(hdc, g_editor.client_w, g_editor.client_h);
        g_editor.bmp_back_old = (HBITMAP)SelectObject(g_editor.hdc_back, g_editor.bmp_back);
        ReleaseDC(hwnd, hdc);
        InvalidateRect(hwnd, NULL, FALSE);
        return 0;
    }

    case WM_DPICHANGED: {
        /* Update DPI scale and recreate all fonts for the new monitor DPI */
        int new_dpi = HIWORD(wParam);
        g_dpi_scale = (float)new_dpi / 96.0f;

        /* Destroy all fonts */
        DeleteObject(g_editor.font_main);
        DeleteObject(g_editor.font_bold);
        DeleteObject(g_editor.font_italic);
        DeleteObject(g_editor.font_ui);
        DeleteObject(g_editor.font_ui_small);
        DeleteObject(g_editor.font_title);
        DeleteObject(g_editor.font_stats_hero);

        /* Recreate all fonts at new DPI */
        g_editor.font_main = CreateFontW(
            -DPI(g_editor.font_size), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_TT_PRECIS, CLIP_DEFAULT_PRECIS,
            CLEARTYPE_QUALITY, FIXED_PITCH | FF_MODERN, L"Consolas");
        g_editor.font_bold = CreateFontW(
            -DPI(g_editor.font_size), 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_TT_PRECIS, CLIP_DEFAULT_PRECIS,
            CLEARTYPE_QUALITY, FIXED_PITCH | FF_MODERN, L"Consolas");
        g_editor.font_italic = CreateFontW(
            -DPI(g_editor.font_size), 0, 0, 0, FW_NORMAL, TRUE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_TT_PRECIS, CLIP_DEFAULT_PRECIS,
            CLEARTYPE_QUALITY, FIXED_PITCH | FF_MODERN, L"Consolas");
        g_editor.font_ui = CreateFontW(
            -DPI(14), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_TT_PRECIS, CLIP_DEFAULT_PRECIS,
            CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_SWISS, L"Segoe UI");
        g_editor.font_ui_small = CreateFontW(
            -DPI(12), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_TT_PRECIS, CLIP_DEFAULT_PRECIS,
            CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_SWISS, L"Segoe UI");
        g_editor.font_title = CreateFontW(
            -DPI(15), 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_TT_PRECIS, CLIP_DEFAULT_PRECIS,
            CLEARTYPE_QUALITY, FIXED_PITCH | FF_MODERN, L"Consolas");
        g_editor.font_stats_hero = CreateFontW(
            -DPI(28), 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_TT_PRECIS, CLIP_DEFAULT_PRECIS,
            CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_SWISS, L"Segoe UI");

        /* Remeasure character dimensions */
        HDC hdc_m = GetDC(hwnd);
        HFONT old_f = (HFONT)SelectObject(hdc_m, g_editor.font_main);
        TEXTMETRICW tm_dpi;
        GetTextMetricsW(hdc_m, &tm_dpi);
        g_editor.char_width = tm_dpi.tmAveCharWidth;
        g_editor.line_height = tm_dpi.tmHeight + tm_dpi.tmExternalLeading + 4;
        SelectObject(hdc_m, old_f);
        ReleaseDC(hwnd, hdc_m);

        /* Resize window to suggested rect from the system */
        RECT *suggested = (RECT *)lParam;
        SetWindowPos(hwnd, NULL,
            suggested->left, suggested->top,
            suggested->right - suggested->left,
            suggested->bottom - suggested->top,
            SWP_NOZORDER | SWP_NOACTIVATE);
        return 0;
    }

    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hwnd, &ps);
        if (g_editor.hdc_back) {
            render(g_editor.hdc_back);
            /* BitBlt only the dirty region — avoids full-window blit for cursor blink,
             * scrollbar hover, and other small invalidations. */
            int bx = ps.rcPaint.left, by = ps.rcPaint.top;
            int bw = ps.rcPaint.right - bx, bh = ps.rcPaint.bottom - by;
            BitBlt(hdc, bx, by, bw, bh, g_editor.hdc_back, bx, by, SRCCOPY);
        }
        EndPaint(hwnd, &ps);
        return 0;
    }

    case WM_ERASEBKGND:
        return 1; /* prevent flicker */

    case WM_SETCURSOR: {
        if (LOWORD(lParam) == HTCLIENT) {
            POINT pt;
            GetCursorPos(&pt);
            ScreenToClient(hwnd, &pt);
            int sb_x = g_editor.client_w - DPI(SCROLLBAR_W);
            int in_editor = (pt.y >= DPI(TITLEBAR_H + MENUBAR_H + TABBAR_H) && pt.y < g_editor.client_h - DPI(STATUSBAR_H));
            int in_scrollbar = (pt.x >= sb_x && in_editor);
            int in_chrome = (pt.y < DPI(TITLEBAR_H + MENUBAR_H + TABBAR_H) || pt.y >= g_editor.client_h - DPI(STATUSBAR_H));

            /* Update scrollbar hover state */
            int was_hover = g_editor.scrollbar_hover;
            g_editor.scrollbar_hover = in_scrollbar;
            if (was_hover != g_editor.scrollbar_hover)
                InvalidateRect(hwnd, NULL, FALSE);

            if (in_scrollbar || in_chrome) {
                SetCursor(LoadCursor(NULL, IDC_ARROW));
            } else {
                SetCursor(LoadCursor(NULL, IDC_IBEAM));
            }
            return TRUE;
        }
        break;
    }

    case WM_TIMER: {
        if (wParam == TIMER_BLINK) {
            /* Refresh stats overlay at ~1Hz for running clock */
            if (g_editor.show_stats_screen) {
                static int stats_tick = 0;
                if (++stats_tick >= 60) {
                    stats_tick = 0;
                    InvalidateRect(hwnd, NULL, FALSE);
                }
            }
            /* Refresh search bar cursor blink (~every 530ms) */
            if (g_editor.search.active) {
                static int search_tick = 0;
                if (++search_tick >= 33) { /* ~530ms at 16ms/tick */
                    search_tick = 0;
                    int bar_w = DPI(460);
                    int sx = g_editor.client_w - bar_w - DPI(24);
                    int sy = DPI(TITLEBAR_H + MENUBAR_H + TABBAR_H) + DPI(8);
                    RECT sr = { sx, sy, sx + bar_w, sy + DPI(80) };
                    InvalidateRect(hwnd, &sr, FALSE);
                }
            }
            /* Invalidate only the cursor rect for smooth fade animation.
             * Skip when stats overlay is showing — the tiny partial repaint
             * slices through ClearType glyphs and leaves visible seams. */
            Document *blink_doc = current_doc();
            if (blink_doc && !g_editor.show_stats_screen) {
                int edit_y = DPI(TITLEBAR_H + MENUBAR_H + TABBAR_H);
                int lh = g_editor.line_height;
                int cw = g_editor.char_width;
                int gw = gutter_width(blink_doc);
                int cline, ccol, cline_start;
                if (blink_doc->mode == MODE_PROSE && blink_doc->wc.count > 0) {
                    cline = wc_visual_line_of(&blink_doc->wc, blink_doc->cursor);
                    ccol = wc_col_in_vline(&blink_doc->wc, blink_doc->cursor, cline);
                    cline_start = blink_doc->wc.entries[cline].pos;
                } else {
                    cline = pos_to_line(blink_doc, blink_doc->cursor);
                    ccol = pos_to_col(blink_doc, blink_doc->cursor);
                    cline_start = lc_line_start(&blink_doc->lc, cline);
                }
                int cy = edit_y + cline * lh - blink_doc->scroll_y;
                int cx = gw + col_to_pixel_x(&blink_doc->gb, cline_start, ccol, cw) - blink_doc->scroll_x;
                RECT cr = { cx - 1, cy, cx + DPI(CURSOR_WIDTH) + 2, cy + lh };
                InvalidateRect(hwnd, &cr, FALSE);
            }
        }
        if (wParam == TIMER_AUTOSAVE) {
            autosave_tick();
        }
        if (wParam == TIMER_DRAG_SCROLL) {
            Document *doc = current_doc();
            if (doc && g_editor.mouse_captured) {
                POINT pt;
                GetCursorPos(&pt);
                ScreenToClient(hwnd, &pt);
                int edit_top = DPI(TITLEBAR_H + MENUBAR_H + TABBAR_H);
                int edit_bot = g_editor.client_h - DPI(STATUSBAR_H);
                int lh = g_editor.line_height;
                int scroll_amt = 0;
                if (pt.y < edit_top) {
                    scroll_amt = -lh * (1 + (edit_top - pt.y) / (lh * 2));
                } else if (pt.y > edit_bot) {
                    scroll_amt = lh * (1 + (pt.y - edit_bot) / (lh * 2));
                }
                if (scroll_amt != 0) {
                    doc->target_scroll_y += scroll_amt;
                    if (doc->target_scroll_y < 0) doc->target_scroll_y = 0;
                    int total_vl = (int)((doc->mode == MODE_PROSE && doc->wc.count > 0) ? doc->wc.count : doc->lc.count);
                    int max_scroll = total_vl * lh - (edit_bot - edit_top);
                    if (max_scroll > 0 && doc->target_scroll_y > max_scroll)
                        doc->target_scroll_y = max_scroll;
                    doc->scroll_y = doc->target_scroll_y;
                    /* Clamp mx to text area */
                    int clamp_mx = pt.x;
                    int max_text_x = g_editor.client_w - DPI(SCROLLBAR_W);
                    if (g_editor.show_minimap) max_text_x -= DPI(MINIMAP_W);
                    if (clamp_mx > max_text_x) clamp_mx = max_text_x;
                    bpos pos = mouse_to_pos(clamp_mx, pt.y);
                    if (doc->sel_anchor < 0) doc->sel_anchor = doc->cursor;
                    doc->cursor = pos;
                    InvalidateRect(hwnd, NULL, FALSE);
                }
            } else {
                KillTimer(hwnd, TIMER_DRAG_SCROLL);
            }
            return 0;
        }
        if (wParam == TIMER_SMOOTH) {
            Document *doc = current_doc();
            int needs_invalidate = 0;
            if (doc && doc->scroll_y != doc->target_scroll_y) {
                int diff = doc->target_scroll_y - doc->scroll_y;
                /* Exponential ease-out: move 1/4 of remaining distance, minimum 1px */
                int step = diff / 4;
                if (step == 0) step = (diff > 0) ? 1 : -1;
                doc->scroll_y += step;
                if (abs(doc->scroll_y - doc->target_scroll_y) <= 1)
                    doc->scroll_y = doc->target_scroll_y;
                needs_invalidate = 1;
            }
            /* Horizontal smooth scroll (code mode) */
            if (doc && doc->scroll_x != doc->target_scroll_x) {
                int diff = doc->target_scroll_x - doc->scroll_x;
                int step = diff / 4;
                if (step == 0) step = (diff > 0) ? 1 : -1;
                doc->scroll_x += step;
                if (abs(doc->scroll_x - doc->target_scroll_x) <= 1)
                    doc->scroll_x = doc->target_scroll_x;
                needs_invalidate = 1;
            }
            if (needs_invalidate) {
                g_editor.scroll_only_repaint = 1;
                invalidate_editor_region(hwnd);
            } else {
                /* Animation complete — kill the timer to stop burning CPU when idle */
                KillTimer(hwnd, TIMER_SMOOTH);
            }
        }
        return 0;
    }

    case WM_MOUSEWHEEL: {
        Document *doc = current_doc();
        if (!doc) break;
        int delta = GET_WHEEL_DELTA_WPARAM(wParam);
        /* Shift+Wheel = horizontal scroll in code mode */
        if ((LOWORD(wParam) & MK_SHIFT) && doc->mode == MODE_CODE) {
            doc->target_scroll_x -= (delta / 120) * g_editor.char_width * 8;
            if (doc->target_scroll_x < 0) doc->target_scroll_x = 0;
        } else {
            doc->target_scroll_y -= (delta / 120) * g_editor.line_height * 3;
            int vlines = (int)((doc->mode == MODE_PROSE && doc->wc.count > 0) ? doc->wc.count : doc->lc.count);
            int max_scroll = vlines * g_editor.line_height - (g_editor.client_h - DPI(TITLEBAR_H + MENUBAR_H + TABBAR_H) - DPI(STATUSBAR_H));
            if (doc->target_scroll_y < 0) doc->target_scroll_y = 0;
            if (max_scroll > 0 && doc->target_scroll_y > max_scroll) doc->target_scroll_y = max_scroll;
        }
        g_editor.scroll_only_repaint = 1;
        start_scroll_animation();
        invalidate_editor_region(hwnd);
        return 0;
    }

    case WM_MOUSEHWHEEL: {
        /* Native horizontal scroll (tilt wheel / trackpad) */
        Document *doc = current_doc();
        if (!doc || doc->mode != MODE_CODE) break;
        int delta = GET_WHEEL_DELTA_WPARAM(wParam);
        doc->target_scroll_x += (delta / 120) * g_editor.char_width * 8;
        if (doc->target_scroll_x < 0) doc->target_scroll_x = 0;
        { int max_x = 300 * g_editor.char_width; /* reasonable max */
          if (doc->target_scroll_x > max_x) doc->target_scroll_x = max_x; }
        g_editor.scroll_only_repaint = 1;
        start_scroll_animation();
        invalidate_editor_region(hwnd);
        return 0;
    }

    case WM_LBUTTONDOWN: {
        int mx = GET_X_LPARAM(lParam);
        int my = GET_Y_LPARAM(lParam);

        /* Click anywhere to dismiss stats overlay */
        if (g_editor.show_stats_screen) {
            g_editor.show_stats_screen = 0;
            InvalidateRect(hwnd, NULL, FALSE);
            return 0;
        }

        /* Title bar area */
        if (my < DPI(TITLEBAR_H)) {
            /* Check window control buttons (rightmost) */
            int bw = DPI(46);
            int x = g_editor.client_w - bw * 3;
            if (mx >= x && mx < x + bw) {
                ShowWindow(hwnd, SW_MINIMIZE);
                return 0;
            }
            x += bw;
            if (mx >= x && mx < x + bw) {
                WINDOWPLACEMENT wp = {0}; wp.length = sizeof(wp);
                GetWindowPlacement(hwnd, &wp);
                ShowWindow(hwnd, wp.showCmd == SW_MAXIMIZE ? SW_RESTORE : SW_MAXIMIZE);
                return 0;
            }
            x += bw;
            if (mx >= x && mx < x + bw) {
                PostMessage(hwnd, WM_CLOSE, 0, 0);
                return 0;
            }

            /* Check dropdown trigger button click */
            int btn_size = DPI(22);
            int btn_x = DPI(8);
            int btn_y = (DPI(TITLEBAR_H) - btn_size) / 2;

            if (mx >= btn_x && mx < btn_x + btn_size &&
                my >= btn_y && my < btn_y + btn_size) {
                if (g_editor.menu_open >= 0) {
                    g_editor.menu_open = -1;
                } else {
                    g_editor.menu_open = 0;
                    g_editor.menu_hover_item = -1;
                }
                InvalidateRect(hwnd, NULL, FALSE);
                return 0;
            }

            /* Drag to move (anywhere else in titlebar) */
            g_editor.titlebar_dragging = 1;
            g_editor.drag_start.x = mx;
            g_editor.drag_start.y = my;
            SetCapture(hwnd);
            return 0;
        }

        /* Combined dropdown click — MUST be checked before tab bar since
         * the dropdown overlaps the tab bar's y-range */
        if (g_editor.menu_open >= 0) {
            int item_h = DPI(26);
            int sep_h = DPI(9);
            int header_h = DPI(24);
            int dropdown_w = DPI(260);
            int dropdown_x = DPI(8);
            int dropdown_y = DPI(TITLEBAR_H);

            /* Calculate total dropdown height */
            int total_dd_h = DPI(4);
            for (int m = 0; m < MENU_COUNT; m++) {
                if (m > 0) total_dd_h += sep_h;
                total_dd_h += header_h;
                for (int i = 0; i < g_menus[m].item_count; i++)
                    total_dd_h += (g_menus[m].items[i].id == MENU_ID_SEP) ? sep_h : item_h;
            }
            total_dd_h += DPI(4);

            if (mx >= dropdown_x && mx < dropdown_x + dropdown_w &&
                my >= dropdown_y && my < dropdown_y + total_dd_h) {
                int cy = dropdown_y + DPI(4);
                for (int m = 0; m < MENU_COUNT; m++) {
                    if (m > 0) cy += sep_h;
                    cy += header_h;  /* skip category header */
                    for (int i = 0; i < g_menus[m].item_count; i++) {
                        int h = (g_menus[m].items[i].id == MENU_ID_SEP) ? sep_h : item_h;
                        if (g_menus[m].items[i].id != MENU_ID_SEP &&
                            my >= cy && my < cy + h) {
                            menu_execute(g_menus[m].items[i].id);
                            return 0;
                        }
                        cy += h;
                    }
                }
                /* Clicked inside dropdown but on nothing (header/separator) */
                return 0;
            }
            /* Clicked outside dropdown — close it */
            g_editor.menu_open = -1;
            g_editor.menu_hover_item = -1;
            InvalidateRect(hwnd, NULL, FALSE);
            /* Fall through to handle the click on whatever was underneath */
        }

        /* Tab bar */
        if (my >= DPI(TITLEBAR_H + MENUBAR_H) && my < DPI(TITLEBAR_H + MENUBAR_H + TABBAR_H)) {
            int tx = DPI(8);
            for (int i = 0; i < g_editor.tab_count; i++) {
                wchar_t label[128];
                swprintf(label, 128, L"%ls%ls", g_editor.tabs[i]->title, g_editor.tabs[i]->modified ? L" \x2022" : L"");
                int tw = (int)wcslen(label) * DPI(8) + DPI(TAB_PAD) * 2;
                if (tw < DPI(TAB_MIN_W)) tw = DPI(TAB_MIN_W);
                if (tw > DPI(TAB_MAX_W)) tw = DPI(TAB_MAX_W);

                if (mx >= tx && mx < tx + tw) {
                    /* Check close button */
                    if (mx >= tx + tw - DPI(24)) {
                        close_tab(i);
                    } else {
                        g_editor.active_tab = i;
                    }
                    InvalidateRect(hwnd, NULL, FALSE);
                    return 0;
                }
                tx += tw + DPI(4);
            }
            /* New tab button */
            if (mx >= tx + DPI(4) && mx < tx + DPI(30)) {
                new_tab();
                InvalidateRect(hwnd, NULL, FALSE);
            }
            return 0;
        }

        /* Search bar close button */
        if (g_editor.search.active) {
            int bar_h = g_editor.search.replace_active ? DPI(72) : DPI(40);
            int bar_w = DPI(460);
            int sb_x = g_editor.client_w - bar_w - DPI(24);
            int sb_y = DPI(TITLEBAR_H + MENUBAR_H + TABBAR_H) + DPI(8);
            int btn_size = DPI(20);
            int bx = sb_x + bar_w - DPI(12) - btn_size;
            int by = sb_y + DPI(10);
            if (mx >= bx && mx < bx + btn_size && my >= by && my < by + btn_size) {
                toggle_search();
                InvalidateRect(hwnd, NULL, FALSE);
                return 0;
            }
        }

        /* Editor area */
        if (my >= DPI(TITLEBAR_H + MENUBAR_H + TABBAR_H) && my < g_editor.client_h - DPI(STATUSBAR_H)) {
            int sb_x = g_editor.client_w - DPI(SCROLLBAR_W);
            int mm_x = g_editor.client_w - DPI(SCROLLBAR_W) - (g_editor.show_minimap ? DPI(MINIMAP_W) : 0);

            /* Check if click is on the minimap */
            if (g_editor.show_minimap && mx >= mm_x && mx < sb_x) {
                Document *doc = current_doc();
                if (doc) {
                    int edit_y_mm = DPI(TITLEBAR_H + MENUBAR_H + TABBAR_H);
                    int edit_h_mm = g_editor.client_h - edit_y_mm - DPI(STATUSBAR_H);
                    int total_vl = (int)((doc->mode == MODE_PROSE && doc->wc.count > 0) ? doc->wc.count : doc->lc.count);
                    if (total_vl < 1) total_vl = 1;
                    float click_ratio = (float)(my - edit_y_mm) / (float)edit_h_mm;
                    int target_line = (int)(click_ratio * total_vl);
                    if (target_line < 0) target_line = 0;
                    if (target_line >= total_vl) target_line = total_vl - 1;
                    int visible_lines = edit_h_mm / g_editor.line_height;
                    doc->target_scroll_y = (target_line - visible_lines / 2) * g_editor.line_height;
                    int max_scroll = total_vl * g_editor.line_height - edit_h_mm;
                    if (doc->target_scroll_y < 0) doc->target_scroll_y = 0;
                    if (max_scroll > 0 && doc->target_scroll_y > max_scroll) doc->target_scroll_y = max_scroll;
                }
                start_scroll_animation();
                InvalidateRect(hwnd, NULL, FALSE);
                return 0;
            }

            /* Check if click is on the scrollbar */
            if (mx >= sb_x) {
                int thumb_y, thumb_h, edit_y_sb, edit_h_sb;
                if (scrollbar_thumb_geometry(&thumb_y, &thumb_h, &edit_y_sb, &edit_h_sb)) {
                    Document *doc = current_doc();
                    if (doc) {
                        int sb_vlines = (int)((doc->mode == MODE_PROSE && doc->wc.count > 0) ? doc->wc.count : doc->lc.count);
                        int total_h = sb_vlines * g_editor.line_height;
                        if (my >= thumb_y && my < thumb_y + thumb_h) {
                            /* Clicked on thumb — start dragging */
                            g_editor.scrollbar_dragging = 1;
                            g_editor.scrollbar_drag_offset = my - thumb_y;
                            SetCapture(hwnd);
                        } else {
                            /* Clicked on track — page jump to that position */
                            int max_scroll = total_h - edit_h_sb;
                            if (max_scroll > 0) {
                                float ratio = (float)(my - edit_y_sb) / (float)edit_h_sb;
                                doc->scroll_y = (int)(ratio * total_h - edit_h_sb / 2);
                                if (doc->scroll_y < 0) doc->scroll_y = 0;
                                if (doc->scroll_y > max_scroll) doc->scroll_y = max_scroll;
                                doc->target_scroll_y = doc->scroll_y;
                                /* Now start dragging from this new position */
                                scrollbar_thumb_geometry(&thumb_y, &thumb_h, NULL, NULL);
                                g_editor.scrollbar_dragging = 1;
                                g_editor.scrollbar_drag_offset = thumb_h / 2;
                                SetCapture(hwnd);
                            }
                        }
                    }
                }
                InvalidateRect(hwnd, NULL, FALSE);
                return 0;
            }

            SetCapture(hwnd);
            g_editor.mouse_captured = 1;

            bpos pos = mouse_to_pos(mx, my);
            int shift = GetKeyState(VK_SHIFT) & 0x8000;

            if (GetKeyState(VK_CONTROL) & 0x8000) {
                /* Ctrl+click: word select (only if clicking on a word character) */
                Document *doc = current_doc();
                if (doc) {
                    bpos tlen = gb_length(&doc->gb);
                    wchar_t ch = (pos < tlen) ? gb_char_at(&doc->gb, pos) : 0;
                    if (iswalnum(ch) || ch == L'_') {
                        doc->sel_anchor = word_start(&doc->gb, pos);
                        doc->cursor = word_end(&doc->gb, pos);
                    } else {
                        /* On whitespace/punctuation: just place cursor, no selection */
                        doc->sel_anchor = -1;
                        doc->cursor = pos;
                    }
                }
            } else {
                editor_move_cursor(pos, shift);
            }

            g_editor.cursor_visible = 1; g_editor.cursor_last_active = GetTickCount();
            InvalidateRect(hwnd, NULL, FALSE);
            return 0;
        }
        break;
    }

    case WM_LBUTTONDBLCLK: {
        int mx = GET_X_LPARAM(lParam);
        int my = GET_Y_LPARAM(lParam);

        /* Double click in title bar = maximize/restore */
        if (my < DPI(TITLEBAR_H)) {
            WINDOWPLACEMENT wp = {0}; wp.length = sizeof(wp);
            GetWindowPlacement(hwnd, &wp);
            ShowWindow(hwnd, wp.showCmd == SW_MAXIMIZE ? SW_RESTORE : SW_MAXIMIZE);
            return 0;
        }

        /* Double click in editor = select word */
        if (my >= DPI(TITLEBAR_H + MENUBAR_H + TABBAR_H) && my < g_editor.client_h - DPI(STATUSBAR_H)) {
            Document *doc = current_doc();
            if (doc) {
                bpos pos = mouse_to_pos(mx, my);
                doc->sel_anchor = word_start(&doc->gb, pos);
                doc->cursor = word_end(&doc->gb, pos);
                InvalidateRect(hwnd, NULL, FALSE);
            }
            return 0;
        }
        break;
    }

    case WM_MOUSEMOVE: {
        int mx = GET_X_LPARAM(lParam);
        int my = GET_Y_LPARAM(lParam);

        /* Dropdown trigger button hover tracking */
        int old_dropdown_hover = g_editor.dropdown_hover;
        g_editor.dropdown_hover = 0;
        if (my < DPI(TITLEBAR_H)) {
            int btn_size = DPI(22);
            int btn_x = DPI(8);
            int btn_y = (DPI(TITLEBAR_H) - btn_size) / 2;
            if (mx >= btn_x && mx < btn_x + btn_size &&
                my >= btn_y && my < btn_y + btn_size) {
                g_editor.dropdown_hover = 1;
            }
        }
        if (old_dropdown_hover != g_editor.dropdown_hover)
            InvalidateRect(hwnd, NULL, FALSE);

        /* Combined dropdown hover tracking */
        if (g_editor.menu_open >= 0) {
            int item_h = DPI(26);
            int sep_h = DPI(9);
            int header_h = DPI(24);
            int dropdown_w = DPI(260);
            int dropdown_x = DPI(8);
            int dropdown_y = DPI(TITLEBAR_H);

            int old_hover = g_editor.menu_hover_item;
            g_editor.menu_hover_item = -1;
            if (mx >= dropdown_x && mx < dropdown_x + dropdown_w) {
                int cy = dropdown_y + DPI(4);
                int flat_idx = 0;
                for (int m = 0; m < MENU_COUNT; m++) {
                    if (m > 0) cy += sep_h;
                    cy += header_h;
                    for (int i = 0; i < g_menus[m].item_count; i++) {
                        int h = (g_menus[m].items[i].id == MENU_ID_SEP) ? sep_h : item_h;
                        if (g_menus[m].items[i].id != MENU_ID_SEP) {
                            if (my >= cy && my < cy + h) {
                                g_editor.menu_hover_item = flat_idx;
                            }
                            flat_idx++;
                        }
                        cy += h;
                    }
                }
            }
            if (old_hover != g_editor.menu_hover_item)
                InvalidateRect(hwnd, NULL, FALSE);
        }



        if (g_editor.scrollbar_dragging) {
            Document *doc = current_doc();
            if (doc) {
                int edit_y = DPI(TITLEBAR_H + MENUBAR_H + TABBAR_H);
                int edit_h = g_editor.client_h - edit_y - DPI(STATUSBAR_H);
                int drag_vlines = (int)((doc->mode == MODE_PROSE && doc->wc.count > 0) ? doc->wc.count : doc->lc.count);
                int total_h = drag_vlines * g_editor.line_height;
                if (total_h > edit_h) {
                    int thumb_h = (edit_h * edit_h) / total_h;
                    if (thumb_h < 30) thumb_h = 30;
                    int max_scroll = total_h - edit_h;
                    int new_thumb_y = my - g_editor.scrollbar_drag_offset;
                    int min_thumb_y = edit_y;
                    int max_thumb_y = edit_y + edit_h - thumb_h;
                    if (new_thumb_y < min_thumb_y) new_thumb_y = min_thumb_y;
                    if (new_thumb_y > max_thumb_y) new_thumb_y = max_thumb_y;
                    float ratio = (max_thumb_y > min_thumb_y)
                        ? (float)(new_thumb_y - min_thumb_y) / (float)(max_thumb_y - min_thumb_y)
                        : 0.0f;
                    doc->scroll_y = (int)(ratio * max_scroll);
                    doc->target_scroll_y = doc->scroll_y;
                }
            }
            InvalidateRect(hwnd, NULL, FALSE);
            g_editor.scroll_only_repaint = 1;
            return 0;
        }

        if (g_editor.titlebar_dragging) {
            POINT pt;
            GetCursorPos(&pt);
            SetWindowPos(hwnd, NULL,
                pt.x - g_editor.drag_start.x,
                pt.y - g_editor.drag_start.y,
                0, 0, SWP_NOSIZE | SWP_NOZORDER);
            return 0;
        }

        if (g_editor.mouse_captured && (wParam & MK_LBUTTON)) {
            /* Clamp mx to the text editing area to avoid minimap coordinates */
            int clamp_mx = mx;
            int max_text_x = g_editor.client_w - DPI(SCROLLBAR_W);
            if (g_editor.show_minimap) max_text_x -= DPI(MINIMAP_W);
            if (clamp_mx > max_text_x) clamp_mx = max_text_x;
            bpos pos = mouse_to_pos(clamp_mx, my);
            Document *doc = current_doc();
            if (doc) {
                if (doc->sel_anchor < 0) doc->sel_anchor = doc->cursor;
                doc->cursor = pos;
                editor_ensure_cursor_visible();
                InvalidateRect(hwnd, NULL, FALSE);
            }

            /* Start/stop auto-scroll timer when dragging above/below editor */
            int edit_top = DPI(TITLEBAR_H + MENUBAR_H + TABBAR_H);
            int edit_bot = g_editor.client_h - DPI(STATUSBAR_H);
            if (my < edit_top || my > edit_bot) {
                SetTimer(hwnd, TIMER_DRAG_SCROLL, 30, NULL);
            } else {
                KillTimer(hwnd, TIMER_DRAG_SCROLL);
            }
        }

        /* Track titlebar button hover */
        {
            int old_hover = g_editor.titlebar_hover_btn;
            g_editor.titlebar_hover_btn = 0;
            if (my < DPI(TITLEBAR_H)) {
                int bw = DPI(46);
                int bx = g_editor.client_w - bw * 3;
                if (mx >= bx && mx < bx + bw)      g_editor.titlebar_hover_btn = 1;
                else if (mx >= bx + bw && mx < bx + bw * 2) g_editor.titlebar_hover_btn = 2;
                else if (mx >= bx + bw * 2 && mx < bx + bw * 3) g_editor.titlebar_hover_btn = 3;
            }
            if (g_editor.titlebar_hover_btn != old_hover) {
                RECT tbar = { 0, 0, g_editor.client_w, DPI(TITLEBAR_H) };
                InvalidateRect(hwnd, &tbar, FALSE);
                /* Track mouse leave to clear hover */
                TRACKMOUSEEVENT tme = { sizeof(tme), TME_LEAVE, hwnd, 0 };
                TrackMouseEvent(&tme);
            }
        }
        return 0;
    }

    case WM_LBUTTONUP: {
        if (g_editor.scrollbar_dragging) {
            g_editor.scrollbar_dragging = 0;
            ReleaseCapture();
        }
        if (g_editor.titlebar_dragging) {
            g_editor.titlebar_dragging = 0;
            ReleaseCapture();
        }
        if (g_editor.mouse_captured) {
            g_editor.mouse_captured = 0;
            KillTimer(hwnd, TIMER_DRAG_SCROLL);
            ReleaseCapture();
        }
        return 0;
    }

    case WM_MOUSELEAVE: {
        if (g_editor.titlebar_hover_btn) {
            g_editor.titlebar_hover_btn = 0;
            RECT tbar = { 0, 0, g_editor.client_w, DPI(TITLEBAR_H) };
            InvalidateRect(hwnd, &tbar, FALSE);
        }
        return 0;
    }

    case WM_KEYDOWN: {
        Document *doc = current_doc();
        if (!doc) break;

        int ctrl = GetKeyState(VK_CONTROL) & 0x8000;
        int shift = GetKeyState(VK_SHIFT) & 0x8000;
        int alt = GetKeyState(VK_MENU) & 0x8000;

        /* Swallow Backspace/Delete while search UI is active —
         * prevent them from editing the document text underneath. */
        if (g_editor.search.active && (wParam == VK_BACK || wParam == VK_DELETE)) {
            wchar_t *target = (g_editor.search.replace_active && g_editor.search.replace_focused)
                              ? g_editor.search.replace_text
                              : g_editor.search.query;
            int n = (int)wcslen(target);
            if (n > 0) target[n - 1] = 0;
            if (target == g_editor.search.query) search_update_matches();
            InvalidateRect(hwnd, NULL, FALSE);
            return 0;
        }

        /* Handle search mode input */
        if (g_editor.search.active && !ctrl) {
            if (wParam == VK_ESCAPE) {
                toggle_search();
                return 0;
            }
            if (wParam == VK_RETURN) {
                if (g_editor.search.replace_active && g_editor.search.replace_focused) {
                    if (shift) do_replace_all();
                    else do_replace();
                } else {
                    if (shift) search_prev();
                    else search_next();
                }
                InvalidateRect(hwnd, NULL, FALSE);
                return 0;
            }
            if (wParam == VK_TAB) {
                if (!g_editor.search.replace_active) {
                    /* Show replace field and focus it */
                    g_editor.search.replace_active = 1;
                    g_editor.search.replace_focused = 1;
                } else {
                    /* Toggle focus between search and replace fields */
                    g_editor.search.replace_focused = !g_editor.search.replace_focused;
                }
                InvalidateRect(hwnd, NULL, FALSE);
                return 0;
            }
            if (wParam == VK_UP || wParam == VK_LEFT) {
                search_prev();
                InvalidateRect(hwnd, NULL, FALSE);
                return 0;
            }
            if (wParam == VK_DOWN || wParam == VK_RIGHT) {
                search_next();
                InvalidateRect(hwnd, NULL, FALSE);
                return 0;
            }
            /* Let WM_CHAR handle text input for search */
        }

        switch (wParam) {
        case VK_LEFT:
            if (ctrl) {
                editor_move_cursor(word_start(&doc->gb, doc->cursor), shift);
            } else if (!shift && has_selection(doc)) {
                /* Collapse selection to its start */
                bpos sel_s = selection_start(doc);
                doc->sel_anchor = -1;
                doc->cursor = sel_s;
            } else {
                editor_move_cursor(doc->cursor - 1, shift);
            }
            doc->desired_col = -1;
            editor_ensure_cursor_visible();
            InvalidateRect(hwnd, NULL, FALSE);
            return 0;

        case VK_RIGHT:
            if (ctrl) {
                editor_move_cursor(word_end(&doc->gb, doc->cursor), shift);
            } else if (!shift && has_selection(doc)) {
                /* Collapse selection to its end */
                bpos sel_e = selection_end(doc);
                doc->sel_anchor = -1;
                doc->cursor = sel_e;
            } else {
                editor_move_cursor(doc->cursor + 1, shift);
            }
            doc->desired_col = -1;
            editor_ensure_cursor_visible();
            InvalidateRect(hwnd, NULL, FALSE);
            return 0;

        case VK_UP: {
            /* Alt+Up: move current line up */
            if (alt && doc->mode == MODE_CODE) {
                bpos line = pos_to_line(doc, doc->cursor);
                if (line > 0) {
                    bpos ls = lc_line_start(&doc->lc, line);
                    bpos le = lc_line_end(&doc->lc, &doc->gb, line);
                    int has_nl = (le < gb_length(&doc->gb) && gb_char_at(&doc->gb, le) == L'\n');
                    int full_len = le - ls + (has_nl ? 1 : 0);
                    wchar_t *line_text = gb_extract_alloc(&doc->gb, ls, full_len);
                    if (line_text) {
                        bpos old_cursor = doc->cursor;
                        bpos col = doc->cursor - ls;
                        int group = ++doc->undo.next_group;

                        /* Capture prev_ls before deleting (line cache will be stale after) */
                        bpos prev_ls = lc_line_start(&doc->lc, line - 1);

                        /* Record the deletion */
                        undo_push(&doc->undo, UNDO_DELETE, ls, line_text, full_len, old_cursor, old_cursor, group);
                        gb_delete(&doc->gb, ls, full_len);

                        /* If original line had no trailing newline, add one */
                        if (!has_nl) {
                            gb_insert(&doc->gb, prev_ls, line_text, full_len);
                            undo_push(&doc->undo, UNDO_INSERT, prev_ls, line_text, full_len, old_cursor, prev_ls + col, group);
                            gb_insert(&doc->gb, prev_ls + full_len, L"\n", 1);
                            undo_push(&doc->undo, UNDO_INSERT, prev_ls + full_len, L"\n", 1, old_cursor, prev_ls + col, group);
                        } else {
                            gb_insert(&doc->gb, prev_ls, line_text, full_len);
                            undo_push(&doc->undo, UNDO_INSERT, prev_ls, line_text, full_len, old_cursor, prev_ls + col, group);
                        }
                        doc->cursor = prev_ls + col;
                        doc->sel_anchor = -1;
                        doc->modified = 1;
                        recalc_lines(doc);
                        free(line_text);
                    }
                }
                editor_ensure_cursor_visible();
                InvalidateRect(hwnd, NULL, FALSE);
                return 0;
            }
            if (doc->mode == MODE_PROSE && doc->wc.count > 0) {
                bpos vl = wc_visual_line_of(&doc->wc, doc->cursor);
                bpos vcol = (doc->desired_col >= 0) ? doc->desired_col
                           : wc_col_in_vline(&doc->wc, doc->cursor, vl);
                if (vl > 0) {
                    int prev_start = doc->wc.entries[vl - 1].pos;
                    bpos prev_end = wc_visual_line_end(&doc->wc, &doc->gb, &doc->lc, vl - 1);
                    int prev_len = prev_end - prev_start;
                    int target_col = (vcol < prev_len) ? vcol : prev_len;
                    editor_move_cursor(prev_start + target_col, shift);
                    doc->desired_col = vcol;
                }
            } else {
                bpos line = pos_to_line(doc, doc->cursor);
                if (doc->mode == MODE_CODE) {
                    /* Use visual columns (tab=4) for stable vertical navigation */
                    bpos vcol = (doc->desired_col >= 0) ? doc->desired_col : pos_to_visual_col(doc, doc->cursor);
                    if (line > 0) {
                        editor_move_cursor(visual_col_to_pos(doc, line - 1, vcol), shift);
                        doc->desired_col = vcol;
                    }
                } else {
                    bpos col = (doc->desired_col >= 0) ? doc->desired_col : pos_to_col(doc, doc->cursor);
                    if (line > 0) {
                        editor_move_cursor(line_col_to_pos(doc, line - 1, col), shift);
                        doc->desired_col = col;
                    }
                }
            }
            editor_ensure_cursor_visible();
            InvalidateRect(hwnd, NULL, FALSE);
            return 0;
        }

        case VK_DOWN: {
            /* Alt+Down: move current line down */
            if (alt && doc->mode == MODE_CODE) {
                bpos line = pos_to_line(doc, doc->cursor);
                if (line < doc->lc.count - 1) {
                    bpos ls = lc_line_start(&doc->lc, line);
                    bpos le = lc_line_end(&doc->lc, &doc->gb, line);
                    int has_nl = (le < gb_length(&doc->gb) && gb_char_at(&doc->gb, le) == L'\n');
                    int full_len = le - ls + (has_nl ? 1 : 0);
                    wchar_t *line_text = gb_extract_alloc(&doc->gb, ls, full_len);
                    if (line_text) {
                        bpos old_cursor = doc->cursor;
                        bpos col = doc->cursor - ls;
                        int group = ++doc->undo.next_group;

                        /* Record and perform the deletion */
                        undo_push(&doc->undo, UNDO_DELETE, ls, line_text, full_len, old_cursor, old_cursor, group);
                        gb_delete(&doc->gb, ls, full_len);
                        recalc_lines(doc);

                        /* After deletion, target line shifted up by 1 */
                        bpos new_ls = lc_line_start(&doc->lc, line);
                        bpos new_le = lc_line_end(&doc->lc, &doc->gb, line);
                        int target_has_nl = (new_le < gb_length(&doc->gb) && gb_char_at(&doc->gb, new_le) == L'\n');
                        int insert_pos = new_le + (target_has_nl ? 1 : 0);

                        /* If inserting at end without newline, prepend one */
                        if (!target_has_nl) {
                            gb_insert(&doc->gb, insert_pos, L"\n", 1);
                            undo_push(&doc->undo, UNDO_INSERT, insert_pos, L"\n", 1, old_cursor, insert_pos + 1 + col, group);
                            insert_pos++;
                        }
                        gb_insert(&doc->gb, insert_pos, line_text, full_len);
                        undo_push(&doc->undo, UNDO_INSERT, insert_pos, line_text, full_len, old_cursor, insert_pos + col, group);

                        doc->cursor = insert_pos + col;
                        doc->sel_anchor = -1;
                        doc->modified = 1;
                        recalc_lines(doc);
                        free(line_text);
                    }
                }
                editor_ensure_cursor_visible();
                InvalidateRect(hwnd, NULL, FALSE);
                return 0;
            }
            if (doc->mode == MODE_PROSE && doc->wc.count > 0) {
                bpos vl = wc_visual_line_of(&doc->wc, doc->cursor);
                bpos vcol = (doc->desired_col >= 0) ? doc->desired_col
                           : wc_col_in_vline(&doc->wc, doc->cursor, vl);
                if (vl < doc->wc.count - 1) {
                    int next_start = doc->wc.entries[vl + 1].pos;
                    bpos next_end = wc_visual_line_end(&doc->wc, &doc->gb, &doc->lc, vl + 1);
                    int next_len = next_end - next_start;
                    int target_col = (vcol < next_len) ? vcol : next_len;
                    editor_move_cursor(next_start + target_col, shift);
                    doc->desired_col = vcol;
                }
            } else {
                bpos line = pos_to_line(doc, doc->cursor);
                if (doc->mode == MODE_CODE) {
                    bpos vcol = (doc->desired_col >= 0) ? doc->desired_col : pos_to_visual_col(doc, doc->cursor);
                    if (line < doc->lc.count - 1) {
                        editor_move_cursor(visual_col_to_pos(doc, line + 1, vcol), shift);
                        doc->desired_col = vcol;
                    }
                } else {
                    bpos col = (doc->desired_col >= 0) ? doc->desired_col : pos_to_col(doc, doc->cursor);
                    if (line < doc->lc.count - 1) {
                        editor_move_cursor(line_col_to_pos(doc, line + 1, col), shift);
                        doc->desired_col = col;
                    }
                }
            }
            editor_ensure_cursor_visible();
            InvalidateRect(hwnd, NULL, FALSE);
            return 0;
        }

        case VK_HOME:
            if (ctrl) {
                editor_move_cursor(0, shift);
            } else {
                /* Smart Home: toggle between first non-whitespace and line start */
                int home_ls;
                if (doc->mode == MODE_PROSE && doc->wc.count > 0) {
                    bpos vl = wc_visual_line_of(&doc->wc, doc->cursor);
                    home_ls = doc->wc.entries[vl].pos;
                } else {
                    bpos line = pos_to_line(doc, doc->cursor);
                    home_ls = lc_line_start(&doc->lc, line);
                }
                /* Find first non-whitespace on this line */
                int first_nws = home_ls;
                bpos text_len = gb_length(&doc->gb);
                while (first_nws < text_len) {
                    wchar_t c = gb_char_at(&doc->gb, first_nws);
                    if (c == L'\n' || (c != L' ' && c != L'\t')) break;
                    first_nws++;
                }
                /* If already at first non-ws, go to column 0; otherwise go to first non-ws */
                if (doc->cursor == first_nws || first_nws == home_ls)
                    editor_move_cursor(home_ls, shift);
                else
                    editor_move_cursor(first_nws, shift);
            }
            doc->desired_col = -1;
            editor_ensure_cursor_visible();
            InvalidateRect(hwnd, NULL, FALSE);
            return 0;

        case VK_END:
            if (ctrl) {
                editor_move_cursor(gb_length(&doc->gb), shift);
            } else if (doc->mode == MODE_PROSE && doc->wc.count > 0) {
                bpos vl = wc_visual_line_of(&doc->wc, doc->cursor);
                editor_move_cursor(wc_visual_line_end(&doc->wc, &doc->gb, &doc->lc, vl), shift);
            } else {
                bpos line = pos_to_line(doc, doc->cursor);
                editor_move_cursor(lc_line_end(&doc->lc, &doc->gb, line), shift);
            }
            doc->desired_col = -1;
            editor_ensure_cursor_visible();
            InvalidateRect(hwnd, NULL, FALSE);
            return 0;

        case VK_PRIOR: { /* Page Up */
            int visible_lines = (g_editor.client_h - DPI(TITLEBAR_H + MENUBAR_H + TABBAR_H) - DPI(STATUSBAR_H)) / g_editor.line_height;
            if (doc->mode == MODE_PROSE && doc->wc.count > 0) {
                bpos vl = wc_visual_line_of(&doc->wc, doc->cursor);
                int target_vl = vl - visible_lines;
                if (target_vl < 0) target_vl = 0;
                editor_move_cursor(doc->wc.entries[target_vl].pos, shift);
            } else {
                bpos line = pos_to_line(doc, doc->cursor);
                if (doc->mode == MODE_CODE) {
                    bpos vcol = pos_to_visual_col(doc, doc->cursor);
                    editor_move_cursor(visual_col_to_pos(doc, line - visible_lines, vcol), shift);
                } else {
                    bpos col = pos_to_col(doc, doc->cursor);
                    editor_move_cursor(line_col_to_pos(doc, line - visible_lines, col), shift);
                }
            }
            editor_ensure_cursor_visible();
            InvalidateRect(hwnd, NULL, FALSE);
            return 0;
        }

        case VK_NEXT: { /* Page Down */
            int visible_lines = (g_editor.client_h - DPI(TITLEBAR_H + MENUBAR_H + TABBAR_H) - DPI(STATUSBAR_H)) / g_editor.line_height;
            if (doc->mode == MODE_PROSE && doc->wc.count > 0) {
                bpos vl = wc_visual_line_of(&doc->wc, doc->cursor);
                int target_vl = vl + visible_lines;
                if (target_vl >= doc->wc.count) target_vl = doc->wc.count - 1;
                editor_move_cursor(doc->wc.entries[target_vl].pos, shift);
            } else {
                bpos line = pos_to_line(doc, doc->cursor);
                if (doc->mode == MODE_CODE) {
                    bpos vcol = pos_to_visual_col(doc, doc->cursor);
                    editor_move_cursor(visual_col_to_pos(doc, line + visible_lines, vcol), shift);
                } else {
                    bpos col = pos_to_col(doc, doc->cursor);
                    editor_move_cursor(line_col_to_pos(doc, line + visible_lines, col), shift);
                }
            }
            editor_ensure_cursor_visible();
            InvalidateRect(hwnd, NULL, FALSE);
            return 0;
        }

        case VK_DELETE:
            if (shift && has_selection(doc)) {
                editor_cut();
            } else if (ctrl) {
                /* Ctrl+Delete: delete to end of word */
                if (!has_selection(doc)) {
                    bpos text_len = gb_length(&doc->gb);
                    if (doc->cursor >= text_len) return 0;
                    bpos end = word_end(&doc->gb, doc->cursor);
                    if (end == doc->cursor && doc->cursor < text_len) end++;
                    if (end > text_len) end = text_len;
                    doc->sel_anchor = doc->cursor;
                    doc->cursor = end;
                }
                editor_delete_selection();
            } else {
                editor_delete_forward();
            }
            editor_ensure_cursor_visible();
            InvalidateRect(hwnd, NULL, FALSE);
            return 0;

        case VK_BACK:
            if (ctrl) {
                /* Ctrl+Backspace: delete to start of word */
                if (!has_selection(doc)) {
                    if (doc->cursor <= 0) return 0;
                    bpos start = word_start(&doc->gb, doc->cursor);
                    if (start == doc->cursor && doc->cursor > 0) start--;
                    if (start < 0) start = 0;
                    doc->sel_anchor = doc->cursor;
                    doc->cursor = start;
                }
                editor_delete_selection();
            } else {
                editor_backspace();
            }
            editor_ensure_cursor_visible();
            InvalidateRect(hwnd, NULL, FALSE);
            return 0;

        case VK_RETURN:
            if (g_editor.search.active) return 0;
            editor_insert_char(L'\n');
            /* Auto-indent in code mode */
            if (doc->mode == MODE_CODE) {
                bpos line = pos_to_line(doc, doc->cursor) - 1;
                if (line >= 0) {
                    bpos ls = lc_line_start(&doc->lc, line);
                    bpos le = lc_line_end(&doc->lc, &doc->gb, line);
                    wchar_t indent[64];
                    int ic = 0;
                    for (int i = ls; i < le && ic < 63; i++) {
                        wchar_t c = gb_char_at(&doc->gb, i);
                        if (c == L' ' || c == L'\t') indent[ic++] = c;
                        else break;
                    }
                    if (ic > 0) {
                        indent[ic] = 0;
                        editor_insert_text(indent, ic);
                    }
                    /* Increase indent if prev line ends with { : ( */
                    if (le > ls) {
                        wchar_t last = gb_char_at(&doc->gb, le - 1);
                        if (last == L'{' || last == L':' || last == L'(') {
                            editor_insert_text(L"    ", 4);
                        }
                    }
                }
            }
            editor_ensure_cursor_visible();
            InvalidateRect(hwnd, NULL, FALSE);
            return 0;

        case VK_TAB:
            if (g_editor.search.active) return 0;
            if (doc->mode == MODE_CODE && has_selection(doc)) {
                /* Block indent/de-indent selected lines */
                bpos s = selection_start(doc);
                bpos e = selection_end(doc);
                bpos first_line = pos_to_line(doc, s);
                bpos last_line = pos_to_line(doc, e);
                if (e == lc_line_start(&doc->lc, last_line) && last_line > first_line)
                    last_line--; /* don't indent line if selection ends at its start */
                int group = ++doc->undo.next_group;
                if (shift) {
                    /* Shift+Tab: de-indent */
                    bpos offset = 0;
                    for (bpos ln = first_line; ln <= last_line; ln++) {
                        bpos ls = lc_line_start(&doc->lc, ln) + offset;
                        int spaces = 0;
                        while (spaces < 4 && gb_char_at(&doc->gb, ls + spaces) == L' ') spaces++;
                        if (spaces > 0) {
                            wchar_t *del = gb_extract_alloc(&doc->gb, ls, spaces);
                            if (del) {
                                undo_push(&doc->undo, UNDO_DELETE, ls, del, spaces, doc->cursor, doc->cursor, group);
                                free(del);
                            }
                            gb_delete(&doc->gb, ls, spaces);
                            offset -= spaces;
                        }
                    }
                    doc->cursor += offset;
                    if (doc->cursor < 0) doc->cursor = 0;
                    doc->sel_anchor = lc_line_start(&doc->lc, first_line);
                } else {
                    /* Tab: indent */
                    bpos offset = 0;
                    for (bpos ln = first_line; ln <= last_line; ln++) {
                        bpos ls = lc_line_start(&doc->lc, ln) + offset;
                        gb_insert(&doc->gb, ls, L"    ", 4);
                        undo_push(&doc->undo, UNDO_INSERT, ls, L"    ", 4, doc->cursor, doc->cursor, group);
                        offset += 4;
                    }
                    doc->cursor += offset;
                    doc->sel_anchor = lc_line_start(&doc->lc, first_line);
                }
                doc->modified = 1;
                recalc_lines(doc);
                update_stats(doc);
            } else if (doc->mode == MODE_CODE) {
                editor_insert_text(L"    ", 4);
            } else {
                editor_insert_char(L'\t');
            }
            editor_ensure_cursor_visible();
            InvalidateRect(hwnd, NULL, FALSE);
            return 0;

        case VK_ESCAPE:
            if (g_editor.menu_open >= 0) {
                g_editor.menu_open = -1;
                g_editor.menu_hover_item = -1;
                InvalidateRect(hwnd, NULL, FALSE);
                return 0;
            }
            if (g_editor.show_stats_screen) {
                g_editor.show_stats_screen = 0;
                InvalidateRect(hwnd, NULL, FALSE);
            } else if (g_editor.search.active) {
                toggle_search();
                InvalidateRect(hwnd, NULL, FALSE);
            } else if (g_editor.focus.active) {
                toggle_focus_mode();
                InvalidateRect(hwnd, NULL, FALSE);
            } else if (has_selection(doc)) {
                doc->sel_anchor = -1;
                InvalidateRect(hwnd, NULL, FALSE);
            }
            return 0;

        /* Ctrl shortcuts */
        case 'A': if (ctrl) { editor_select_all(); InvalidateRect(hwnd, NULL, FALSE); return 0; } break;
        case 'C': if (ctrl) { editor_copy(); return 0; } break;
        case 'X': if (ctrl) { editor_cut(); InvalidateRect(hwnd, NULL, FALSE); return 0; } break;
        case 'V': if (ctrl) { editor_paste(); editor_ensure_cursor_visible(); InvalidateRect(hwnd, NULL, FALSE); return 0; } break;
        case 'Z': if (ctrl) { if (shift) editor_redo(); else editor_undo(); editor_ensure_cursor_visible(); InvalidateRect(hwnd, NULL, FALSE); return 0; } break;
        case 'Y': if (ctrl) { editor_redo(); editor_ensure_cursor_visible(); InvalidateRect(hwnd, NULL, FALSE); return 0; } break;
        case 'S': if (ctrl) { save_current_file(); InvalidateRect(hwnd, NULL, FALSE); return 0; } break;
        case 'O': if (ctrl) { open_file_dialog(); InvalidateRect(hwnd, NULL, FALSE); return 0; } break;
        case 'N': if (ctrl) { new_tab(); InvalidateRect(hwnd, NULL, FALSE); return 0; } break;
        case 'W': if (ctrl) { close_tab(g_editor.active_tab); InvalidateRect(hwnd, NULL, FALSE); return 0; } break;
        case 'F': if (ctrl) { toggle_search(); InvalidateRect(hwnd, NULL, FALSE); return 0; } break;
        case 'H': if (ctrl) { g_editor.search.active = 1; g_editor.search.replace_active = 1; InvalidateRect(hwnd, NULL, FALSE); return 0; } break;
        case 'G': if (ctrl) { search_next(); InvalidateRect(hwnd, NULL, FALSE); return 0; } break;
        case 'M': if (ctrl) { if (shift) { g_editor.show_minimap = !g_editor.show_minimap; } else { toggle_mode(); } InvalidateRect(hwnd, NULL, FALSE); return 0; } break;
        case 'D': if (ctrl) {
            if (shift && doc->mode == MODE_CODE) {
                /* Ctrl+Shift+D: Duplicate line */
                bpos line = pos_to_line(doc, doc->cursor);
                bpos ls = lc_line_start(&doc->lc, line);
                bpos le = lc_line_end(&doc->lc, &doc->gb, line);
                bpos ll = le - ls;
                wchar_t *dup = gb_extract_alloc(&doc->gb, ls, ll);
                if (dup) {
                    bpos old_cursor = doc->cursor;
                    int group = ++doc->undo.next_group;
                    bpos insert_at;
                    int is_last = (line + 1 >= doc->lc.count);

                    if (is_last) {
                        /* Last line: insert \n then duplicate at end */
                        insert_at = le;
                        gb_insert(&doc->gb, insert_at, L"\n", 1);
                        undo_push(&doc->undo, UNDO_INSERT, insert_at, L"\n", 1, old_cursor, insert_at + 1 + (doc->cursor - ls), group);
                        gb_insert(&doc->gb, insert_at + 1, dup, ll);
                        undo_push(&doc->undo, UNDO_INSERT, insert_at + 1, dup, ll, old_cursor, insert_at + 1 + (doc->cursor - ls), group);
                    } else {
                        /* Non-last line: insert after the existing \n (at le + 1, which is start of next line) */
                        insert_at = le + 1;
                        gb_insert(&doc->gb, insert_at, dup, ll);
                        undo_push(&doc->undo, UNDO_INSERT, insert_at, dup, ll, old_cursor, insert_at + (doc->cursor - ls), group);
                        gb_insert(&doc->gb, insert_at + ll, L"\n", 1);
                        undo_push(&doc->undo, UNDO_INSERT, insert_at + ll, L"\n", 1, old_cursor, insert_at + (doc->cursor - ls), group);
                    }

                    doc->cursor = (is_last ? le + 1 : insert_at) + (doc->cursor - ls);
                    doc->modified = 1;
                    recalc_lines(doc);
                    free(dup);
                }
            } else {
                toggle_focus_mode();
            }
            InvalidateRect(hwnd, NULL, FALSE); return 0;
        } break;
        case 'K': if (ctrl && shift && doc->mode == MODE_CODE) {
            /* Ctrl+Shift+K: Delete entire line */
            bpos line = pos_to_line(doc, doc->cursor);
            bpos ls = lc_line_start(&doc->lc, line);
            bpos le = lc_line_end(&doc->lc, &doc->gb, line);
            int del_end = le;
            if (del_end < gb_length(&doc->gb) && gb_char_at(&doc->gb, del_end) == L'\n') del_end++;
            if (del_end > ls) {
                bpos old_cursor = doc->cursor;
                int del_len = del_end - ls;
                wchar_t *deleted = gb_extract_alloc(&doc->gb, ls, del_len);
                if (deleted) {
                    undo_push(&doc->undo, UNDO_DELETE, ls, deleted, del_len, old_cursor, ls, 0);
                    free(deleted);
                }
                gb_delete(&doc->gb, ls, del_len);
                doc->cursor = ls;
                if (doc->cursor > gb_length(&doc->gb)) doc->cursor = gb_length(&doc->gb);
                doc->sel_anchor = -1;
                doc->modified = 1;
                recalc_lines(doc);
            }
            editor_ensure_cursor_visible();
            InvalidateRect(hwnd, NULL, FALSE);
            return 0;
        } break;
        case 'I': if (ctrl) { g_editor.show_stats_screen = !g_editor.show_stats_screen; InvalidateRect(hwnd, NULL, FALSE); return 0; } break;
        case 'T': if (ctrl) { apply_theme(g_theme_index == 0 ? 1 : 0); return 0; } break;
        case VK_F7: g_editor.spellcheck_enabled = !g_editor.spellcheck_enabled; InvalidateRect(hwnd, NULL, FALSE); return 0;
        }

        /* Ctrl+Tab */
        if (wParam == VK_TAB && ctrl) {
            if (shift) {
                g_editor.active_tab = (g_editor.active_tab - 1 + g_editor.tab_count) % g_editor.tab_count;
            } else {
                g_editor.active_tab = (g_editor.active_tab + 1) % g_editor.tab_count;
            }
            InvalidateRect(hwnd, NULL, FALSE);
            return 0;
        }

        /* Ctrl+/ : Toggle line comment (code mode) */
        if (ctrl && wParam == VK_OEM_2 && doc->mode == MODE_CODE) {
            bpos line = pos_to_line(doc, doc->cursor);
            bpos ls = lc_line_start(&doc->lc, line);
            bpos old_cursor = doc->cursor;
            /* Check if line starts with // (possibly after whitespace) */
            int first_nonws = ls;
            while (first_nonws < gb_length(&doc->gb) &&
                   (gb_char_at(&doc->gb, first_nonws) == L' ' || gb_char_at(&doc->gb, first_nonws) == L'\t'))
                first_nonws++;
            int has_comment = (first_nonws + 1 < gb_length(&doc->gb) &&
                               gb_char_at(&doc->gb, first_nonws) == L'/' &&
                               gb_char_at(&doc->gb, first_nonws + 1) == L'/');
            if (has_comment) {
                /* Remove // (and optional trailing space) */
                int del_len = 2;
                if (first_nonws + 2 < gb_length(&doc->gb) && gb_char_at(&doc->gb, first_nonws + 2) == L' ')
                    del_len = 3;
                wchar_t *deleted = gb_extract_alloc(&doc->gb, first_nonws, del_len);
                if (deleted) {
                    bpos new_cursor = doc->cursor - ((doc->cursor > first_nonws) ? ((doc->cursor - first_nonws < del_len) ? doc->cursor - first_nonws : del_len) : 0);
                    undo_push(&doc->undo, UNDO_DELETE, first_nonws, deleted, del_len, old_cursor, new_cursor, 0);
                    free(deleted);
                }
                gb_delete(&doc->gb, first_nonws, del_len);
                doc->cursor -= (doc->cursor > first_nonws) ? ((doc->cursor - first_nonws < del_len) ? doc->cursor - first_nonws : del_len) : 0;
            } else {
                /* Insert // at first non-whitespace */
                gb_insert(&doc->gb, first_nonws, L"// ", 3);
                bpos new_cursor = doc->cursor + ((doc->cursor >= first_nonws) ? 3 : 0);
                undo_push(&doc->undo, UNDO_INSERT, first_nonws, L"// ", 3, old_cursor, new_cursor, 0);
                doc->cursor = new_cursor;
            }
            doc->modified = 1;
            recalc_lines(doc);
            editor_ensure_cursor_visible();
            InvalidateRect(hwnd, NULL, FALSE);
            return 0;
        }

        /* Font size: Ctrl+= / Ctrl+- */
        if (ctrl && (wParam == VK_OEM_PLUS || wParam == VK_ADD)) {
            g_editor.font_size += 2;
            if (g_editor.font_size > 48) g_editor.font_size = 48;
            PostMessage(hwnd, WM_USER + 1, 0, 0); /* recreate fonts */
            return 0;
        }
        if (ctrl && (wParam == VK_OEM_MINUS || wParam == VK_SUBTRACT)) {
            g_editor.font_size -= 2;
            if (g_editor.font_size < 8) g_editor.font_size = 8;
            PostMessage(hwnd, WM_USER + 1, 0, 0);
            return 0;
        }

        break;
    }

    case WM_CHAR: {
        /* Swallow all text input while stats overlay is open */
        if (g_editor.show_stats_screen) return 0;

        wchar_t c = (wchar_t)wParam;

        /* Handle search input */
        if (g_editor.search.active) {
            if (c == 27 || c == '\r' || c == '\t') return 0; /* handled in WM_KEYDOWN */
            int ctrl = GetKeyState(VK_CONTROL) & 0x8000;
            if (ctrl) return 0;

            /* Direct input to the focused field */
            wchar_t *target;
            int max_len = 255;
            if (g_editor.search.replace_active && g_editor.search.replace_focused) {
                target = g_editor.search.replace_text;
            } else {
                target = g_editor.search.query;
            }
            int slen = (int)wcslen(target);

            if (c == 8) { /* backspace */
                if (slen > 0) target[slen - 1] = 0;
            } else if (c >= 32) {
                if (slen < max_len) {
                    target[slen] = c;
                    target[slen + 1] = 0;
                }
            }
            /* Only update matches when search query changes */
            if (target == g_editor.search.query)
                search_update_matches();
            InvalidateRect(hwnd, NULL, FALSE);
            return 0;
        }

        /* Normal text input */
        if (c >= 32 && !(GetKeyState(VK_CONTROL) & 0x8000)) {
            editor_insert_char(c);
            editor_ensure_cursor_visible();
            g_editor.cursor_visible = 1; g_editor.cursor_last_active = GetTickCount();
            InvalidateRect(hwnd, NULL, FALSE);
        }
        return 0;
    }

    case WM_USER + 1: {
        /* Recreate fonts after size change */
        DeleteObject(g_editor.font_main);
        DeleteObject(g_editor.font_bold);
        DeleteObject(g_editor.font_italic);

        g_editor.font_main = CreateFontW(
            -DPI(g_editor.font_size), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_TT_PRECIS, CLIP_DEFAULT_PRECIS,
            CLEARTYPE_QUALITY, FIXED_PITCH | FF_MODERN, L"Consolas");
        g_editor.font_bold = CreateFontW(
            -DPI(g_editor.font_size), 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_TT_PRECIS, CLIP_DEFAULT_PRECIS,
            CLEARTYPE_QUALITY, FIXED_PITCH | FF_MODERN, L"Consolas");
        g_editor.font_italic = CreateFontW(
            -DPI(g_editor.font_size), 0, 0, 0, FW_NORMAL, TRUE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_TT_PRECIS, CLIP_DEFAULT_PRECIS,
            CLEARTYPE_QUALITY, FIXED_PITCH | FF_MODERN, L"Consolas");

        HDC hdc = GetDC(hwnd);
        HFONT old = (HFONT)SelectObject(hdc, g_editor.font_main);
        TEXTMETRICW tm;
        GetTextMetricsW(hdc, &tm);
        g_editor.char_width = tm.tmAveCharWidth;
        g_editor.line_height = tm.tmHeight + tm.tmExternalLeading + 4;
        SelectObject(hdc, old);
        ReleaseDC(hwnd, hdc);

        InvalidateRect(hwnd, NULL, FALSE);
        return 0;
    }

    case WM_NCHITTEST: {
        /* Allow resizing from edges, but not over the scrollbar area */
        LRESULT hit = DefWindowProcW(hwnd, msg, wParam, lParam);
        if (hit == HTCLIENT) {
            POINT pt = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
            ScreenToClient(hwnd, &pt);
            int border = 6;
            RECT rc;
            GetClientRect(hwnd, &rc);

            /* Don't report resize hits when mouse is in the scrollbar zone of the editor area */
            int in_editor_y = (pt.y >= DPI(TITLEBAR_H + MENUBAR_H + TABBAR_H) && pt.y < rc.bottom - DPI(STATUSBAR_H));
            int in_scrollbar_x = (pt.x >= rc.right - DPI(SCROLLBAR_W) - border);

            if (pt.y < border) {
                if (pt.x < border) return HTTOPLEFT;
                if (pt.x >= rc.right - border) return HTTOPRIGHT;
                return HTTOP;
            }
            if (pt.y >= rc.bottom - border) {
                if (pt.x < border) return HTBOTTOMLEFT;
                if (pt.x >= rc.right - border) return HTBOTTOMRIGHT;
                return HTBOTTOM;
            }
            if (pt.x < border) return HTLEFT;
            /* Only report right-edge resize if NOT in the scrollbar region of the editor */
            if (pt.x >= rc.right - border && !in_editor_y) return HTRIGHT;
            if (pt.x >= rc.right - border && in_editor_y && !in_scrollbar_x) return HTRIGHT;
        }
        return hit;
    }

    case WM_GETMINMAXINFO: {
        MINMAXINFO *mmi = (MINMAXINFO *)lParam;
        mmi->ptMinTrackSize.x = 600;
        mmi->ptMinTrackSize.y = 400;
        return 0;
    }

    case WM_DROPFILES: {
        HDROP hDrop = (HDROP)wParam;
        wchar_t path[MAX_PATH];
        UINT count = DragQueryFileW(hDrop, 0xFFFFFFFF, NULL, 0);
        for (UINT i = 0; i < count; i++) {
            DragQueryFileW(hDrop, i, path, MAX_PATH);
            Document *doc = doc_create();
            load_file(doc, path);
            if (g_editor.tab_count < MAX_TABS) {
                g_editor.tabs[g_editor.tab_count] = doc;
                g_editor.active_tab = g_editor.tab_count;
                g_editor.tab_count++;
            } else {
                doc_free(doc);
            }
        }
        DragFinish(hDrop);
        InvalidateRect(hwnd, NULL, FALSE);
        return 0;
    }

    case WM_ACTIVATE: {
        if (LOWORD(wParam) == WA_INACTIVE) {
            /* Window going to background — slow cursor blink to ~2fps to save CPU */
            SetTimer(hwnd, TIMER_BLINK, 500, NULL);
        } else {
            /* Window regaining focus — restore normal blink rate and reset cursor */
            SetTimer(hwnd, TIMER_BLINK, 33, NULL);
            g_editor.cursor_last_active = GetTickCount();
            InvalidateRect(hwnd, NULL, FALSE);
        }
        return 0;
    }

    case WM_CLOSE: {
        /* Prompt per-tab for unsaved changes (user can cancel) */
        for (int i = 0; i < g_editor.tab_count; i++) {
            if (g_editor.tabs[i]->modified) {
                if (!prompt_save_doc(i)) return 0; /* cancelled — abort close */
            }
        }
        /* Clean exit: remove all autosave shadows */
        autosave_cleanup_all();
        autosave_cleanup_tmp();
        DestroyWindow(hwnd);
        return 0;
    }

    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }

    return DefWindowProcW(hwnd, msg, wParam, lParam);
}
