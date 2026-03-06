#include "prose_code.h"

Document *doc_create(void) {
    Document *doc = (Document *)calloc(1, sizeof(Document));
    gb_init(&doc->gb, GAP_INIT);
    lc_init(&doc->lc);
    wc_init(&doc->wc);
    undo_init(&doc->undo);
    doc->sel_anchor = -1;
    doc->mode = MODE_PROSE;
    safe_wcscpy(doc->title, 64, L"Untitled");
    doc->desired_col = -1;
    doc->bc_cached_mutation = -1;
    doc->bc_cached_line = -1;
    doc->autosave_id = g_editor.next_autosave_id++;
    return doc;
}

void doc_free(Document *doc) {
    gb_free(&doc->gb);
    lc_free(&doc->lc);
    wc_free(&doc->wc);
    undo_free(&doc->undo);
    free(doc);
}

Document *current_doc(void) {
    if (g_editor.active_tab < 0 || g_editor.active_tab >= g_editor.tab_count) return NULL;
    return g_editor.tabs[g_editor.active_tab];
}

void recalc_lines(Document *doc) {
    lc_rebuild(&doc->lc, &doc->gb);
    doc->line_count = doc->lc.count;
    if (doc->mode == MODE_PROSE && doc->wc.wrap_col > 0) {
        doc->wrap_dirty = 1;
    }
}

void recalc_wrap_now(Document *doc) {
    if (!doc->wrap_dirty) return;
    doc->wrap_dirty = 0;
    if (doc->mode == MODE_PROSE && doc->wc.wrap_col > 0) {
        wc_rebuild(&doc->wc, &doc->gb, &doc->lc, doc->wc.wrap_col);
    }
}

void update_stats(Document *doc) {
    doc->stats_dirty = 1;
}

void update_stats_now(Document *doc) {
    if (!doc->stats_dirty) return;
    doc->stats_dirty = 0;
    GapBuffer *gb = &doc->gb;
    doc->char_count = gb_length(gb);
    bpos words = 0;
    int in_word = 0;

    for (bpos i = 0; i < gb->gap_start; i++) {
        wchar_t c = gb->buf[i];
        if (iswalpha(c) || c == L'\'' || c == L'-') {
            if (!in_word) { words++; in_word = 1; }
        } else {
            in_word = 0;
        }
    }
    for (bpos i = gb->gap_end; i < gb->total; i++) {
        wchar_t c = gb->buf[i];
        if (iswalpha(c) || c == L'\'' || c == L'-') {
            if (!in_word) { words++; in_word = 1; }
        } else {
            in_word = 0;
        }
    }
    doc->word_count = words;
}

void snapshot_session_baseline(Document *doc) {
    doc->stats_dirty = 1;
    update_stats_now(doc);
    doc->session_start_words = doc->word_count;
    doc->session_start_chars = doc->char_count;
    doc->session_start_lines = doc->line_count;
}

bpos pos_to_line(Document *doc, bpos pos) {
    return lc_line_of(&doc->lc, pos);
}

bpos pos_to_col(Document *doc, bpos pos) {
    bpos line = pos_to_line(doc, pos);
    return pos - lc_line_start(&doc->lc, line);
}

bpos line_col_to_pos(Document *doc, bpos line, bpos col) {
    if (line < 0) line = 0;
    if (line >= doc->lc.count) line = doc->lc.count - 1;
    bpos start = lc_line_start(&doc->lc, line);
    bpos end = lc_line_end(&doc->lc, &doc->gb, line);
    bpos maxcol = end - start;
    if (col > maxcol) col = maxcol;
    if (col < 0) col = 0;
    return start + col;
}

bpos pos_to_visual_col(Document *doc, bpos pos) {
    bpos line = pos_to_line(doc, pos);
    bpos ls = lc_line_start(&doc->lc, line);
    int vcol = 0;
    for (bpos i = ls; i < pos; i++) {
        wchar_t c = gb_char_at(&doc->gb, i);
        vcol += (c == L'\t') ? 4 : 1;
    }
    return vcol;
}

bpos visual_col_to_pos(Document *doc, bpos line, int target_vcol) {
    if (line < 0) line = 0;
    if (line >= doc->lc.count) line = doc->lc.count - 1;
    bpos ls = lc_line_start(&doc->lc, line);
    bpos le = lc_line_end(&doc->lc, &doc->gb, line);
    int vcol = 0;
    for (bpos i = ls; i < le; i++) {
        if (vcol >= target_vcol) return i;
        wchar_t c = gb_char_at(&doc->gb, i);
        vcol += (c == L'\t') ? 4 : 1;
    }
    return le;
}

int col_to_pixel_x(GapBuffer *gb, bpos line_start_pos, bpos col, int cw) {
    int xp = 0;
    for (int i = 0; i < col; i++) {
        wchar_t c = gb_char_at(gb, line_start_pos + i);
        xp += (c == L'\t') ? cw * 4 : cw;
    }
    return xp;
}

bpos pixel_x_to_col(GapBuffer *gb, bpos line_start_pos, bpos line_len, int px, int cw) {
    int xp = 0;
    for (int i = 0; i < line_len; i++) {
        wchar_t c = gb_char_at(gb, line_start_pos + i);
        int w = (c == L'\t') ? cw * 4 : cw;
        if (px < xp + w / 2) return i;
        xp += w;
    }
    return line_len;
}

int has_selection(Document *doc) {
    return doc->sel_anchor >= 0 && doc->sel_anchor != doc->cursor;
}

bpos selection_start(Document *doc) {
    if (!has_selection(doc)) return doc->cursor;
    return doc->cursor < doc->sel_anchor ? doc->cursor : doc->sel_anchor;
}

bpos selection_end(Document *doc) {
    if (!has_selection(doc)) return doc->cursor;
    return doc->cursor > doc->sel_anchor ? doc->cursor : doc->sel_anchor;
}

void invalidate_editor_region(HWND hwnd) {
    RECT rc;
    rc.left = 0;
    rc.right = g_editor.client_w;
    rc.top = DPI(TITLEBAR_H + MENUBAR_H + TABBAR_H);
    rc.bottom = g_editor.client_h;
    InvalidateRect(hwnd, &rc, FALSE);
}

void start_scroll_animation(void) {
    if (g_editor.hwnd)
        SetTimer(g_editor.hwnd, TIMER_SMOOTH, 16, NULL);
}

int gutter_width(Document *doc) {
    if (!doc) return DPI(24);
    return (doc->mode == MODE_CODE)
        ? (LINE_NUM_CHARS * g_editor.char_width + DPI(GUTTER_PAD) * 2)
        : DPI(24);
}
