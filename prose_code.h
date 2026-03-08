/*
 * prose_code.h — Shared header for the Prose_Code text editor
 * All modules include this file.
 */
#ifndef PROSE_CODE_H
#define PROSE_CODE_H

#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif
#define WIN32_LEAN_AND_MEAN
#define COBJMACROS
/* NOTE: Do NOT put #define INITGUID here — it causes duplicate GUID
 * symbol definitions across translation units. GUIDs are defined
 * as static const in spell.c only. */

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

/* ── Buffer position type ── */
typedef ptrdiff_t bpos;
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
    int is_dark;
} Theme;

/* ── DPI scaling ── */
#define DPI(x) ((int)((x) * g_dpi_scale + 0.5f))

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

/* ── Layout constants ── */
#define TITLEBAR_H       36
#define TABBAR_H         0
#define MENUBAR_H        0
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
#define TIMER_DRAG_SCROLL 4

/* ═══════════════════════════════════════════════════════════════
 * DATA STRUCTURES
 * ═══════════════════════════════════════════════════════════════ */

typedef struct {
    char *base;
    size_t used;
    size_t capacity;
} Arena;

typedef struct {
    wchar_t *buf;
    bpos total;
    bpos gap_start;
    bpos gap_end;
    int  mutation;
} GapBuffer;

typedef struct {
    bpos *offsets;
    bpos count;
    bpos capacity;
    int  dirty;
} LineCache;

typedef struct {
    bpos pos;
    bpos line;
} WrapEntry;

typedef struct {
    WrapEntry *entries;
    bpos count;
    bpos capacity;
    int  wrap_col;
} WrapCache;

typedef enum { UNDO_INSERT, UNDO_DELETE } UndoType;

typedef struct {
    UndoType type;
    bpos pos;
    wchar_t *text;
    bpos len;
    bpos cursor_before;
    bpos cursor_after;
    int  group;
} UndoEntry;

#define UNDO_INIT_CAP 256

typedef struct {
    UndoEntry *entries;
    int count;
    int current;
    int capacity;
    int next_group;
    int save_point;
} UndoStack;

typedef enum { MODE_PROSE, MODE_CODE } EditorMode;

typedef struct {
    GapBuffer gb;
    LineCache lc;
    WrapCache wc;
    UndoStack undo;
    bpos cursor;
    bpos sel_anchor;
    int scroll_y;
    int target_scroll_y;
    int scroll_x;
    int target_scroll_x;
    bpos desired_col;
    EditorMode mode;
    wchar_t filepath[MAX_PATH];
    wchar_t title[64];
    int modified;
    bpos word_count;
    bpos char_count;
    bpos line_count;
    int stats_dirty;
    int wrap_dirty;
    bpos session_start_words;
    bpos session_start_chars;
    bpos session_start_lines;
    unsigned int autosave_id;
    int autosave_mutation_snapshot;
    DWORD autosave_last_time;
    int bc_cached_mutation;
    bpos bc_cached_line;
    int bc_cached_state;
} Document;

typedef struct {
    wchar_t query[256];
    int active;
    int current_match;
    int match_count;
    bpos *match_positions;
    int replace_active;
    int replace_focused;
    wchar_t replace_text[256];
} SearchState;

typedef struct {
    int active;
    float dim_alpha;
} FocusMode;

/* ── Menu definitions ── */

#define MENU_ID_NEW         1
#define MENU_ID_OPEN        2
#define MENU_ID_SAVE        3
#define MENU_ID_CLOSE_TAB   4
#define MENU_ID_SEP         0
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
#define MENU_ID_SPELLCHECK  37

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

#define MENU_COUNT 4

/* ── Editor state ── */

typedef struct {
    HWND hwnd;
    HDC hdc_back;
    HBITMAP bmp_back;
    HBITMAP bmp_back_old;
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
    int cursor_phase;
    DWORD cursor_last_active;
    int mouse_captured;
    int titlebar_dragging;
    int titlebar_hover_btn;
    POINT drag_start;

    int show_minimap;

    int scrollbar_dragging;
    int scrollbar_drag_offset;
    int scrollbar_hover;

    int show_stats_screen;
    DWORD session_start_time;
    HFONT font_stats_hero;

    wchar_t autosave_dir[MAX_PATH];
    unsigned int next_autosave_id;

    int menu_open;          /* -1 = closed, 0 = combined dropdown open */
    int menu_hover_item;    /* flat index across all menus */

    int dropdown_hover;     /* 1 if hovering the dropdown trigger button */

    int spellcheck_enabled;

    int scroll_only_repaint;
} EditorState;

/* ── Syntax highlighting tokens ── */

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
    TOK_MD_HEADING,
    TOK_MD_BOLD,
    TOK_MD_ITALIC,
    TOK_MD_CODE,
    TOK_MD_LINK,
    TOK_MD_BLOCKQUOTE,
    TOK_MD_LIST,
    TOK_MISSPELLED,
} SynToken;

/* ── COM interface types for spell checker ── */

typedef enum {
    CORRECTIVE_ACTION_NONE          = 0,
    CORRECTIVE_ACTION_GET_SUGGESTIONS = 1,
    CORRECTIVE_ACTION_REPLACE       = 2,
    CORRECTIVE_ACTION_DELETE        = 3
} CORRECTIVE_ACTION;

typedef struct ISpellingErrorVtbl {
    HRESULT (STDMETHODCALLTYPE *QueryInterface)(void *This, REFIID riid, void **ppv);
    ULONG   (STDMETHODCALLTYPE *AddRef)(void *This);
    ULONG   (STDMETHODCALLTYPE *Release)(void *This);
    HRESULT (STDMETHODCALLTYPE *get_StartIndex)(void *This, ULONG *value);
    HRESULT (STDMETHODCALLTYPE *get_Length)(void *This, ULONG *value);
    HRESULT (STDMETHODCALLTYPE *get_CorrectiveAction)(void *This, CORRECTIVE_ACTION *value);
    HRESULT (STDMETHODCALLTYPE *get_Replacement)(void *This, LPWSTR *value);
} ISpellingErrorVtbl;

typedef struct { ISpellingErrorVtbl *lpVtbl; } ISpellingError;

typedef struct IEnumSpellingErrorVtbl {
    HRESULT (STDMETHODCALLTYPE *QueryInterface)(void *This, REFIID riid, void **ppv);
    ULONG   (STDMETHODCALLTYPE *AddRef)(void *This);
    ULONG   (STDMETHODCALLTYPE *Release)(void *This);
    HRESULT (STDMETHODCALLTYPE *Next)(void *This, ISpellingError **value);
} IEnumSpellingErrorVtbl;

typedef struct { IEnumSpellingErrorVtbl *lpVtbl; } IEnumSpellingError;

typedef struct ISpellCheckerVtbl {
    HRESULT (STDMETHODCALLTYPE *QueryInterface)(void *This, REFIID riid, void **ppv);
    ULONG   (STDMETHODCALLTYPE *AddRef)(void *This);
    ULONG   (STDMETHODCALLTYPE *Release)(void *This);
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

typedef struct ISpellCheckerFactoryVtbl {
    HRESULT (STDMETHODCALLTYPE *QueryInterface)(void *This, REFIID riid, void **ppv);
    ULONG   (STDMETHODCALLTYPE *AddRef)(void *This);
    ULONG   (STDMETHODCALLTYPE *Release)(void *This);
    HRESULT (STDMETHODCALLTYPE *get_SupportedLanguages)(void *This, void **value);
    HRESULT (STDMETHODCALLTYPE *IsSupported)(void *This, LPCWSTR lang, BOOL *value);
    HRESULT (STDMETHODCALLTYPE *CreateSpellChecker)(void *This, LPCWSTR lang, ISpellChecker **value);
} ISpellCheckerFactoryVtbl;

typedef struct { ISpellCheckerFactoryVtbl *lpVtbl; } ISpellCheckerFactory;

/* Spell cache constants */
#define SPELL_CACHE_BUCKETS 4096
#define SPELL_CACHE_MASK    (SPELL_CACHE_BUCKETS - 1)
#define SPELL_CACHE_MAX 8192

typedef struct SpellCacheNode {
    wchar_t *word;
    int      correct;
    struct SpellCacheNode *next;
} SpellCacheNode;

/* Keyword hash set */
#define KW_HASH_BUCKETS 256
#define KW_HASH_MASK    (KW_HASH_BUCKETS - 1)
typedef struct KwNode { const wchar_t *word; int len; struct KwNode *next; } KwNode;

/* ═══════════════════════════════════════════════════════════════
 * EXTERN GLOBALS
 * ═══════════════════════════════════════════════════════════════ */

extern Arena g_frame_arena;
extern Theme g_theme;
extern int   g_theme_index;
extern float g_dpi_scale;
extern const Theme THEME_DARK;
extern const Theme THEME_LIGHT;
extern EditorState g_editor;
extern const MenuDef g_menus[];
extern ISpellChecker *g_spell_checker;
extern int g_spell_loaded;

/* ═══════════════════════════════════════════════════════════════
 * FUNCTION PROTOTYPES
 * ═══════════════════════════════════════════════════════════════ */

/* buffer.c */
void arena_init(Arena *a, size_t cap);
void *arena_alloc(Arena *a, size_t size);
void arena_reset(Arena *a);
void safe_wcscpy(wchar_t *dst, int dst_count, const wchar_t *src);
void gb_init(GapBuffer *gb, bpos initial_cap);
void gb_free(GapBuffer *gb);
bpos gb_length(GapBuffer *gb);
wchar_t gb_char_at(GapBuffer *gb, bpos pos);
void gb_grow(GapBuffer *gb, bpos needed);
void gb_move_gap(GapBuffer *gb, bpos pos);
void gb_insert(GapBuffer *gb, bpos pos, const wchar_t *text, bpos len);
void gb_delete(GapBuffer *gb, bpos pos, bpos len);
void gb_copy_range(GapBuffer *gb, bpos start, bpos len, wchar_t *dst);
wchar_t *gb_extract(GapBuffer *gb, bpos start, bpos len, Arena *a);
wchar_t *gb_extract_alloc(GapBuffer *gb, bpos start, bpos len);
void lc_init(LineCache *lc);
void lc_free(LineCache *lc);
void lc_rebuild(LineCache *lc, GapBuffer *gb);
bpos lc_line_of(LineCache *lc, bpos pos);
bpos lc_line_start(LineCache *lc, bpos line);
bpos lc_line_end(LineCache *lc, GapBuffer *gb, bpos line);
int  lc_notify_insert(LineCache *lc, bpos pos, const wchar_t *text, bpos len);
int  lc_notify_delete(LineCache *lc, bpos pos, const wchar_t *deleted_text, bpos len);
void wc_init(WrapCache *wc);
void wc_free(WrapCache *wc);
void wc_push(WrapCache *wc, bpos pos, bpos line);
void wc_rebuild(WrapCache *wc, GapBuffer *gb, LineCache *lc, int wrap_col);
bpos wc_visual_line_of(WrapCache *wc, bpos pos);
bpos wc_visual_line_end(WrapCache *wc, GapBuffer *gb, LineCache *lc, bpos vline);
bpos wc_col_in_vline(WrapCache *wc, bpos pos, bpos vline);
void undo_init(UndoStack *us);
void undo_push(UndoStack *us, UndoType type, bpos pos, const wchar_t *text, bpos len, bpos cursor_before, bpos cursor_after, int group);
void undo_clear(UndoStack *us);
void undo_free(UndoStack *us);

/* theme.c */
void apply_theme(int index);

/* spell.c */
void spell_init(void);
int  spell_check(const wchar_t *word, int len);
void spell_cache_free(void);

/* syntax.c */
COLORREF token_color(SynToken t);
void kw_table_init(void);
int  is_c_keyword(const wchar_t *word, int len);
int  tokenize_line_code(const wchar_t *chars, int line_len, SynToken *out, int in_block_comment);
void tokenize_line_prose(const wchar_t *chars, int line_len, SynToken *out);

/* document.c */
Document *doc_create(void);
void doc_free(Document *doc);
Document *current_doc(void);
void recalc_lines(Document *doc);
void recalc_wrap_now(Document *doc);
void update_stats(Document *doc);
void update_stats_now(Document *doc);
void snapshot_session_baseline(Document *doc);
bpos pos_to_line(Document *doc, bpos pos);
bpos pos_to_col(Document *doc, bpos pos);
bpos line_col_to_pos(Document *doc, bpos line, bpos col);
bpos pos_to_visual_col(Document *doc, bpos pos);
bpos visual_col_to_pos(Document *doc, bpos line, int target_vcol);
int  col_to_pixel_x(GapBuffer *gb, bpos line_start_pos, bpos col, int cw);
bpos pixel_x_to_col(GapBuffer *gb, bpos line_start_pos, bpos line_len, int px, int cw);
int  has_selection(Document *doc);
bpos selection_start(Document *doc);
bpos selection_end(Document *doc);
void invalidate_editor_region(HWND hwnd);
void start_scroll_animation(void);
int  gutter_width(Document *doc);

/* editor.c */
void editor_insert_text(const wchar_t *text, bpos len);
void editor_insert_char(wchar_t c);
void editor_delete_selection(void);
void editor_backspace(void);
void editor_delete_forward(void);
void editor_move_cursor(bpos pos, int extend_selection);
void editor_undo(void);
void editor_redo(void);
void editor_select_all(void);
void editor_copy(void);
void editor_cut(void);
void editor_paste(void);
void editor_ensure_cursor_visible(void);
void toggle_focus_mode(void);
void toggle_mode(void);

/* search.c */
void search_update_matches(void);
void toggle_search(void);
void search_next(void);
void search_prev(void);
void do_replace(void);
void do_replace_all(void);

/* menu.c */
void menu_execute(int id);

/* file_io.c */
void load_file(Document *doc, const wchar_t *path);
char *doc_to_utf8(Document *doc, int *out_len);
int  write_file_atomic(const wchar_t *final_path, const char *data, int data_len);
void save_file(Document *doc, const wchar_t *path);
void autosave_ensure_dir(void);
void autosave_path_for_doc(Document *doc, wchar_t *out);
void autosave_write(Document *doc);
void autosave_tick(void);
void autosave_delete_for_doc(Document *doc);
void autosave_cleanup_all(void);
void autosave_cleanup_tmp(void);
void autosave_recover(void);
int  prompt_save_doc(int tab_idx);
void open_file_dialog(void);
void save_file_dialog(void);
int  save_file_dialog_for_doc(Document *doc);
void save_current_file(void);
void new_tab(void);
void close_tab(int idx);

/* render.c */
void fill_rect(HDC hdc, int x, int y, int w, int h, COLORREF c);
void fill_rounded_rect(HDC hdc, int x, int y, int w, int h, int r, COLORREF c);
void draw_text(HDC hdc, int x, int y, const wchar_t *text, int len, COLORREF color);
void render_titlebar(HDC hdc);
void render_tabbar(HDC hdc);
void render_statusbar(HDC hdc);
void render_searchbar(HDC hdc);
void render_editor(HDC hdc);
void render_stats_screen(HDC hdc);
void render_menubar(HDC hdc);
void render_menu_dropdown(HDC hdc);
void render(HDC hdc);

/* wndproc.c */
bpos mouse_to_pos(int mx, int my);
int  scrollbar_thumb_geometry(int *out_thumb_y, int *out_thumb_h, int *out_edit_y, int *out_edit_h);
bpos word_start(GapBuffer *gb, bpos pos);
bpos word_end(GapBuffer *gb, bpos pos);
int  compute_block_comment_state(GapBuffer *gb, bpos up_to);
int  advance_block_comment_state_for_line(GapBuffer *gb, bpos ls, bpos le, int in_bc);
bpos find_matching_bracket(GapBuffer *gb, bpos pos);
LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

/* main.c */
HICON create_app_icon(void);

#endif /* PROSE_CODE_H */
