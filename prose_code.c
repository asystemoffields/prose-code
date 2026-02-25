/*
 * ╔═══════════════════════════════════════════════════════════════╗
 * ║  PROSE_CODE — A Pure C Text Editor for Prose & Code           ║
 * ║  Built with raw Win32 API, zero external dependencies         ║
 * ║                                                               ║
 * ╚═══════════════════════════════════════════════════════════════╝
 *
 * Architecture:
 *   - Gap buffer for O(1) local edits
 *   - Custom-drawn UI (no standard controls in edit area)
 *   - Double-buffered GDI rendering
 *   - Arena allocator for transient allocations
 *   - Windows ISpellChecker COM for spellcheck
 */

#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif
#define WIN32_LEAN_AND_MEAN
#define COBJMACROS
#define INITGUID

#include <windows.h>
#include <windowsx.h>
#include <commctrl.h>
#include <commdlg.h>
#include <shellapi.h>
#include <objbase.h>
#include <shlwapi.h>
#include <dwmapi.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#include <ctype.h>
#include <math.h>
#include <stddef.h>

/* Buffer position type — ptrdiff_t gives us pointer-width signed
 * integers (8 bytes on 64-bit), avoiding overflow on large files.
 * Use bpos for any text position, length, or line index.
 * Keep int for pixel coords, UI dimensions, flags, and enums. */
typedef ptrdiff_t bpos;

/* Portable format specifier for bpos in wide printf.
 * Usage: swprintf(buf, n, L"pos=%" BPOS_FMT, some_bpos); */
#ifdef _MSC_VER
  #define BPOS_FMT L"Id"
#else
  #define BPOS_FMT L"td"
#endif

#ifdef _MSC_VER
#pragma comment(lib, "user32")
#pragma comment(lib, "gdi32")
#pragma comment(lib, "comdlg32")
#pragma comment(lib, "shell32")
#pragma comment(lib, "ole32")
#pragma comment(lib, "shlwapi")
#pragma comment(lib, "dwmapi")
#pragma comment(lib, "comctl32")
#pragma comment(lib, "uxtheme")
#endif

/* ═══════════════════════════════════════════════════════════════
 * CONFIGURATION & THEME
 * ═══════════════════════════════════════════════════════════════ */

/* ── Runtime theme system ── */

typedef struct {
    COLORREF bg, bg_dark, surface0, surface1, surface2, overlay0;
    COLORREF text, subtext;
    COLORREF lavender, blue, sapphire, sky, teal, green;
    COLORREF yellow, peach, maroon, red, mauve, pink, flamingo, rosewater;
    COLORREF cursor, selection, activeline;
    COLORREF gutter, gutter_text;
    COLORREF tab_active, tab_inactive;
    COLORREF scrollbar_bg, scrollbar_th, scrollbar_hover;
    COLORREF accent, misspelled, focus_dim, search_hl;
    int is_dark;  /* 1 = dark title bar, 0 = light */
} Theme;

static const Theme THEME_DARK = {
    .bg          = RGB(28,  28,  34),   /* editor background — cool-tinted charcoal */
    .bg_dark     = RGB(22,  22,  28),   /* sidebars, title bar */
    .surface0    = RGB(38,  38,  46),   /* subtle borders */
    .surface1    = RGB(52,  52,  62),   /* active borders */
    .surface2    = RGB(70,  70,  82),   /* lighter accents */
    .overlay0    = RGB(120, 120, 135),  /* muted text */
    .text        = RGB(205, 208, 218),  /* primary text — warm off-white */
    .subtext     = RGB(145, 150, 168),  /* secondary text — soft lavender-gray */
    .lavender    = RGB(148, 160, 220),  /* calm lavender brand accent */
    .blue        = RGB(110, 168, 230),  /* keywords — softer sky blue */
    .sapphire    = RGB(96,  195, 178),  /* teal/cyan — muted jade */
    .sky         = RGB(145, 200, 240),  /* light blue — gentle */
    .teal        = RGB(96,  195, 178),
    .green       = RGB(158, 203, 155),  /* strings — sage green */
    .yellow      = RGB(220, 200, 140),  /* types/classes — warm wheat */
    .peach       = RGB(215, 160, 130),  /* string literals — soft terra cotta */
    .maroon      = RGB(200, 140, 148),
    .red         = RGB(230, 120, 110),  /* errors — warm coral */
    .mauve       = RGB(188, 148, 200),  /* keywords purple — dusty orchid */
    .pink        = RGB(195, 155, 220),
    .flamingo    = RGB(200, 172, 172),
    .rosewater   = RGB(210, 195, 195),
    .cursor      = RGB(148, 160, 220),  /* cursor matches accent for cohesion */
    .selection   = RGB(48,  58,  90),   /* muted slate-blue selection */
    .activeline  = RGB(34,  34,  42),
    .gutter      = RGB(22,  22,  28),
    .gutter_text = RGB(72,  74,  88),
    .tab_active  = RGB(28,  28,  34),
    .tab_inactive= RGB(22,  22,  28),
    .scrollbar_bg= RGB(28,  28,  34),
    .scrollbar_th= RGB(58,  58,  72),
    .scrollbar_hover = RGB(90,  92, 108),
    .accent      = RGB(148, 160, 220),  /* lavender accent — cohesive */
    .misspelled  = RGB(230, 120, 110),
    .focus_dim   = RGB(28,  28,  34),
    .search_hl   = RGB(72,  65,  36),   /* warm amber highlight */
    .is_dark     = 1,
};

static const Theme THEME_LIGHT = {
    .bg          = RGB(255, 255, 255),
    .bg_dark     = RGB(243, 243, 243),
    .surface0    = RGB(228, 228, 228),
    .surface1    = RGB(210, 210, 210),
    .surface2    = RGB(185, 185, 185),
    .overlay0    = RGB(130, 130, 140),
    .text        = RGB(30,  30,  30),
    .subtext     = RGB(100, 100, 110),
    .lavender    = RGB(220, 0, 120),   /* hot pink brand accent */
    .blue        = RGB(0,   102, 204),   /* keywords */
    .sapphire    = RGB(0,   122, 105),   /* teal identifiers */
    .sky         = RGB(38,  120, 178),
    .teal        = RGB(0,   122, 105),
    .green       = RGB(22,  126, 64),    /* strings */
    .yellow      = RGB(120, 100, 0),     /* types */
    .peach       = RGB(163, 21,  21),    /* string literals */
    .maroon      = RGB(163, 21,  21),
    .red         = RGB(205, 49,  49),    /* errors */
    .mauve       = RGB(136, 23,  152),   /* keywords purple */
    .pink        = RGB(160, 50,  170),
    .flamingo    = RGB(140, 100, 100),
    .rosewater   = RGB(130, 90,  90),
    .cursor      = RGB(0,   0,   0),
    .selection   = RGB(173, 214, 255),   /* light blue selection */
    .activeline  = RGB(245, 245, 245),
    .gutter      = RGB(243, 243, 243),
    .gutter_text = RGB(160, 160, 165),
    .tab_active  = RGB(255, 255, 255),
    .tab_inactive= RGB(243, 243, 243),
    .scrollbar_bg= RGB(255, 255, 255),
    .scrollbar_th= RGB(195, 195, 195),
    .scrollbar_hover = RGB(165, 165, 165),
    .accent      = RGB(47,  93,  163),
    .misspelled  = RGB(205, 49,  49),
    .focus_dim   = RGB(255, 255, 255),
    .search_hl   = RGB(255, 235, 120),
    .is_dark     = 0,
};

static Theme g_theme;
static int   g_theme_index = 0; /* 0=dark, 1=light */


/* ── DPI scaling ── */
static float g_dpi_scale = 1.0f;
#define DPI(x) ((int)((x) * g_dpi_scale + 0.5f))

static void apply_theme(int index);  /* forward decl — defined after g_editor */

/* Redirect all CLR_ macros through the runtime theme struct */
#define CLR_BG           (g_theme.bg)
#define CLR_BG_DARK      (g_theme.bg_dark)
#define CLR_SURFACE0     (g_theme.surface0)
#define CLR_SURFACE1     (g_theme.surface1)
#define CLR_SURFACE2     (g_theme.surface2)
#define CLR_OVERLAY0     (g_theme.overlay0)
#define CLR_TEXT         (g_theme.text)
#define CLR_SUBTEXT      (g_theme.subtext)
#define CLR_LAVENDER     (g_theme.lavender)
#define CLR_BLUE         (g_theme.blue)
#define CLR_SAPPHIRE     (g_theme.sapphire)
#define CLR_SKY          (g_theme.sky)
#define CLR_TEAL         (g_theme.teal)
#define CLR_GREEN        (g_theme.green)
#define CLR_YELLOW       (g_theme.yellow)
#define CLR_PEACH        (g_theme.peach)
#define CLR_MAROON       (g_theme.maroon)
#define CLR_RED          (g_theme.red)
#define CLR_MAUVE        (g_theme.mauve)
#define CLR_PINK         (g_theme.pink)
#define CLR_FLAMINGO     (g_theme.flamingo)
#define CLR_ROSEWATER    (g_theme.rosewater)
#define CLR_CURSOR       (g_theme.cursor)
#define CLR_SELECTION    (g_theme.selection)
#define CLR_ACTIVELINE   (g_theme.activeline)
#define CLR_GUTTER       (g_theme.gutter)
#define CLR_GUTTER_TEXT  (g_theme.gutter_text)
#define CLR_TAB_ACTIVE   (g_theme.tab_active)
#define CLR_TAB_INACTIVE (g_theme.tab_inactive)
#define CLR_SCROLLBAR_BG (g_theme.scrollbar_bg)
#define CLR_SCROLLBAR_TH (g_theme.scrollbar_th)
#define CLR_ACCENT       (g_theme.accent)
#define CLR_MISSPELLED   (g_theme.misspelled)
#define CLR_FOCUS_DIM    (g_theme.focus_dim)
#define CLR_SEARCH_HL    (g_theme.search_hl)

#define TITLEBAR_H       40
#define TABBAR_H         38
#define MENUBAR_H        28
#define STATUSBAR_H      30
#define GUTTER_PAD       16
#define LINE_NUM_CHARS   5
#define SCROLLBAR_W      10
#define MINIMAP_W        80
#define TAB_MAX_W        200
#define TAB_MIN_W        100
#define TAB_PAD          14
#define CURSOR_WIDTH     2
#define FONT_SIZE_DEFAULT 16

#define MAX_TABS         32
#define GAP_INIT         4096
#define GAP_GROW         4096
#define MAX_LINE_CACHE   65536
#define ARENA_SIZE       (1 << 20)

/* Timer IDs */
#define TIMER_BLINK      1
#define TIMER_SMOOTH     2
#define TIMER_AUTOSAVE   3

/* ═══════════════════════════════════════════════════════════════
 * DATA STRUCTURES
 * ═══════════════════════════════════════════════════════════════ */

/* Simple arena allocator for transient per-frame allocations */
typedef struct {
    char *base;
    size_t used;
    size_t capacity;
} Arena;

static Arena g_frame_arena;

static void arena_init(Arena *a, size_t cap) {
    a->base = (char *)malloc(cap);
    a->used = 0;
    a->capacity = a->base ? cap : 0;
}

static void *arena_alloc(Arena *a, size_t size) {
    size = (size + 7) & ~7; /* 8-byte align */
    if (a->used + size > a->capacity) return NULL;
    void *p = a->base + a->used;
    a->used += size;
    return p;
}

static void arena_reset(Arena *a) { a->used = 0; }

/* Safe wide string copy — always null-terminates, never overflows dst */
static void safe_wcscpy(wchar_t *dst, int dst_count, const wchar_t *src) {
    if (dst_count <= 0) return;
    int i = 0;
    for (; i < dst_count - 1 && src[i]; i++) dst[i] = src[i];
    dst[i] = 0;
}

/* Gap buffer — the heart of our text storage */
typedef struct {
    wchar_t *buf;
    bpos total;       /* total buffer size */
    bpos gap_start;   /* gap start index */
    bpos gap_end;     /* gap end index (exclusive) */
    int  mutation;    /* bumped on every insert/delete — cheap change detector */
} GapBuffer;

static void gb_init(GapBuffer *gb, bpos initial_cap) {
    gb->total = initial_cap > GAP_INIT ? initial_cap : GAP_INIT;
    gb->buf = (wchar_t *)calloc(gb->total, sizeof(wchar_t));
    if (!gb->buf) { gb->total = 0; }
    gb->gap_start = 0;
    gb->gap_end = gb->total;
}

static void gb_free(GapBuffer *gb) {
    free(gb->buf);
    gb->buf = NULL;
    gb->total = gb->gap_start = gb->gap_end = 0;
}

static bpos gb_length(GapBuffer *gb) {
    return gb->total - (gb->gap_end - gb->gap_start);
}

static wchar_t gb_char_at(GapBuffer *gb, bpos pos) {
    if (pos < 0 || pos >= gb_length(gb)) return 0;
    return pos < gb->gap_start ? gb->buf[pos] : gb->buf[pos + (gb->gap_end - gb->gap_start)];
}

static void gb_grow(GapBuffer *gb, bpos needed) {
    bpos gap_size = gb->gap_end - gb->gap_start;
    if (gap_size >= needed) return;

    bpos new_total = gb->total + needed + GAP_GROW;
    wchar_t *new_buf = (wchar_t *)calloc(new_total, sizeof(wchar_t));
    if (!new_buf) return; /* OOM — leave buffer unchanged */

    /* Copy before gap */
    memcpy(new_buf, gb->buf, gb->gap_start * sizeof(wchar_t));
    /* Copy after gap */
    bpos after_gap = gb->total - gb->gap_end;
    memcpy(new_buf + new_total - after_gap, gb->buf + gb->gap_end, after_gap * sizeof(wchar_t));

    gb->gap_end = new_total - after_gap;
    gb->total = new_total;
    free(gb->buf);
    gb->buf = new_buf;
}

static void gb_move_gap(GapBuffer *gb, bpos pos) {
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

static void gb_insert(GapBuffer *gb, bpos pos, const wchar_t *text, bpos len) {
    if (len <= 0 || !text) return;
    gb_grow(gb, len);
    bpos gap_size = gb->gap_end - gb->gap_start;
    if (gap_size < len) return; /* gb_grow failed (OOM) */
    gb_move_gap(gb, pos);
    memcpy(gb->buf + gb->gap_start, text, len * sizeof(wchar_t));
    gb->gap_start += len;
    gb->mutation++;
}

static void gb_delete(GapBuffer *gb, bpos pos, bpos len) {
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

/* Helper: copy a logical range [start, start+len) from gap buffer into dst.
   dst must have room for at least len wchar_t. */
static void gb_copy_range(GapBuffer *gb, bpos start, bpos len, wchar_t *dst) {
    if (start < 0) start = 0;
    bpos text_len = gb_length(gb);
    if (start + len > text_len) len = text_len - start;
    if (len <= 0) return;
    bpos gap_start = gb->gap_start;
    bpos gap_len = gb->gap_end - gb->gap_start;
    bpos end = start + len;

    if (end <= gap_start) {
        /* Entirely before gap */
        memcpy(dst, gb->buf + start, len * sizeof(wchar_t));
    } else if (start >= gap_start) {
        /* Entirely after gap */
        memcpy(dst, gb->buf + start + gap_len, len * sizeof(wchar_t));
    } else {
        /* Spans the gap */
        bpos before = gap_start - start;
        memcpy(dst, gb->buf + start, before * sizeof(wchar_t));
        memcpy(dst + before, gb->buf + gb->gap_end, (len - before) * sizeof(wchar_t));
    }
}

/* Extract text from gap buffer into a flat wchar_t array */
static wchar_t *gb_extract(GapBuffer *gb, bpos start, bpos len, Arena *a) {
    wchar_t *out = (wchar_t *)arena_alloc(a, (len + 1) * sizeof(wchar_t));
    if (!out) return NULL;
    gb_copy_range(gb, start, len, out);
    out[len] = 0;
    return out;
}

/* Extract text using malloc — for data that must outlive the frame (e.g. undo) */
static wchar_t *gb_extract_alloc(GapBuffer *gb, bpos start, bpos len) {
    wchar_t *out = (wchar_t *)malloc((len + 1) * sizeof(wchar_t));
    if (!out) return NULL;
    gb_copy_range(gb, start, len, out);
    out[len] = 0;
    return out;
}

/* Line offsets cache */
typedef struct {
    bpos *offsets;   /* offset of start of each line */
    bpos count;      /* number of lines */
    bpos capacity;
    int  dirty;       /* needs rebuild */
} LineCache;

static void lc_init(LineCache *lc) {
    lc->capacity = 1024;
    lc->offsets = (bpos *)malloc(lc->capacity * sizeof(bpos));
    if (!lc->offsets) { lc->capacity = 0; lc->count = 0; lc->dirty = 1; return; }
    lc->offsets[0] = 0;
    lc->count = 1;
    lc->dirty = 1;
}

static void lc_free(LineCache *lc) {
    free(lc->offsets);
}

static void lc_rebuild(LineCache *lc, GapBuffer *gb) {
    if (!lc->offsets) return;
    lc->count = 0;
    lc->offsets[lc->count++] = 0;

    /* Scan pre-gap segment directly — no branch per char */
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
    /* Scan post-gap segment directly */
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

static bpos lc_line_of(LineCache *lc, bpos pos) {
    /* binary search */
    bpos lo = 0, hi = lc->count - 1;
    while (lo < hi) {
        bpos mid = (lo + hi + 1) / 2;
        if (lc->offsets[mid] <= pos) lo = mid;
        else hi = mid - 1;
    }
    return lo;
}

static bpos lc_line_start(LineCache *lc, bpos line) {
    if (line < 0) return 0;
    if (line >= lc->count) return lc->offsets[lc->count - 1];
    return lc->offsets[line];
}

static bpos lc_line_end(LineCache *lc, GapBuffer *gb, bpos line) {
    if (line + 1 < lc->count) return lc->offsets[line + 1] - 1;
    return gb_length(gb);
}

/* Incremental line cache update after inserting text at pos.
 * If text contains no newlines, just shifts offsets — O(line_count) instead of O(doc_size).
 * Returns 1 if handled incrementally, 0 if caller should do full lc_rebuild. */
static int lc_notify_insert(LineCache *lc, bpos pos, const wchar_t *text, bpos len) {
    if (lc->dirty) return 0;

    /* Count newlines in inserted text */
    int newlines = 0;
    for (bpos i = 0; i < len; i++) {
        if (text[i] == L'\n') newlines++;
    }

    if (newlines == 0) {
        /* No newlines — just shift all offsets after the insertion point */
        bpos line = lc_line_of(lc, pos);
        /* Also shift this line's offset if insertion is exactly at its start */
        bpos start = (line < lc->count && lc->offsets[line] == pos && line > 0) ? line : line + 1;
        for (bpos i = start; i < lc->count; i++)
            lc->offsets[i] += len;
        return 1;
    }

    if (newlines == 1 && len == 1) {
        /* Single newline insert (Enter key) — insert one offset entry */
        bpos line = lc_line_of(lc, pos);
        /* Grow if needed */
        if (lc->count >= lc->capacity) {
            bpos new_cap = lc->capacity * 2;
            bpos *tmp = (bpos *)realloc(lc->offsets, new_cap * sizeof(bpos));
            if (!tmp) return 0; /* fall back to full rebuild */
            lc->offsets = tmp;
            lc->capacity = new_cap;
        }
        /* Shift offsets after insertion point up by 1 slot and add 1 to each */
        for (bpos i = lc->count; i > line + 1; i--)
            lc->offsets[i] = lc->offsets[i - 1] + 1;
        lc->offsets[line + 1] = pos + 1;
        lc->count++;
        return 1;
    }

    return 0; /* Multi-newline insert — fall back to full rebuild */
}

/* Incremental line cache update after deleting len chars starting at pos.
 * deleted_text is the text that was removed (needed to check for newlines).
 * Returns 1 if handled incrementally, 0 if caller should do full lc_rebuild. */
static int lc_notify_delete(LineCache *lc, bpos pos, const wchar_t *deleted_text, bpos len) {
    if (lc->dirty) return 0;

    int newlines = 0;
    for (bpos i = 0; i < len; i++) {
        if (deleted_text[i] == L'\n') newlines++;
    }

    if (newlines == 0) {
        /* No newlines — just shift offsets down */
        bpos line = lc_line_of(lc, pos);
        /* Also shift this line's offset if deletion is exactly at its start */
        bpos start = (line < lc->count && lc->offsets[line] == pos && line > 0) ? line : line + 1;
        for (bpos i = start; i < lc->count; i++)
            lc->offsets[i] -= len;
        return 1;
    }

    if (newlines == 1 && len == 1) {
        /* Single newline delete (Backspace on newline) — remove one offset entry */
        bpos line = lc_line_of(lc, pos);
        /* The newline at pos ends line 'line', so the offset for line+1 gets removed */
        if (line + 1 < lc->count) {
            for (bpos i = line + 1; i < lc->count - 1; i++)
                lc->offsets[i] = lc->offsets[i + 1] - len;
            lc->count--;
        }
        return 1;
    }

    return 0; /* Multi-newline delete — fall back to full rebuild */
}

/* ── Soft word-wrap cache ──
 * Maps logical text to "visual lines" (screen rows) for prose mode.
 * Each entry stores the text position where a visual line begins
 * and which logical line it belongs to. */
typedef struct {
    bpos pos;    /* text position where this visual line starts */
    bpos line;   /* logical line index */
} WrapEntry;

typedef struct {
    WrapEntry *entries;
    bpos count;
    bpos capacity;
    int  wrap_col;  /* wrap column used when last computed */
} WrapCache;

static void wc_init(WrapCache *wc) {
    wc->capacity = 1024;
    wc->entries = (WrapEntry *)malloc(wc->capacity * sizeof(WrapEntry));
    if (!wc->entries) wc->capacity = 0;
    wc->count = 0;
    wc->wrap_col = 0;
}

static void wc_free(WrapCache *wc) {
    free(wc->entries);
    wc->entries = NULL;
    wc->count = 0;
}

static void wc_push(WrapCache *wc, bpos pos, bpos line) {
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

/* Rebuild wrap cache for the entire document.
 * wrap_col = number of character columns available for text. */
static void wc_rebuild(WrapCache *wc, GapBuffer *gb, LineCache *lc, int wrap_col) {
    wc->count = 0;
    wc->wrap_col = wrap_col;
    if (wrap_col <= 0) wrap_col = 80;

    /* Pre-compute gap offset for direct buffer access — avoids function call per char */
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
                    col += cw_char; /* count current character */
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

/* Binary search: find the visual line index containing text position pos */
static bpos wc_visual_line_of(WrapCache *wc, bpos pos) {
    if (wc->count == 0) return 0;
    bpos lo = 0, hi = wc->count - 1;
    while (lo < hi) {
        bpos mid = (lo + hi + 1) / 2;
        if (wc->entries[mid].pos <= pos) lo = mid;
        else hi = mid - 1;
    }
    return lo;
}

/* End position of a visual line (exclusive — first char NOT in this vline) */
static bpos wc_visual_line_end(WrapCache *wc, GapBuffer *gb, LineCache *lc, bpos vline) {
    bpos logical_line = wc->entries[vline].line;
    if (vline + 1 < wc->count && wc->entries[vline + 1].line == logical_line) {
        return wc->entries[vline + 1].pos;
    }
    /* Last visual line of this logical line — goes to end of logical line */
    return lc_line_end(lc, gb, logical_line);
}

/* Column offset within a visual line */
static bpos wc_col_in_vline(WrapCache *wc, bpos pos, bpos vline) {
    return pos - wc->entries[vline].pos;
}

/* Undo entry */
typedef enum { UNDO_INSERT, UNDO_DELETE } UndoType;

typedef struct {
    UndoType type;
    bpos pos;
    wchar_t *text;
    bpos len;
    bpos cursor_before;
    bpos cursor_after;
    int  group;  /* non-zero: entries with same group are undone/redone atomically */
} UndoEntry;

typedef struct {
    UndoEntry *entries;  /* dynamically allocated */
    int count;
    int current;    /* points to next slot */
    int capacity;   /* allocated size of entries[] */
    int next_group; /* counter for generating unique group IDs */
    int save_point; /* current value at last save — used to track modified state */
} UndoStack;

#define UNDO_INIT_CAP 256

static void undo_init(UndoStack *us) {
    us->capacity = UNDO_INIT_CAP;
    us->entries = (UndoEntry *)calloc(us->capacity, sizeof(UndoEntry));
    us->count = us->current = us->next_group = 0;
}

static void undo_push(UndoStack *us, UndoType type, bpos pos, const wchar_t *text, bpos len, bpos cursor_before, bpos cursor_after, int group) {
    /* Discard any redo history */
    for (int i = us->current; i < us->count; i++) {
        free(us->entries[i].text);
    }
    us->count = us->current;

    /* Grow if needed — double capacity */
    if (us->count >= us->capacity) {
        int new_cap = us->capacity * 2;
        UndoEntry *new_entries = (UndoEntry *)realloc(us->entries, new_cap * sizeof(UndoEntry));
        if (!new_entries) return; /* OOM — silently drop this undo entry */
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
    if (!e->text) return; /* OOM — silently drop this undo entry */
    memcpy(e->text, text, len * sizeof(wchar_t));
    e->text[len] = 0;

    us->count++;
    us->current = us->count;
}

static void undo_clear(UndoStack *us) {
    for (int i = 0; i < us->count; i++) free(us->entries[i].text);
    us->count = us->current = 0;
}

static void undo_free(UndoStack *us) {
    undo_clear(us);
    free(us->entries);
    us->entries = NULL;
    us->capacity = 0;
}

/* Editor mode */
typedef enum { MODE_PROSE, MODE_CODE } EditorMode;

/* Document / Tab */
typedef struct {
    GapBuffer gb;
    LineCache lc;
    WrapCache wc;
    UndoStack undo;
    bpos cursor;         /* cursor position in text */
    bpos sel_anchor;     /* selection anchor (-1 if no selection) */
    int scroll_y;        /* scroll offset in pixels */
    int target_scroll_y; /* for smooth scrolling */
    int scroll_x;        /* horizontal scroll offset in pixels (code mode) */
    int target_scroll_x; /* for smooth horizontal scrolling */
    bpos desired_col;    /* desired column for up/down movement — visual column for wrap */
    EditorMode mode;
    wchar_t filepath[MAX_PATH];
    wchar_t title[64];
    int modified;
    bpos word_count;
    bpos char_count;
    bpos line_count;
    int stats_dirty; /* deferred: recount in render, not per-keystroke */
    int wrap_dirty;  /* deferred: wc_rebuild in render, not per-keystroke */
    /* Session baseline — snapshot at load/create for delta tracking */
    bpos session_start_words;
    bpos session_start_chars;
    bpos session_start_lines;
    /* Autosave tracking */
    unsigned int autosave_id;      /* unique per-document ID for untitled autosave filenames */
    int autosave_mutation_snapshot; /* gb.mutation at last autosave — skip if unchanged */
    DWORD autosave_last_time;      /* GetTickCount() of last autosave write */
    /* Block comment state cache — avoid full-document rescan every paint */
    int bc_cached_mutation;   /* gb.mutation when cache was computed */
    bpos bc_cached_line;      /* logical line at time of computation */
    int bc_cached_state;      /* 0 or 1: block comment state at cached line */
} Document;

/* Search state */
typedef struct {
    wchar_t query[256];
    int active;
    int current_match;
    int match_count;
    bpos *match_positions;
    int replace_active;
    int replace_focused;   /* 0 = search field, 1 = replace field has focus */
    wchar_t replace_text[256];
} SearchState;

/* Focus mode state */
typedef struct {
    int active;
    float dim_alpha;
} FocusMode;


/* ═══════════════════════════════════════════════════════════════
 * MENU BAR — custom-drawn dropdown menus
 * ═══════════════════════════════════════════════════════════════ */

#define MENU_ID_NEW         1
#define MENU_ID_OPEN        2
#define MENU_ID_SAVE        3
#define MENU_ID_CLOSE_TAB   4
#define MENU_ID_SEP         0   /* separator */
#define MENU_ID_UNDO        10
#define MENU_ID_REDO        11
#define MENU_ID_CUT         12
#define MENU_ID_COPY        13
#define MENU_ID_PASTE       14
#define MENU_ID_SELECT_ALL  15
#define MENU_ID_FIND        20
#define MENU_ID_REPLACE     21
#define MENU_ID_FIND_NEXT   22
#define MENU_ID_TOGGLE_MODE 30
#define MENU_ID_MINIMAP     31
#define MENU_ID_FOCUS       32
#define MENU_ID_STATS       33
#define MENU_ID_THEME       34
#define MENU_ID_ZOOM_IN     35
#define MENU_ID_ZOOM_OUT    36

typedef struct {
    const wchar_t *label;
    const wchar_t *shortcut;
    int id;
} MenuItem;

typedef struct {
    const wchar_t *label;
    const MenuItem *items;
    int item_count;
} MenuDef;

static const MenuItem g_file_items[] = {
    { L"New Tab",            L"Ctrl+N",        MENU_ID_NEW },
    { L"Open File...",       L"Ctrl+O",        MENU_ID_OPEN },
    { L"Save",               L"Ctrl+S",        MENU_ID_SAVE },
    { NULL, NULL, MENU_ID_SEP },
    { L"Close Tab",          L"Ctrl+W",        MENU_ID_CLOSE_TAB },
};

static const MenuItem g_edit_items[] = {
    { L"Undo",               L"Ctrl+Z",        MENU_ID_UNDO },
    { L"Redo",               L"Ctrl+Y",        MENU_ID_REDO },
    { NULL, NULL, MENU_ID_SEP },
    { L"Cut",                L"Ctrl+X",        MENU_ID_CUT },
    { L"Copy",               L"Ctrl+C",        MENU_ID_COPY },
    { L"Paste",              L"Ctrl+V",        MENU_ID_PASTE },
    { NULL, NULL, MENU_ID_SEP },
    { L"Select All",         L"Ctrl+A",        MENU_ID_SELECT_ALL },
};

static const MenuItem g_search_items[] = {
    { L"Find",               L"Ctrl+F",        MENU_ID_FIND },
    { L"Replace",            L"Ctrl+H",        MENU_ID_REPLACE },
    { L"Find Next",          L"Ctrl+G",        MENU_ID_FIND_NEXT },
};

static const MenuItem g_view_items[] = {
    { L"Toggle Prose/Code",  L"Ctrl+M",        MENU_ID_TOGGLE_MODE },
    { L"Toggle Minimap",     L"Ctrl+Shift+M",  MENU_ID_MINIMAP },
    { L"Focus Mode",         L"Ctrl+D",        MENU_ID_FOCUS },
    { L"Session Stats",      L"Ctrl+I",        MENU_ID_STATS },
    { NULL, NULL, MENU_ID_SEP },
    { L"Toggle Theme",       L"Ctrl+T",        MENU_ID_THEME },
    { L"Zoom In",            L"Ctrl++",        MENU_ID_ZOOM_IN },
    { L"Zoom Out",           L"Ctrl+-",        MENU_ID_ZOOM_OUT },
};

#define MENU_COUNT 4
static const MenuDef g_menus[MENU_COUNT] = {
    { L"File",   g_file_items,   sizeof(g_file_items)/sizeof(g_file_items[0]) },
    { L"Edit",   g_edit_items,   sizeof(g_edit_items)/sizeof(g_edit_items[0]) },
    { L"Search", g_search_items, sizeof(g_search_items)/sizeof(g_search_items[0]) },
    { L"View",   g_view_items,   sizeof(g_view_items)/sizeof(g_view_items[0]) },
};

/* Global editor state */
typedef struct {
    HWND hwnd;
    HDC hdc_back;
    HBITMAP bmp_back;
    HBITMAP bmp_back_old; /* original bitmap to restore before deletion */
    HFONT font_main;
    HFONT font_bold;
    HFONT font_italic;
    HFONT font_ui;
    HFONT font_ui_small;
    HFONT font_title;
    int font_size;
    int char_width;
    int line_height;
    int client_w;
    int client_h;

    Document *tabs[MAX_TABS];
    int tab_count;
    int active_tab;

    SearchState search;
    FocusMode focus;

    int cursor_visible;
    int cursor_phase; /* for smooth blinking */
    DWORD cursor_last_active; /* timestamp for fade reset on edit */
    int mouse_captured;
    int titlebar_dragging;
    int titlebar_hover_btn; /* 0=none, 1=minimize, 2=maximize, 3=close */
    POINT drag_start;

    /* Minimap */
    int show_minimap;

    /* Scrollbar interaction */
    int scrollbar_dragging;
    int scrollbar_drag_offset; /* offset from top of thumb to mouse */
    int scrollbar_hover;       /* mouse is over scrollbar */

    /* Session stats overlay */
    int show_stats_screen;
    DWORD session_start_time;  /* GetTickCount() at app launch */
    HFONT font_stats_hero;     /* persistent big number font for stats screen */

    /* Autosave */
    wchar_t autosave_dir[MAX_PATH];
    unsigned int next_autosave_id;  /* monotonic counter for untitled doc IDs */

    /* Menu bar state */
    int menu_open;          /* -1 = closed, 0..3 = which menu dropdown is open */
    int menu_hover_item;    /* hovered item index within open dropdown (-1 = none) */
    int menu_bar_widths[MENU_COUNT]; /* cached pixel widths per menu label */

    /* Perf: skip chrome redraws during smooth scroll — only editor area changes */
    int scroll_only_repaint;

} EditorState;

static EditorState g_editor;

/* Apply a theme and update the window's dark/light title bar attribute */
static void apply_theme(int index) {
    g_theme_index = index;
    if (index == 0) g_theme = THEME_DARK;
    else            g_theme = THEME_LIGHT;

    if (g_editor.hwnd) {
        BOOL dark = g_theme.is_dark;
        DwmSetWindowAttribute(g_editor.hwnd, 20, &dark, sizeof(dark));
        /* Force title bar repaint */
        SetWindowPos(g_editor.hwnd, NULL, 0, 0, 0, 0,
            SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_FRAMECHANGED);
        InvalidateRect(g_editor.hwnd, NULL, FALSE);
    }
}

/* ═══════════════════════════════════════════════════════════════
 * FORWARD DECLARATIONS
 * ═══════════════════════════════════════════════════════════════ */

static Document *current_doc(void);
static void editor_insert_char(wchar_t c);
static void editor_insert_text(const wchar_t *text, bpos len);
static void editor_delete_selection(void);
static void editor_backspace(void);
static void editor_delete_forward(void);
static void editor_move_cursor(bpos pos, int extend_selection);
static void editor_ensure_cursor_visible(void);
static void update_stats(Document *doc);
static void recalc_lines(Document *doc);
static void render(HDC hdc);
static void open_file_dialog(void);
static void save_file_dialog(void);
static int  save_file_dialog_for_doc(Document *doc);
static void save_current_file(void);
static void new_tab(void);
static void close_tab(int idx);
static void editor_undo(void);
static void editor_redo(void);
static void editor_select_all(void);
static void editor_copy(void);
static void editor_cut(void);
static void editor_paste(void);
static void toggle_focus_mode(void);
static void toggle_mode(void);
static bpos pos_to_line(Document *doc, bpos pos);
static bpos pos_to_col(Document *doc, bpos pos);
static bpos line_col_to_pos(Document *doc, bpos line, bpos col);
static bpos selection_start(Document *doc);
static bpos selection_end(Document *doc);
static int has_selection(Document *doc);
static void toggle_search(void);
static int gutter_width(Document *doc);
static int compute_block_comment_state(GapBuffer *gb, bpos up_to);
static int advance_block_comment_state_for_line(GapBuffer *gb, bpos ls, bpos le, int in_bc);
static void autosave_ensure_dir(void);
static void autosave_path_for_doc(Document *doc, wchar_t *out);
static int  write_file_atomic(const wchar_t *final_path, const char *data, int data_len);
static void autosave_write(Document *doc);
static void autosave_tick(void);
static void autosave_delete_for_doc(Document *doc);
static void autosave_cleanup_all(void);
static void autosave_recover(void);
static int  prompt_save_doc(int tab_idx);
static void search_next(void);
static void search_prev(void);
static void do_replace(void);
static void do_replace_all(void);
static void search_update_matches(void);
static bpos find_matching_bracket(GapBuffer *gb, bpos pos);

/* ═══════════════════════════════════════════════════════════════
 * SPELL CHECKER — Windows ISpellChecker COM API (Win8+)
 *
 * We define the COM vtables manually so we don't need the
 * Windows 8 SDK headers — just the binary COM ABI.
 * Results are cached in a hash set so we call COM at most
 * once per unique word, keeping rendering fast.
 * ═══════════════════════════════════════════════════════════════ */

/* ---- COM interface definitions (binary-compatible vtables) ---- */

/* GUIDs for the Spell Checking API */
static const GUID CLSID_SpellCheckerFactory =
    {0x7AB36653, 0x1796, 0x484B, {0xBD, 0xFA, 0xE7, 0x4F, 0x1D, 0xB7, 0xC1, 0xDC}};
static const GUID IID_ISpellCheckerFactory =
    {0x8E018A9D, 0x2415, 0x4677, {0xBF, 0x08, 0x79, 0x4E, 0xA6, 0x1F, 0x94, 0xBB}};

/* CORRECTIVE_ACTION enum from spellcheck.h */
typedef enum {
    CORRECTIVE_ACTION_NONE          = 0,
    CORRECTIVE_ACTION_GET_SUGGESTIONS = 1,
    CORRECTIVE_ACTION_REPLACE       = 2,
    CORRECTIVE_ACTION_DELETE        = 3
} CORRECTIVE_ACTION;

/* ISpellingError vtable */
typedef struct ISpellingErrorVtbl {
    /* IUnknown */
    HRESULT (STDMETHODCALLTYPE *QueryInterface)(void *This, REFIID riid, void **ppv);
    ULONG   (STDMETHODCALLTYPE *AddRef)(void *This);
    ULONG   (STDMETHODCALLTYPE *Release)(void *This);
    /* ISpellingError */
    HRESULT (STDMETHODCALLTYPE *get_StartIndex)(void *This, ULONG *value);
    HRESULT (STDMETHODCALLTYPE *get_Length)(void *This, ULONG *value);
    HRESULT (STDMETHODCALLTYPE *get_CorrectiveAction)(void *This, CORRECTIVE_ACTION *value);
    HRESULT (STDMETHODCALLTYPE *get_Replacement)(void *This, LPWSTR *value);
} ISpellingErrorVtbl;

typedef struct { ISpellingErrorVtbl *lpVtbl; } ISpellingError;

/* IEnumSpellingError vtable */
typedef struct IEnumSpellingErrorVtbl {
    /* IUnknown */
    HRESULT (STDMETHODCALLTYPE *QueryInterface)(void *This, REFIID riid, void **ppv);
    ULONG   (STDMETHODCALLTYPE *AddRef)(void *This);
    ULONG   (STDMETHODCALLTYPE *Release)(void *This);
    /* IEnumSpellingError */
    HRESULT (STDMETHODCALLTYPE *Next)(void *This, ISpellingError **value);
} IEnumSpellingErrorVtbl;

typedef struct { IEnumSpellingErrorVtbl *lpVtbl; } IEnumSpellingError;

/* ISpellChecker vtable (we only need Check) */
typedef struct ISpellCheckerVtbl {
    /* IUnknown */
    HRESULT (STDMETHODCALLTYPE *QueryInterface)(void *This, REFIID riid, void **ppv);
    ULONG   (STDMETHODCALLTYPE *AddRef)(void *This);
    ULONG   (STDMETHODCALLTYPE *Release)(void *This);
    /* ISpellChecker */
    HRESULT (STDMETHODCALLTYPE *get_LanguageTag)(void *This, LPWSTR *value);
    HRESULT (STDMETHODCALLTYPE *Check)(void *This, LPCWSTR text, IEnumSpellingError **value);
    HRESULT (STDMETHODCALLTYPE *Suggest)(void *This, LPCWSTR word, void **value);
    HRESULT (STDMETHODCALLTYPE *Add)(void *This, LPCWSTR word);
    HRESULT (STDMETHODCALLTYPE *Ignore)(void *This, LPCWSTR word);
    HRESULT (STDMETHODCALLTYPE *AutoCorrect)(void *This, LPCWSTR from, LPCWSTR to);
    HRESULT (STDMETHODCALLTYPE *GetOptionValue)(void *This, LPCWSTR optionId, BYTE *value);
    HRESULT (STDMETHODCALLTYPE *get_OptionIds)(void *This, void **value);
    HRESULT (STDMETHODCALLTYPE *get_Id)(void *This, LPWSTR *value);
    HRESULT (STDMETHODCALLTYPE *get_LocalizedName)(void *This, LPWSTR *value);
    HRESULT (STDMETHODCALLTYPE *add_SpellCheckerChanged)(void *This, void *handler, DWORD *token);
    HRESULT (STDMETHODCALLTYPE *remove_SpellCheckerChanged)(void *This, DWORD token);
    HRESULT (STDMETHODCALLTYPE *GetOptionDescription)(void *This, LPCWSTR optionId, void **value);
    HRESULT (STDMETHODCALLTYPE *ComprehensiveCheck)(void *This, LPCWSTR text, IEnumSpellingError **value);
} ISpellCheckerVtbl;

typedef struct { ISpellCheckerVtbl *lpVtbl; } ISpellChecker;

/* ISpellCheckerFactory vtable */
typedef struct ISpellCheckerFactoryVtbl {
    /* IUnknown */
    HRESULT (STDMETHODCALLTYPE *QueryInterface)(void *This, REFIID riid, void **ppv);
    ULONG   (STDMETHODCALLTYPE *AddRef)(void *This);
    ULONG   (STDMETHODCALLTYPE *Release)(void *This);
    /* ISpellCheckerFactory */
    HRESULT (STDMETHODCALLTYPE *get_SupportedLanguages)(void *This, void **value);
    HRESULT (STDMETHODCALLTYPE *IsSupported)(void *This, LPCWSTR lang, BOOL *value);
    HRESULT (STDMETHODCALLTYPE *CreateSpellChecker)(void *This, LPCWSTR lang, ISpellChecker **value);
} ISpellCheckerFactoryVtbl;

typedef struct { ISpellCheckerFactoryVtbl *lpVtbl; } ISpellCheckerFactory;

/* ---- Result cache: hash set of (word -> correct/misspelled) ---- */

#define SPELL_CACHE_BUCKETS 4096
#define SPELL_CACHE_MASK    (SPELL_CACHE_BUCKETS - 1)

typedef struct SpellCacheNode {
    wchar_t *word;
    int      correct;  /* 1 = ok, 0 = misspelled */
    struct SpellCacheNode *next;
} SpellCacheNode;

static SpellCacheNode *g_spell_cache[SPELL_CACHE_BUCKETS];
static int             g_spell_cache_count = 0;
#define SPELL_CACHE_MAX 8192
static ISpellChecker  *g_spell_checker = NULL;
static int             g_spell_loaded  = 0;
static void spell_cache_free(void); /* forward decl for eviction in insert */

static unsigned int spell_hash(const wchar_t *word) {
    unsigned int h = 5381;
    while (*word) {
        h = ((h << 5) + h) + towlower(*word);
        word++;
    }
    return h;
}

/* Look up a word in the result cache. Returns: 1=cached-correct, 0=cached-bad, -1=not cached */
static int spell_cache_lookup(const wchar_t *lower, int len) {
    unsigned int h = spell_hash(lower) & SPELL_CACHE_MASK;
    for (SpellCacheNode *n = g_spell_cache[h]; n; n = n->next) {
        if (wcsncmp(n->word, lower, len) == 0 && n->word[len] == 0)
            return n->correct;
    }
    return -1;
}

static void spell_cache_insert(const wchar_t *lower, int len, int correct) {
    /* Evict half the cache if it's grown too large (avoids render hitch) */
    if (g_spell_cache_count >= SPELL_CACHE_MAX) {
        int evicted = 0;
        for (int i = 0; i < SPELL_CACHE_BUCKETS; i += 2) {
            SpellCacheNode *n = g_spell_cache[i];
            while (n) {
                SpellCacheNode *next = n->next;
                free(n->word);
                free(n);
                evicted++;
                n = next;
            }
            g_spell_cache[i] = NULL;
        }
        g_spell_cache_count -= evicted;
    }
    unsigned int h = spell_hash(lower) & SPELL_CACHE_MASK;
    SpellCacheNode *n = (SpellCacheNode *)malloc(sizeof(SpellCacheNode));
    if (!n) return;
    n->word = (wchar_t *)malloc((len + 1) * sizeof(wchar_t));
    if (!n->word) { free(n); return; }
    memcpy(n->word, lower, len * sizeof(wchar_t));
    n->word[len] = 0;
    n->correct = correct;
    n->next = g_spell_cache[h];
    g_spell_cache[h] = n;
    g_spell_cache_count++;
}

/* Check a single word. Returns 1 if correct, 0 if misspelled. */
static int spell_check(const wchar_t *word, int len) {
    if (len <= 1) return 1;
    if (!g_spell_loaded) return 1;

    /* Skip words with digits */
    for (int i = 0; i < len; i++) {
        if (iswdigit(word[i])) return 1;
    }

    /* Lowercase for cache key */
    wchar_t lower[64];
    if (len >= 64) return 1;
    for (int i = 0; i < len; i++) lower[i] = towlower(word[i]);
    lower[len] = 0;

    /* Check cache first */
    int cached = spell_cache_lookup(lower, len);
    if (cached >= 0) return cached;

    /* Ask Windows ISpellChecker */
    if (g_spell_checker) {
        /* Null-terminate the word for COM */
        wchar_t tmp[64];
        memcpy(tmp, word, len * sizeof(wchar_t));
        tmp[len] = 0;

        IEnumSpellingError *errors = NULL;
        HRESULT hr = g_spell_checker->lpVtbl->Check(g_spell_checker, tmp, &errors);
        if (SUCCEEDED(hr) && errors) {
            ISpellingError *err = NULL;
            hr = errors->lpVtbl->Next(errors, &err);
            if (err) {
                /* There's at least one error → word is misspelled */
                err->lpVtbl->Release(err);
                errors->lpVtbl->Release(errors);
                spell_cache_insert(lower, len, 0);
                return 0;
            }
            errors->lpVtbl->Release(errors);
            /* No errors → word is correct */
            spell_cache_insert(lower, len, 1);
            return 1;
        }
        if (errors) errors->lpVtbl->Release(errors);
    }

    /* If COM isn't available, assume correct (don't annoy the user) */
    return 1;
}

static void spell_init(void) {
    memset(g_spell_cache, 0, sizeof(g_spell_cache));

    /* Try to create ISpellChecker via COM (Windows 8+) */
    ISpellCheckerFactory *factory = NULL;
    HRESULT hr = CoCreateInstance(
        &CLSID_SpellCheckerFactory, NULL, CLSCTX_INPROC_SERVER,
        &IID_ISpellCheckerFactory, (void **)&factory);

    if (SUCCEEDED(hr) && factory) {
        /* Check that en-US is supported */
        BOOL supported = FALSE;
        factory->lpVtbl->IsSupported(factory, L"en-US", &supported);

        if (supported) {
            hr = factory->lpVtbl->CreateSpellChecker(factory, L"en-US", &g_spell_checker);
            if (SUCCEEDED(hr) && g_spell_checker) {
                g_spell_loaded = 1;
            }
        }
        factory->lpVtbl->Release(factory);
    }

    if (!g_spell_loaded) {
        /* COM unavailable (Windows 7 or error) — spellcheck disabled.
         * We don't flag anything rather than produce false positives. */
        g_spell_loaded = 0;
    }
}

/* Free the spell cache hash table */
static void spell_cache_free(void) {
    for (int i = 0; i < SPELL_CACHE_BUCKETS; i++) {
        SpellCacheNode *n = g_spell_cache[i];
        while (n) {
            SpellCacheNode *next = n->next;
            free(n->word);
            free(n);
            n = next;
        }
        g_spell_cache[i] = NULL;
    }
    g_spell_cache_count = 0;
}

/* ═══════════════════════════════════════════════════════════════
 * SYNTAX HIGHLIGHTING
 * ═══════════════════════════════════════════════════════════════ */

typedef enum {
    TOK_NORMAL = 0,
    TOK_KEYWORD,
    TOK_STRING,
    TOK_COMMENT,
    TOK_NUMBER,
    TOK_PREPROCESSOR,
    TOK_TYPE,
    TOK_FUNCTION,
    TOK_OPERATOR,
    /* Markdown tokens */
    TOK_MD_HEADING,
    TOK_MD_BOLD,
    TOK_MD_ITALIC,
    TOK_MD_CODE,
    TOK_MD_LINK,
    TOK_MD_BLOCKQUOTE,
    TOK_MD_LIST,
    TOK_MISSPELLED,
} SynToken;

static COLORREF token_color(SynToken t) {
    switch (t) {
        case TOK_KEYWORD:       return CLR_MAUVE;
        case TOK_STRING:        return CLR_GREEN;
        case TOK_COMMENT:       return CLR_OVERLAY0;
        case TOK_NUMBER:        return CLR_PEACH;
        case TOK_PREPROCESSOR:  return CLR_PINK;
        case TOK_TYPE:          return CLR_YELLOW;
        case TOK_FUNCTION:      return CLR_BLUE;
        case TOK_OPERATOR:      return CLR_SKY;
        case TOK_MD_HEADING:    return CLR_MAUVE;
        case TOK_MD_BOLD:       return CLR_PEACH;
        case TOK_MD_ITALIC:     return CLR_FLAMINGO;
        case TOK_MD_CODE:       return CLR_GREEN;
        case TOK_MD_LINK:       return CLR_BLUE;
        case TOK_MD_BLOCKQUOTE: return CLR_OVERLAY0;
        case TOK_MD_LIST:       return CLR_TEAL;
        case TOK_MISSPELLED:    return CLR_MISSPELLED;
        default:                return CLR_TEXT;
    }
}

/* C/C++ keywords */
static const wchar_t *c_keywords[] = {
    /* C keywords */
    L"auto",L"break",L"case",L"char",L"const",L"continue",L"default",
    L"do",L"double",L"else",L"enum",L"extern",L"float",L"for",L"goto",
    L"if",L"inline",L"int",L"long",L"register",L"restrict",L"return",
    L"short",L"signed",L"sizeof",L"static",L"struct",L"switch",
    L"typedef",L"union",L"unsigned",L"void",L"volatile",L"while",
    L"bool",L"true",L"false",L"NULL",L"nullptr",
    /* C++ extras */
    L"class",L"namespace",L"template",L"typename",L"virtual",L"override",
    L"public",L"private",L"protected",L"new",L"delete",L"this",
    L"try",L"catch",L"throw",L"using",L"const_cast",L"dynamic_cast",
    L"static_cast",L"reinterpret_cast",L"noexcept",L"constexpr",
    L"decltype",L"explicit",L"friend",L"mutable",
    /* Python keywords */
    L"def",L"import",L"from",L"as",L"pass",L"lambda",
    L"with",L"yield",L"assert",L"raise",L"except",L"finally",
    L"global",L"nonlocal",L"del",L"in",L"not",L"and",L"or",L"is",
    L"None",L"True",L"False",L"self",L"elif",L"async",L"await",
    /* JS/TS keywords */
    L"function",L"var",L"let",
    L"export",L"extends",L"implements",
    L"interface",L"type",L"declare",L"module",L"require",
    L"undefined",L"NaN",L"Infinity",L"arguments",L"of",
    /* Rust keywords */
    L"fn",L"mut",L"pub",L"crate",L"mod",L"use",L"impl",
    L"trait",L"where",L"loop",L"match",L"ref",L"move",L"unsafe",
    L"dyn",L"Box",L"Vec",L"String",L"Option",L"Result",L"Some",
    L"Ok",L"Err",L"Self",L"super",
    NULL
};

/* Keyword hash set for O(1) lookup */
#define KW_HASH_BUCKETS 256
#define KW_HASH_MASK    (KW_HASH_BUCKETS - 1)
typedef struct KwNode { const wchar_t *word; int len; struct KwNode *next; } KwNode;
static KwNode *g_kw_table[KW_HASH_BUCKETS];
static KwNode  g_kw_pool[256]; /* pre-allocated nodes (enough for all keywords) */

static unsigned int kw_hash(const wchar_t *word, int len) {
    unsigned int h = 5381;
    for (int i = 0; i < len; i++) h = ((h << 5) + h) + word[i];
    return h;
}

static void kw_table_init(void) {
    int pool_idx = 0;
    for (int i = 0; c_keywords[i]; i++) {
        int len = (int)wcslen(c_keywords[i]);
        unsigned int h = kw_hash(c_keywords[i], len) & KW_HASH_MASK;
        /* Check for duplicate before inserting */
        int dup = 0;
        for (KwNode *n = g_kw_table[h]; n; n = n->next) {
            if (n->len == len && wcsncmp(n->word, c_keywords[i], len) == 0) { dup = 1; break; }
        }
        if (dup) continue;
        if (pool_idx >= 256) break; /* pool exhausted */
        KwNode *n = &g_kw_pool[pool_idx++];
        n->word = c_keywords[i];
        n->len = len;
        n->next = g_kw_table[h];
        g_kw_table[h] = n;
    }
}

static int is_c_keyword(const wchar_t *word, int len) {
    unsigned int h = kw_hash(word, len) & KW_HASH_MASK;
    for (KwNode *n = g_kw_table[h]; n; n = n->next) {
        if (n->len == len && wcsncmp(n->word, word, len) == 0) return 1;
    }
    return 0;
}

/* ═══════════════════════════════════════════════════════════════
 * DOCUMENT MANAGEMENT
 * ═══════════════════════════════════════════════════════════════ */

static Document *doc_create(void) {
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

static void doc_free(Document *doc) {
    gb_free(&doc->gb);
    lc_free(&doc->lc);
    wc_free(&doc->wc);
    undo_free(&doc->undo);
    free(doc);
}

static Document *current_doc(void) {
    if (g_editor.active_tab < 0 || g_editor.active_tab >= g_editor.tab_count) return NULL;
    return g_editor.tabs[g_editor.active_tab];
}

static void recalc_lines(Document *doc) {
    lc_rebuild(&doc->lc, &doc->gb);
    doc->line_count = doc->lc.count;
    /* Defer wrap cache rebuild to render frame — avoids multiple rebuilds per frame */
    if (doc->mode == MODE_PROSE && doc->wc.wrap_col > 0) {
        doc->wrap_dirty = 1;
    }
}

/* Called once per render frame, before drawing */
static void recalc_wrap_now(Document *doc) {
    if (!doc->wrap_dirty) return;
    doc->wrap_dirty = 0;
    if (doc->mode == MODE_PROSE && doc->wc.wrap_col > 0) {
        wc_rebuild(&doc->wc, &doc->gb, &doc->lc, doc->wc.wrap_col);
    }
}

static void update_stats(Document *doc) {
    doc->stats_dirty = 1;
}

static void update_stats_now(Document *doc) {
    if (!doc->stats_dirty) return;
    doc->stats_dirty = 0;
    GapBuffer *gb = &doc->gb;
    doc->char_count = gb_length(gb);
    bpos words = 0;
    int in_word = 0;

    /* Scan pre-gap segment directly — no per-char branch */
    for (bpos i = 0; i < gb->gap_start; i++) {
        wchar_t c = gb->buf[i];
        if (iswalpha(c) || c == L'\'' || c == L'-') {
            if (!in_word) { words++; in_word = 1; }
        } else {
            in_word = 0;
        }
    }
    /* Scan post-gap segment directly */
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

/* Force a stats recount and snapshot values as the session baseline */
static void snapshot_session_baseline(Document *doc) {
    doc->stats_dirty = 1;
    update_stats_now(doc);
    doc->session_start_words = doc->word_count;
    doc->session_start_chars = doc->char_count;
    doc->session_start_lines = doc->line_count;
}

static bpos pos_to_line(Document *doc, bpos pos) {
    return lc_line_of(&doc->lc, pos);
}

static bpos pos_to_col(Document *doc, bpos pos) {
    bpos line = pos_to_line(doc, pos);
    return pos - lc_line_start(&doc->lc, line);
}

static bpos line_col_to_pos(Document *doc, bpos line, bpos col) {
    if (line < 0) line = 0;
    if (line >= doc->lc.count) line = doc->lc.count - 1;
    bpos start = lc_line_start(&doc->lc, line);
    bpos end = lc_line_end(&doc->lc, &doc->gb, line);
    bpos maxcol = end - start;
    if (col > maxcol) col = maxcol;
    if (col < 0) col = 0;
    return start + col;
}

/* Tab-aware visual column: counts each tab as 4 columns, everything else as 1.
 * Used by up/down arrow to preserve visual alignment across lines with different tabs. */
static bpos pos_to_visual_col(Document *doc, bpos pos) {
    bpos line = pos_to_line(doc, pos);
    bpos ls = lc_line_start(&doc->lc, line);
    int vcol = 0;
    for (int i = ls; i < pos; i++) {
        wchar_t c = gb_char_at(&doc->gb, i);
        vcol += (c == L'\t') ? 4 : 1;
    }
    return vcol;
}

/* Convert a visual column (tab=4) to a text position on the given line.
 * Stops at or past the target visual column, clamped to line end. */
static bpos visual_col_to_pos(Document *doc, bpos line, int target_vcol) {
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

/* Compute tab-aware pixel X offset for a character column on a line.
 * Walks from line_start_pos accumulating tab (4×cw) vs normal (cw) widths. */
static int col_to_pixel_x(GapBuffer *gb, bpos line_start_pos, bpos col, int cw) {
    int xp = 0;
    for (int i = 0; i < col; i++) {
        wchar_t c = gb_char_at(gb, line_start_pos + i);
        xp += (c == L'\t') ? cw * 4 : cw;
    }
    return xp;
}

/* Convert pixel X offset to character column, accounting for tabs.
 * Snaps to nearest character boundary (half-width threshold). */
static bpos pixel_x_to_col(GapBuffer *gb, bpos line_start_pos, bpos line_len, int px, int cw) {
    int xp = 0;
    for (int i = 0; i < line_len; i++) {
        wchar_t c = gb_char_at(gb, line_start_pos + i);
        int w = (c == L'\t') ? cw * 4 : cw;
        if (px < xp + w / 2) return i;
        xp += w;
    }
    return line_len;
}

static int has_selection(Document *doc) {
    return doc->sel_anchor >= 0 && doc->sel_anchor != doc->cursor;
}

static bpos selection_start(Document *doc) {
    if (!has_selection(doc)) return doc->cursor;
    return doc->cursor < doc->sel_anchor ? doc->cursor : doc->sel_anchor;
}

static bpos selection_end(Document *doc) {
    if (!has_selection(doc)) return doc->cursor;
    return doc->cursor > doc->sel_anchor ? doc->cursor : doc->sel_anchor;
}

static void invalidate_editor_region(HWND hwnd) {
    RECT rc;
    rc.left = 0;
    rc.right = g_editor.client_w;
    rc.top = DPI(TITLEBAR_H + MENUBAR_H + TABBAR_H);
    rc.bottom = g_editor.client_h;
    InvalidateRect(hwnd, &rc, FALSE);
}

/* Start the smooth scroll animation timer on-demand.
 * SetTimer with the same ID is a no-op if already running. */
static void start_scroll_animation(void) {
    if (g_editor.hwnd)
        SetTimer(g_editor.hwnd, TIMER_SMOOTH, 16, NULL);
}

/* ═══════════════════════════════════════════════════════════════
 * EDITING OPERATIONS
 * ═══════════════════════════════════════════════════════════════ */

static void editor_insert_text(const wchar_t *text, bpos len) {
    Document *doc = current_doc();
    if (!doc) return;

    bpos old_cursor = doc->cursor;
    int sel_group = 0; /* non-zero if replacing a selection */
    if (has_selection(doc)) {
        bpos s = selection_start(doc);
        bpos e = selection_end(doc);
        /* Group the delete + insert so they undo as one atomic action */
        sel_group = ++doc->undo.next_group;
        /* Save deleted text for undo */
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
    /* Try incremental line cache update (O(line_count) vs O(doc_size)).
     * Fall back to full rebuild for selection-replacement or multi-newline paste. */
    if (!sel_group && lc_notify_insert(&doc->lc, doc->cursor - len, text, len)) {
        doc->line_count = doc->lc.count;
        if (doc->mode == MODE_PROSE && doc->wc.wrap_col > 0)
            doc->wrap_dirty = 1;
    } else {
        recalc_lines(doc);
    }
    update_stats(doc);
}

static void editor_insert_char(wchar_t c) {
    wchar_t buf[2] = { c, 0 };
    editor_insert_text(buf, 1);
}

static void editor_delete_selection(void) {
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

static void editor_backspace(void) {
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
    /* Incremental line cache update for single-char delete */
    if (lc_notify_delete(&doc->lc, doc->cursor, buf, 1)) {
        doc->line_count = doc->lc.count;
        if (doc->mode == MODE_PROSE && doc->wc.wrap_col > 0)
            doc->wrap_dirty = 1;
    } else {
        recalc_lines(doc);
    }
    update_stats(doc);
}

static void editor_delete_forward(void) {
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
    /* Incremental line cache update for single-char delete */
    if (lc_notify_delete(&doc->lc, doc->cursor, buf, 1)) {
        doc->line_count = doc->lc.count;
        if (doc->mode == MODE_PROSE && doc->wc.wrap_col > 0)
            doc->wrap_dirty = 1;
    } else {
        recalc_lines(doc);
    }
    update_stats(doc);
}

static void editor_move_cursor(bpos pos, int extend_selection) {
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

static void editor_undo(void) {
    Document *doc = current_doc();
    if (!doc) return;
    UndoStack *us = &doc->undo;
    if (us->current <= 0) return;

    /* Peek at the group of the entry we're about to undo */
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
        /* Continue undoing if this is part of a non-zero group */
    } while (group != 0 && us->current > 0 &&
             us->entries[us->current - 1].group == group);

    doc->sel_anchor = -1;
    doc->modified = (us->current != us->save_point);
    recalc_lines(doc);
    update_stats(doc);
}

static void editor_redo(void) {
    Document *doc = current_doc();
    if (!doc) return;
    UndoStack *us = &doc->undo;
    if (us->current >= us->count) return;

    /* Peek at the group of the entry we're about to redo */
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
        /* Continue redoing if this is part of a non-zero group */
    } while (group != 0 && us->current < us->count &&
             us->entries[us->current].group == group);

    doc->sel_anchor = -1;
    doc->modified = (us->current != us->save_point);
    recalc_lines(doc);
    update_stats(doc);
}

static void editor_select_all(void) {
    Document *doc = current_doc();
    if (!doc) return;
    doc->sel_anchor = 0;
    doc->cursor = gb_length(&doc->gb);
}

static void editor_copy(void) {
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

static void editor_cut(void) {
    editor_copy();
    editor_delete_selection();
}

static void editor_paste(void) {
    if (!OpenClipboard(g_editor.hwnd)) return;
    HANDLE hg = GetClipboardData(CF_UNICODETEXT);
    if (hg) {
        wchar_t *text = (wchar_t *)GlobalLock(hg);
        if (text) {
            int len = (int)wcslen(text);
            /* Convert \r\n to \n */
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

static void editor_ensure_cursor_visible(void) {
    Document *doc = current_doc();
    if (!doc) return;

    /* Flush deferred wrap rebuild if needed before reading wrap cache */
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

    /* Horizontal scroll for code mode (no word wrap) */
    if (doc->mode == MODE_CODE) {
        int cw = g_editor.char_width;
        int gw = gutter_width(doc);
        int edit_w = g_editor.client_w - DPI(SCROLLBAR_W) - gw;
        if (g_editor.show_minimap) edit_w -= DPI(MINIMAP_W);
        bpos col = pos_to_col(doc, doc->cursor);
        bpos line = pos_to_line(doc, doc->cursor);
        bpos ls = lc_line_start(&doc->lc, line);
        int cx = col_to_pixel_x(&doc->gb, ls, col, cw);
        int margin = cw * 4; /* keep 4 chars of breathing room */
        if (cx - doc->scroll_x < 0) {
            doc->target_scroll_x = cx - margin;
        } else if (cx - doc->scroll_x + cw > edit_w) {
            doc->target_scroll_x = cx - edit_w + margin;
        }
        if (doc->target_scroll_x < 0) doc->target_scroll_x = 0;
    }
    /* Timer will animate scroll_y toward target_scroll_y */
    if (doc->scroll_y != doc->target_scroll_y || doc->scroll_x != doc->target_scroll_x)
        start_scroll_animation();
}

/* ═══════════════════════════════════════════════════════════════
 * SEARCH & REPLACE
 * ═══════════════════════════════════════════════════════════════ */

static void search_update_matches(void) {
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
    if (tlen < (bpos)qlen) return; /* text shorter than query — no matches */
    int cap = 256;
    ss->match_positions = (bpos *)malloc(cap * sizeof(bpos));
    if (!ss->match_positions) return;

    /* Extract and pre-lowercase full text once — eliminates per-char
     * towlower in inner loop and enables first-char fast reject */
    wchar_t *text = (wchar_t *)malloc((tlen + 1) * sizeof(wchar_t));
    if (!text) { free(ss->match_positions); ss->match_positions = NULL; return; }
    gb_copy_range(&doc->gb, 0, tlen, text);

    /* Lowercase text and query once upfront */
    for (bpos i = 0; i < tlen; i++) text[i] = towlower(text[i]);
    wchar_t lower_query[256];
    for (int i = 0; i < qlen; i++) lower_query[i] = towlower(ss->query[i]);
    lower_query[qlen] = 0;

    wchar_t first_ch = lower_query[0];
    for (bpos i = 0; i <= tlen - (bpos)qlen; i++) {
        /* Fast reject on first character */
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

    /* Snap current_match to the match nearest the cursor */
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

static void toggle_search(void) {
    g_editor.search.active = !g_editor.search.active;
    if (!g_editor.search.active) {
        g_editor.search.match_count = 0;
        free(g_editor.search.match_positions);
        g_editor.search.match_positions = NULL;
        g_editor.search.replace_focused = 0;
    }
}

static void search_next(void) {
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

static void search_prev(void) {
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

static void do_replace(void) {
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
    /* Move cursor past replacement so search_update_matches snaps to next */
    doc->cursor = pos + rlen;
    search_update_matches();
}

static void do_replace_all(void) {
    SearchState *ss = &g_editor.search;
    Document *doc = current_doc();
    if (!doc || ss->match_count == 0) return;

    int qlen = (int)wcslen(ss->query);
    int rlen = (int)wcslen(ss->replace_text);
    bpos old_cursor = doc->cursor;
    bpos diff = rlen - qlen;

    /* Allocate a unique group ID so all entries undo/redo atomically.
     * Replace front-to-back, adjusting positions by accumulated offset.
     * Push undo entries in forward order so LIFO stack replays correctly. */
    int group = ++doc->undo.next_group;

    for (int i = 0; i < ss->match_count; i++) {
        bpos pos = ss->match_positions[i] + (bpos)i * diff;

        /* Push DELETE undo for the original text */
        wchar_t *deleted = gb_extract_alloc(&doc->gb, pos, qlen);
        if (deleted) {
            undo_push(&doc->undo, UNDO_DELETE, pos, deleted, qlen, old_cursor, pos, group);
            free(deleted);
        }
        gb_delete(&doc->gb, pos, qlen);

        /* Push INSERT undo for the replacement text */
        gb_insert(&doc->gb, pos, ss->replace_text, rlen);
        undo_push(&doc->undo, UNDO_INSERT, pos, ss->replace_text, rlen, pos, pos + rlen, group);
    }

    /* Position cursor at end of last replacement */
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

/* ═══════════════════════════════════════════════════════════════
 * FILE I/O
 * ═══════════════════════════════════════════════════════════════ */

static void load_file(Document *doc, const wchar_t *path) {
    HANDLE hFile = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, NULL,
                               OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hFile == INVALID_HANDLE_VALUE) return;

    LARGE_INTEGER li_size;
    if (!GetFileSizeEx(hFile, &li_size) || li_size.QuadPart > (LONGLONG)(512 * 1024 * 1024)) {
        /* File too large (>512MB) */
        CloseHandle(hFile);
        return;
    }
    int size = (int)li_size.QuadPart;

    char *raw = (char *)malloc(size + 1);
    if (!raw) { CloseHandle(hFile); return; }
    DWORD bytes_read = 0;
    if (!ReadFile(hFile, raw, (DWORD)size, &bytes_read, NULL)) {
        free(raw); CloseHandle(hFile); return;
    }
    raw[bytes_read] = 0;
    CloseHandle(hFile);
    size = (int)bytes_read;

    /* Detect UTF-8 BOM */
    char *start = raw;
    if (size >= 3 && (unsigned char)raw[0] == 0xEF &&
        (unsigned char)raw[1] == 0xBB && (unsigned char)raw[2] == 0xBF) {
        start += 3;
        size -= 3;
    }

    /* Convert to wide chars */
    int wlen = MultiByteToWideChar(CP_UTF8, 0, start, (int)size, NULL, 0);
    wchar_t *wtext = (wchar_t *)malloc((wlen + 1) * sizeof(wchar_t));
    if (!wtext) { free(raw); return; }
    MultiByteToWideChar(CP_UTF8, 0, start, (int)size, wtext, wlen);
    wtext[wlen] = 0;
    free(raw);

    /* Strip \r */
    int j = 0;
    for (int i = 0; i < wlen; i++) {
        if (wtext[i] != L'\r') wtext[j++] = wtext[i];
    }
    wtext[j] = 0;

    /* Load into gap buffer */
    gb_free(&doc->gb);
    gb_init(&doc->gb, j + GAP_INIT);
    gb_insert(&doc->gb, 0, wtext, j);
    free(wtext);

    safe_wcscpy(doc->filepath, MAX_PATH, path);
    /* Extract filename for title */
    const wchar_t *slash = wcsrchr(path, L'\\');
    if (!slash) slash = wcsrchr(path, L'/');
    safe_wcscpy(doc->title, 64, slash ? slash + 1 : path);

    /* Auto-detect mode based on extension */
    const wchar_t *ext = wcsrchr(path, L'.');
    if (ext) {
        if (_wcsicmp(ext, L".c") == 0 || _wcsicmp(ext, L".h") == 0 ||
            _wcsicmp(ext, L".cpp") == 0 || _wcsicmp(ext, L".hpp") == 0 ||
            _wcsicmp(ext, L".py") == 0 || _wcsicmp(ext, L".js") == 0 ||
            _wcsicmp(ext, L".ts") == 0 || _wcsicmp(ext, L".rs") == 0 ||
            _wcsicmp(ext, L".java") == 0 || _wcsicmp(ext, L".cs") == 0 ||
            _wcsicmp(ext, L".go") == 0 || _wcsicmp(ext, L".rb") == 0 ||
            _wcsicmp(ext, L".sh") == 0 || _wcsicmp(ext, L".json") == 0 ||
            _wcsicmp(ext, L".xml") == 0 || _wcsicmp(ext, L".html") == 0 ||
            _wcsicmp(ext, L".css") == 0 || _wcsicmp(ext, L".sql") == 0 ||
            _wcsicmp(ext, L".yaml") == 0 || _wcsicmp(ext, L".toml") == 0) {
            doc->mode = MODE_CODE;
        } else {
            doc->mode = MODE_PROSE;
        }
    }

    doc->cursor = 0;
    doc->sel_anchor = -1;
    doc->scroll_y = 0;
    doc->target_scroll_y = 0;
    doc->scroll_x = 0;
    doc->target_scroll_x = 0;
    doc->modified = 0;
    doc->bc_cached_mutation = -1;
    doc->bc_cached_line = -1;
    undo_clear(&doc->undo);
    recalc_lines(doc);
    update_stats(doc);
    snapshot_session_baseline(doc);
}

/* ═══════════════════════════════════════════════════════════════
 * ROBUST SAVE SYSTEM — atomic writes, autosave, crash recovery
 * ═══════════════════════════════════════════════════════════════ */

/* Extract document text as UTF-8 with \r\n line endings.
 * Returns malloc'd buffer; caller must free. Sets *out_len. */
static char *doc_to_utf8(Document *doc, int *out_len) {
    GapBuffer *gb = &doc->gb;
    bpos len = gb_length(gb);

    /* Handle empty document */
    if (len == 0) {
        char *empty = (char *)malloc(1);
        if (empty) empty[0] = 0;
        *out_len = 0;
        return empty;
    }

    /* Fast bulk extraction via gb_copy_range */
    wchar_t *text = (wchar_t *)malloc((len + 1) * sizeof(wchar_t));
    if (!text) { *out_len = 0; return NULL; }
    gb_copy_range(gb, 0, len, text);
    text[len] = 0;

    /* Count newlines for \r\n expansion */
    bpos newlines = 0;
    for (bpos i = 0; i < len; i++) {
        if (text[i] == L'\n') newlines++;
    }

    /* Build \r\n version */
    wchar_t *winfmt = (wchar_t *)malloc((len + newlines + 1) * sizeof(wchar_t));
    if (!winfmt) { free(text); *out_len = 0; return NULL; }
    bpos j = 0;
    for (bpos i = 0; i < len; i++) {
        if (text[i] == L'\n') winfmt[j++] = L'\r';
        winfmt[j++] = text[i];
    }
    winfmt[j] = 0;
    free(text);

    /* Convert to UTF-8 */
    int utf8len = WideCharToMultiByte(CP_UTF8, 0, winfmt, (int)j, NULL, 0, NULL, NULL);
    char *utf8 = (char *)malloc(utf8len);
    if (!utf8) { free(winfmt); *out_len = 0; return NULL; }
    WideCharToMultiByte(CP_UTF8, 0, winfmt, (int)j, utf8, utf8len, NULL, NULL);
    free(winfmt);

    *out_len = utf8len;
    return utf8;
}

/* Atomic file write: write to temp, flush to disk, rename into place.
 * Returns 1 on success, 0 on failure. */
static int write_file_atomic(const wchar_t *final_path, const char *data, int data_len) {
    /* Build temp path: final_path + ".tmp~" */
    wchar_t tmp_path[MAX_PATH + 16];
    swprintf(tmp_path, MAX_PATH + 16, L"%s.tmp~", final_path);

    /* Write to temp file using Win32 API for flush control */
    HANDLE hFile = CreateFileW(tmp_path, GENERIC_WRITE, 0, NULL,
                               CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hFile == INVALID_HANDLE_VALUE) return 0;

    DWORD written;
    BOOL ok = WriteFile(hFile, data, (DWORD)data_len, &written, NULL);
    if (!ok || (int)written != data_len) {
        CloseHandle(hFile);
        DeleteFileW(tmp_path);
        return 0;
    }

    /* Force OS to flush all buffers to physical disk */
    FlushFileBuffers(hFile);
    CloseHandle(hFile);

    /* Atomic swap: ReplaceFileW preserves ACLs/streams and creates a .bak~ */
    if (GetFileAttributesW(final_path) != INVALID_FILE_ATTRIBUTES) {
        /* Existing file — use ReplaceFile for atomic swap + backup */
        wchar_t bak_path[MAX_PATH + 16];
        swprintf(bak_path, MAX_PATH + 16, L"%s.bak~", final_path);
        if (ReplaceFileW(final_path, tmp_path, bak_path,
                         REPLACEFILE_IGNORE_MERGE_ERRORS, NULL, NULL)) {
            return 1;
        }
        /* ReplaceFile can fail (e.g. bak locked) — fallback to MoveFileEx */
    }
    /* New file or ReplaceFile fallback */
    if (MoveFileExW(tmp_path, final_path,
                    MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        return 1;
    }

    /* Last resort: direct write (temp is already on disk and flushed) */
    DeleteFileW(final_path);
    if (MoveFileW(tmp_path, final_path)) return 1;

    DeleteFileW(tmp_path);
    return 0;
}

static void save_file(Document *doc, const wchar_t *path) {
    int utf8len;
    char *utf8 = doc_to_utf8(doc, &utf8len);
    if (!utf8) return;

    if (write_file_atomic(path, utf8, utf8len)) {
        doc->modified = 0;
        doc->undo.save_point = doc->undo.current;
        safe_wcscpy(doc->filepath, MAX_PATH, path);
        const wchar_t *slash = wcsrchr(path, L'\\');
        if (!slash) slash = wcsrchr(path, L'/');
        safe_wcscpy(doc->title, 64, slash ? slash + 1 : path);

        /* Clean up autosave shadow since we just saved successfully */
        autosave_delete_for_doc(doc);
        doc->autosave_mutation_snapshot = doc->gb.mutation;
    }
    free(utf8);
}

/* ── Autosave shadow file system ── */

static void autosave_ensure_dir(void) {
    if (g_editor.autosave_dir[0]) return; /* already resolved */

    wchar_t appdata[MAX_PATH];
    if (GetEnvironmentVariableW(L"LOCALAPPDATA", appdata, MAX_PATH) == 0)
        return;

    swprintf(g_editor.autosave_dir, MAX_PATH, L"%s\\ProseCode\\autosave", appdata);

    /* Create directory tree: ProseCode, then ProseCode\autosave */
    wchar_t parent[MAX_PATH];
    swprintf(parent, MAX_PATH, L"%s\\ProseCode", appdata);
    CreateDirectoryW(parent, NULL);   /* OK if exists */
    CreateDirectoryW(g_editor.autosave_dir, NULL);
}

/* Simple hash of a filepath to a hex string for the shadow filename */
static unsigned int path_hash(const wchar_t *path) {
    unsigned int h = 5381;
    while (*path) {
        h = ((h << 5) + h) ^ (unsigned int)(*path);
        path++;
    }
    return h;
}

static void autosave_path_for_doc(Document *doc, wchar_t *out) {
    autosave_ensure_dir();
    if (doc->filepath[0]) {
        unsigned int h = path_hash(doc->filepath);
        swprintf(out, MAX_PATH + 32, L"%s\\%08x.pctmp", g_editor.autosave_dir, h);
    } else {
        /* Untitled tabs: use per-document autosave_id for stable filenames */
        swprintf(out, MAX_PATH + 32, L"%s\\untitled_%u_%u.pctmp",
                 g_editor.autosave_dir, doc->autosave_id, g_editor.session_start_time);
    }
}

/* Write autosave shadow for a single document.
 * Format: 4-byte header length (LE) + JSON-like header + raw UTF-8 content.
 * Header is kept tiny so we can extract the original path for recovery. */

/* Escape a UTF-8 string for safe JSON embedding (handles \ and ") */
static int json_escape_path(const char *src, char *dst, int dst_size) {
    int j = 0;
    for (int i = 0; src[i] && j < dst_size - 2; i++) {
        char c = src[i];
        if (c == '"' || c == '\\') {
            dst[j++] = '\\';
            if (j >= dst_size - 1) break;
        }
        dst[j++] = c;
    }
    dst[j] = 0;
    return j;
}

static void autosave_write(Document *doc) {
    int utf8len;
    char *utf8 = doc_to_utf8(doc, &utf8len);
    if (!utf8) return;

    /* Build header: original filepath (JSON-escaped) + timestamp */
    char header[2048];
    char narrow_path[MAX_PATH * 3];
    char escaped_path[MAX_PATH * 6]; /* worst case: every char escaped */
    WideCharToMultiByte(CP_UTF8, 0, doc->filepath, -1, narrow_path,
                        sizeof(narrow_path), NULL, NULL);
    json_escape_path(narrow_path, escaped_path, sizeof(escaped_path));
    int hdr_len = snprintf(header, sizeof(header),
        "{\"path\":\"%s\",\"time\":%u,\"chars\":%lld}\n",
        escaped_path, (unsigned)GetTickCount(), (long long)gb_length(&doc->gb));
    if (hdr_len < 0 || hdr_len >= (int)sizeof(header))
        hdr_len = (int)sizeof(header) - 1; /* clamp to actual written bytes */

    /* Combine header + content into one buffer */
    int total = 4 + hdr_len + utf8len;
    char *buf = (char *)malloc(total);
    if (!buf) { free(utf8); return; }

    /* 4-byte header length prefix (little-endian) */
    buf[0] = (char)(hdr_len & 0xFF);
    buf[1] = (char)((hdr_len >> 8) & 0xFF);
    buf[2] = (char)((hdr_len >> 16) & 0xFF);
    buf[3] = (char)((hdr_len >> 24) & 0xFF);
    memcpy(buf + 4, header, hdr_len);
    memcpy(buf + 4 + hdr_len, utf8, utf8len);
    free(utf8);

    /* Write atomically to shadow path */
    wchar_t shadow_path[MAX_PATH + 32];
    autosave_path_for_doc(doc, shadow_path);
    write_file_atomic(shadow_path, buf, total);
    free(buf);

    doc->autosave_mutation_snapshot = doc->gb.mutation;
    doc->autosave_last_time = GetTickCount();
}

/* Called from TIMER_AUTOSAVE every 30 seconds */
static void autosave_tick(void) {
    for (int i = 0; i < g_editor.tab_count; i++) {
        Document *doc = g_editor.tabs[i];
        if (!doc->modified) continue;
        /* Skip if content hasn't actually changed since last autosave */
        if (doc->gb.mutation == doc->autosave_mutation_snapshot) continue;
        autosave_write(doc);
    }
}

static void autosave_delete_for_doc(Document *doc) {
    wchar_t shadow_path[MAX_PATH + 32];
    autosave_path_for_doc(doc, shadow_path);
    DeleteFileW(shadow_path);
}

/* Remove all shadow files — called on clean exit */
static void autosave_cleanup_all(void) {
    autosave_ensure_dir();
    if (!g_editor.autosave_dir[0]) return;

    wchar_t pattern[MAX_PATH];
    swprintf(pattern, MAX_PATH, L"%s\\*.pctmp", g_editor.autosave_dir);

    WIN32_FIND_DATAW fd;
    HANDLE hFind = FindFirstFileW(pattern, &fd);
    if (hFind == INVALID_HANDLE_VALUE) return;
    do {
        wchar_t full[MAX_PATH];
        swprintf(full, MAX_PATH, L"%s\\%s", g_editor.autosave_dir, fd.cFileName);
        DeleteFileW(full);
    } while (FindNextFileW(hFind, &fd));
    FindClose(hFind);
}

/* Also clean up .tmp~ files left behind on clean exit */
static void autosave_cleanup_tmp(void) {
    autosave_ensure_dir();
    if (!g_editor.autosave_dir[0]) return;

    wchar_t pattern[MAX_PATH];
    swprintf(pattern, MAX_PATH, L"%s\\*.tmp~", g_editor.autosave_dir);

    WIN32_FIND_DATAW fd;
    HANDLE hFind = FindFirstFileW(pattern, &fd);
    if (hFind == INVALID_HANDLE_VALUE) return;
    do {
        wchar_t full[MAX_PATH];
        swprintf(full, MAX_PATH, L"%s\\%s", g_editor.autosave_dir, fd.cFileName);
        DeleteFileW(full);
    } while (FindNextFileW(hFind, &fd));
    FindClose(hFind);
}

/* ── Crash recovery ──
 * Scan autosave_dir for .pctmp files, offer to recover each one */
static void autosave_recover(void) {
    autosave_ensure_dir();
    if (!g_editor.autosave_dir[0]) return;

    wchar_t pattern[MAX_PATH];
    swprintf(pattern, MAX_PATH, L"%s\\*.pctmp", g_editor.autosave_dir);

    WIN32_FIND_DATAW fd;
    HANDLE hFind = FindFirstFileW(pattern, &fd);
    if (hFind == INVALID_HANDLE_VALUE) return;

    int recovered = 0;
    do {
        wchar_t full[MAX_PATH];
        swprintf(full, MAX_PATH, L"%s\\%s", g_editor.autosave_dir, fd.cFileName);

        /* Read shadow file */
        HANDLE hFile = CreateFileW(full, GENERIC_READ, FILE_SHARE_READ, NULL,
                                   OPEN_EXISTING, 0, NULL);
        if (hFile == INVALID_HANDLE_VALUE) continue;

        DWORD file_size = GetFileSize(hFile, NULL);
        if (file_size < 8) { CloseHandle(hFile); DeleteFileW(full); continue; }

        char *raw = (char *)malloc(file_size);
        if (!raw) { CloseHandle(hFile); continue; }
        DWORD bytes_read;
        ReadFile(hFile, raw, file_size, &bytes_read, NULL);
        CloseHandle(hFile);

        if (bytes_read < 8) { free(raw); DeleteFileW(full); continue; }

        /* Parse header length */
        int hdr_len = (unsigned char)raw[0] |
                      ((unsigned char)raw[1] << 8) |
                      ((unsigned char)raw[2] << 16) |
                      ((unsigned char)raw[3] << 24);

        if (hdr_len <= 0 || hdr_len >= 1024 || 4 + hdr_len > (int)bytes_read) {
            free(raw); DeleteFileW(full); continue;
        }

        /* Extract original path from header */
        char hdr_buf[1024];
        memcpy(hdr_buf, raw + 4, hdr_len);
        hdr_buf[hdr_len] = 0;

        /* Simple extraction: find "path":"..." */
        wchar_t orig_path[MAX_PATH] = {0};
        char *pp = strstr(hdr_buf, "\"path\":\"");
        if (pp) {
            pp += 8;
            char *pe = strchr(pp, '"');
            if (pe) {
                *pe = 0;
                MultiByteToWideChar(CP_UTF8, 0, pp, -1, orig_path, MAX_PATH);
            }
        }

        /* Build friendly prompt */
        wchar_t msg[512];
        const wchar_t *name = orig_path[0] ? wcsrchr(orig_path, L'\\') : NULL;
        if (name) name++;
        else if (orig_path[0]) name = orig_path;
        else name = L"Untitled";
        swprintf(msg, 512,
                 L"Prose_Code found unsaved work:\n\n  \"%s\"\n\nRecover this file?",
                 name);

        if (MessageBoxW(g_editor.hwnd, msg, L"Crash Recovery",
                        MB_YESNO | MB_ICONINFORMATION) == IDYES) {
            /* Load content (skip header) into a new tab */
            char *content = raw + 4 + hdr_len;
            int content_len = (int)bytes_read - 4 - hdr_len;

            int wlen = MultiByteToWideChar(CP_UTF8, 0, content, content_len, NULL, 0);
            wchar_t *wtext = (wchar_t *)malloc((wlen + 1) * sizeof(wchar_t));
            if (!wtext) { free(raw); DeleteFileW(full); continue; }
            MultiByteToWideChar(CP_UTF8, 0, content, content_len, wtext, wlen);
            wtext[wlen] = 0;

            /* Strip \r */
            int j = 0;
            for (int i = 0; i < wlen; i++) {
                if (wtext[i] != L'\r') wtext[j++] = wtext[i];
            }

            Document *doc = doc_create();
            gb_insert(&doc->gb, 0, wtext, j);
            free(wtext);

            if (orig_path[0]) {
                safe_wcscpy(doc->filepath, MAX_PATH, orig_path);
                const wchar_t *slash = wcsrchr(orig_path, L'\\');
                if (!slash) slash = wcsrchr(orig_path, L'/');
                safe_wcscpy(doc->title, 64, slash ? slash + 1 : orig_path);
            } else {
                safe_wcscpy(doc->title, 64, L"Recovered");
            }
            doc->modified = 1; /* Mark as modified so user must explicitly save */
            recalc_lines(doc);
            update_stats(doc);
            snapshot_session_baseline(doc);

            if (g_editor.tab_count < MAX_TABS) {
                /* If the only tab is a fresh untitled, replace it */
                if (g_editor.tab_count == 1 && !g_editor.tabs[0]->modified &&
                    !g_editor.tabs[0]->filepath[0] &&
                    gb_length(&g_editor.tabs[0]->gb) == 0) {
                    doc_free(g_editor.tabs[0]);
                    g_editor.tabs[0] = doc;
                    g_editor.active_tab = 0;
                } else {
                    g_editor.tabs[g_editor.tab_count] = doc;
                    g_editor.active_tab = g_editor.tab_count;
                    g_editor.tab_count++;
                }
                recovered++;
            } else {
                doc_free(doc);
            }
        }

        free(raw);
        DeleteFileW(full);
    } while (FindNextFileW(hFind, &fd));
    FindClose(hFind);

    if (recovered > 0) {
        InvalidateRect(g_editor.hwnd, NULL, FALSE);
    }
}

/* ── Unsaved changes prompt ──
 * Returns: 1 = saved or discarded (OK to proceed), 0 = cancelled */
static int prompt_save_doc(int tab_idx) {
    if (tab_idx < 0 || tab_idx >= g_editor.tab_count) return 1;
    Document *doc = g_editor.tabs[tab_idx];
    if (!doc->modified) return 1;

    wchar_t msg[256];
    swprintf(msg, 256, L"Save changes to \"%s\"?", doc->title);
    int result = MessageBoxW(g_editor.hwnd, msg, L"Prose_Code",
                             MB_YESNOCANCEL | MB_ICONWARNING);
    if (result == IDYES) {
        return save_file_dialog_for_doc(doc);
    } else if (result == IDNO) {
        /* Discard — delete autosave shadow */
        autosave_delete_for_doc(doc);
        return 1;
    }
    return 0; /* IDCANCEL */
}

static void open_file_dialog(void) {
    wchar_t path[MAX_PATH] = {0};
    OPENFILENAMEW ofn = {0};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = g_editor.hwnd;
    ofn.lpstrFilter = L"All Files\0*.*\0Text Files\0*.txt;*.md;*.markdown\0C/C++ Files\0*.c;*.h;*.cpp;*.hpp\0Python Files\0*.py\0\0";
    ofn.lpstrFile = path;
    ofn.nMaxFile = MAX_PATH;
    ofn.Flags = OFN_FILEMUSTEXIST;

    if (GetOpenFileNameW(&ofn)) {
        Document *doc = current_doc();
        /* Smart Tab Reuse: If the current tab is Untitled, Unmodified, and Empty,
         * reuse it instead of creating a new tab. */
        int reuse = 0;
        if (doc && !doc->modified && doc->filepath[0] == 0 && gb_length(&doc->gb) == 0) {
            reuse = 1;
        }

        if (reuse) {
            /* Reuse existing doc */
            load_file(doc, path);
        } else {
            /* Create new tab */
            if (g_editor.tab_count < MAX_TABS) {
                doc = doc_create();
                load_file(doc, path);
                g_editor.tabs[g_editor.tab_count] = doc;
                g_editor.active_tab = g_editor.tab_count;
                g_editor.tab_count++;
            }
        }
        /* Force immediate repaint to update title bar and tab bar */
        InvalidateRect(g_editor.hwnd, NULL, FALSE);
    }
}

static void save_file_dialog(void) {
    Document *doc = current_doc();
    if (!doc) return;

    if (doc->filepath[0]) {
        save_file(doc, doc->filepath);
        return;
    }

    wchar_t path[MAX_PATH] = {0};
    OPENFILENAMEW ofn = {0};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = g_editor.hwnd;
    ofn.lpstrFilter = L"All Files\0*.*\0Text Files\0*.txt\0Markdown\0*.md\0\0";
    ofn.lpstrFile = path;
    ofn.nMaxFile = MAX_PATH;
    ofn.Flags = OFN_OVERWRITEPROMPT;

    if (GetSaveFileNameW(&ofn)) {
        save_file(doc, path);
    }
}

/* Save a specific document, prompting for path if needed.
 * Returns: 1 = saved successfully, 0 = cancelled or failed */
static int save_file_dialog_for_doc(Document *doc) {
    if (!doc) return 0;

    if (doc->filepath[0]) {
        save_file(doc, doc->filepath);
        return doc->modified ? 0 : 1;
    }

    wchar_t path[MAX_PATH] = {0};
    OPENFILENAMEW ofn = {0};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = g_editor.hwnd;
    ofn.lpstrFilter = L"All Files\0*.*\0Text Files\0*.txt\0Markdown\0*.md\0\0";
    ofn.lpstrFile = path;
    ofn.nMaxFile = MAX_PATH;
    ofn.Flags = OFN_OVERWRITEPROMPT;

    if (!GetSaveFileNameW(&ofn)) return 0; /* user cancelled */
    save_file(doc, path);
    return doc->modified ? 0 : 1;
}

static void save_current_file(void) {
    Document *doc = current_doc();
    if (!doc) return;
    if (doc->filepath[0]) {
        save_file(doc, doc->filepath);
    } else {
        save_file_dialog();
    }
}

static void new_tab(void) {
    if (g_editor.tab_count >= MAX_TABS) return;
    Document *doc = doc_create();
    g_editor.tabs[g_editor.tab_count] = doc;
    g_editor.active_tab = g_editor.tab_count;
    g_editor.tab_count++;
    recalc_lines(doc);
    snapshot_session_baseline(doc);
}

static void close_tab(int idx) {
    if (idx < 0 || idx >= g_editor.tab_count) return;
    /* Prompt for unsaved changes */
    if (!prompt_save_doc(idx)) return; /* user cancelled */
    autosave_delete_for_doc(g_editor.tabs[idx]);
    doc_free(g_editor.tabs[idx]);
    for (int i = idx; i < g_editor.tab_count - 1; i++) {
        g_editor.tabs[i] = g_editor.tabs[i + 1];
    }
    g_editor.tab_count--;
    if (g_editor.active_tab >= g_editor.tab_count)
        g_editor.active_tab = g_editor.tab_count - 1;
    if (g_editor.tab_count == 0) new_tab();
}

static void toggle_focus_mode(void) {
    g_editor.focus.active = !g_editor.focus.active;
}

static void toggle_mode(void) {
    Document *doc = current_doc();
    if (doc) {
        doc->mode = (doc->mode == MODE_PROSE) ? MODE_CODE : MODE_PROSE;
    }
}

/* ═══════════════════════════════════════════════════════════════
 * RENDERING — Custom-drawn UI with GDI
 * ═══════════════════════════════════════════════════════════════ */

/* Helper: draw filled rect */
static void fill_rect(HDC hdc, int x, int y, int w, int h, COLORREF c) {
    SetDCBrushColor(hdc, c);
    RECT r = { x, y, x + w, y + h };
    FillRect(hdc, &r, (HBRUSH)GetStockObject(DC_BRUSH));
}

/* Helper: draw rounded rect */
static void fill_rounded_rect(HDC hdc, int x, int y, int w, int h, int r, COLORREF c) {
    SetDCBrushColor(hdc, c);
    SetDCPenColor(hdc, c);
    HBRUSH obr = (HBRUSH)SelectObject(hdc, GetStockObject(DC_BRUSH));
    HPEN open = (HPEN)SelectObject(hdc, GetStockObject(DC_PEN));
    RoundRect(hdc, x, y, x + w, y + h, r, r);
    SelectObject(hdc, obr);
    SelectObject(hdc, open);
}

/* Helper: draw text at position. Caller must have already set SetBkMode(hdc, TRANSPARENT). */
static void draw_text(HDC hdc, int x, int y, const wchar_t *text, int len, COLORREF color) {
    SetTextColor(hdc, color);
    TextOutW(hdc, x, y, text, len);
}

/* Render the custom title bar */
static void render_titlebar(HDC hdc) {
    int th = DPI(TITLEBAR_H);
    fill_rect(hdc, 0, 0, g_editor.client_w, th, CLR_BG_DARK);

    SelectObject(hdc, g_editor.font_title);
    SetBkMode(hdc, TRANSPARENT);

    /* App icon/name */
    wchar_t title[256];
    Document *doc = current_doc();
    if (doc) {
        swprintf(title, 256, L"  \x270E  PROSE_CODE  \x2014  %s%s",
                 doc->title, doc->modified ? L" \x2022" : L"");
    } else {
        safe_wcscpy(title, 256, L"  \x270E  PROSE_CODE");
    }
    draw_text(hdc, DPI(8), (th - DPI(16)) / 2, title, (int)wcslen(title), CLR_LAVENDER);

    /* Window controls */
    int bw = DPI(46), bh = th;
    int x = g_editor.client_w - bw * 3;

    /* Minimize */
    if (g_editor.titlebar_hover_btn == 1)
        fill_rect(hdc, x, 0, bw, bh, g_theme.is_dark ? RGB(45, 45, 56) : RGB(210, 210, 215));
    SetTextColor(hdc, CLR_SUBTEXT);
    RECT r1 = { x, 0, x + bw, bh };
    DrawTextW(hdc, L"\x2500", 1, &r1, DT_CENTER | DT_VCENTER | DT_SINGLELINE);

    /* Maximize */
    x += bw;
    if (g_editor.titlebar_hover_btn == 2)
        fill_rect(hdc, x, 0, bw, bh, g_theme.is_dark ? RGB(45, 45, 56) : RGB(210, 210, 215));
    SetTextColor(hdc, CLR_SUBTEXT);
    RECT r2 = { x, 0, x + bw, bh };
    DrawTextW(hdc, L"\x25A1", 1, &r2, DT_CENTER | DT_VCENTER | DT_SINGLELINE);

    /* Close */
    x += bw;
    if (g_editor.titlebar_hover_btn == 3)
        fill_rect(hdc, x, 0, bw, bh, RGB(196, 43, 28));
    SetTextColor(hdc, g_editor.titlebar_hover_btn == 3 ? RGB(255, 255, 255) : CLR_SUBTEXT);
    RECT r3 = { x, 0, x + bw, bh };
    DrawTextW(hdc, L"\x2715", 1, &r3, DT_CENTER | DT_VCENTER | DT_SINGLELINE);

    /* Bottom border accent */
    fill_rect(hdc, 0, th - 1, g_editor.client_w, 1, CLR_SURFACE0);
}

/* Render tab bar */
static void render_tabbar(HDC hdc) {
    int y = DPI(TITLEBAR_H + MENUBAR_H);
    int tbh = DPI(TABBAR_H);
    fill_rect(hdc, 0, y, g_editor.client_w, tbh, CLR_BG_DARK);

    SelectObject(hdc, g_editor.font_ui_small);
    SetBkMode(hdc, TRANSPARENT);

    int x = DPI(8);
    for (int i = 0; i < g_editor.tab_count; i++) {
        Document *doc = g_editor.tabs[i];
        wchar_t label[128];
        swprintf(label, 128, L"%s%s", doc->title, doc->modified ? L" \x2022" : L"");
        int tw = (int)wcslen(label) * DPI(8) + DPI(TAB_PAD) * 2;
        if (tw < DPI(TAB_MIN_W)) tw = DPI(TAB_MIN_W);
        if (tw > DPI(TAB_MAX_W)) tw = DPI(TAB_MAX_W);

        if (i == g_editor.active_tab) {
            fill_rounded_rect(hdc, x, y + DPI(5), tw, tbh - DPI(5), DPI(10), CLR_TAB_ACTIVE);
            /* Active tab bottom indicator — subtle accent bar */
            fill_rounded_rect(hdc, x + DPI(12), y + tbh - DPI(3), tw - DPI(24), DPI(2), DPI(1), CLR_ACCENT);
            draw_text(hdc, x + DPI(TAB_PAD), y + (tbh - DPI(12)) / 2, label, (int)wcslen(label), CLR_TEXT);
        } else {
            draw_text(hdc, x + DPI(TAB_PAD), y + (tbh - DPI(12)) / 2, label, (int)wcslen(label), CLR_OVERLAY0);
        }

        /* Close button for tab */
        draw_text(hdc, x + tw - DPI(20), y + (tbh - DPI(12)) / 2, L"\x00D7", 1, CLR_OVERLAY0);

        x += tw + DPI(4);
    }

    /* New tab button */
    draw_text(hdc, x + DPI(8), y + (tbh - DPI(12)) / 2, L"+", 1, CLR_OVERLAY0);

    fill_rect(hdc, 0, y + tbh - 1, g_editor.client_w, 1, CLR_SURFACE0);
}

/* Render status bar */
static void render_statusbar(HDC hdc) {
    Document *doc = current_doc();
    int y = g_editor.client_h - DPI(STATUSBAR_H);
    fill_rect(hdc, 0, y, g_editor.client_w, DPI(STATUSBAR_H), CLR_BG_DARK);
    fill_rect(hdc, 0, y, g_editor.client_w, 1, CLR_SURFACE0);

    if (!doc) return;

    SelectObject(hdc, g_editor.font_ui_small);
    SetBkMode(hdc, TRANSPARENT);

    bpos line = pos_to_line(doc, doc->cursor) + 1;
    bpos col = pos_to_col(doc, doc->cursor) + 1;

    /* Left side: mode, line/col */
    wchar_t left[256];
    swprintf(left, 256, L"  %s  \x2502  Ln %lld, Col %lld",
             doc->mode == MODE_PROSE ? L"\x270D Prose" : L"\x2699 Code",
             (long long)line, (long long)col);
    draw_text(hdc, DPI(8), y + (DPI(STATUSBAR_H) - DPI(12)) / 2, left, (int)wcslen(left), CLR_SUBTEXT);

    /* Right side: stats */
    wchar_t right[256];
    int reading_time = (int)((doc->word_count + 237) / 238); /* ~238 wpm average */
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

    /* Focus mode indicator */
    if (g_editor.focus.active) {
        wchar_t foc[] = L"\x2B24 Focus";
        draw_text(hdc, g_editor.client_w / 2 - 30, y + (DPI(STATUSBAR_H) - DPI(12)) / 2,
                  foc, (int)wcslen(foc), CLR_GREEN);
    }

    /* Theme label */
    {
        const wchar_t *tname = g_theme_index == 0 ? L"Dark" : L"Light";
        wchar_t tbuf[32];
        swprintf(tbuf, 32, L"\x263C %s", tname);
        SIZE tsz;
        GetTextExtentPoint32W(hdc, tbuf, (int)wcslen(tbuf), &tsz);
        draw_text(hdc, g_editor.client_w / 2 + 40, y + (DPI(STATUSBAR_H) - DPI(12)) / 2,
                  tbuf, (int)wcslen(tbuf), CLR_OVERLAY0);
    }
}

/* Render search bar overlay */
static void render_searchbar(HDC hdc) {
    if (!g_editor.search.active) return;

    int bar_h = g_editor.search.replace_active ? DPI(72) : DPI(40);
    int bar_w = DPI(460);
    int x = g_editor.client_w - bar_w - DPI(24);
    int y = DPI(TITLEBAR_H + MENUBAR_H + TABBAR_H) + DPI(8);

    /* Shadow */
    fill_rounded_rect(hdc, x + 2, y + 2, bar_w, bar_h, DPI(10), RGB(8, 8, 14));
    /* Background */
    fill_rounded_rect(hdc, x, y, bar_w, bar_h, DPI(10), CLR_SURFACE0);
    /* Border */
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

    /* Search icon */
    draw_text(hdc, x + DPI(12), y + DPI(11), L"\x2315", 1, CLR_OVERLAY0);

    /* Query text */
    int qlen = (int)wcslen(g_editor.search.query);
    draw_text(hdc, x + DPI(32), y + DPI(12), g_editor.search.query, qlen, CLR_TEXT);

    /* Search field cursor (blinking) */
    if (!g_editor.search.replace_focused || !g_editor.search.replace_active) {
        DWORD now = GetTickCount();
        if ((now / 530) % 2 == 0) {
            SIZE qs;
            GetTextExtentPoint32W(hdc, g_editor.search.query, qlen, &qs);
            fill_rect(hdc, x + DPI(32) + qs.cx + 1, y + DPI(11), DPI(1), DPI(16), CLR_ACCENT);
        }
    }

    /* Focus underline for search field */
    if (!g_editor.search.replace_focused || !g_editor.search.replace_active) {
        fill_rect(hdc, x + DPI(30), y + DPI(28), bar_w - DPI(130), 1, CLR_ACCENT);
    }

    /* Match count */
    wchar_t mc[32];
    if (g_editor.search.match_count > 0) {
        swprintf(mc, 32, L"%d/%d", g_editor.search.current_match + 1, g_editor.search.match_count);
    } else {
        wcscpy(mc, L"No results");
    }
    draw_text(hdc, x + bar_w - DPI(90), y + DPI(12), mc, (int)wcslen(mc),
              g_editor.search.match_count > 0 ? CLR_SUBTEXT : CLR_RED);

    /* Replace field */
    if (g_editor.search.replace_active) {
        draw_text(hdc, x + DPI(12), y + DPI(43), L"\x21C6", 1, CLR_OVERLAY0);
        int rlen = (int)wcslen(g_editor.search.replace_text);
        draw_text(hdc, x + DPI(32), y + DPI(44), g_editor.search.replace_text, rlen, CLR_TEXT);
        /* Replace field cursor (blinking) */
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

/* Determine syntax token for a character position in code mode */
/* Single-pass line tokenizer for code — O(n) per line instead of O(n²) */
static int tokenize_line_code(const wchar_t *chars, int line_len, SynToken *out, int in_block_comment) {
    int i = 0;

    /* Default all to NORMAL, then overwrite */
    for (i = 0; i < line_len; i++) out[i] = TOK_NORMAL;

    /* If we're inside a block comment from a previous line, scan for closing */
    if (in_block_comment) {
        for (i = 0; i < line_len - 1; i++) {
            out[i] = TOK_COMMENT;
            if (chars[i] == L'*' && chars[i + 1] == L'/') {
                out[i + 1] = TOK_COMMENT;
                i += 2;
                goto normal_scan; /* resume normal tokenization after the close */
            }
        }
        /* No closing found — entire line is comment */
        if (i < line_len) out[i] = TOK_COMMENT;
        return 1; /* still in block comment */
    }

    /* Check for preprocessor: line starts with optional whitespace then # */
    {
        int j = 0;
        while (j < line_len && iswspace(chars[j])) j++;
        if (j < line_len && chars[j] == L'#') {
            /* Check if it looks like a preprocessor directive (# followed by alpha) */
            if (j + 1 < line_len && iswalpha(chars[j + 1])) {
                for (int k = 0; k < line_len; k++) out[k] = TOK_PREPROCESSOR;
                return 0;
            }
        }
    }

    i = 0;
normal_scan:
    while (i < line_len) {
        wchar_t c = chars[i];

        /* Block comment open: slash-star */
        if (c == L'/' && i + 1 < line_len && chars[i + 1] == L'*') {
            out[i] = TOK_COMMENT;
            out[i + 1] = TOK_COMMENT;
            i += 2;
            while (i < line_len) {
                if (i + 1 < line_len && chars[i] == L'*' && chars[i + 1] == L'/') {
                    out[i] = TOK_COMMENT;
                    out[i + 1] = TOK_COMMENT;
                    i += 2;
                    goto normal_scan; /* closed on this line, continue */
                }
                out[i] = TOK_COMMENT;
                i++;
            }
            return 1; /* unclosed — still in block comment */
        }

        /* Line comment: // */
        if (c == L'/' && i + 1 < line_len && chars[i + 1] == L'/') {
            for (int j = i; j < line_len; j++) out[j] = TOK_COMMENT;
            return 0;
        }

        /* Hash comment (Python/shell): # not preceded by alpha */
        if (c == L'#' && (i == 0 || !iswalpha(chars[i - 1]))) {
            for (int j = i; j < line_len; j++) out[j] = TOK_COMMENT;
            return 0;
        }

        /* String literal */
        if (c == L'"' || c == L'\'') {
            wchar_t q = c;
            out[i] = TOK_STRING;
            i++;
            while (i < line_len) {
                wchar_t sc = chars[i];
                out[i] = TOK_STRING;
                if (sc == q) { i++; break; }
                if (sc == L'\\' && i + 1 < line_len) { i++; out[i] = TOK_STRING; }
                i++;
            }
            continue;
        }

        /* Number — with proper hex/binary/scientific notation support */
        if (iswdigit(c) || (c == L'.' && i + 1 < line_len && iswdigit(chars[i + 1]))) {
            int ns = i;
            if (c == L'0' && i + 1 < line_len) {
                wchar_t next = chars[i + 1];
                if (next == L'x' || next == L'X') {
                    /* Hex: consume 0x then hex digits */
                    i += 2;
                    while (i < line_len && iswxdigit(chars[i])) i++;
                } else if (next == L'b' || next == L'B') {
                    /* Binary: 0b... */
                    i += 2;
                    while (i < line_len && (chars[i] == L'0' || chars[i] == L'1')) i++;
                } else {
                    goto decimal;
                }
            } else {
                decimal:
                while (i < line_len && (iswdigit(chars[i]) || chars[i] == L'.')) i++;
                /* Scientific notation */
                if (i < line_len && (chars[i] == L'e' || chars[i] == L'E')) {
                    i++;
                    if (i < line_len && (chars[i] == L'+' || chars[i] == L'-')) i++;
                    while (i < line_len && iswdigit(chars[i])) i++;
                }
            }
            /* Consume integer/float suffixes: u, l, f */
            while (i < line_len && (chars[i] == L'u' || chars[i] == L'U' ||
                   chars[i] == L'l' || chars[i] == L'L' ||
                   chars[i] == L'f' || chars[i] == L'F')) i++;
            for (int j = ns; j < i; j++) out[j] = TOK_NUMBER;
            continue;
        }

        /* Identifier / keyword */
        if (iswalpha(c) || c == L'_') {
            int ws = i;
            wchar_t word[64];
            int wl = 0;
            while (i < line_len && (iswalnum(chars[i]) || chars[i] == L'_')) {
                if (wl < 63) word[wl++] = chars[i];
                i++;
            }
            word[wl] = 0;

            SynToken tok = TOK_NORMAL;
            if (is_c_keyword(word, wl)) {
                tok = TOK_KEYWORD;
            } else if (i < line_len && chars[i] == L'(') {
                tok = TOK_FUNCTION;
            } else if (iswupper(word[0])) {
                tok = TOK_TYPE;
            }
            for (int j = ws; j < i; j++) out[j] = tok;
            continue;
        }

        /* Operators */
        if (wcschr(L"+-*/%=<>!&|^~?:", c)) {
            out[i] = TOK_OPERATOR;
            i++;
            continue;
        }

        i++;
    }
    return 0; /* not in block comment */
}

/* Single-pass line tokenizer for prose/markdown — O(n) per line */
static void tokenize_line_prose(const wchar_t *chars, int line_len, SynToken *out) {
    /* Default all to NORMAL */
    for (int i = 0; i < line_len; i++) out[i] = TOK_NORMAL;
    if (line_len == 0) return;

    wchar_t first = chars[0];

    /* Heading: line starts with # */
    if (first == L'#') {
        for (int i = 0; i < line_len; i++) out[i] = TOK_MD_HEADING;
        return;
    }

    /* Blockquote */
    if (first == L'>') {
        for (int i = 0; i < line_len; i++) out[i] = TOK_MD_BLOCKQUOTE;
        return;
    }

    /* List marker: -, *, + */
    if ((first == L'-' || first == L'*' || first == L'+') &&
        line_len > 1 && chars[1] == L' ') {
        out[0] = TOK_MD_LIST;
    }

    /* Numbered list marker: 1. 2. etc. */
    if (iswdigit(first)) {
        int j = 0;
        while (j < line_len && iswdigit(chars[j])) j++;
        if (j < line_len && chars[j] == L'.' && j + 1 < line_len && chars[j + 1] == L' ') {
            for (int k = 0; k <= j; k++) out[k] = TOK_MD_LIST;
        }
    }

    /* Horizontal rule: line of 3+ dashes, stars, or underscores (with optional spaces) */
    if (first == L'-' || first == L'*' || first == L'_') {
        wchar_t rule_ch = first;
        int rule_count = 0;
        int is_rule = 1;
        for (int i = 0; i < line_len; i++) {
            if (chars[i] == rule_ch) rule_count++;
            else if (chars[i] != L' ') { is_rule = 0; break; }
        }
        if (is_rule && rule_count >= 3) {
            for (int i = 0; i < line_len; i++) out[i] = TOK_MD_HEADING;
        }
    }

    /* Inline code: `...` */
    {
        int i = 0;
        while (i < line_len) {
            if (chars[i] == L'`') {
                out[i] = TOK_MD_CODE;
                i++;
                while (i < line_len && chars[i] != L'`') {
                    out[i] = TOK_MD_CODE;
                    i++;
                }
                if (i < line_len) { out[i] = TOK_MD_CODE; i++; }
            } else {
                i++;
            }
        }
    }

    /* Bold: **text** — don't overwrite code spans */
    {
        int i = 0;
        while (i < line_len - 1) {
            if (out[i] != TOK_MD_CODE &&
                chars[i] == L'*' && chars[i + 1] == L'*') {
                out[i] = TOK_MD_BOLD;
                out[i + 1] = TOK_MD_BOLD;
                i += 2;
                while (i < line_len - 1) {
                    if (chars[i] == L'*' && chars[i + 1] == L'*') {
                        out[i] = TOK_MD_BOLD;
                        out[i + 1] = TOK_MD_BOLD;
                        i += 2;
                        break;
                    }
                    out[i] = TOK_MD_BOLD;
                    i++;
                }
            } else {
                i++;
            }
        }
    }

    /* Italic: *text* or _text_ — skip code spans and bold markers */
    {
        int i = 0;
        while (i < line_len) {
            if (out[i] != TOK_MD_CODE && out[i] != TOK_MD_BOLD &&
                (chars[i] == L'*' || chars[i] == L'_')) {
                wchar_t delim = chars[i];
                /* Skip if next char is also a delimiter (bold opener) or space */
                if (i + 1 < line_len && chars[i + 1] != delim && chars[i + 1] != L' ') {
                    int start = i;
                    i++;
                    while (i < line_len && chars[i] != delim &&
                           out[i] != TOK_MD_CODE && out[i] != TOK_MD_BOLD) {
                        i++;
                    }
                    if (i < line_len && chars[i] == delim) {
                        /* Found closing delimiter — mark the span */
                        for (int j = start; j <= i; j++) {
                            if (out[j] == TOK_NORMAL) out[j] = TOK_MD_ITALIC;
                        }
                        i++;
                    }
                    /* else: no closing delimiter, leave as normal */
                } else {
                    i++;
                }
            } else {
                i++;
            }
        }
    }

    /* Links: [text](url) — skip code spans */
    {
        int i = 0;
        while (i < line_len) {
            if (out[i] == TOK_MD_CODE) { i++; continue; }
            if (chars[i] == L'[') {
                int bracket_start = i;
                i++;
                /* Find closing ] */
                while (i < line_len && chars[i] != L']') i++;
                if (i < line_len && i + 1 < line_len && chars[i + 1] == L'(') {
                    /* Found ]( — now find closing ) */
                    int paren_start = i + 1;
                    i = paren_start + 1;
                    while (i < line_len && chars[i] != L')') i++;
                    if (i < line_len) {
                        /* Mark entire [text](url) as link */
                        for (int j = bracket_start; j <= i; j++) {
                            if (out[j] == TOK_NORMAL) out[j] = TOK_MD_LINK;
                        }
                        i++;
                    }
                } else {
                    i++;
                }
            } else {
                i++;
            }
        }
    }
}

/* Main editor area rendering */
static void render_editor(HDC hdc) {
    Document *doc = current_doc();
    if (!doc) return;

    int edit_x = 0;
    int edit_y = DPI(TITLEBAR_H + MENUBAR_H + TABBAR_H);
    int edit_w = g_editor.client_w - DPI(SCROLLBAR_W);
    if (g_editor.show_minimap) edit_w -= DPI(MINIMAP_W);
    int edit_h = g_editor.client_h - edit_y - DPI(STATUSBAR_H);

    /* Background */
    fill_rect(hdc, edit_x, edit_y, g_editor.client_w, edit_h, CLR_BG);

    /* Clip to editor area */
    HRGN clip = CreateRectRgn(edit_x, edit_y, edit_x + edit_w, edit_y + edit_h);
    SelectClipRgn(hdc, clip);

    int lh = g_editor.line_height;
    int cw = g_editor.char_width;
    int gutter_w = (doc->mode == MODE_CODE) ? (LINE_NUM_CHARS * cw + DPI(GUTTER_PAD) * 2) : DPI(24);
    int text_x = edit_x + gutter_w;
    /* Apply horizontal scroll offset in code mode */
    if (doc->mode == MODE_CODE) text_x -= doc->scroll_x;
    int text_w = edit_w - gutter_w;

    /* Soft wrap: rebuild WrapCache if needed (prose mode only) */
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

    /* Total visual lines for scrolling and range computation */
    int total_vlines = (int)(use_wrap ? doc->wc.count : doc->lc.count);
    if (total_vlines < 1) total_vlines = 1;

    bpos first_vline = doc->scroll_y / lh;
    bpos last_vline = (doc->scroll_y + edit_h) / lh + 1;
    if (first_vline < 0) first_vline = 0;
    if (last_vline >= total_vlines) last_vline = total_vlines - 1;

    /* Cursor visual line */
    bpos cursor_vline = use_wrap ? wc_visual_line_of(&doc->wc, doc->cursor)
                                : pos_to_line(doc, doc->cursor);
    bpos cursor_logical = pos_to_line(doc, doc->cursor);
    bpos sel_s = selection_start(doc);
    bpos sel_e = selection_end(doc);
    int has_sel = has_selection(doc);

    SelectObject(hdc, g_editor.font_main);
    SetBkMode(hdc, TRANSPARENT);

    /* Gutter background */
    fill_rect(hdc, edit_x, edit_y, gutter_w, edit_h, CLR_GUTTER);
    fill_rect(hdc, edit_x + gutter_w - 1, edit_y, 1, edit_h, CLR_SURFACE0);

    /* Binary search to find first search match visible on screen */
    int match_cursor = 0;
    if (g_editor.search.active && g_editor.search.match_count > 0) {
        int first_pos = use_wrap ? doc->wc.entries[first_vline].pos
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

    /* Compute bracket match pair (once before the line loop) */
    bpos bracket_pos1 = -1, bracket_pos2 = -1;
    if (doc->mode == MODE_CODE) {
        /* Check character at cursor and before cursor */
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

    /* Compute focus-mode paragraph range ONCE (not per-line) */
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

    /* Pre-create indent guide pen for code mode (avoids CreatePen/DeleteObject per line) */
    HPEN guide_pen = NULL;
    if (doc->mode == MODE_CODE) {
        guide_pen = CreatePen(PS_DOT, 1, CLR_SURFACE0);
    }

    /* Pre-create spell check pen for prose mode (avoids CreatePen/DeleteObject per line) */
    HPEN spell_pen = NULL;
    if (doc->mode == MODE_PROSE && g_spell_loaded) {
        spell_pen = CreatePen(PS_DOT, 1, CLR_MISSPELLED);
    }

    /* Block comment state: compute whether we're inside a multi-line comment
     * at the first visible logical line. Cache advances line-by-line to avoid
     * full-document rescans while scrolling. */
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

    /* Hoist large per-line buffers above the loop to reduce stack churn.
     * Reused across iterations — ~28KB total instead of per-iteration. */
    SynToken line_tokens[2048];
    wchar_t  line_chars[2048];
    int      x_positions[2049]; /* one extra for end-of-line */
    wchar_t  run_buf[2048];

    for (bpos vline = first_vline; vline <= last_vline; vline++) {
        int y = edit_y + (int)(vline * lh - doc->scroll_y);

        /* Derive text range and logical line from visual line */
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
        /* Alias for code that still uses 'line' for logical line number */
        bpos line = logical_line;

        /* Active line highlight */
        if (vline == cursor_vline) {
            fill_rect(hdc, text_x, y, text_w, lh, CLR_ACTIVELINE);
        }

        /* Focus mode dimming — use pre-computed paragraph range */
        int dim_this_line = 0;
        if (focus_para_start >= 0) {
            if (line < focus_para_start || line > focus_para_end) {
                dim_this_line = 1;
            }
        }

        /* Line numbers (code mode) */
        if (doc->mode == MODE_CODE) {
            SelectObject(hdc, g_editor.font_main);
            wchar_t num[16];
            swprintf(num, 16, L"%*d", LINE_NUM_CHARS, (int)(line + 1));
            COLORREF nc = (line == cursor_logical) ? CLR_TEXT : CLR_GUTTER_TEXT;
            if (dim_this_line) nc = CLR_SURFACE1;
            /* Highlight pill behind current line number */
            if (line == cursor_logical) {
                int num_w = (int)wcslen(num) * cw;
                fill_rounded_rect(hdc, edit_x + DPI(GUTTER_PAD) - 2, y + 2,
                                  num_w + 4, lh - 4, 4, CLR_SURFACE0);
            }
            draw_text(hdc, edit_x + DPI(GUTTER_PAD), y + 1, num, (int)wcslen(num), nc);
        }

        /* Prose gutter: wrap continuation indicator */
        if (use_wrap && doc->wc.count > 0 && vline > 0 &&
            doc->wc.entries[vline].line == doc->wc.entries[vline - 1].line) {
            draw_text(hdc, edit_x + DPI(6), y + 1, L"\x21A9", 1, CLR_SURFACE1);
        }

        /* Tokenize entire line in one pass — O(n) instead of O(n²) */
        int safe_len = (line_len < 2048) ? line_len : 2048;

        /* Extract visible line text into flat array — avoids per-char gb_char_at
         * throughout the rest of this loop body (x_positions, rendering, indent, spell) */
        gb_copy_range(&doc->gb, ls, safe_len, line_chars);

        if (doc->mode == MODE_CODE) {
            in_block_comment = tokenize_line_code(line_chars, safe_len, line_tokens, in_block_comment);
        } else {
            tokenize_line_prose(line_chars, safe_len, line_tokens);
        }

        /* Pre-compute x positions accounting for tabs */
        {
            int xp = 0;
            for (int i = 0; i < safe_len; i++) {
                x_positions[i] = xp;
                xp += (line_chars[i] == L'\t') ? cw * 4 : cw;
            }
            x_positions[safe_len] = xp;
        }

        /* Bracket match highlights (uses tab-aware x_positions) */
        if (bracket_pos1 >= 0) {
            if (bracket_pos1 >= ls && bracket_pos1 < le && bracket_pos1 - ls < safe_len) {
                int bi = bracket_pos1 - ls;
                int bw = (line_chars[bi] == L'\t') ? cw * 4 : cw;
                fill_rect(hdc, text_x + x_positions[bi], y, bw, lh, CLR_SURFACE1);
            }
            if (bracket_pos2 >= ls && bracket_pos2 < le && bracket_pos2 - ls < safe_len) {
                int bi = bracket_pos2 - ls;
                int bw = (line_chars[bi] == L'\t') ? cw * 4 : cw;
                fill_rect(hdc, text_x + x_positions[bi], y, bw, lh, CLR_SURFACE1);
            }
        }

        /* Draw selection background as contiguous rects */
        if (has_sel) {
            int rs = -1;
            for (int i = 0; i <= safe_len; i++) {
                int pos = ls + i;
                int in_sel = (i < safe_len && pos >= sel_s && pos < sel_e);
                if (in_sel && rs < 0) rs = i;
                if (!in_sel && rs >= 0) {
                    fill_rect(hdc, text_x + x_positions[rs], y,
                              x_positions[i] - x_positions[rs], lh, CLR_SELECTION);
                    rs = -1;
                }
            }
        }

        /* Draw search match highlights (advancing cursor, not full scan) */
        if (g_editor.search.active && g_editor.search.match_count > 0) {
            int qlen = (int)wcslen(g_editor.search.query);
            int line_end_pos = ls + safe_len;
            for (int m = match_cursor; m < g_editor.search.match_count; m++) {
                int ms = g_editor.search.match_positions[m];
                if (ms >= line_end_pos) break;
                int me = ms + qlen;
                if (me <= ls) { match_cursor = m + 1; continue; }
                /* Clamp to line range */
                int hs = (ms > ls) ? ms - ls : 0;
                int he = (me < line_end_pos) ? me - ls : safe_len;
                COLORREF hl = (m == g_editor.search.current_match) ? CLR_SEARCH_HL : CLR_SURFACE1;
                fill_rect(hdc, text_x + x_positions[hs], y,
                          x_positions[he] - x_positions[hs], lh, hl);
            }
        }

        /* Batched text rendering: consecutive chars with same token → single TextOutW */
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
                    /* Select font for this run */
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
                        /* Tabs are skipped in text output (x_positions handles spacing) */
                        if (run_len > 0 && !run_is_tab) {
                            /* Already flushed above */
                        }
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
                        run_buf[run_len++] = c;
                    }
                }
            }
        }

        /* Long line truncation indicator */
        if (line_len > 2048) {
            int trunc_x = text_x + x_positions[safe_len];
            draw_text(hdc, trunc_x, y + 1, L"\x2026", 1, CLR_OVERLAY0); /* … */
        }

        /* Indent guides (code mode only) */
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

        /* Spell check underlines (prose mode, visible words only) */
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
                                /* Use precomputed x_positions for tab-correct placement */
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

    /* Clean up indent guide pen */
    if (guide_pen) DeleteObject(guide_pen);
    /* Clean up spell check pen */
    if (spell_pen) DeleteObject(spell_pen);

    /* Cursor — smooth sinusoidal fade blink */
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
            /* Stay fully visible for 400ms after any edit/move */
            alpha = 1.0f;
        } else {
            /* Sinusoidal fade: period 1200ms */
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

        /* Background */
        fill_rect(hdc, mm_x, edit_y, mm_w, mm_h, CLR_BG_DARK);
        /* Left border */
        fill_rect(hdc, mm_x, edit_y, 1, mm_h, CLR_SURFACE0);

        int mm_total = total_vlines;
        if (mm_total <= 0) mm_total = 1;

        /* Scale: map all visual lines to minimap height, min 1px per line */
        float scale = (float)mm_h / (float)mm_total;
        if (scale > 3.0f) scale = 3.0f; /* cap line height */

        /* Viewport rectangle (which visual lines are visible) */
        int vp_top = (int)(first_vline * scale);
        int vp_bot = (int)((last_vline + 1) * scale);
        if (vp_bot > mm_h) vp_bot = mm_h;
        if (vp_bot - vp_top < 8) vp_bot = vp_top + 8; /* minimum viewport height */
        fill_rect(hdc, mm_x + 1, edit_y + vp_top, mm_w - 1, vp_bot - vp_top,
                  g_theme.is_dark ? RGB(255, 255, 255) : RGB(0, 0, 0));
        /* Semi-transparent effect: draw bg_dark over with slight alpha via blend */
        fill_rect(hdc, mm_x + 1, edit_y + vp_top, mm_w - 1, vp_bot - vp_top,
                  g_theme.is_dark ? RGB(48, 50, 62) : RGB(200, 210, 230));

        /* Draw minimap lines — visual line bars */
        int max_render_lines = (int)(mm_h / scale) + 1;
        if (max_render_lines > mm_total) max_render_lines = mm_total;

        /* Stride optimization: for large files, skip lines that would map to
         * the same pixel row. Caps iterations to ~mm_h regardless of file size. */
        int stride = 1;
        if (mm_total > mm_h * 2) stride = mm_total / mm_h;
        if (stride < 1) stride = 1;

        int mm_in_block_comment = 0;
        int mm_prev_line = -1; /* track sequential lines for comment state */

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

            /* Advance block comment state through skipped lines incrementally.
             * Old code called compute_block_comment_state(pos=0..mls) which is O(file_size)
             * per gap — catastrophic for large files. Instead, walk from the last known
             * state through intermediate lines using the O(line_len) per-line advancer. */
            if (doc->mode == MODE_CODE && mm_prev_line >= 0 && i != mm_prev_line + stride) {
                for (int skip = mm_prev_line + 1; skip < i; skip++) {
                    bpos sls = lc_line_start(&doc->lc, skip);
                    bpos sle = lc_line_end(&doc->lc, &doc->gb, skip);
                    mm_in_block_comment = advance_block_comment_state_for_line(&doc->gb, sls, sle, mm_in_block_comment);
                }
            }
            mm_prev_line = i;

            /* Proportional width bar, clamped to minimap width */
            int bar_w = (mline_len * (mm_w - 8)) / 120;
            if (bar_w < 2) bar_w = 2;
            if (bar_w > mm_w - 8) bar_w = mm_w - 8;

            int bar_h = (int)scale;
            if (bar_h < 1) bar_h = 1;

            COLORREF mc;
            /* Syntax-colored minimap: tokenize first 40 chars for dominant token */
            if (doc->mode == MODE_CODE && mline_len > 0) {
                int mm_len = (mline_len < 40) ? mline_len : 40;
                wchar_t mm_chars[40];
                SynToken mm_tokens[40];
                gb_copy_range(&doc->gb, mls, mm_len, mm_chars);
                mm_in_block_comment = tokenize_line_code(mm_chars, mm_len, mm_tokens, mm_in_block_comment);
                /* Find most common non-NORMAL token */
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
                    /* Dim to ~40% brightness for minimap */
                    mc = RGB(GetRValue(tc)*2/5, GetGValue(tc)*2/5, GetBValue(tc)*2/5);
                } else {
                    mc = g_theme.is_dark ? RGB(120, 122, 138) : RGB(120, 120, 130);
                }
            } else {
                mc = g_theme.is_dark ? RGB(120, 122, 138) : RGB(120, 120, 130);
            }
            /* Dim lines outside viewport slightly */
            if (my < edit_y + vp_top || my > edit_y + vp_bot) {
                mc = RGB(GetRValue(mc)*3/5, GetGValue(mc)*3/5, GetBValue(mc)*3/5);
            }
            fill_rect(hdc, mm_x + 4, my, bar_w, bar_h, mc);
        }
    }
}

/* ═══════════════════════════════════════════════════════════════
 * SESSION STATS OVERLAY — Ctrl+I toggle
 * ═══════════════════════════════════════════════════════════════ */

static void render_stats_screen(HDC hdc) {
    if (!g_editor.show_stats_screen) return;

    int cw = g_editor.client_w;
    int ch = g_editor.client_h;

    /* Dim the background */
    fill_rect(hdc, 0, 0, cw, ch, g_theme.is_dark ? RGB(14, 14, 20) : RGB(240, 240, 245));

    /* Panel dimensions */
    int pw = DPI(480), ph = DPI(420);
    if (pw > cw - DPI(40)) pw = cw - DPI(40);
    if (ph > ch - DPI(40)) ph = ch - DPI(40);
    int px = (cw - pw) / 2;
    int py = (ch - ph) / 2;

    /* Panel background + border */
    fill_rounded_rect(hdc, px + 3, py + 3, pw, ph, DPI(16),
                      g_theme.is_dark ? RGB(6, 6, 12) : RGB(180, 180, 185));
    fill_rounded_rect(hdc, px, py, pw, ph, DPI(16), CLR_BG);
    fill_rounded_rect(hdc, px + 1, py + 1, pw - 2, ph - 2, DPI(15), CLR_BG);

    /* Accent bar at top of panel */
    fill_rounded_rect(hdc, px + DPI(24), py + DPI(12), pw - DPI(48), DPI(2), DPI(1), CLR_ACCENT);

    SetBkMode(hdc, TRANSPARENT);
    int y = py + DPI(24);
    int left_margin = px + DPI(32);
    int right_col = px + pw - DPI(32);

    /* Title */
    SelectObject(hdc, g_editor.font_title);
    {
        wchar_t title[] = L"\x2328  SESSION STATS";
        draw_text(hdc, left_margin, y, title, (int)wcslen(title), CLR_LAVENDER);
    }
    y += DPI(32);

    /* Divider */
    fill_rect(hdc, left_margin, y, pw - DPI(64), 1, CLR_SURFACE0);
    y += DPI(16);

    /* Session duration */
    DWORD elapsed_ms = GetTickCount() - g_editor.session_start_time;
    int elapsed_s = (int)(elapsed_ms / 1000);
    int hrs = elapsed_s / 3600;
    int mins = (elapsed_s % 3600) / 60;
    int secs = elapsed_s % 60;

    SelectObject(hdc, g_editor.font_ui);

    /* ── Aggregate across all tabs ── */
    bpos total_words_added = 0;
    bpos total_chars_added = 0;
    bpos total_lines_added = 0;
    bpos total_words_now = 0;
    bpos total_chars_now = 0;

    for (int i = 0; i < g_editor.tab_count; i++) {
        Document *d = g_editor.tabs[i];
        /* Force fresh count for accurate display */
        d->stats_dirty = 1;
        update_stats_now(d);
        total_words_added += (d->word_count - d->session_start_words);
        total_chars_added += (d->char_count - d->session_start_chars);
        total_lines_added += (d->line_count - d->session_start_lines);
        total_words_now += d->word_count;
        total_chars_now += d->char_count;
    }

    /* Time display */
    {
        wchar_t tbuf[64];
        swprintf(tbuf, 64, L"%dh %02dm %02ds", hrs, mins, secs);
        draw_text(hdc, left_margin, y, L"Session Duration", 16, CLR_SUBTEXT);
        SIZE sz; GetTextExtentPoint32W(hdc, tbuf, (int)wcslen(tbuf), &sz);
        draw_text(hdc, right_col - sz.cx, y, tbuf, (int)wcslen(tbuf), CLR_TEXT);
    }
    y += DPI(28);

    /* ── Words Added (hero stat) ── */
    fill_rounded_rect(hdc, left_margin - DPI(8), y - DPI(4), pw - DPI(48), DPI(52), DPI(10), CLR_SURFACE0);
    {
        SelectObject(hdc, g_editor.font_title);
        draw_text(hdc, left_margin, y, L"\x270D  Words Added", 14, CLR_SUBTEXT);
        y += DPI(4);

        /* Big number */
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

    /* Divider */
    fill_rect(hdc, left_margin, y, pw - DPI(64), 1, CLR_SURFACE0);
    y += DPI(16);

    /* Chars Added */
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

    /* Lines Added */
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

    /* Words Per Minute (session average) */
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

    /* Divider */
    fill_rect(hdc, left_margin, y, pw - DPI(64), 1, CLR_SURFACE0);
    y += DPI(16);

    /* Document totals header */
    SelectObject(hdc, g_editor.font_title);
    draw_text(hdc, left_margin, y, L"DOCUMENT TOTALS", 15, CLR_OVERLAY0);
    y += DPI(24);
    SelectObject(hdc, g_editor.font_ui);

    /* Total words */
    {
        wchar_t buf[64];
        swprintf(buf, 64, L"%lld", (long long)total_words_now);
        draw_text(hdc, left_margin, y, L"Total Words", 11, CLR_SUBTEXT);
        SIZE sz; GetTextExtentPoint32W(hdc, buf, (int)wcslen(buf), &sz);
        draw_text(hdc, right_col - sz.cx, y, buf, (int)wcslen(buf), CLR_TEXT);
    }
    y += DPI(24);

    /* Total chars */
    {
        wchar_t buf[64];
        swprintf(buf, 64, L"%lld", (long long)total_chars_now);
        draw_text(hdc, left_margin, y, L"Total Characters", 16, CLR_SUBTEXT);
        SIZE sz; GetTextExtentPoint32W(hdc, buf, (int)wcslen(buf), &sz);
        draw_text(hdc, right_col - sz.cx, y, buf, (int)wcslen(buf), CLR_TEXT);
    }
    y += DPI(24);

    /* Open tabs */
    {
        wchar_t buf[64];
        swprintf(buf, 64, L"%d", g_editor.tab_count);
        draw_text(hdc, left_margin, y, L"Open Tabs", 9, CLR_SUBTEXT);
        SIZE sz; GetTextExtentPoint32W(hdc, buf, (int)wcslen(buf), &sz);
        draw_text(hdc, right_col - sz.cx, y, buf, (int)wcslen(buf), CLR_TEXT);
    }
    y += DPI(32);

    /* Hint */
    SelectObject(hdc, g_editor.font_ui_small);
    {
        wchar_t hint[] = L"Press Ctrl+I or Esc to close";
        SIZE sz; GetTextExtentPoint32W(hdc, hint, (int)wcslen(hint), &sz);
        draw_text(hdc, px + (pw - sz.cx) / 2, y, hint, (int)wcslen(hint), CLR_OVERLAY0);
    }
}

/* Main render function */

/* ═══════════════════════════════════════════════════════════════
 * MENU ACTION DISPATCH
 * ═══════════════════════════════════════════════════════════════ */

static void menu_execute(int id) {
    HWND hwnd = g_editor.hwnd;
    switch (id) {
    case MENU_ID_NEW:        new_tab(); break;
    case MENU_ID_OPEN:       open_file_dialog(); break;
    case MENU_ID_SAVE:       save_current_file(); break;
    case MENU_ID_CLOSE_TAB:  close_tab(g_editor.active_tab); break;
    case MENU_ID_UNDO:       editor_undo(); editor_ensure_cursor_visible(); break;
    case MENU_ID_REDO:       editor_redo(); editor_ensure_cursor_visible(); break;
    case MENU_ID_CUT:        editor_cut(); break;
    case MENU_ID_COPY:       editor_copy(); break;
    case MENU_ID_PASTE:      editor_paste(); editor_ensure_cursor_visible(); break;
    case MENU_ID_SELECT_ALL: editor_select_all(); break;
    case MENU_ID_FIND:       toggle_search(); break;
    case MENU_ID_REPLACE:    g_editor.search.active = 1; g_editor.search.replace_active = 1; break;
    case MENU_ID_FIND_NEXT:  search_next(); break;
    case MENU_ID_TOGGLE_MODE: toggle_mode(); break;
    case MENU_ID_MINIMAP:    g_editor.show_minimap = !g_editor.show_minimap; break;
    case MENU_ID_FOCUS:      toggle_focus_mode(); break;
    case MENU_ID_STATS:      g_editor.show_stats_screen = !g_editor.show_stats_screen; break;
    case MENU_ID_THEME:      apply_theme(g_theme_index == 0 ? 1 : 0); break;
    case MENU_ID_ZOOM_IN:
        g_editor.font_size += 2;
        if (g_editor.font_size > 48) g_editor.font_size = 48;
        PostMessage(hwnd, WM_USER + 1, 0, 0);
        break;
    case MENU_ID_ZOOM_OUT:
        g_editor.font_size -= 2;
        if (g_editor.font_size < 8) g_editor.font_size = 8;
        PostMessage(hwnd, WM_USER + 1, 0, 0);
        break;
    }
    g_editor.menu_open = -1;
    InvalidateRect(hwnd, NULL, FALSE);
}

/* ═══════════════════════════════════════════════════════════════
 * MENU BAR RENDERING
 * ═══════════════════════════════════════════════════════════════ */

static void render_menubar(HDC hdc) {
    int y = DPI(TITLEBAR_H);
    int h = DPI(MENUBAR_H);
    fill_rect(hdc, 0, y, g_editor.client_w, h, CLR_BG_DARK);
    fill_rect(hdc, 0, y + h - 1, g_editor.client_w, 1, CLR_SURFACE0);

    SelectObject(hdc, g_editor.font_ui_small);
    SetBkMode(hdc, TRANSPARENT);

    int x = DPI(8);
    int pad = DPI(16);
    for (int i = 0; i < MENU_COUNT; i++) {
        SIZE sz;
        GetTextExtentPoint32W(hdc, g_menus[i].label, (int)wcslen(g_menus[i].label), &sz);
        int item_w = sz.cx + pad * 2;
        g_editor.menu_bar_widths[i] = item_w;

        /* Highlight if this menu is open or hovered */
        if (g_editor.menu_open == i) {
            fill_rounded_rect(hdc, x, y + DPI(3), item_w, h - DPI(6), DPI(4),
                              g_theme.is_dark ? RGB(42, 42, 54) : RGB(210, 210, 215));
        }

        COLORREF clr = (g_editor.menu_open == i) ? CLR_TEXT : CLR_SUBTEXT;
        draw_text(hdc, x + pad, y + (h - sz.cy) / 2,
                  g_menus[i].label, (int)wcslen(g_menus[i].label), clr);
        x += item_w;
    }
}

static void render_menu_dropdown(HDC hdc) {
    if (g_editor.menu_open < 0 || g_editor.menu_open >= MENU_COUNT) return;

    const MenuDef *menu = &g_menus[g_editor.menu_open];
    int item_h = DPI(26);
    int sep_h = DPI(9);
    int dropdown_w = DPI(260);
    int pad_x = DPI(12);
    int shortcut_margin = DPI(80);

    /* Calculate dropdown position */
    int bar_x = DPI(8);
    for (int i = 0; i < g_editor.menu_open; i++)
        bar_x += g_editor.menu_bar_widths[i];

    int dropdown_x = bar_x;
    int dropdown_y = DPI(TITLEBAR_H + MENUBAR_H);

    /* Calculate total height */
    int total_h = DPI(4); /* top padding */
    for (int i = 0; i < menu->item_count; i++) {
        total_h += (menu->items[i].id == MENU_ID_SEP) ? sep_h : item_h;
    }
    total_h += DPI(4); /* bottom padding */

    /* Shadow */
    fill_rounded_rect(hdc, dropdown_x + DPI(2), dropdown_y + DPI(2),
                      dropdown_w, total_h, DPI(8), RGB(0, 0, 0));
    /* Background */
    fill_rounded_rect(hdc, dropdown_x, dropdown_y, dropdown_w, total_h, DPI(8),
                      g_theme.is_dark ? RGB(34, 34, 42) : RGB(248, 248, 248));
    /* Border */
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

        /* Hover highlight */
        if (g_editor.menu_hover_item == i) {
            fill_rounded_rect(hdc, dropdown_x + DPI(4), cy, dropdown_w - DPI(8), item_h,
                              DPI(4), CLR_ACCENT);
            SetTextColor(hdc, g_theme.is_dark ? RGB(255, 255, 255) : RGB(255, 255, 255));
        }

        /* Label */
        COLORREF label_clr = (g_editor.menu_hover_item == i)
            ? (g_theme.is_dark ? RGB(255, 255, 255) : RGB(255, 255, 255))
            : CLR_TEXT;
        draw_text(hdc, dropdown_x + pad_x, cy + (item_h - DPI(12)) / 2,
                  item->label, (int)wcslen(item->label), label_clr);

        /* Shortcut hint (right-aligned) */
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

static void render(HDC hdc) {
    arena_reset(&g_frame_arena);

    /* Deferred stats recount — at most once per frame */
    Document *stats_doc = current_doc();
    if (stats_doc) update_stats_now(stats_doc);

    int scroll_only = g_editor.scroll_only_repaint;
    g_editor.scroll_only_repaint = 0;

    if (scroll_only && g_editor.menu_open < 0 && !g_editor.show_stats_screen) {
        /* Scroll-only fast path: skip chrome, just redraw editor + statusbar.
         * The back buffer retains titlebar/menubar/tabbar from the last full render. */
        render_editor(hdc);
        render_statusbar(hdc);
        render_searchbar(hdc);
    } else {
        /* Full repaint */
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

/* ═══════════════════════════════════════════════════════════════
 * WINDOW PROCEDURE
 * ═══════════════════════════════════════════════════════════════ */

static int gutter_width(Document *doc) {
    if (!doc) return DPI(24);
    return (doc->mode == MODE_CODE)
        ? (LINE_NUM_CHARS * g_editor.char_width + DPI(GUTTER_PAD) * 2)
        : DPI(24);
}

/* Convert mouse position to text position */
static bpos mouse_to_pos(int mx, int my) {
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
static int scrollbar_thumb_geometry(int *out_thumb_y, int *out_thumb_h, int *out_edit_y, int *out_edit_h) {
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
static bpos word_start(GapBuffer *gb, bpos pos) {
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

static bpos word_end(GapBuffer *gb, bpos pos) {
    bpos len = gb_length(gb);
    while (pos < len) {
        wchar_t c = gb_char_at(gb, pos);
        if (!iswalnum(c) && c != L'_') break;
        pos++;
    }
    return pos;
}

/* Find the matching bracket for the character at pos. Returns -1 if not found. */
/* Compute whether position 'up_to' is inside a block comment.
 * Scans from document start, respecting strings and line comments. */
static int compute_block_comment_state(GapBuffer *gb, bpos up_to) {
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

static int advance_block_comment_state_for_line(GapBuffer *gb, bpos ls, bpos le, int in_bc) {
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

static bpos find_matching_bracket(GapBuffer *gb, bpos pos) {
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

static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {

    case WM_CREATE: {
        /* Create fonts */
        g_editor.font_size = FONT_SIZE_DEFAULT;
        g_editor.menu_open = -1;
        g_editor.menu_hover_item = -1;

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
            /* Check window control buttons */
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

            /* Drag to move */
            g_editor.titlebar_dragging = 1;
            g_editor.drag_start.x = mx;
            g_editor.drag_start.y = my;
            SetCapture(hwnd);
            return 0;
        }

        /* Menu bar area */
        if (my >= DPI(TITLEBAR_H) && my < DPI(TITLEBAR_H + MENUBAR_H)) {
            int mx2 = DPI(8);
            for (int i = 0; i < MENU_COUNT; i++) {
                int mw = g_editor.menu_bar_widths[i];
                if (mw == 0) mw = DPI(60); /* fallback before first render */
                if (mx >= mx2 && mx < mx2 + mw) {
                    /* Toggle this menu open/closed */
                    if (g_editor.menu_open == i) {
                        g_editor.menu_open = -1;
                    } else {
                        g_editor.menu_open = i;
                        g_editor.menu_hover_item = -1;
                    }
                    InvalidateRect(hwnd, NULL, FALSE);
                    return 0;
                }
                mx2 += mw;
            }
            /* Clicked on menu bar but not on a label — close any open menu */
            g_editor.menu_open = -1;
            InvalidateRect(hwnd, NULL, FALSE);
            return 0;
        }

        /* Menu dropdown click — MUST be checked before tab bar since
         * the dropdown overlaps the tab bar's y-range */
        if (g_editor.menu_open >= 0) {
            const MenuDef *menu = &g_menus[g_editor.menu_open];
            int item_h = DPI(26);
            int sep_h = DPI(9);
            int dropdown_w = DPI(260);
            int bar_x2 = DPI(8);
            for (int i = 0; i < g_editor.menu_open; i++)
                bar_x2 += g_editor.menu_bar_widths[i];
            int dropdown_x = bar_x2;
            int dropdown_y = DPI(TITLEBAR_H + MENUBAR_H);

            /* Calculate total dropdown height */
            int total_dd_h = DPI(4);
            for (int i = 0; i < menu->item_count; i++)
                total_dd_h += (menu->items[i].id == MENU_ID_SEP) ? sep_h : item_h;
            total_dd_h += DPI(4);

            if (mx >= dropdown_x && mx < dropdown_x + dropdown_w &&
                my >= dropdown_y && my < dropdown_y + total_dd_h) {
                int cy = dropdown_y + DPI(4);
                for (int i = 0; i < menu->item_count; i++) {
                    int h = (menu->items[i].id == MENU_ID_SEP) ? sep_h : item_h;
                    if (menu->items[i].id != MENU_ID_SEP &&
                        my >= cy && my < cy + h) {
                        menu_execute(menu->items[i].id);
                        return 0;
                    }
                    cy += h;
                }
                /* Clicked inside dropdown but on nothing (e.g. separator) */
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
                swprintf(label, 128, L"%s%s", g_editor.tabs[i]->title, g_editor.tabs[i]->modified ? L" \x2022" : L"");
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

        /* Menu bar hover: switch menus when one is open */
        if (g_editor.menu_open >= 0 && my >= DPI(TITLEBAR_H) && my < DPI(TITLEBAR_H + MENUBAR_H)) {
            int mx2 = DPI(8);
            for (int i = 0; i < MENU_COUNT; i++) {
                int mw = g_editor.menu_bar_widths[i];
                if (mw == 0) mw = DPI(60);
                if (mx >= mx2 && mx < mx2 + mw) {
                    if (g_editor.menu_open != i) {
                        g_editor.menu_open = i;
                        g_editor.menu_hover_item = -1;
                        InvalidateRect(hwnd, NULL, FALSE);
                    }
                    break;
                }
                mx2 += mw;
            }
        }

        /* Menu dropdown hover tracking */
        if (g_editor.menu_open >= 0) {
            const MenuDef *menu = &g_menus[g_editor.menu_open];
            int item_h = DPI(26);
            int sep_h = DPI(9);
            int dropdown_w = DPI(260);
            int bar_x2 = DPI(8);
            for (int i = 0; i < g_editor.menu_open; i++)
                bar_x2 += g_editor.menu_bar_widths[i];
            int dropdown_x = bar_x2;
            int dropdown_y = DPI(TITLEBAR_H + MENUBAR_H);

            int old_hover = g_editor.menu_hover_item;
            g_editor.menu_hover_item = -1;
            if (mx >= dropdown_x && mx < dropdown_x + dropdown_w) {
                int cy = dropdown_y + DPI(4);
                for (int i = 0; i < menu->item_count; i++) {
                    int h = (menu->items[i].id == MENU_ID_SEP) ? sep_h : item_h;
                    if (menu->items[i].id != MENU_ID_SEP &&
                        my >= cy && my < cy + h) {
                        g_editor.menu_hover_item = i;
                        break;
                    }
                    cy += h;
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
                        lc_rebuild(&doc->lc, &doc->gb);
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

/* ═══════════════════════════════════════════════════════════════
 * APPLICATION ICON — Generated programmatically
 * ═══════════════════════════════════════════════════════════════ */

static HICON create_app_icon(void) {
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
