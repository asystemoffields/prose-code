#include "prose_code.h"

Arena g_frame_arena;

void arena_init(Arena *a, size_t cap) {
    a->base = (char *)malloc(cap);
    a->used = 0;
    a->capacity = a->base ? cap : 0;
}

void *arena_alloc(Arena *a, size_t size) {
    size = (size + 7) & ~7;
    if (a->used + size > a->capacity) return NULL;
    void *p = a->base + a->used;
    a->used += size;
    return p;
}

void arena_reset(Arena *a) { a->used = 0; }

void safe_wcscpy(wchar_t *dst, int dst_count, const wchar_t *src) {
    if (dst_count <= 0) return;
    int i = 0;
    for (; i < dst_count - 1 && src[i]; i++) dst[i] = src[i];
    dst[i] = 0;
}

void gb_init(GapBuffer *gb, bpos initial_cap) {
    gb->total = initial_cap > GAP_INIT ? initial_cap : GAP_INIT;
    gb->buf = (wchar_t *)calloc(gb->total, sizeof(wchar_t));
    if (!gb->buf) { gb->total = 0; }
    gb->gap_start = 0;
    gb->gap_end = gb->total;
}

void gb_free(GapBuffer *gb) {
    free(gb->buf);
    gb->buf = NULL;
    gb->total = gb->gap_start = gb->gap_end = 0;
}

bpos gb_length(GapBuffer *gb) {
    return gb->total - (gb->gap_end - gb->gap_start);
}

wchar_t gb_char_at(GapBuffer *gb, bpos pos) {
    if (pos < 0 || pos >= gb_length(gb)) return 0;
    return pos < gb->gap_start ? gb->buf[pos] : gb->buf[pos + (gb->gap_end - gb->gap_start)];
}

void gb_grow(GapBuffer *gb, bpos needed) {
    bpos gap_size = gb->gap_end - gb->gap_start;
    if (gap_size >= needed) return;

    bpos new_total = gb->total + needed + GAP_GROW;
    wchar_t *new_buf = (wchar_t *)calloc(new_total, sizeof(wchar_t));
    if (!new_buf) return;

    memcpy(new_buf, gb->buf, gb->gap_start * sizeof(wchar_t));
    bpos after_gap = gb->total - gb->gap_end;
    memcpy(new_buf + new_total - after_gap, gb->buf + gb->gap_end, after_gap * sizeof(wchar_t));

    gb->gap_end = new_total - after_gap;
    gb->total = new_total;
    free(gb->buf);
    gb->buf = new_buf;
}

void gb_move_gap(GapBuffer *gb, bpos pos) {
    if (pos < 0) pos = 0;
    bpos len = gb_length(gb);
    if (pos > len) pos = len;
    if (pos == gb->gap_start) return;
    if (pos < gb->gap_start) {
        bpos count = gb->gap_start - pos;
        memmove(gb->buf + gb->gap_end - count, gb->buf + pos, count * sizeof(wchar_t));
        gb->gap_start = pos;
        gb->gap_end -= count;
    } else {
        bpos count = pos - gb->gap_start;
        memmove(gb->buf + gb->gap_start, gb->buf + gb->gap_end, count * sizeof(wchar_t));
        gb->gap_start += count;
        gb->gap_end += count;
    }
}

void gb_insert(GapBuffer *gb, bpos pos, const wchar_t *text, bpos len) {
    if (len <= 0 || !text) return;
    gb_grow(gb, len);
    bpos gap_size = gb->gap_end - gb->gap_start;
    if (gap_size < len) return;
    gb_move_gap(gb, pos);
    memcpy(gb->buf + gb->gap_start, text, len * sizeof(wchar_t));
    gb->gap_start += len;
    gb->mutation++;
}

void gb_delete(GapBuffer *gb, bpos pos, bpos len) {
    if (len <= 0) return;
    if (pos < 0) pos = 0;
    bpos text_len = gb_length(gb);
    if (pos > text_len) pos = text_len;
    if (pos + len > text_len) len = text_len - pos;
    if (len <= 0) return;
    gb_move_gap(gb, pos);
    gb->gap_end += len;
    if (gb->gap_end > gb->total) gb->gap_end = gb->total;
    gb->mutation++;
}

void gb_copy_range(GapBuffer *gb, bpos start, bpos len, wchar_t *dst) {
    if (start < 0) start = 0;
    bpos text_len = gb_length(gb);
    if (start + len > text_len) len = text_len - start;
    if (len <= 0) return;
    bpos gap_start = gb->gap_start;
    bpos gap_len = gb->gap_end - gb->gap_start;
    bpos end = start + len;

    if (end <= gap_start) {
        memcpy(dst, gb->buf + start, len * sizeof(wchar_t));
    } else if (start >= gap_start) {
        memcpy(dst, gb->buf + start + gap_len, len * sizeof(wchar_t));
    } else {
        bpos before = gap_start - start;
        memcpy(dst, gb->buf + start, before * sizeof(wchar_t));
        memcpy(dst + before, gb->buf + gb->gap_end, (len - before) * sizeof(wchar_t));
    }
}

wchar_t *gb_extract(GapBuffer *gb, bpos start, bpos len, Arena *a) {
    wchar_t *out = (wchar_t *)arena_alloc(a, (len + 1) * sizeof(wchar_t));
    if (!out) return NULL;
    gb_copy_range(gb, start, len, out);
    out[len] = 0;
    return out;
}

wchar_t *gb_extract_alloc(GapBuffer *gb, bpos start, bpos len) {
    wchar_t *out = (wchar_t *)malloc((len + 1) * sizeof(wchar_t));
    if (!out) return NULL;
    gb_copy_range(gb, start, len, out);
    out[len] = 0;
    return out;
}

void lc_init(LineCache *lc) {
    lc->capacity = 1024;
    lc->offsets = (bpos *)malloc(lc->capacity * sizeof(bpos));
    if (!lc->offsets) { lc->capacity = 0; lc->count = 0; lc->dirty = 1; return; }
    lc->offsets[0] = 0;
    lc->count = 1;
    lc->dirty = 1;
}

void lc_free(LineCache *lc) {
    free(lc->offsets);
}

void lc_rebuild(LineCache *lc, GapBuffer *gb) {
    if (!lc->offsets) return;
    lc->count = 0;
    lc->offsets[lc->count++] = 0;

    bpos logical_pos = 0;
    for (bpos i = 0; i < gb->gap_start; i++, logical_pos++) {
        if (gb->buf[i] == L'\n') {
            if (lc->count >= lc->capacity) {
                bpos new_cap = lc->capacity * 2;
                bpos *tmp = (bpos *)realloc(lc->offsets, new_cap * sizeof(bpos));
                if (!tmp) { lc->dirty = 1; return; }
                lc->offsets = tmp;
                lc->capacity = new_cap;
            }
            lc->offsets[lc->count++] = logical_pos + 1;
        }
    }
    for (bpos i = gb->gap_end; i < gb->total; i++, logical_pos++) {
        if (gb->buf[i] == L'\n') {
            if (lc->count >= lc->capacity) {
                bpos new_cap = lc->capacity * 2;
                bpos *tmp = (bpos *)realloc(lc->offsets, new_cap * sizeof(bpos));
                if (!tmp) { lc->dirty = 1; return; }
                lc->offsets = tmp;
                lc->capacity = new_cap;
            }
            lc->offsets[lc->count++] = logical_pos + 1;
        }
    }
    lc->dirty = 0;
}

bpos lc_line_of(LineCache *lc, bpos pos) {
    bpos lo = 0, hi = lc->count - 1;
    while (lo < hi) {
        bpos mid = (lo + hi + 1) / 2;
        if (lc->offsets[mid] <= pos) lo = mid;
        else hi = mid - 1;
    }
    return lo;
}

bpos lc_line_start(LineCache *lc, bpos line) {
    if (line < 0) return 0;
    if (line >= lc->count) return lc->offsets[lc->count - 1];
    return lc->offsets[line];
}

bpos lc_line_end(LineCache *lc, GapBuffer *gb, bpos line) {
    if (line + 1 < lc->count) return lc->offsets[line + 1] - 1;
    return gb_length(gb);
}

int lc_notify_insert(LineCache *lc, bpos pos, const wchar_t *text, bpos len) {
    if (lc->dirty) return 0;

    int newlines = 0;
    for (bpos i = 0; i < len; i++) {
        if (text[i] == L'\n') newlines++;
    }

    if (newlines == 0) {
        bpos line = lc_line_of(lc, pos);
        for (bpos i = line + 1; i < lc->count; i++)
            lc->offsets[i] += len;
        return 1;
    }

    if (newlines == 1 && len == 1) {
        bpos line = lc_line_of(lc, pos);
        if (lc->count >= lc->capacity) {
            bpos new_cap = lc->capacity * 2;
            bpos *tmp = (bpos *)realloc(lc->offsets, new_cap * sizeof(bpos));
            if (!tmp) return 0;
            lc->offsets = tmp;
            lc->capacity = new_cap;
        }
        for (bpos i = lc->count; i > line + 1; i--)
            lc->offsets[i] = lc->offsets[i - 1] + 1;
        lc->offsets[line + 1] = pos + 1;
        lc->count++;
        return 1;
    }

    return 0;
}

int lc_notify_delete(LineCache *lc, bpos pos, const wchar_t *deleted_text, bpos len) {
    if (lc->dirty) return 0;

    int newlines = 0;
    for (bpos i = 0; i < len; i++) {
        if (deleted_text[i] == L'\n') newlines++;
    }

    if (newlines == 0) {
        bpos line = lc_line_of(lc, pos);
        for (bpos i = line + 1; i < lc->count; i++)
            lc->offsets[i] -= len;
        return 1;
    }

    if (newlines == 1 && len == 1) {
        bpos line = lc_line_of(lc, pos);
        if (line + 1 < lc->count) {
            for (bpos i = line + 1; i < lc->count - 1; i++)
                lc->offsets[i] = lc->offsets[i + 1] - len;
            lc->count--;
        }
        return 1;
    }

    return 0;
}

void wc_init(WrapCache *wc) {
    wc->capacity = 1024;
    wc->entries = (WrapEntry *)malloc(wc->capacity * sizeof(WrapEntry));
    if (!wc->entries) wc->capacity = 0;
    wc->count = 0;
    wc->wrap_col = 0;
}

void wc_free(WrapCache *wc) {
    free(wc->entries);
    wc->entries = NULL;
    wc->count = 0;
}

void wc_push(WrapCache *wc, bpos pos, bpos line) {
    if (wc->count >= wc->capacity) {
        bpos new_cap = wc->capacity ? wc->capacity * 2 : 1024;
        WrapEntry *tmp = (WrapEntry *)realloc(wc->entries, new_cap * sizeof(WrapEntry));
        if (!tmp) return;
        wc->entries = tmp;
        wc->capacity = new_cap;
    }
    wc->entries[wc->count].pos = pos;
    wc->entries[wc->count].line = line;
    wc->count++;
}

void wc_rebuild(WrapCache *wc, GapBuffer *gb, LineCache *lc, int wrap_col) {
    wc->count = 0;
    wc->wrap_col = wrap_col;
    if (wrap_col <= 0) wrap_col = 80;

    wchar_t *buf = gb->buf;
    bpos gs = gb->gap_start;
    bpos goff = gb->gap_end - gb->gap_start;
    #define WC_CHAR(pos) (buf[(pos) < gs ? (pos) : (pos) + goff])

    for (bpos ln = 0; ln < lc->count; ln++) {
        bpos ls = lc->offsets[ln];
        bpos le = (ln + 1 < lc->count) ? lc->offsets[ln + 1] - 1 : gb_length(gb);
        bpos line_len = le - ls;

        if (line_len == 0) {
            wc_push(wc, ls, ln);
            continue;
        }

        bpos row_start = ls;
        int col = 0;
        bpos last_break = -1;

        for (bpos i = ls; i < le; i++) {
            wchar_t c = WC_CHAR(i);
            int cw_char = (c == L'\t') ? 4 : 1;
            col += cw_char;

            if (c == L' ' || c == L'\t') {
                last_break = i + 1;
            }

            if (col > wrap_col) {
                if (last_break > row_start) {
                    wc_push(wc, row_start, ln);
                    row_start = last_break;
                    col = 0;
                    for (bpos j = row_start; j < i; j++) {
                        wchar_t jc = WC_CHAR(j);
                        col += (jc == L'\t') ? 4 : 1;
                    }
                    col += cw_char;
                } else {
                    wc_push(wc, row_start, ln);
                    row_start = i;
                    col = cw_char;
                }
                last_break = -1;
            }
        }
        wc_push(wc, row_start, ln);
    }
    #undef WC_CHAR
}

bpos wc_visual_line_of(WrapCache *wc, bpos pos) {
    if (wc->count == 0) return 0;
    bpos lo = 0, hi = wc->count - 1;
    while (lo < hi) {
        bpos mid = (lo + hi + 1) / 2;
        if (wc->entries[mid].pos <= pos) lo = mid;
        else hi = mid - 1;
    }
    return lo;
}

bpos wc_visual_line_end(WrapCache *wc, GapBuffer *gb, LineCache *lc, bpos vline) {
    bpos logical_line = wc->entries[vline].line;
    if (vline + 1 < wc->count && wc->entries[vline + 1].line == logical_line) {
        return wc->entries[vline + 1].pos;
    }
    return lc_line_end(lc, gb, logical_line);
}

bpos wc_col_in_vline(WrapCache *wc, bpos pos, bpos vline) {
    return pos - wc->entries[vline].pos;
}

void undo_init(UndoStack *us) {
    us->capacity = UNDO_INIT_CAP;
    us->entries = (UndoEntry *)calloc(us->capacity, sizeof(UndoEntry));
    if (!us->entries) us->capacity = 0;
    us->count = us->current = us->next_group = 0;
}

void undo_push(UndoStack *us, UndoType type, bpos pos, const wchar_t *text, bpos len, bpos cursor_before, bpos cursor_after, int group) {
    for (int i = us->current; i < us->count; i++) {
        free(us->entries[i].text);
    }
    us->count = us->current;

    if (us->count >= us->capacity) {
        int new_cap = us->capacity * 2;
        UndoEntry *new_entries = (UndoEntry *)realloc(us->entries, new_cap * sizeof(UndoEntry));
        if (!new_entries) return;
        us->entries = new_entries;
        us->capacity = new_cap;
    }

    UndoEntry *e = &us->entries[us->count];
    e->type = type;
    e->pos = pos;
    e->len = len;
    e->cursor_before = cursor_before;
    e->cursor_after = cursor_after;
    e->group = group;
    e->text = (wchar_t *)malloc((len + 1) * sizeof(wchar_t));
    if (!e->text) return;
    memcpy(e->text, text, len * sizeof(wchar_t));
    e->text[len] = 0;

    us->count++;
    us->current = us->count;
}

void undo_clear(UndoStack *us) {
    for (int i = 0; i < us->count; i++) free(us->entries[i].text);
    us->count = us->current = 0;
}

void undo_free(UndoStack *us) {
    undo_clear(us);
    free(us->entries);
    us->entries = NULL;
    us->capacity = 0;
}
