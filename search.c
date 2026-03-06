#include "prose_code.h"

void search_update_matches(void) {
    SearchState *ss = &g_editor.search;
    Document *doc = current_doc();
    if (!doc || ss->query[0] == 0) {
        ss->match_count = 0;
        return;
    }

    free(ss->match_positions);
    ss->match_positions = NULL;
    ss->match_count = 0;

    int qlen = (int)wcslen(ss->query);
    bpos tlen = gb_length(&doc->gb);
    if (tlen < (bpos)qlen) return;
    int cap = 256;
    ss->match_positions = (bpos *)malloc(cap * sizeof(bpos));
    if (!ss->match_positions) return;

    wchar_t *text = (wchar_t *)malloc((tlen + 1) * sizeof(wchar_t));
    if (!text) { free(ss->match_positions); ss->match_positions = NULL; return; }
    gb_copy_range(&doc->gb, 0, tlen, text);

    for (bpos i = 0; i < tlen; i++) text[i] = towlower(text[i]);
    wchar_t lower_query[256];
    for (int i = 0; i < qlen; i++) lower_query[i] = towlower(ss->query[i]);
    lower_query[qlen] = 0;

    wchar_t first_ch = lower_query[0];
    for (bpos i = 0; i <= tlen - (bpos)qlen; i++) {
        if (text[i] != first_ch) continue;
        if (wcsncmp(text + i, lower_query, qlen) == 0) {
            if (ss->match_count >= cap) {
                cap *= 2;
                bpos *tmp = (bpos *)realloc(ss->match_positions, cap * sizeof(bpos));
                if (!tmp) break;
                ss->match_positions = tmp;
            }
            ss->match_positions[ss->match_count++] = i;
        }
    }

    free(text);

    if (ss->match_count > 0) {
        if (ss->current_match >= ss->match_count)
            ss->current_match = 0;
        bpos cursor = doc->cursor;
        for (int i = 0; i < ss->match_count; i++) {
            if (ss->match_positions[i] >= cursor) {
                ss->current_match = i;
                break;
            }
        }
    }
}

void toggle_search(void) {
    g_editor.search.active = !g_editor.search.active;
    if (!g_editor.search.active) {
        g_editor.search.match_count = 0;
        free(g_editor.search.match_positions);
        g_editor.search.match_positions = NULL;
        g_editor.search.replace_focused = 0;
    } else {
        if (g_editor.search.query[0] != 0)
            search_update_matches();
    }
}

void search_next(void) {
    SearchState *ss = &g_editor.search;
    if (ss->match_count == 0) return;
    ss->current_match = (ss->current_match + 1) % ss->match_count;
    Document *doc = current_doc();
    if (doc) {
        bpos pos = ss->match_positions[ss->current_match];
        doc->sel_anchor = pos;
        doc->cursor = pos + (bpos)wcslen(ss->query);
        editor_ensure_cursor_visible();
    }
}

void search_prev(void) {
    SearchState *ss = &g_editor.search;
    if (ss->match_count == 0) return;
    ss->current_match = (ss->current_match - 1 + ss->match_count) % ss->match_count;
    Document *doc = current_doc();
    if (doc) {
        bpos pos = ss->match_positions[ss->current_match];
        doc->sel_anchor = pos;
        doc->cursor = pos + (bpos)wcslen(ss->query);
        editor_ensure_cursor_visible();
    }
}

void do_replace(void) {
    SearchState *ss = &g_editor.search;
    Document *doc = current_doc();
    if (!doc || ss->match_count == 0) return;

    bpos pos = ss->match_positions[ss->current_match];
    int qlen = (int)wcslen(ss->query);
    int rlen = (int)wcslen(ss->replace_text);

    doc->cursor = pos;
    doc->sel_anchor = pos + qlen;
    editor_delete_selection();
    editor_insert_text(ss->replace_text, rlen);
    doc->cursor = pos + rlen;
    search_update_matches();
}

void do_replace_all(void) {
    SearchState *ss = &g_editor.search;
    Document *doc = current_doc();
    if (!doc || ss->match_count == 0) return;

    int qlen = (int)wcslen(ss->query);
    int rlen = (int)wcslen(ss->replace_text);
    bpos old_cursor = doc->cursor;
    bpos diff = rlen - qlen;

    int group = ++doc->undo.next_group;

    for (int i = 0; i < ss->match_count; i++) {
        bpos pos = ss->match_positions[i] + (bpos)i * diff;

        wchar_t *deleted = gb_extract_alloc(&doc->gb, pos, qlen);
        if (deleted) {
            undo_push(&doc->undo, UNDO_DELETE, pos, deleted, qlen, old_cursor, pos, group);
            free(deleted);
        }
        gb_delete(&doc->gb, pos, qlen);

        gb_insert(&doc->gb, pos, ss->replace_text, rlen);
        undo_push(&doc->undo, UNDO_INSERT, pos, ss->replace_text, rlen, pos, pos + rlen, group);
    }

    bpos total = gb_length(&doc->gb);
    if (ss->match_count > 0) {
        bpos last_pos = ss->match_positions[ss->match_count - 1] + (bpos)(ss->match_count - 1) * diff;
        doc->cursor = last_pos + rlen;
    }
    if (doc->cursor > total) doc->cursor = total;

    doc->modified = 1;
    recalc_lines(doc);
    update_stats(doc);
    search_update_matches();
}
