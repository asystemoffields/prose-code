#include "prose_code.h"

void editor_insert_text(const wchar_t *text, bpos len) {
    Document *doc = current_doc();
    if (!doc) return;

    bpos old_cursor = doc->cursor;
    int sel_group = 0;
    if (has_selection(doc)) {
        bpos s = selection_start(doc);
        bpos e = selection_end(doc);
        sel_group = ++doc->undo.next_group;
        wchar_t *deleted = gb_extract_alloc(&doc->gb, s, e - s);
        if (deleted) {
            undo_push(&doc->undo, UNDO_DELETE, s, deleted, e - s, old_cursor, s, sel_group);
            free(deleted);
        }
        gb_delete(&doc->gb, s, e - s);
        doc->cursor = s;
        doc->sel_anchor = -1;
        old_cursor = s;
    }

    gb_insert(&doc->gb, doc->cursor, text, len);
    undo_push(&doc->undo, UNDO_INSERT, doc->cursor, text, len, old_cursor, doc->cursor + len, sel_group);
    doc->cursor += len;
    doc->sel_anchor = -1;
    doc->modified = 1;
    doc->desired_col = -1;
    if (!sel_group && lc_notify_insert(&doc->lc, doc->cursor - len, text, len)) {
        doc->line_count = doc->lc.count;
        if (doc->mode == MODE_PROSE && doc->wc.wrap_col > 0)
            doc->wrap_dirty = 1;
    } else {
        recalc_lines(doc);
    }
    update_stats(doc);
}

void editor_insert_char(wchar_t c) {
    wchar_t buf[2] = { c, 0 };
    editor_insert_text(buf, 1);
}

void editor_delete_selection(void) {
    Document *doc = current_doc();
    if (!doc || !has_selection(doc)) return;

    bpos s = selection_start(doc);
    bpos e = selection_end(doc);
    wchar_t *deleted = gb_extract_alloc(&doc->gb, s, e - s);
    if (deleted) {
        undo_push(&doc->undo, UNDO_DELETE, s, deleted, e - s, doc->cursor, s, 0);
        free(deleted);
    }
    gb_delete(&doc->gb, s, e - s);
    doc->cursor = s;
    doc->sel_anchor = -1;
    doc->modified = 1;
    doc->desired_col = -1;
    recalc_lines(doc);
    update_stats(doc);
}

void editor_backspace(void) {
    Document *doc = current_doc();
    if (!doc) return;

    if (has_selection(doc)) {
        editor_delete_selection();
        return;
    }
    if (doc->cursor <= 0) return;

    wchar_t c = gb_char_at(&doc->gb, doc->cursor - 1);
    wchar_t buf[2] = { c, 0 };
    undo_push(&doc->undo, UNDO_DELETE, doc->cursor - 1, buf, 1, doc->cursor, doc->cursor - 1, 0);
    gb_delete(&doc->gb, doc->cursor - 1, 1);
    doc->cursor--;
    doc->modified = 1;
    doc->desired_col = -1;
    if (lc_notify_delete(&doc->lc, doc->cursor, buf, 1)) {
        doc->line_count = doc->lc.count;
        if (doc->mode == MODE_PROSE && doc->wc.wrap_col > 0)
            doc->wrap_dirty = 1;
    } else {
        recalc_lines(doc);
    }
    update_stats(doc);
}

void editor_delete_forward(void) {
    Document *doc = current_doc();
    if (!doc) return;

    if (has_selection(doc)) {
        editor_delete_selection();
        return;
    }
    if (doc->cursor >= gb_length(&doc->gb)) return;

    wchar_t c = gb_char_at(&doc->gb, doc->cursor);
    wchar_t buf[2] = { c, 0 };
    undo_push(&doc->undo, UNDO_DELETE, doc->cursor, buf, 1, doc->cursor, doc->cursor, 0);
    gb_delete(&doc->gb, doc->cursor, 1);
    doc->modified = 1;
    doc->desired_col = -1;
    if (lc_notify_delete(&doc->lc, doc->cursor, buf, 1)) {
        doc->line_count = doc->lc.count;
        if (doc->mode == MODE_PROSE && doc->wc.wrap_col > 0)
            doc->wrap_dirty = 1;
    } else {
        recalc_lines(doc);
    }
    update_stats(doc);
}

void editor_move_cursor(bpos pos, int extend_selection) {
    Document *doc = current_doc();
    if (!doc) return;

    bpos len = gb_length(&doc->gb);
    if (pos < 0) pos = 0;
    if (pos > len) pos = len;

    if (extend_selection) {
        if (doc->sel_anchor < 0) doc->sel_anchor = doc->cursor;
    } else {
        doc->sel_anchor = -1;
    }

    doc->cursor = pos;
    g_editor.cursor_visible = 1; g_editor.cursor_last_active = GetTickCount();
}

void editor_undo(void) {
    Document *doc = current_doc();
    if (!doc) return;
    UndoStack *us = &doc->undo;
    if (us->current <= 0) return;

    int group = us->entries[us->current - 1].group;

    do {
        us->current--;
        UndoEntry *e = &us->entries[us->current];
        if (e->type == UNDO_INSERT) {
            gb_delete(&doc->gb, e->pos, e->len);
        } else {
            gb_insert(&doc->gb, e->pos, e->text, e->len);
        }
        doc->cursor = e->cursor_before;
    } while (group != 0 && us->current > 0 &&
             us->entries[us->current - 1].group == group);

    doc->sel_anchor = -1;
    doc->modified = (us->current != us->save_point);
    recalc_lines(doc);
    update_stats(doc);
}

void editor_redo(void) {
    Document *doc = current_doc();
    if (!doc) return;
    UndoStack *us = &doc->undo;
    if (us->current >= us->count) return;

    int group = us->entries[us->current].group;

    do {
        UndoEntry *e = &us->entries[us->current];
        if (e->type == UNDO_INSERT) {
            gb_insert(&doc->gb, e->pos, e->text, e->len);
        } else {
            gb_delete(&doc->gb, e->pos, e->len);
        }
        doc->cursor = e->cursor_after;
        us->current++;
    } while (group != 0 && us->current < us->count &&
             us->entries[us->current].group == group);

    doc->sel_anchor = -1;
    doc->modified = (us->current != us->save_point);
    recalc_lines(doc);
    update_stats(doc);
}

void editor_select_all(void) {
    Document *doc = current_doc();
    if (!doc) return;
    doc->sel_anchor = 0;
    doc->cursor = gb_length(&doc->gb);
}

void editor_copy(void) {
    Document *doc = current_doc();
    if (!doc || !has_selection(doc)) return;

    bpos s = selection_start(doc);
    bpos e = selection_end(doc);
    bpos len = e - s;

    wchar_t *text = (wchar_t *)malloc((len + 1) * sizeof(wchar_t));
    if (!text) return;
    gb_copy_range(&doc->gb, s, len, text);
    text[len] = 0;

    if (OpenClipboard(g_editor.hwnd)) {
        HGLOBAL hg = GlobalAlloc(GMEM_MOVEABLE, (len + 1) * sizeof(wchar_t));
        if (hg) {
            wchar_t *dest = (wchar_t *)GlobalLock(hg);
            memcpy(dest, text, (len + 1) * sizeof(wchar_t));
            GlobalUnlock(hg);
            EmptyClipboard();
            SetClipboardData(CF_UNICODETEXT, hg);
        }
        CloseClipboard();
    }
    free(text);
}

void editor_cut(void) {
    editor_copy();
    editor_delete_selection();
}

void editor_paste(void) {
    if (!OpenClipboard(g_editor.hwnd)) return;
    HANDLE hg = GetClipboardData(CF_UNICODETEXT);
    if (hg) {
        wchar_t *text = (wchar_t *)GlobalLock(hg);
        if (text) {
            int len = (int)wcslen(text);
            wchar_t *clean = (wchar_t *)malloc((len + 1) * sizeof(wchar_t));
            if (!clean) { GlobalUnlock(hg); CloseClipboard(); return; }
            int j = 0;
            for (int i = 0; i < len; i++) {
                if (text[i] == L'\r') continue;
                clean[j++] = text[i];
            }
            clean[j] = 0;
            editor_insert_text(clean, j);
            free(clean);
            GlobalUnlock(hg);
        }
    }
    CloseClipboard();
}

void editor_ensure_cursor_visible(void) {
    Document *doc = current_doc();
    if (!doc) return;

    recalc_wrap_now(doc);

    bpos vline;
    if (doc->mode == MODE_PROSE && doc->wc.count > 0) {
        vline = wc_visual_line_of(&doc->wc, doc->cursor);
    } else {
        vline = pos_to_line(doc, doc->cursor);
    }
    int y = (int)(vline * g_editor.line_height);

    int edit_top = DPI(TITLEBAR_H + MENUBAR_H + TABBAR_H);
    int edit_h = g_editor.client_h - edit_top - DPI(STATUSBAR_H);

    if (y - doc->scroll_y < 0) {
        doc->target_scroll_y = y - g_editor.line_height;
    } else if (y - doc->scroll_y + g_editor.line_height > edit_h) {
        doc->target_scroll_y = y - edit_h + g_editor.line_height * 2;
    }

    if (doc->target_scroll_y < 0) doc->target_scroll_y = 0;

    if (doc->mode == MODE_CODE) {
        int cw = g_editor.char_width;
        int gw = gutter_width(doc);
        int edit_w = g_editor.client_w - DPI(SCROLLBAR_W) - gw;
        if (g_editor.show_minimap) edit_w -= DPI(MINIMAP_W);
        bpos col = pos_to_col(doc, doc->cursor);
        bpos line = pos_to_line(doc, doc->cursor);
        bpos ls = lc_line_start(&doc->lc, line);
        int cx = col_to_pixel_x(&doc->gb, ls, col, cw);
        int margin = cw * 4;
        if (cx - doc->scroll_x < 0) {
            doc->target_scroll_x = cx - margin;
        } else if (cx - doc->scroll_x + cw > edit_w) {
            doc->target_scroll_x = cx - edit_w + margin;
        }
        if (doc->target_scroll_x < 0) doc->target_scroll_x = 0;
    }
    if (doc->scroll_y != doc->target_scroll_y || doc->scroll_x != doc->target_scroll_x)
        start_scroll_animation();
}

void toggle_focus_mode(void) {
    g_editor.focus.active = !g_editor.focus.active;
}

void toggle_mode(void) {
    Document *doc = current_doc();
    if (doc) {
        doc->mode = (doc->mode == MODE_PROSE) ? MODE_CODE : MODE_PROSE;
    }
}
