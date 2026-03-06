#include "prose_code.h"

void fill_rect(HDC hdc, int x, int y, int w, int h, COLORREF c) {
    SetDCBrushColor(hdc, c);
    RECT r = { x, y, x + w, y + h };
    FillRect(hdc, &r, (HBRUSH)GetStockObject(DC_BRUSH));
}

void fill_rounded_rect(HDC hdc, int x, int y, int w, int h, int r, COLORREF c) {
    SetDCBrushColor(hdc, c);
    SetDCPenColor(hdc, c);
    HBRUSH obr = (HBRUSH)SelectObject(hdc, GetStockObject(DC_BRUSH));
    HPEN open = (HPEN)SelectObject(hdc, GetStockObject(DC_PEN));
    RoundRect(hdc, x, y, x + w, y + h, r, r);
    SelectObject(hdc, obr);
    SelectObject(hdc, open);
}

void draw_text(HDC hdc, int x, int y, const wchar_t *text, int len, COLORREF color) {
    SetTextColor(hdc, color);
    TextOutW(hdc, x, y, text, len);
}

void render_titlebar(HDC hdc) {
    int th = DPI(TITLEBAR_H);
    fill_rect(hdc, 0, 0, g_editor.client_w, th, CLR_BG_DARK);

    SelectObject(hdc, g_editor.font_title);
    SetBkMode(hdc, TRANSPARENT);

    static const wchar_t *fab_icons[MENU_COUNT] = {
        L"\x2630", L"\x270E", L"\x2315", L"\x2699",
    };

    int fab_size = DPI(28);
    int fab_pad = DPI(6);
    int fab_start_x = DPI(8);
    int fab_y = (th - fab_size) / 2;

    SelectObject(hdc, g_editor.font_ui_small);
    for (int i = 0; i < MENU_COUNT; i++) {
        int fx = fab_start_x + i * (fab_size + fab_pad);

        COLORREF fab_bg;
        if (g_editor.menu_open == i) {
            fab_bg = CLR_ACCENT;
        } else if (g_editor.fab_hover == i) {
            fab_bg = g_theme.is_dark ? RGB(50, 50, 62) : RGB(220, 220, 225);
        } else {
            fab_bg = g_theme.is_dark ? RGB(38, 38, 46) : RGB(235, 235, 238);
        }
        fill_rounded_rect(hdc, fx, fab_y, fab_size, fab_size, fab_size / 2, fab_bg);

        COLORREF icon_clr = (g_editor.menu_open == i) ?
            (g_theme.is_dark ? RGB(255, 255, 255) : RGB(255, 255, 255)) : CLR_SUBTEXT;
        SetTextColor(hdc, icon_clr);
        RECT fr = { fx, fab_y, fx + fab_size, fab_y + fab_size };
        DrawTextW(hdc, fab_icons[i], 1, &fr, DT_CENTER | DT_VCENTER | DT_SINGLELINE);

        g_editor.menu_bar_widths[i] = fab_size + fab_pad;
    }

    int fab_total_w = MENU_COUNT * (fab_size + fab_pad);
    int title_x = fab_start_x + fab_total_w + DPI(12);

    SelectObject(hdc, g_editor.font_title);
    Document *doc = current_doc();
    if (doc) {
        wchar_t title[256];
        swprintf(title, 256, L"%ls%ls", doc->title, doc->modified ? L" \x2022" : L"");
        draw_text(hdc, title_x, (th - DPI(16)) / 2, title, (int)wcslen(title), CLR_SUBTEXT);
    }

    int bw = DPI(46), bh = th;
    int x = g_editor.client_w - bw * 3;

    if (g_editor.titlebar_hover_btn == 1)
        fill_rect(hdc, x, 0, bw, bh, g_theme.is_dark ? RGB(45, 45, 56) : RGB(210, 210, 215));
    SetTextColor(hdc, CLR_SUBTEXT);
    RECT r1 = { x, 0, x + bw, bh };
    DrawTextW(hdc, L"\x2500", 1, &r1, DT_CENTER | DT_VCENTER | DT_SINGLELINE);

    x += bw;
    if (g_editor.titlebar_hover_btn == 2)
        fill_rect(hdc, x, 0, bw, bh, g_theme.is_dark ? RGB(45, 45, 56) : RGB(210, 210, 215));
    SetTextColor(hdc, CLR_SUBTEXT);
    RECT r2 = { x, 0, x + bw, bh };
    DrawTextW(hdc, L"\x25A1", 1, &r2, DT_CENTER | DT_VCENTER | DT_SINGLELINE);

    x += bw;
    if (g_editor.titlebar_hover_btn == 3)
        fill_rect(hdc, x, 0, bw, bh, RGB(196, 43, 28));
    SetTextColor(hdc, g_editor.titlebar_hover_btn == 3 ? RGB(255, 255, 255) : CLR_SUBTEXT);
    RECT r3 = { x, 0, x + bw, bh };
    DrawTextW(hdc, L"\x2715", 1, &r3, DT_CENTER | DT_VCENTER | DT_SINGLELINE);

    fill_rect(hdc, 0, th - 1, g_editor.client_w, 1, CLR_SURFACE0);
}

void render_tabbar(HDC hdc) {
    int y = DPI(TITLEBAR_H + MENUBAR_H);
    int tbh = DPI(TABBAR_H);
    fill_rect(hdc, 0, y, g_editor.client_w, tbh, CLR_BG_DARK);

    SelectObject(hdc, g_editor.font_ui_small);
    SetBkMode(hdc, TRANSPARENT);

    int x = DPI(8);
    for (int i = 0; i < g_editor.tab_count; i++) {
        Document *doc = g_editor.tabs[i];
        wchar_t label[128];
        swprintf(label, 128, L"%ls%ls", doc->title, doc->modified ? L" \x2022" : L"");
        int tw = (int)wcslen(label) * DPI(8) + DPI(TAB_PAD) * 2;
        if (tw < DPI(TAB_MIN_W)) tw = DPI(TAB_MIN_W);
        if (tw > DPI(TAB_MAX_W)) tw = DPI(TAB_MAX_W);

        if (i == g_editor.active_tab) {
            fill_rounded_rect(hdc, x, y + DPI(5), tw, tbh - DPI(5), DPI(10), CLR_TAB_ACTIVE);
            fill_rounded_rect(hdc, x + DPI(12), y + tbh - DPI(3), tw - DPI(24), DPI(2), DPI(1), CLR_ACCENT);
            draw_text(hdc, x + DPI(TAB_PAD), y + (tbh - DPI(12)) / 2, label, (int)wcslen(label), CLR_TEXT);
        } else {
            draw_text(hdc, x + DPI(TAB_PAD), y + (tbh - DPI(12)) / 2, label, (int)wcslen(label), CLR_OVERLAY0);
        }

        draw_text(hdc, x + tw - DPI(20), y + (tbh - DPI(12)) / 2, L"\x00D7", 1, CLR_OVERLAY0);

        x += tw + DPI(4);
    }

    draw_text(hdc, x + DPI(8), y + (tbh - DPI(12)) / 2, L"+", 1, CLR_OVERLAY0);

    fill_rect(hdc, 0, y + tbh - 1, g_editor.client_w, 1, CLR_SURFACE0);
}

void render_statusbar(HDC hdc) {
    Document *doc = current_doc();
    int y = g_editor.client_h - DPI(STATUSBAR_H);
    fill_rect(hdc, 0, y, g_editor.client_w, DPI(STATUSBAR_H), CLR_BG_DARK);
    fill_rect(hdc, 0, y, g_editor.client_w, 1, CLR_SURFACE0);

    if (!doc) return;

    SelectObject(hdc, g_editor.font_ui_small);
    SetBkMode(hdc, TRANSPARENT);

    bpos line = pos_to_line(doc, doc->cursor) + 1;
    bpos col = pos_to_col(doc, doc->cursor) + 1;

    wchar_t left[256];
    swprintf(left, 256, L"  %ls  \x2502  Ln %lld, Col %lld",
             doc->mode == MODE_PROSE ? L"\x270D Prose" : L"\x2699 Code",
             (long long)line, (long long)col);
    draw_text(hdc, DPI(8), y + (DPI(STATUSBAR_H) - DPI(12)) / 2, left, (int)wcslen(left), CLR_SUBTEXT);

    wchar_t right[256];
    int reading_time = (int)((doc->word_count + 237) / 238);
    if (doc->mode == MODE_PROSE) {
        swprintf(right, 256, L"%lld words  \x2502  %lld chars  \x2502  ~%d min read  \x2502  %lld lines  ",
                 (long long)doc->word_count, (long long)doc->char_count, reading_time > 0 ? reading_time : 1, (long long)doc->line_count);
    } else {
        swprintf(right, 256, L"%lld lines  \x2502  %lld chars  ",
                 (long long)doc->line_count, (long long)doc->char_count);
    }

    SIZE sz;
    GetTextExtentPoint32W(hdc, right, (int)wcslen(right), &sz);
    draw_text(hdc, g_editor.client_w - sz.cx - DPI(8), y + (DPI(STATUSBAR_H) - DPI(12)) / 2,
              right, (int)wcslen(right), CLR_SUBTEXT);

    if (g_editor.focus.active) {
        wchar_t foc[] = L"\x2B24 Focus";
        draw_text(hdc, g_editor.client_w / 2 - 30, y + (DPI(STATUSBAR_H) - DPI(12)) / 2,
                  foc, (int)wcslen(foc), CLR_GREEN);
    }

    {
        const wchar_t *tname = g_theme_index == 0 ? L"Dark" : L"Light";
        wchar_t tbuf[32];
        swprintf(tbuf, 32, L"\x263C %ls", tname);
        SIZE tsz;
        GetTextExtentPoint32W(hdc, tbuf, (int)wcslen(tbuf), &tsz);
        draw_text(hdc, g_editor.client_w / 2 + 40, y + (DPI(STATUSBAR_H) - DPI(12)) / 2,
                  tbuf, (int)wcslen(tbuf), CLR_OVERLAY0);
    }
}

void render_searchbar(HDC hdc) {
    if (!g_editor.search.active) return;

    int bar_h = g_editor.search.replace_active ? DPI(72) : DPI(40);
    int bar_w = DPI(460);
    int x = g_editor.client_w - bar_w - DPI(24);
    int y = DPI(TITLEBAR_H + MENUBAR_H + TABBAR_H) + DPI(8);

    fill_rounded_rect(hdc, x + 2, y + 2, bar_w, bar_h, DPI(10), RGB(8, 8, 14));
    fill_rounded_rect(hdc, x, y, bar_w, bar_h, DPI(10), CLR_SURFACE0);
    HPEN pen = CreatePen(PS_SOLID, 1, CLR_SURFACE1);
    HBRUSH hollow = (HBRUSH)GetStockObject(HOLLOW_BRUSH);
    HPEN old_pen = (HPEN)SelectObject(hdc, pen);
    HBRUSH old_brush = (HBRUSH)SelectObject(hdc, hollow);
    RoundRect(hdc, x, y, x + bar_w, y + bar_h, DPI(10), DPI(10));
    SelectObject(hdc, old_pen);
    SelectObject(hdc, old_brush);
    DeleteObject(pen);

    SelectObject(hdc, g_editor.font_ui_small);
    SetBkMode(hdc, TRANSPARENT);

    draw_text(hdc, x + DPI(12), y + DPI(11), L"\x2315", 1, CLR_OVERLAY0);

    int qlen = (int)wcslen(g_editor.search.query);
    draw_text(hdc, x + DPI(32), y + DPI(12), g_editor.search.query, qlen, CLR_TEXT);

    if (!g_editor.search.replace_focused || !g_editor.search.replace_active) {
        DWORD now = GetTickCount();
        if ((now / 530) % 2 == 0) {
            SIZE qs;
            GetTextExtentPoint32W(hdc, g_editor.search.query, qlen, &qs);
            fill_rect(hdc, x + DPI(32) + qs.cx + 1, y + DPI(11), DPI(1), DPI(16), CLR_ACCENT);
        }
    }

    if (!g_editor.search.replace_focused || !g_editor.search.replace_active) {
        fill_rect(hdc, x + DPI(30), y + DPI(28), bar_w - DPI(130), 1, CLR_ACCENT);
    }

    wchar_t mc[32];
    if (g_editor.search.match_count > 0) {
        swprintf(mc, 32, L"%d/%d", g_editor.search.current_match + 1, g_editor.search.match_count);
    } else {
        wcscpy(mc, L"No results");
    }
    draw_text(hdc, x + bar_w - DPI(90), y + DPI(12), mc, (int)wcslen(mc),
              g_editor.search.match_count > 0 ? CLR_SUBTEXT : CLR_RED);

    /* Close (X) button */
    {
        int btn_size = DPI(20);
        int bx = x + bar_w - DPI(12) - btn_size;
        int by = y + DPI(10);
        draw_text(hdc, bx + DPI(4), by + DPI(1), L"\x2715", 1, CLR_OVERLAY0);
    }

    if (g_editor.search.replace_active) {
        draw_text(hdc, x + DPI(12), y + DPI(43), L"\x21C6", 1, CLR_OVERLAY0);
        int rlen = (int)wcslen(g_editor.search.replace_text);
        draw_text(hdc, x + DPI(32), y + DPI(44), g_editor.search.replace_text, rlen, CLR_TEXT);
        if (g_editor.search.replace_focused) {
            DWORD now = GetTickCount();
            if ((now / 530) % 2 == 0) {
                SIZE rs;
                GetTextExtentPoint32W(hdc, g_editor.search.replace_text, rlen, &rs);
                fill_rect(hdc, x + DPI(32) + rs.cx + 1, y + DPI(43), DPI(1), DPI(16), CLR_ACCENT);
            }
            fill_rect(hdc, x + DPI(30), y + DPI(60), bar_w - DPI(130), 1, CLR_ACCENT);
        }
    }
}

void render_editor(HDC hdc) {
    Document *doc = current_doc();
    if (!doc) return;

    int edit_x = 0;
    int edit_y = DPI(TITLEBAR_H + MENUBAR_H + TABBAR_H);
    int edit_w = g_editor.client_w - DPI(SCROLLBAR_W);
    if (g_editor.show_minimap) edit_w -= DPI(MINIMAP_W);
    int edit_h = g_editor.client_h - edit_y - DPI(STATUSBAR_H);

    fill_rect(hdc, edit_x, edit_y, g_editor.client_w, edit_h, CLR_BG);

    HRGN clip = CreateRectRgn(edit_x, edit_y, edit_x + edit_w, edit_y + edit_h);
    SelectClipRgn(hdc, clip);

    int lh = g_editor.line_height;
    int cw = g_editor.char_width;
    int gutter_w = (doc->mode == MODE_CODE) ? (LINE_NUM_CHARS * cw + DPI(GUTTER_PAD) * 2) : DPI(24);
    int text_x = edit_x + gutter_w;
    if (doc->mode == MODE_CODE) text_x -= doc->scroll_x;
    int text_w = edit_w - gutter_w;

    int use_wrap = (doc->mode == MODE_PROSE);
    if (use_wrap) {
        int wrap_col = text_w / cw;
        if (wrap_col < 20) wrap_col = 20;
        if (wrap_col != doc->wc.wrap_col || doc->lc.dirty || doc->wc.count == 0) {
            wc_rebuild(&doc->wc, &doc->gb, &doc->lc, wrap_col);
            doc->wrap_dirty = 0;
        } else {
            recalc_wrap_now(doc);
        }
    }

    int total_vlines = (int)(use_wrap ? doc->wc.count : doc->lc.count);
    if (total_vlines < 1) total_vlines = 1;

    bpos first_vline = doc->scroll_y / lh;
    bpos last_vline = (doc->scroll_y + edit_h) / lh + 1;
    if (first_vline < 0) first_vline = 0;
    if (last_vline >= total_vlines) last_vline = total_vlines - 1;

    bpos cursor_vline = use_wrap ? wc_visual_line_of(&doc->wc, doc->cursor)
                                : pos_to_line(doc, doc->cursor);
    bpos cursor_logical = pos_to_line(doc, doc->cursor);
    bpos sel_s = selection_start(doc);
    bpos sel_e = selection_end(doc);
    int has_sel = has_selection(doc);

    SelectObject(hdc, g_editor.font_main);
    SetBkMode(hdc, TRANSPARENT);

    fill_rect(hdc, edit_x, edit_y, gutter_w, edit_h, CLR_GUTTER);
    fill_rect(hdc, edit_x + gutter_w - 1, edit_y, 1, edit_h, CLR_SURFACE0);

    int match_cursor = 0;
    if (g_editor.search.active && g_editor.search.match_count > 0) {
        bpos first_pos = use_wrap ? doc->wc.entries[first_vline].pos
                                  : lc_line_start(&doc->lc, first_vline);
        int qlen = (int)wcslen(g_editor.search.query);
        int lo = 0, hi = g_editor.search.match_count - 1;
        while (lo < hi) {
            int mid = (lo + hi) / 2;
            if (g_editor.search.match_positions[mid] + qlen <= first_pos) lo = mid + 1;
            else hi = mid;
        }
        match_cursor = lo;
    }

    bpos bracket_pos1 = -1, bracket_pos2 = -1;
    if (doc->mode == MODE_CODE) {
        bracket_pos2 = find_matching_bracket(&doc->gb, doc->cursor);
        if (bracket_pos2 >= 0) {
            bracket_pos1 = doc->cursor;
        } else if (doc->cursor > 0) {
            bracket_pos2 = find_matching_bracket(&doc->gb, doc->cursor - 1);
            if (bracket_pos2 >= 0) {
                bracket_pos1 = doc->cursor - 1;
            }
        }
    }

    bpos focus_para_start = -1, focus_para_end = -1;
    if (g_editor.focus.active && doc->mode == MODE_PROSE) {
        focus_para_start = cursor_logical;
        while (focus_para_start > 0) {
            bpos prev_ls = lc_line_start(&doc->lc, focus_para_start - 1);
            bpos prev_le = lc_line_end(&doc->lc, &doc->gb, focus_para_start - 1);
            if (prev_le <= prev_ls) break;
            focus_para_start--;
        }
        focus_para_end = cursor_logical;
        while (focus_para_end < doc->lc.count - 1) {
            bpos next_ls = lc_line_start(&doc->lc, focus_para_end + 1);
            bpos next_le = lc_line_end(&doc->lc, &doc->gb, focus_para_end + 1);
            if (next_le <= next_ls) break;
            focus_para_end++;
        }
    }

    HPEN guide_pen = NULL;
    if (doc->mode == MODE_CODE) {
        guide_pen = CreatePen(PS_DOT, 1, CLR_SURFACE0);
    }

    HPEN spell_pen = NULL;
    if (doc->mode == MODE_PROSE && g_spell_loaded && g_editor.spellcheck_enabled) {
        spell_pen = CreatePen(PS_DOT, 1, CLR_MISSPELLED);
    }

    int in_block_comment = 0;
    if (doc->mode == MODE_CODE) {
        bpos target_line = first_vline;
        if (use_wrap && doc->wc.count > 0 && first_vline < doc->wc.count)
            target_line = doc->wc.entries[first_vline].line;
        if (target_line < 0) target_line = 0;

        if (doc->bc_cached_mutation == doc->gb.mutation && doc->bc_cached_line >= 0) {
            if (doc->bc_cached_line == target_line) {
                in_block_comment = doc->bc_cached_state;
            } else if (doc->bc_cached_line < target_line) {
                in_block_comment = doc->bc_cached_state;
                for (bpos line = doc->bc_cached_line; line < target_line; line++) {
                    bpos ls = lc_line_start(&doc->lc, line);
                    bpos le = lc_line_end(&doc->lc, &doc->gb, line);
                    in_block_comment = advance_block_comment_state_for_line(&doc->gb, ls, le, in_block_comment);
                }
            } else {
                bpos scan_end = lc_line_start(&doc->lc, target_line);
                in_block_comment = compute_block_comment_state(&doc->gb, scan_end);
            }
        } else {
            bpos scan_end = lc_line_start(&doc->lc, target_line);
            in_block_comment = compute_block_comment_state(&doc->gb, scan_end);
        }

        doc->bc_cached_mutation = doc->gb.mutation;
        doc->bc_cached_line = target_line;
        doc->bc_cached_state = in_block_comment;
    }

    SynToken line_tokens[2048];
    wchar_t  line_chars[2048];
    int      x_positions[2049];
    wchar_t  run_buf[2049];

    for (bpos vline = first_vline; vline <= last_vline; vline++) {
        int y = edit_y + (int)(vline * lh - doc->scroll_y);

        bpos ls, le, line_len, logical_line;
        if (use_wrap && doc->wc.count > 0) {
            ls = doc->wc.entries[vline].pos;
            le = wc_visual_line_end(&doc->wc, &doc->gb, &doc->lc, vline);
            logical_line = doc->wc.entries[vline].line;
        } else {
            ls = lc_line_start(&doc->lc, vline);
            le = lc_line_end(&doc->lc, &doc->gb, vline);
            logical_line = vline;
        }
        line_len = le - ls;
        bpos line = logical_line;

        if (vline == cursor_vline) {
            fill_rect(hdc, text_x, y, text_w, lh, CLR_ACTIVELINE);
        }

        int dim_this_line = 0;
        if (focus_para_start >= 0) {
            if (line < focus_para_start || line > focus_para_end) {
                dim_this_line = 1;
            }
        }

        if (doc->mode == MODE_CODE) {
            SelectObject(hdc, g_editor.font_main);
            wchar_t num[16];
            swprintf(num, 16, L"%*d", LINE_NUM_CHARS, (int)(line + 1));
            COLORREF nc = (line == cursor_logical) ? CLR_TEXT : CLR_GUTTER_TEXT;
            if (dim_this_line) nc = CLR_SURFACE1;
            if (line == cursor_logical) {
                int num_w = (int)wcslen(num) * cw;
                fill_rounded_rect(hdc, edit_x + DPI(GUTTER_PAD) - 2, y + 2,
                                  num_w + 4, lh - 4, 4, CLR_SURFACE0);
            }
            draw_text(hdc, edit_x + DPI(GUTTER_PAD), y + 1, num, (int)wcslen(num), nc);
        }

        if (use_wrap && doc->wc.count > 0 && vline > 0 &&
            doc->wc.entries[vline].line == doc->wc.entries[vline - 1].line) {
            draw_text(hdc, edit_x + DPI(6), y + 1, L"\x21A9", 1, CLR_SURFACE1);
        }

        int safe_len = (line_len < 2048) ? (int)line_len : 2048;

        gb_copy_range(&doc->gb, ls, safe_len, line_chars);

        if (doc->mode == MODE_CODE) {
            in_block_comment = tokenize_line_code(line_chars, safe_len, line_tokens, in_block_comment);
        } else {
            tokenize_line_prose(line_chars, safe_len, line_tokens);
        }

        {
            int xp = 0;
            for (int i = 0; i < safe_len; i++) {
                x_positions[i] = xp;
                xp += (line_chars[i] == L'\t') ? cw * 4 : cw;
            }
            x_positions[safe_len] = xp;
        }

        if (bracket_pos1 >= 0) {
            if (bracket_pos1 >= ls && bracket_pos1 < le && bracket_pos1 - ls < safe_len) {
                int bi = (int)(bracket_pos1 - ls);
                int bw = (line_chars[bi] == L'\t') ? cw * 4 : cw;
                fill_rect(hdc, text_x + x_positions[bi], y, bw, lh, CLR_SURFACE1);
            }
            if (bracket_pos2 >= ls && bracket_pos2 < le && bracket_pos2 - ls < safe_len) {
                int bi = (int)(bracket_pos2 - ls);
                int bw = (line_chars[bi] == L'\t') ? cw * 4 : cw;
                fill_rect(hdc, text_x + x_positions[bi], y, bw, lh, CLR_SURFACE1);
            }
        }

        if (has_sel) {
            int rs = -1;
            for (int i = 0; i <= safe_len; i++) {
                bpos pos = ls + i;
                int in_sel = (i < safe_len && pos >= sel_s && pos < sel_e);
                if (in_sel && rs < 0) rs = i;
                if (!in_sel && rs >= 0) {
                    fill_rect(hdc, text_x + x_positions[rs], y,
                              x_positions[i] - x_positions[rs], lh, CLR_SELECTION);
                    rs = -1;
                }
            }
        }

        if (g_editor.search.active && g_editor.search.match_count > 0) {
            int qlen = (int)wcslen(g_editor.search.query);
            bpos line_end_pos = ls + safe_len;
            for (int m = match_cursor; m < g_editor.search.match_count; m++) {
                bpos ms = g_editor.search.match_positions[m];
                if (ms >= line_end_pos) break;
                bpos me = ms + qlen;
                if (me <= ls) { match_cursor = m + 1; continue; }
                int hs = (int)((ms > ls) ? ms - ls : 0);
                int he = (int)((me < line_end_pos) ? me - ls : safe_len);
                COLORREF hl = (m == g_editor.search.current_match) ? CLR_SEARCH_HL : CLR_SURFACE1;
                fill_rect(hdc, text_x + x_positions[hs], y,
                          x_positions[he] - x_positions[hs], lh, hl);
            }
        }

        SelectObject(hdc, g_editor.font_main);
        {
            int run_start = 0;
            int run_len = 0;
            SynToken run_tok = (safe_len > 0) ? line_tokens[0] : TOK_NORMAL;
            int run_is_tab = (safe_len > 0 && line_chars[0] == L'\t');

            for (int i = 0; i <= safe_len; i++) {
                SynToken cur_tok = (i < safe_len) ? line_tokens[i] : TOK_NORMAL;
                wchar_t c = (i < safe_len) ? line_chars[i] : 0;
                int is_tab = (c == L'\t');
                int flush = (i == safe_len) || (cur_tok != run_tok) || is_tab || run_is_tab;

                if (flush && run_len > 0 && !run_is_tab) {
                    if (run_tok == TOK_MD_BOLD || run_tok == TOK_MD_HEADING)
                        SelectObject(hdc, g_editor.font_bold);
                    else if (run_tok == TOK_MD_ITALIC)
                        SelectObject(hdc, g_editor.font_italic);
                    else
                        SelectObject(hdc, g_editor.font_main);

                    COLORREF color = token_color(run_tok);
                    if (dim_this_line) {
                        int r = (GetRValue(color) + GetRValue(CLR_BG) * 2) / 3;
                        int g = (GetGValue(color) + GetGValue(CLR_BG) * 2) / 3;
                        int b = (GetBValue(color) + GetBValue(CLR_BG) * 2) / 3;
                        color = RGB(r, g, b);
                    }
                    draw_text(hdc, text_x + x_positions[run_start], y + 1,
                              run_buf, run_len, color);
                    run_len = 0;
                }

                if (i < safe_len) {
                    if (is_tab) {
                        run_start = i + 1;
                        run_len = 0;
                        run_tok = (i + 1 < safe_len) ? line_tokens[i + 1] : TOK_NORMAL;
                        run_is_tab = 0;
                    } else if (run_len == 0) {
                        run_start = i;
                        run_tok = cur_tok;
                        run_is_tab = 0;
                        run_buf[run_len++] = c;
                    } else {
                        if (run_len < 2048) run_buf[run_len++] = c;
                    }
                }
            }
        }

        if (line_len > 2048) {
            int trunc_x = text_x + x_positions[safe_len];
            draw_text(hdc, trunc_x, y + 1, L"\x2026", 1, CLR_OVERLAY0);
        }

        if (doc->mode == MODE_CODE && !dim_this_line && line_len > 0) {
            int indent_size = 4;
            int content_col = 0;
            for (int i = 0; i < safe_len; i++) {
                wchar_t c = line_chars[i];
                if (c == L' ') content_col++;
                else if (c == L'\t') content_col += 4;
                else break;
            }
            if (content_col > indent_size) {
                HPEN old_guide_pen = (HPEN)SelectObject(hdc, guide_pen);
                for (int col = indent_size; col < content_col; col += indent_size) {
                    int gx = text_x + col * cw;
                    MoveToEx(hdc, gx, y, NULL);
                    LineTo(hdc, gx, y + lh);
                }
                SelectObject(hdc, old_guide_pen);
            }
        }

        if (spell_pen && !dim_this_line) {
            HPEN old_spell_pen = (HPEN)SelectObject(hdc, spell_pen);
            int ws = -1;
            for (int i = 0; i <= safe_len; i++) {
                wchar_t c = (i < safe_len) ? line_chars[i] : L' ';
                if (iswalpha(c) || c == L'\'') {
                    if (ws < 0) ws = i;
                } else {
                    if (ws >= 0) {
                        int wl = i - ws;
                        wchar_t word[64];
                        if (wl < 64) {
                            for (int j = 0; j < wl; j++)
                                word[j] = line_chars[ws + j];
                            word[wl] = 0;
                            if (!spell_check(word, wl)) {
                                int ux = text_x + ((ws < safe_len) ? x_positions[ws] : x_positions[safe_len]);
                                int ux_end = text_x + ((i < safe_len) ? x_positions[i] : x_positions[safe_len]);
                                int uy = y + lh - 3;
                                MoveToEx(hdc, ux, uy, NULL);
                                for (int px = ux; px < ux_end; px += 4) {
                                    LineTo(hdc, px + 2, uy + (((px - ux) / 4) % 2 ? 2 : 0));
                                }
                            }
                        }
                        ws = -1;
                    }
                }
            }
            SelectObject(hdc, old_spell_pen);
        }
    }

    if (guide_pen) DeleteObject(guide_pen);
    if (spell_pen) DeleteObject(spell_pen);

    /* Cursor */
    {
        int cy = edit_y + (int)(cursor_vline * lh) - doc->scroll_y;
        bpos cursor_col;
        bpos cursor_line_start;
        if (use_wrap && doc->wc.count > 0) {
            cursor_col = wc_col_in_vline(&doc->wc, doc->cursor, cursor_vline);
            cursor_line_start = doc->wc.entries[cursor_vline].pos;
        } else {
            cursor_col = pos_to_col(doc, doc->cursor);
            cursor_line_start = lc_line_start(&doc->lc, pos_to_line(doc, doc->cursor));
        }
        int cx = text_x + col_to_pixel_x(&doc->gb, cursor_line_start, cursor_col, cw);

        DWORD now = GetTickCount();
        DWORD since_active = now - g_editor.cursor_last_active;
        float alpha;
        if (since_active < 400) {
            alpha = 1.0f;
        } else {
            float t = (float)((now - g_editor.cursor_last_active - 400) % 1200) / 1200.0f;
            alpha = 0.5f + 0.5f * cosf(t * 3.14159f * 2.0f);
        }
        int cr = (int)(GetRValue(CLR_CURSOR) * alpha + GetRValue(CLR_BG) * (1.0f - alpha));
        int cg = (int)(GetGValue(CLR_CURSOR) * alpha + GetGValue(CLR_BG) * (1.0f - alpha));
        int cb = (int)(GetBValue(CLR_CURSOR) * alpha + GetBValue(CLR_BG) * (1.0f - alpha));
        fill_rect(hdc, cx, cy, DPI(CURSOR_WIDTH), lh, RGB(cr, cg, cb));
    }

    /* Scrollbar */
    SelectClipRgn(hdc, NULL);
    DeleteObject(clip);

    int sb_x = g_editor.client_w - DPI(SCROLLBAR_W);
    fill_rect(hdc, sb_x, edit_y, DPI(SCROLLBAR_W), edit_h, CLR_SCROLLBAR_BG);

    int total_h = total_vlines * lh;
    if (total_h > edit_h) {
        int thumb_h = (edit_h * edit_h) / total_h;
        if (thumb_h < 30) thumb_h = 30;
        int thumb_y = edit_y + (doc->scroll_y * (edit_h - thumb_h)) / (total_h - edit_h);
        COLORREF thumb_clr = (g_editor.scrollbar_dragging || g_editor.scrollbar_hover)
                             ? g_theme.scrollbar_hover : CLR_SCROLLBAR_TH;
        fill_rounded_rect(hdc, sb_x + DPI(2), thumb_y, DPI(SCROLLBAR_W) - DPI(4), thumb_h, DPI(6), thumb_clr);
    }

    /* Minimap */
    if (g_editor.show_minimap) {
        int mm_x = edit_w;
        int mm_w = DPI(MINIMAP_W);
        int mm_h = edit_h;

        fill_rect(hdc, mm_x, edit_y, mm_w, mm_h, CLR_BG_DARK);
        fill_rect(hdc, mm_x, edit_y, 1, mm_h, CLR_SURFACE0);

        int mm_total = total_vlines;
        if (mm_total <= 0) mm_total = 1;

        float scale = (float)mm_h / (float)mm_total;
        if (scale > 3.0f) scale = 3.0f;

        int vp_top = (int)(first_vline * scale);
        int vp_bot = (int)((last_vline + 1) * scale);
        if (vp_bot > mm_h) vp_bot = mm_h;
        if (vp_bot - vp_top < 8) vp_bot = vp_top + 8;
        fill_rect(hdc, mm_x + 1, edit_y + vp_top, mm_w - 1, vp_bot - vp_top,
                  g_theme.is_dark ? RGB(255, 255, 255) : RGB(0, 0, 0));
        fill_rect(hdc, mm_x + 1, edit_y + vp_top, mm_w - 1, vp_bot - vp_top,
                  g_theme.is_dark ? RGB(48, 50, 62) : RGB(200, 210, 230));

        int max_render_lines = (int)(mm_h / scale) + 1;
        if (max_render_lines > mm_total) max_render_lines = mm_total;

        int stride = 1;
        if (mm_total > mm_h * 2) stride = mm_total / mm_h;
        if (stride < 1) stride = 1;

        int mm_in_block_comment = 0;
        int mm_prev_line = -1;

        for (int i = 0; i < max_render_lines; i += stride) {
            int my = edit_y + (int)(i * scale);
            if (my >= edit_y + mm_h) break;

            bpos mls, mle;
            if (use_wrap && doc->wc.count > 0) {
                mls = doc->wc.entries[i].pos;
                mle = wc_visual_line_end(&doc->wc, &doc->gb, &doc->lc, i);
            } else {
                mls = lc_line_start(&doc->lc, i);
                mle = lc_line_end(&doc->lc, &doc->gb, i);
            }
            bpos mline_len = mle - mls;
            if (mline_len <= 0) continue;

            if (doc->mode == MODE_CODE && mm_prev_line >= 0 && i != mm_prev_line + stride) {
                for (int skip = mm_prev_line + 1; skip < i; skip++) {
                    bpos sls = lc_line_start(&doc->lc, skip);
                    bpos sle = lc_line_end(&doc->lc, &doc->gb, skip);
                    mm_in_block_comment = advance_block_comment_state_for_line(&doc->gb, sls, sle, mm_in_block_comment);
                }
            }
            mm_prev_line = i;

            int bar_w = (int)(mline_len * (mm_w - 8)) / 120;
            if (bar_w < 2) bar_w = 2;
            if (bar_w > mm_w - 8) bar_w = mm_w - 8;

            int bar_h = (int)scale;
            if (bar_h < 1) bar_h = 1;

            COLORREF mc;
            if (doc->mode == MODE_CODE && mline_len > 0) {
                int mm_len = (mline_len < 40) ? (int)mline_len : 40;
                wchar_t mm_chars[40];
                SynToken mm_tokens[40];
                gb_copy_range(&doc->gb, mls, mm_len, mm_chars);
                mm_in_block_comment = tokenize_line_code(mm_chars, mm_len, mm_tokens, mm_in_block_comment);
                int counts[10] = {0};
                for (int j = 0; j < mm_len; j++) {
                    if (mm_tokens[j] > 0 && mm_tokens[j] < 10) counts[mm_tokens[j]]++;
                }
                SynToken dominant = TOK_NORMAL;
                int max_count = 0;
                for (int t = 1; t < 10; t++) {
                    if (counts[t] > max_count) { max_count = counts[t]; dominant = (SynToken)t; }
                }
                if (dominant != TOK_NORMAL) {
                    COLORREF tc = token_color(dominant);
                    COLORREF bg = CLR_BG_DARK;
                    if (g_theme.is_dark) {
                        mc = RGB((GetRValue(bg)   + GetRValue(tc)*3) / 4,
                                 (GetGValue(bg)   + GetGValue(tc)*3) / 4,
                                 (GetBValue(bg)   + GetBValue(tc)*3) / 4);
                    } else {
                        mc = RGB((GetRValue(bg)*11 + GetRValue(tc)*9) / 20,
                                 (GetGValue(bg)*11 + GetGValue(tc)*9) / 20,
                                 (GetBValue(bg)*11 + GetBValue(tc)*9) / 20);
                    }
                } else {
                    mc = g_theme.is_dark ? RGB(100, 102, 118) : RGB(175, 175, 185);
                }
            } else {
                mc = g_theme.is_dark ? RGB(100, 102, 118) : RGB(175, 175, 185);
            }
            if (my < edit_y + vp_top || my > edit_y + vp_bot) {
                COLORREF bg = CLR_BG_DARK;
                mc = RGB((GetRValue(bg)*2 + GetRValue(mc)*3) / 5,
                         (GetGValue(bg)*2 + GetGValue(mc)*3) / 5,
                         (GetBValue(bg)*2 + GetBValue(mc)*3) / 5);
            }
            fill_rect(hdc, mm_x + 4, my, bar_w, bar_h, mc);
        }
    }
}

void render_stats_screen(HDC hdc) {
    if (!g_editor.show_stats_screen) return;

    int cw = g_editor.client_w;
    int ch = g_editor.client_h;

    fill_rect(hdc, 0, 0, cw, ch, g_theme.is_dark ? RGB(14, 14, 20) : RGB(240, 240, 245));

    int pw = DPI(480), ph = DPI(420);
    if (pw > cw - DPI(40)) pw = cw - DPI(40);
    if (ph > ch - DPI(40)) ph = ch - DPI(40);
    int px = (cw - pw) / 2;
    int py = (ch - ph) / 2;

    fill_rounded_rect(hdc, px + 3, py + 3, pw, ph, DPI(16),
                      g_theme.is_dark ? RGB(6, 6, 12) : RGB(180, 180, 185));
    fill_rounded_rect(hdc, px, py, pw, ph, DPI(16), CLR_BG);
    fill_rounded_rect(hdc, px + 1, py + 1, pw - 2, ph - 2, DPI(15), CLR_BG);

    fill_rounded_rect(hdc, px + DPI(24), py + DPI(12), pw - DPI(48), DPI(2), DPI(1), CLR_ACCENT);

    SetBkMode(hdc, TRANSPARENT);
    int y = py + DPI(24);
    int left_margin = px + DPI(32);
    int right_col = px + pw - DPI(32);

    SelectObject(hdc, g_editor.font_title);
    {
        wchar_t title[] = L"\x2328  SESSION STATS";
        draw_text(hdc, left_margin, y, title, (int)wcslen(title), CLR_LAVENDER);
    }
    y += DPI(32);

    fill_rect(hdc, left_margin, y, pw - DPI(64), 1, CLR_SURFACE0);
    y += DPI(16);

    DWORD elapsed_ms = GetTickCount() - g_editor.session_start_time;
    int elapsed_s = (int)(elapsed_ms / 1000);
    int hrs = elapsed_s / 3600;
    int mins = (elapsed_s % 3600) / 60;
    int secs = elapsed_s % 60;

    SelectObject(hdc, g_editor.font_ui);

    bpos total_words_added = 0;
    bpos total_chars_added = 0;
    bpos total_lines_added = 0;
    bpos total_words_now = 0;
    bpos total_chars_now = 0;

    for (int i = 0; i < g_editor.tab_count; i++) {
        Document *d = g_editor.tabs[i];
        d->stats_dirty = 1;
        update_stats_now(d);
        total_words_added += (d->word_count - d->session_start_words);
        total_chars_added += (d->char_count - d->session_start_chars);
        total_lines_added += (d->line_count - d->session_start_lines);
        total_words_now += d->word_count;
        total_chars_now += d->char_count;
    }

    {
        wchar_t tbuf[64];
        swprintf(tbuf, 64, L"%dh %02dm %02ds", hrs, mins, secs);
        draw_text(hdc, left_margin, y, L"Session Duration", 16, CLR_SUBTEXT);
        SIZE sz; GetTextExtentPoint32W(hdc, tbuf, (int)wcslen(tbuf), &sz);
        draw_text(hdc, right_col - sz.cx, y, tbuf, (int)wcslen(tbuf), CLR_TEXT);
    }
    y += DPI(28);

    fill_rounded_rect(hdc, left_margin - DPI(8), y - DPI(4), pw - DPI(48), DPI(52), DPI(10), CLR_SURFACE0);
    {
        SelectObject(hdc, g_editor.font_title);
        draw_text(hdc, left_margin, y, L"\x270D  Words Added", 14, CLR_SUBTEXT);
        y += DPI(4);

        wchar_t big[32];
        swprintf(big, 32, L"%+lld", (long long)total_words_added);
        SelectObject(hdc, g_editor.font_stats_hero);
        SIZE bsz; GetTextExtentPoint32W(hdc, big, (int)wcslen(big), &bsz);
        COLORREF word_clr = (total_words_added > 0) ? CLR_GREEN
                          : (total_words_added < 0) ? CLR_RED : CLR_TEXT;
        draw_text(hdc, right_col - bsz.cx, y, big, (int)wcslen(big), word_clr);
    }
    y += DPI(40);
    SelectObject(hdc, g_editor.font_ui);

    fill_rect(hdc, left_margin, y, pw - DPI(64), 1, CLR_SURFACE0);
    y += DPI(16);

    {
        wchar_t buf[64];
        swprintf(buf, 64, L"%+lld", (long long)total_chars_added);
        draw_text(hdc, left_margin, y, L"Characters Added", 16, CLR_SUBTEXT);
        SIZE sz; GetTextExtentPoint32W(hdc, buf, (int)wcslen(buf), &sz);
        COLORREF clr = (total_chars_added > 0) ? CLR_GREEN
                      : (total_chars_added < 0) ? CLR_RED : CLR_TEXT;
        draw_text(hdc, right_col - sz.cx, y, buf, (int)wcslen(buf), clr);
    }
    y += DPI(28);

    {
        wchar_t buf[64];
        swprintf(buf, 64, L"%+lld", (long long)total_lines_added);
        draw_text(hdc, left_margin, y, L"Lines Added", 11, CLR_SUBTEXT);
        SIZE sz; GetTextExtentPoint32W(hdc, buf, (int)wcslen(buf), &sz);
        COLORREF clr = (total_lines_added > 0) ? CLR_GREEN
                      : (total_lines_added < 0) ? CLR_RED : CLR_TEXT;
        draw_text(hdc, right_col - sz.cx, y, buf, (int)wcslen(buf), clr);
    }
    y += DPI(28);

    {
        wchar_t buf[64];
        float wpm = 0.0f;
        if (elapsed_s > 10 && total_words_added > 0) {
            wpm = (float)total_words_added / ((float)elapsed_s / 60.0f);
        }
        swprintf(buf, 64, L"%.1f wpm", wpm);
        draw_text(hdc, left_margin, y, L"Avg Writing Pace", 16, CLR_SUBTEXT);
        SIZE sz; GetTextExtentPoint32W(hdc, buf, (int)wcslen(buf), &sz);
        draw_text(hdc, right_col - sz.cx, y, buf, (int)wcslen(buf), CLR_TEXT);
    }
    y += DPI(16);

    fill_rect(hdc, left_margin, y, pw - DPI(64), 1, CLR_SURFACE0);
    y += DPI(16);

    SelectObject(hdc, g_editor.font_title);
    draw_text(hdc, left_margin, y, L"DOCUMENT TOTALS", 15, CLR_OVERLAY0);
    y += DPI(24);
    SelectObject(hdc, g_editor.font_ui);

    {
        wchar_t buf[64];
        swprintf(buf, 64, L"%lld", (long long)total_words_now);
        draw_text(hdc, left_margin, y, L"Total Words", 11, CLR_SUBTEXT);
        SIZE sz; GetTextExtentPoint32W(hdc, buf, (int)wcslen(buf), &sz);
        draw_text(hdc, right_col - sz.cx, y, buf, (int)wcslen(buf), CLR_TEXT);
    }
    y += DPI(24);

    {
        wchar_t buf[64];
        swprintf(buf, 64, L"%lld", (long long)total_chars_now);
        draw_text(hdc, left_margin, y, L"Total Characters", 16, CLR_SUBTEXT);
        SIZE sz; GetTextExtentPoint32W(hdc, buf, (int)wcslen(buf), &sz);
        draw_text(hdc, right_col - sz.cx, y, buf, (int)wcslen(buf), CLR_TEXT);
    }
    y += DPI(24);

    {
        wchar_t buf[64];
        swprintf(buf, 64, L"%d", g_editor.tab_count);
        draw_text(hdc, left_margin, y, L"Open Tabs", 9, CLR_SUBTEXT);
        SIZE sz; GetTextExtentPoint32W(hdc, buf, (int)wcslen(buf), &sz);
        draw_text(hdc, right_col - sz.cx, y, buf, (int)wcslen(buf), CLR_TEXT);
    }
    y += DPI(32);

    SelectObject(hdc, g_editor.font_ui_small);
    {
        wchar_t hint[] = L"Press Ctrl+I or Esc to close";
        SIZE sz; GetTextExtentPoint32W(hdc, hint, (int)wcslen(hint), &sz);
        draw_text(hdc, px + (pw - sz.cx) / 2, y, hint, (int)wcslen(hint), CLR_OVERLAY0);
    }
}

void render_menubar(HDC hdc) {
    (void)hdc;
}

void render_menu_dropdown(HDC hdc) {
    if (g_editor.menu_open < 0 || g_editor.menu_open >= MENU_COUNT) return;

    const MenuDef *menu = &g_menus[g_editor.menu_open];
    int item_h = DPI(26);
    int sep_h = DPI(9);
    int dropdown_w = DPI(260);
    int pad_x = DPI(12);

    int fab_size = DPI(28);
    int fab_pad = DPI(6);
    int fab_start_x = DPI(8);

    int dropdown_x = fab_start_x + g_editor.menu_open * (fab_size + fab_pad);
    int dropdown_y = DPI(TITLEBAR_H);

    int total_h = DPI(4);
    for (int i = 0; i < menu->item_count; i++) {
        total_h += (menu->items[i].id == MENU_ID_SEP) ? sep_h : item_h;
    }
    total_h += DPI(4);

    fill_rounded_rect(hdc, dropdown_x + DPI(2), dropdown_y + DPI(2),
                      dropdown_w, total_h, DPI(8), RGB(0, 0, 0));
    fill_rounded_rect(hdc, dropdown_x, dropdown_y, dropdown_w, total_h, DPI(8),
                      g_theme.is_dark ? RGB(34, 34, 42) : RGB(248, 248, 248));
    HPEN pen = CreatePen(PS_SOLID, 1, CLR_SURFACE1);
    HBRUSH hollow = (HBRUSH)GetStockObject(HOLLOW_BRUSH);
    HPEN old_pen = (HPEN)SelectObject(hdc, pen);
    HBRUSH old_br = (HBRUSH)SelectObject(hdc, hollow);
    RoundRect(hdc, dropdown_x, dropdown_y, dropdown_x + dropdown_w,
              dropdown_y + total_h, DPI(8), DPI(8));
    SelectObject(hdc, old_pen);
    SelectObject(hdc, old_br);
    DeleteObject(pen);

    SelectObject(hdc, g_editor.font_ui_small);
    SetBkMode(hdc, TRANSPARENT);

    int cy = dropdown_y + DPI(4);
    for (int i = 0; i < menu->item_count; i++) {
        const MenuItem *item = &menu->items[i];

        if (item->id == MENU_ID_SEP) {
            int line_y = cy + sep_h / 2;
            fill_rect(hdc, dropdown_x + pad_x, line_y, dropdown_w - pad_x * 2, 1, CLR_SURFACE0);
            cy += sep_h;
            continue;
        }

        if (g_editor.menu_hover_item == i) {
            fill_rounded_rect(hdc, dropdown_x + DPI(4), cy, dropdown_w - DPI(8), item_h,
                              DPI(4), CLR_ACCENT);
            SetTextColor(hdc, g_theme.is_dark ? RGB(255, 255, 255) : RGB(255, 255, 255));
        }

        COLORREF label_clr = (g_editor.menu_hover_item == i)
            ? (g_theme.is_dark ? RGB(255, 255, 255) : RGB(255, 255, 255))
            : CLR_TEXT;
        draw_text(hdc, dropdown_x + pad_x, cy + (item_h - DPI(12)) / 2,
                  item->label, (int)wcslen(item->label), label_clr);

        if (item->shortcut) {
            SIZE ssz;
            GetTextExtentPoint32W(hdc, item->shortcut, (int)wcslen(item->shortcut), &ssz);
            COLORREF sc_clr = (g_editor.menu_hover_item == i)
                ? (g_theme.is_dark ? RGB(205, 210, 230) : RGB(220, 230, 255))
                : CLR_OVERLAY0;
            draw_text(hdc, dropdown_x + dropdown_w - pad_x - ssz.cx,
                      cy + (item_h - DPI(12)) / 2,
                      item->shortcut, (int)wcslen(item->shortcut), sc_clr);
        }

        cy += item_h;
    }
}

void render(HDC hdc) {
    arena_reset(&g_frame_arena);

    Document *stats_doc = current_doc();
    if (stats_doc) update_stats_now(stats_doc);

    int scroll_only = g_editor.scroll_only_repaint;
    g_editor.scroll_only_repaint = 0;

    if (scroll_only && g_editor.menu_open < 0 && !g_editor.show_stats_screen) {
        render_editor(hdc);
        render_statusbar(hdc);
        render_searchbar(hdc);
    } else {
        fill_rect(hdc, 0, 0, g_editor.client_w, g_editor.client_h, CLR_BG);
        render_titlebar(hdc);
        render_menubar(hdc);
        render_tabbar(hdc);
        render_editor(hdc);
        render_statusbar(hdc);
        render_searchbar(hdc);
        render_stats_screen(hdc);
        render_menu_dropdown(hdc);
    }
}
