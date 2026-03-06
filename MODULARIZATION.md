# Modularization Plan for prose_code.c

## Overview

Split the monolithic `prose_code.c` (6,327 lines) into a multi-file project. The app is a pure Win32/GDI text editor compiled exclusively with MinGW (not MSVC). After modularization, add a GitHub Actions CI workflow that cross-compiles on Ubuntu and produces a Windows `.exe` artifact.

## Compilation

**Toolchain:** MinGW-w64 (x86_64-w64-mingw32-gcc)

**Key flags:**
- `-municode` — wide-char entry point (`wWinMain`)
- `-mwindows` — GUI subsystem (no console window)
- `-DUNICODE -D_UNICODE -DCOBJMACROS` — Windows wide-char API + COM macros

**Libraries:** `-lgdi32 -lcomdlg32 -lcomctl32 -lshell32 -lole32 -lshlwapi -ldwmapi -luxtheme`

---

## Architecture: Shared Header (`prose_code.h`)

Create a single shared header included by all `.c` files. It must contain:

### System includes
```c
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
```

### The `bpos` typedef and format specifier
```c
typedef ptrdiff_t bpos;
#ifdef _MSC_VER
  #define BPOS_FMT L"Id"
#else
  #define BPOS_FMT L"td"
#endif
```

### All constants and macros
From lines 177-237 of the original. Includes `DPI()`, `CLR_*` macros, layout constants (`TITLEBAR_H`, `TABBAR_H`, etc.), limits (`MAX_TABS`, `GAP_INIT`, etc.), and timer IDs.

### All typedefs and struct definitions
In this order (dependencies matter):
1. `Theme` (lines 74-85)
2. `Arena` (lines 244-248)
3. `GapBuffer` (lines 277-283)
4. `LineCache` struct (find it around line 419 area — has `bpos *lines; bpos count; bpos capacity;`)
5. `WrapCache` struct (around line 586 — has `entries` array with `.pos` and `.line` fields)
6. `UndoType` enum, `UndoEntry`, `UndoStack` (lines 702-723)
7. `EditorMode` enum (line 777)
8. `Document` (lines 780-813)
9. `SearchState` (lines 816-825)
10. `FocusMode` (lines 828-831)
11. `MenuItem`, `MenuDef` (lines 861-871)
12. `EditorState` (lines 919-982)
13. `SynToken` enum (lines 1316-1335)
14. All COM vtable types for spell checker (lines 1074-1146): `CORRECTIVE_ACTION`, `ISpellingError`, `IEnumSpellingError`, `ISpellChecker`, `ISpellCheckerFactory`
15. `SpellCacheNode` (lines 1153-1157) and spell cache constants
16. `KwNode` (line 1395)

### Extern global declarations
```c
extern Arena g_frame_arena;        /* defined in buffer.c */
extern Theme g_theme;              /* defined in theme.c */
extern int   g_theme_index;        /* defined in theme.c */
extern float g_dpi_scale;          /* defined in theme.c */
extern const Theme THEME_DARK;     /* defined in theme.c */
extern const Theme THEME_LIGHT;    /* defined in theme.c */
extern EditorState g_editor;       /* defined in main.c */
extern const MenuDef g_menus[];    /* defined in menu.c */
extern ISpellChecker *g_spell_checker;  /* defined in spell.c */
extern int g_spell_loaded;              /* defined in spell.c */
```

### All function prototypes
Every function that was `static` in the original and is called from another module must be declared `extern` (or just non-static) here. Remove `static` from all function definitions in the `.c` files. The complete list is in the "Module Breakdown" section below.

```c
#endif /* PROSE_CODE_H */
```

---

## Module Breakdown

### `buffer.c` — Arena, GapBuffer, LineCache, WrapCache, UndoStack
**Source lines:** 252-833 (DATA STRUCTURES section)
**Defines global:** `Arena g_frame_arena;`
**Functions:**
- `arena_init`, `arena_alloc`, `arena_reset`
- `safe_wcscpy`
- `gb_init`, `gb_free`, `gb_length`, `gb_char_at`, `gb_grow`, `gb_move_gap`, `gb_insert`, `gb_delete`, `gb_copy_range`, `gb_extract`, `gb_extract_alloc`
- `lc_init`, `lc_free`, `lc_rebuild`, `lc_line_of`, `lc_line_start`, `lc_line_end`, `lc_notify_insert`, `lc_notify_delete`
- `wc_init`, `wc_free`, `wc_push`, `wc_rebuild`, `wc_visual_line_of`, `wc_visual_line_end`, `wc_col_in_vline`
- `undo_init`, `undo_push`, `undo_clear`, `undo_free`

### `theme.c` — Theme definitions and switching
**Source lines:** 87-175 + 987-1000
**Defines globals:** `Theme g_theme;`, `int g_theme_index;`, `float g_dpi_scale;`, `const Theme THEME_DARK;`, `const Theme THEME_LIGHT;`
**Functions:**
- `apply_theme`

### `spell.c` — Windows ISpellChecker COM integration
**Source lines:** 1057-1310 (SPELL CHECKER section)
**Defines globals:** `ISpellChecker *g_spell_checker;`, `int g_spell_loaded;`
**CRITICAL:** This file needs the GUID definitions. Since `INITGUID` is NOT in the shared header, define GUIDs as `static const GUID` directly:
```c
static const GUID CLSID_SpellCheckerFactory =
    {0x7AB36653, 0x1796, 0x484B, {0xBD, 0xFA, 0xE7, 0x4F, 0x1D, 0xB7, 0xC1, 0xDC}};
static const GUID IID_ISpellCheckerFactory =
    {0x8E018A9D, 0x2415, 0x4677, {0xBF, 0x08, 0x79, 0x4E, 0xA6, 0x1F, 0x94, 0xBB}};
```
**Internal statics (keep file-local):** `g_spell_cache[]`, `g_spell_cache_count`, `spell_hash()`, `spell_cache_lookup()`, `spell_cache_insert()`
**Functions (extern):**
- `spell_init`, `spell_check`, `spell_cache_free`

### `syntax.c` — Tokenizers and keyword table
**Source lines:** 1312-1431 (SYNTAX HIGHLIGHTING section)
**Internal statics:** `c_keywords[]`, `g_kw_table[]`, `g_kw_pool[]`, `kw_hash()`
**Functions (extern):**
- `token_color`, `kw_table_init`, `is_c_keyword`
- `tokenize_line_code` (lines 3058-3210 — NOTE: this is in the RENDERING section of the original, but it belongs in syntax.c)
- `tokenize_line_prose` (lines 3212-3365 — same note)

**IMPORTANT:** The original file has DUPLICATE copies of `tokenize_line_code` and `tokenize_line_prose` — once as forward declarations used in the syntax section, and the actual implementations appear in the rendering section (lines 3058-3365). Only include them ONCE in syntax.c.

### `document.c` — Document lifecycle and coordinate math
**Source lines:** 1433-1628 (DOCUMENT MANAGEMENT section)
**Functions:**
- `doc_create`, `doc_free`, `current_doc`
- `recalc_lines`, `recalc_wrap_now`
- `update_stats`, `update_stats_now`, `snapshot_session_baseline`
- `pos_to_line`, `pos_to_col`, `line_col_to_pos`
- `pos_to_visual_col`, `visual_col_to_pos`
- `col_to_pixel_x`, `pixel_x_to_col`
- `has_selection`, `selection_start`, `selection_end`
- `invalidate_editor_region`, `start_scroll_animation`
- `gutter_width` (line 4324-4329 — in the WINDOW PROCEDURE section of original, but logically belongs here)

### `editor.c` — Text editing operations
**Source lines:** 1629-1939 (EDITING OPERATIONS section)
**Functions:**
- `editor_insert_text`, `editor_insert_char`
- `editor_delete_selection`, `editor_backspace`, `editor_delete_forward`
- `editor_move_cursor`, `editor_undo`, `editor_redo`
- `editor_select_all`, `editor_copy`, `editor_cut`, `editor_paste`
- `editor_ensure_cursor_visible`
- `toggle_focus_mode` (lines 2753-2755)
- `toggle_mode` (lines 2757-2762)

### `search.c` — Find and replace
**Source lines:** 1940-2104 (SEARCH & REPLACE section)
**Functions:**
- `search_update_matches`, `toggle_search`
- `search_next`, `search_prev`
- `do_replace`, `do_replace_all`

### `menu.c` — Menu item arrays and dispatch
**Source lines:** 834-916 (menu item arrays) + 4154-4193 (MENU ACTION DISPATCH)
**Defines global:** `const MenuDef g_menus[MENU_COUNT];`
**Internal statics:** `g_file_items[]`, `g_edit_items[]`, `g_search_items[]`, `g_view_items[]`
**Functions:**
- `menu_execute` (lines 4158-4193)

### `file_io.c` — File loading, saving, autosave, crash recovery, tab management
**Source lines:** 2105-2762 (FILE I/O + ROBUST SAVE SYSTEM sections)
**Internal statics:** `path_hash()`, `json_escape_path()`
**Functions:**
- `load_file`, `doc_to_utf8`, `write_file_atomic`, `save_file`
- `autosave_ensure_dir`, `autosave_path_for_doc`, `autosave_write`
- `autosave_tick`, `autosave_delete_for_doc`, `autosave_cleanup_all`, `autosave_cleanup_tmp`
- `autosave_recover`
- `prompt_save_doc`
- `open_file_dialog`, `save_file_dialog`, `save_file_dialog_for_doc`, `save_current_file`
- `new_tab`, `close_tab`

**NOTE:** `toggle_focus_mode` and `toggle_mode` appear at the end of this section (lines 2753-2762) but belong in `editor.c`, not here. Do NOT duplicate them.

### `render.c` — All GDI rendering
**Source lines:** 2764-4153 (RENDERING section) + 3958-4153 (STATS OVERLAY) + 4195-4318 (MENU BAR RENDERING)
**Functions:**
- `fill_rect`, `fill_rounded_rect`, `draw_text`
- `render_titlebar`, `render_tabbar`, `render_statusbar`, `render_searchbar`
- `render_editor` (the big one, ~590 lines)
- `render_stats_screen`
- `render_menubar`, `render_menu_dropdown`
- `render` (the top-level compositing function, lines 4290-4318)

**CRITICAL:** Do NOT include `tokenize_line_code` or `tokenize_line_prose` in render.c — they're called from `render_editor` but defined in `syntax.c`. The header prototypes handle linkage.

### `wndproc.c` — Window procedure and input handling
**Source lines:** 4320-6125 (WINDOW PROCEDURE section)
**Functions:**
- `mouse_to_pos` (lines 4332-4368)
- `scrollbar_thumb_geometry` (lines 4371-4395)
- `word_start` (lines 4398-4412)
- `word_end` (lines 4415-4423)
- `compute_block_comment_state` (lines 4428-4455)
- `advance_block_comment_state_for_line` (lines 4457-4493)
- `find_matching_bracket` (lines 4495-4549)
- `WndProc` (lines 4551-6125 — ~1,575 lines, the largest single function)

**NOTE:** `gutter_width` appears at lines 4324-4329 but belongs in `document.c`. Do NOT include it here.

### `main.c` — Entry point and app icon
**Source lines:** 6127-6327 (APPLICATION ICON + ENTRY POINT sections)
**Defines global:** `EditorState g_editor;`
**Functions:**
- `create_app_icon` (lines 6131-6182)
- `wWinMain` (lines 6188-6327)

---

## Makefile

```makefile
CC      ?= x86_64-w64-mingw32-gcc
WINDRES ?= x86_64-w64-mingw32-windres
CFLAGS  = -O2 -Wall -Wextra -Wno-unused-parameter -municode \
          -DUNICODE -D_UNICODE -DCOBJMACROS
LDFLAGS = -mwindows -municode
LIBS    = -lgdi32 -lcomdlg32 -lcomctl32 -lshell32 -lole32 \
          -lshlwapi -ldwmapi -luxtheme

SRCS    = main.c buffer.c theme.c spell.c syntax.c document.c \
          editor.c search.c menu.c file_io.c render.c wndproc.c
OBJS    = $(SRCS:.c=.o)
TARGET  = prose_code.exe

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(LDFLAGS) -o $@ $^ $(LIBS)

%.o: %.c prose_code.h
	$(CC) $(CFLAGS) -c -o $@ $<

clean:
	rm -f $(OBJS) $(TARGET)

.PHONY: all clean
```

---

## GitHub Actions Workflow (`.github/workflows/build.yml`)

```yaml
name: Build
on:
  push:
    branches: [ main, master, 'claude/**' ]
  pull_request:
    branches: [ main, master ]

jobs:
  build:
    runs-on: ubuntu-latest
    steps:
      - uses: actions/checkout@v4

      - name: Install MinGW-w64
        run: sudo apt-get update && sudo apt-get install -y gcc-mingw-w64-x86-64

      - name: Build
        run: make CC=x86_64-w64-mingw32-gcc

      - name: Upload artifact
        uses: actions/upload-artifact@v4
        with:
          name: prose_code
          path: prose_code.exe
```

---

## Key Pitfalls & Gotchas

### 1. INITGUID Must NOT Be in the Shared Header
`#define INITGUID` before `<objbase.h>` causes GUID symbols to be **defined** (not just declared) in every translation unit that includes the header. This causes "multiple definition" linker errors. Instead, define the two GUIDs (`CLSID_SpellCheckerFactory`, `IID_ISpellCheckerFactory`) as `static const GUID` directly in `spell.c`.

### 2. Remove `static` from All Cross-Module Functions
Every function listed above must have its `static` keyword removed in the `.c` file, and have a prototype in `prose_code.h`. Functions that are truly file-internal (like `spell_hash`, `path_hash`, `json_escape_path`, `spell_cache_lookup`, `spell_cache_insert`) can remain `static`.

### 3. Duplicate Function Definitions
The original `prose_code.c` contains `tokenize_line_code` and `tokenize_line_prose` defined TWICE:
- Once in the SYNTAX HIGHLIGHTING section (used by the bracket matcher, etc.)
- Once in the RENDERING section (the actual implementations at lines 3058-3365)

They are the SAME code. Put them in `syntax.c` ONCE. Other modules call them via the header prototype.

### 4. Functions That Appear in Wrong Sections
Some functions appear in a section that doesn't match their logical module:
- `gutter_width` (line 4324) is in the WINDOW PROCEDURE section → put in `document.c`
- `toggle_focus_mode` (line 2753) and `toggle_mode` (line 2757) are at the end of FILE I/O → put in `editor.c`
- `tokenize_line_code`/`tokenize_line_prose` (lines 3058-3365) are in RENDERING → put in `syntax.c`

### 5. Global Ownership
Each global must be **defined** in exactly one `.c` file and **declared extern** in the header:

| Global | Defined in | Type |
|--------|-----------|------|
| `g_frame_arena` | `buffer.c` | `Arena` |
| `g_theme` | `theme.c` | `Theme` |
| `g_theme_index` | `theme.c` | `int` |
| `g_dpi_scale` | `theme.c` | `float` |
| `THEME_DARK` | `theme.c` | `const Theme` |
| `THEME_LIGHT` | `theme.c` | `const Theme` |
| `g_editor` | `main.c` | `EditorState` |
| `g_menus[]` | `menu.c` | `const MenuDef[MENU_COUNT]` |
| `g_spell_checker` | `spell.c` | `ISpellChecker *` |
| `g_spell_loaded` | `spell.c` | `int` |

### 6. COM Interface Types
The COM vtable structs (`ISpellingErrorVtbl`, `IEnumSpellingErrorVtbl`, `ISpellCheckerVtbl`, `ISpellCheckerFactoryVtbl`) and their wrapper structs must be in the shared header because `spell_check()` is called from `render.c` (via `render_editor`), and the `ISpellChecker` pointer is a global.

### 7. Forward Declaration of `tokenize_line_code`
`render_editor` calls `tokenize_line_code` and `tokenize_line_prose`. `find_matching_bracket` (in `wndproc.c`) also calls `tokenize_line_code` and `compute_block_comment_state`. All prototypes must be in the header.

### 8. `#define MENU_COUNT 4`
This must be in the header since `EditorState` uses it (`menu_bar_widths[MENU_COUNT]`) and other files reference `MENU_COUNT`.

---

## Execution Checklist

1. [ ] Create `prose_code.h` with all types, macros, extern globals, and function prototypes
2. [ ] Create `buffer.c` (lines 252-833)
3. [ ] Create `theme.c` (lines 87-175, 987-1000)
4. [ ] Create `spell.c` (lines 1057-1310, with static GUID definitions)
5. [ ] Create `syntax.c` (lines 1312-1431 + lines 3058-3365)
6. [ ] Create `document.c` (lines 1433-1628 + gutter_width from line 4324)
7. [ ] Create `editor.c` (lines 1629-1939 + toggle_focus_mode/toggle_mode from lines 2753-2762)
8. [ ] Create `search.c` (lines 1940-2104)
9. [ ] Create `menu.c` (lines 834-916 + menu_execute from lines 4158-4193)
10. [ ] Create `file_io.c` (lines 2105-2752, excluding toggle_focus_mode/toggle_mode)
11. [ ] Create `render.c` (lines 2764-3057 + 3368-4153 + 4195-4318, excluding tokenizer functions)
12. [ ] Create `wndproc.c` (lines 4331-6125, excluding gutter_width)
13. [ ] Create `main.c` (lines 6127-6327)
14. [ ] Create `Makefile`
15. [ ] Create `.github/workflows/build.yml`
16. [ ] Run `make` and fix compilation errors
17. [ ] Commit and push to `claude/check-modularization-branch-OoON2`
