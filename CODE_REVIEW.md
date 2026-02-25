# Code Review: prose_code.c

**Reviewed by:** 4 parallel Sonnet code review agents
**Date:** 2025-02-25
**File:** `prose_code.c` (6,061 lines)
**Summary:** Pure C text editor using Win32 API with gap buffer, syntax highlighting, spellcheck, and minimap

---

## Overview

Four independent review agents examined the codebase from different angles:
1. **Memory Safety** — buffer overflows, null derefs, gap buffer correctness
2. **Logic Bugs** — editing operations, undo/redo, cursor movement, edge cases
3. **Resource Leaks** — GDI/COM leaks, Win32 API misuse, handle management
4. **UI & Rendering** — drawing bugs, input handling, scroll behavior

**Total issues found: ~100** (with overlap across agents on shared critical issues)

---

## Critical Issues

### C1. Missing brace in minimap for-loop (Lines 3753–3758)
**Found by: All 4 agents** | Compile error / logic bug

```c
for (int skip = mm_prev_line + 1; skip < i; skip++)
    bpos sls = lc_line_start(&doc->lc, skip);       // only this is in the loop
    bpos sle = lc_line_end(&doc->lc, &doc->gb, skip);
    mm_in_block_comment = advance_block_comment_state_for_line(...);
}
```

The `for` loop body is missing its opening `{`. Only the first statement is inside the loop. The block-comment state for skipped lines in the minimap is computed incorrectly, producing wrong syntax highlighting colors. This may also fail to compile under C89/C90.

### C2. NULL dereference in `gb_grow` after failed `calloc` (Lines 313–316)
**Found by: 3 agents** | Crash on OOM

```c
wchar_t *new_buf = (wchar_t *)calloc(new_total, sizeof(wchar_t));
memcpy(new_buf, gb->buf, gb->gap_start * sizeof(wchar_t));  // NULL deref if calloc fails
```

`calloc` can return NULL. No check before `memcpy`. Then `free(gb->buf); gb->buf = new_buf;` sets `gb->buf = NULL`, corrupting every subsequent operation. Every insert path passes through `gb_grow`.

### C3. NULL dereference in `undo_push` (Lines 715–716)
**Found by: 2 agents** | Crash on OOM

```c
e->text = (wchar_t *)malloc((len + 1) * sizeof(wchar_t));
memcpy(e->text, text, len * sizeof(wchar_t));  // NULL deref if malloc fails
```

Called on every text-modifying keystroke.

### C4. NULL dereference in `spell_cache_insert` (Lines 1151–1153)
**Found by: 2 agents** | Crash on OOM

```c
SpellCacheNode *n = (SpellCacheNode *)malloc(sizeof(SpellCacheNode));
n->word = (wchar_t *)malloc((len + 1) * sizeof(wchar_t));
memcpy(n->word, lower, len * sizeof(wchar_t));
```

Neither `malloc` return is checked. Runs during every render pass in prose mode.

### C5. `gb_delete` can corrupt gap buffer with negative or oversized `len` (Lines 350–354)
**Found by: 2 agents** | Data corruption

```c
static void gb_delete(GapBuffer *gb, bpos pos, bpos len) {
    gb_move_gap(gb, pos);
    gb->gap_end += len;
    if (gb->gap_end > gb->total) gb->gap_end = gb->total;
}
```

No validation that `len >= 0`, `pos >= 0`, or `pos + len <= gb_length(gb)`. A negative `len` (from signed `bpos`) decreases `gap_end` into the gap or before `gap_start`.

---

## High Severity Issues

### H1. `search_update_matches`: Underflow when text shorter than query (Line 1914)
```c
for (bpos i = 0; i <= tlen - qlen; i++) {
```
When `tlen < qlen`, `tlen - qlen` wraps to a huge positive number, causing billions of iterations reading past the buffer end.

### H2. `search_update_matches`: `bpos` positions truncated to `int` (Lines 779, 1921)
Match positions are stored as `int *` but text positions are `bpos` (64-bit). Files >2GB produce wrong cursor positions. `realloc` failure also causes NULL deref.

### H3. Autosave recovery off-by-one stack overflow (Lines 2427–2434)
```c
char hdr_buf[1024];
memcpy(hdr_buf, raw + 4, hdr_len);  // hdr_len allowed to be 1024
hdr_buf[hdr_len] = 0;               // writes past end of array
```
`hdr_len` is allowed to be exactly 1024, but `hdr_buf[1024]` is one byte past the stack buffer.

### H4. `write_file_atomic`: Path buffer overflow (Lines 2164–2165)
```c
wchar_t tmp_path[MAX_PATH + 8];
swprintf(tmp_path, MAX_PATH + 8, L"%s.tmp~", final_path);
```
A path at `MAX_PATH` characters plus the `.tmp~` suffix overflows the buffer.

### H5. `Alt+Up` line move uses stale line cache (Lines 5228–5230)
```c
gb_delete(&doc->gb, ls, full_len);
bpos prev_ls = lc_line_start(&doc->lc, line - 1);  // lc is dirty after gb_delete
```
`gb_delete` is called directly without updating the line cache. The subsequent `lc_line_start` reads stale data, placing the moved line at the wrong location.

### H6. `do_replace_all`: Undo entries in wrong order (Lines 1997–2011)
Replacements are back-to-front but undo entries are pushed in that same order. LIFO undo unwinds them front-to-back, using positions that were valid at different document states. Undo of Replace All produces corrupted text.

### H7. Multiple unchecked `realloc` calls (5 locations)
Lines 426, 436, 492, 571, 1920 — `realloc` result assigned directly back to the pointer. If `realloc` returns NULL, the original allocation is leaked and subsequent access crashes.

### H8. `lc_init` / `wc_init` NULL dereference (Lines 407, 558)
```c
lc->offsets = (bpos *)malloc(lc->capacity * sizeof(bpos));
lc->offsets[0] = 0;  // NULL deref if malloc fails
```
Called on every `doc_create`.

### H9. `lc_notify_insert`/`lc_notify_delete`: Off-by-one at line boundaries (Lines 478–483, 516–522)
When `pos` is exactly at the start of a line, that line's own offset should be shifted but is not. This causes line-start positions to be wrong, producing rendering glitches and incorrect cursor-to-line mapping.

### H10. `lc_notify_delete`: Single-newline delete uses wrong offset formula (Lines 524–533)
```c
lc->offsets[i] = lc->offsets[i + 1] - 1;
```
Subtracts 1 from the next entry's offset instead of shifting by `len`. Produces stale line positions.

### H11. `Ctrl+Backspace` at position 0 produces negative index (Lines 5477–5485)
`start--` can produce -1 when cursor is at position 0, leading to `gb_delete` with a negative position.

### H12. Back-buffer bitmap leaked on every `WM_SIZE` (Line 4490)
```c
SelectObject(g_editor.hdc_back, GetStockObject(NULL_PEN));  // wrong stock object for bitmap
DeleteObject(g_editor.bmp_back);  // GDI ignores delete of selected object
```
`NULL_PEN` is a pen, not a bitmap. The bitmap remains selected and `DeleteObject` is ignored. Leaked on every window resize.

### H13. `HICON` never destroyed (Lines 5980–5991)
The icon created by `create_app_icon()` is registered with the window class but never passed to `DestroyIcon`.

### H14. `render_searchbar`: Dangling pen handle (Lines 2861–2866)
Old pen not saved/restored before `DeleteObject(pen)`. The DC's selected pen becomes a dangling handle.

### H15. DPI initialized from primary monitor before window creation (Lines 5972–5977)
`GetDC(NULL)` reads the primary monitor DPI. On a multi-monitor system with different DPIs, all initial layout is wrong until `WM_DPICHANGED`.

### H16. `kw_table_init`: No bounds check on `g_kw_pool[256]` (Lines 1341, 1349–1365)
If keywords are added to the tables, `pool_idx` silently walks past `g_kw_pool[255]` into adjacent static memory.

### H17. `gb_move_gap` called with unchecked `pos` from `gb_insert` (Lines 342–345)
No bounds check on `pos`. If `pos > gb_length(gb)` (stale cursor after undo/redo), `memmove` produces out-of-bounds read/write.

### H18. `load_file`: `malloc` not checked before `MultiByteToWideChar` (Lines 2058–2059)
```c
wchar_t *wtext = (wchar_t *)malloc((wlen + 1) * sizeof(wchar_t));
MultiByteToWideChar(CP_UTF8, 0, start, (int)size, wtext, wlen);  // NULL deref
```

### H19. Arrow keys don't collapse selection properly (Lines 5190–5210)
Left/Right arrows with an active selection overshoot by one character instead of collapsing to selection start/end.

### H20. Tab close button misaligned (Lines 4839–4845)
Hit area for tab close button is calculated with a different width estimate than the rendered button position (uses `char_count * DPI(8)` vs `GetTextExtentPoint32W`).

### H21. Closing tabs orphans autosave files (Lines 2646–2658)
When a tab is closed, remaining tabs' indices shift but their autosave shadow file paths (which encode tab index) become stale/orphaned.

### H22. COM `IEnumSpellingError`/`ISpellingError` leaks (Lines 1188–1205)
Interface leaked if `SUCCEEDED(hr)` but `errors == NULL`, or if `Next()` returns a non-`S_OK` success code.

### H23. `CoInitializeEx` return not checked (Line 5954)
`CoUninitialize` called unconditionally at shutdown, even if `CoInitializeEx` failed.

### H24. Font zoom only recreates 3 of 7 fonts (Lines 5747–5777)
`WM_USER+1` handler deletes and recreates `font_main`, `font_bold`, `font_italic` but not `font_ui`, `font_ui_small`, `font_title`, `font_stats_hero`.

### H25. `doc_to_utf8` integer overflow (Lines 2133–2139)
`len + newlines + 1` can overflow, producing a tiny `malloc` followed by a massive out-of-bounds write.

---

## Medium Severity Issues

### M1. `Ctrl+Delete` at end of document (Lines 5460–5468)
`end++` goes past buffer end, causing `gb_delete` to corrupt `gap_end`.

### M2. `wc_rebuild` word-wrap off-by-one (Lines 619–622)
Column recount loop uses `j <= i` (inclusive), double-counting the overflow character. Wrap triggers one character too early on subsequent lines.

### M3. `Ctrl+Shift+D` duplicate line inserts newline before existing `\n` (Lines 5586–5592)
Creates an unwanted blank line between the original and duplicate.

### M4. `doc_to_utf8` empty document produces `malloc(0)` (Lines 2148–2153)
`malloc(0)` may return NULL, which is treated as OOM, silently preventing empty file saves.

### M5. `editor_undo`/`editor_redo` never clears `modified` flag (Lines 1736–1737)
Unconditionally sets `modified = 1`. Undoing all changes back to saved state still shows modified indicator.

### M6. `word_start` returns wrong result on non-word characters (Lines 4268–4276)
Cursor after a space: Ctrl+Backspace deletes only the space instead of the preceding word.

### M7. `find_matching_bracket` OOB on lines >2048 chars (Lines 4378–4400)
`cached_tokens` has 2048 elements but line length is not clamped before indexing.

### M8. Selection highlight doesn't cover trailing newline at wrapped line ends (Lines 3488–3499)

### M9. Cursor blink invalidation rect missing `scroll_x` offset (Lines 5651–5654)
In code mode with horizontal scroll, the blink erases the wrong region.

### M10. Search-bar blink rect uses non-DPI-scaled values (Lines 5625–5630)
At 150%+ DPI, the invalidation rectangle covers only a fraction of the search bar.

### M11. Scrollbar thumb not clamped (Lines 3686, 4260)
Renders outside the track area when `scroll_y` exceeds the expected maximum.

### M12. Selection drag into minimap passes wrong coordinates (Lines 4205–4241)

### M13. `render_titlebar` / `render_tabbar` buffer margins are thin (Lines 2709–2763)
Stack buffers `title[128]` and `label[80]` have small margins relative to `doc->title[64]` + format overhead.

### M14. `autosave_path_for_doc` buffer overflow (Lines 2264–2269)
`autosave_dir[MAX_PATH]` + suffix > `out[MAX_PATH]` — truncation or overflow.

### M15. `autosave_write` `snprintf` truncation → `memcpy` overread (Lines 2298–2318)
If `hdr_len` equals `sizeof(header)` (truncation), `memcpy` uses the un-truncated length.

### M16. `gb_copy_range` has no bounds validation (Lines 359–376)
Out-of-bounds `start`/`len` from any caller produces memory corruption.

### M17. `ReadFile` partial read not handled in `load_file` (Lines 2043–2044)
No retry loop; return value not checked. Network files may load truncated.

### M18. Tab width estimated with `char_count * DPI(8)` not actual measurement (Lines 2764, 4839)

### M19. `load_file` size check too lenient (Lines 2033–2038)
Allows ~2.1 GB files whose wchar conversion could overflow allocation sizes.

### M20. Search `current_match` not snapped to cursor after edits (Lines 1928–1929)
After typing in the search query, the highlighted match jumps to the top of the file.

### M21. Replace doesn't advance `current_match` (Lines 1976–1979)
Pressing Replace again may re-replace the same position.

### M22. Replace All doesn't reposition cursor (Lines 1984–2020)
Cursor position becomes stale after replacements of varying lengths.

### M23. Tab with selection replaces selection instead of indenting (Lines 5526–5534)
Standard behavior is to indent all selected lines, not replace the selection with spaces.

### M24. Shift+Tab (de-indent) not implemented (Line 5526)

### M25. Minimap tokenizer truncates at 40 chars (Lines 3772–3777)
Block comments opening after column 40 break minimap highlighting.

### M26. `Ctrl+Click` on whitespace selects word to the left (Lines 4932–4938)

### M27. Back-buffer bitmap leaks on every resize due to wrong deselect (Line 4488–4496)
`SelectObject(hdc_back, NULL_PEN)` doesn't deselect the bitmap.

### M28. Indent guides misalign on horizontal scroll (Lines 3595–3600)

### M29. Horizontal wheel scroll has no maximum clamp (Lines 4699–4701)

### M30. `GlobalAlloc` failure silently empties clipboard (Lines 1785–1801)

---

## Low Severity Issues

| # | Lines | Description |
|---|-------|-------------|
| L1 | 2410 | `autosave_recover`: `GetFileSize` can't distinguish 4GB files from errors |
| L2 | 700–704 | `undo_push`: `int` overflow in capacity doubling |
| L3 | 1388 | `doc_create`: `wcscpy` instead of `safe_wcscpy` |
| L4 | 589 | `WC_CHAR` macro: unsafe without bounds context |
| L5 | 2839–2841 | `render_statusbar`: dead `GetTextExtentPoint32W` call |
| L6 | 4664–4670 | Smooth scroll snap threshold causes 1-pixel jitter |
| L7 | 3715–3719 | Minimap viewport highlight is opaque, not semi-transparent |
| L8 | 3577–3581 | Long-line ellipsis drawn at wrong X when horizontally scrolled |
| L9 | 5136, 5722 | Backspace in search handled in both WM_KEYDOWN and WM_CHAR (IME risk) |
| L10 | 5564 | Ctrl+Shift+Z redo undocumented in menus |
| L11 | 4856–4858 | New-tab `+` hit area misaligned by 4px |
| L12 | 5818–5836 | Drag-and-drop doesn't check for already-open file |
| L13 | 5747–5751 | Zoom only recreates 3 fonts; UI fonts stale after zoom |
| L14 | 4566–4578 | Back-buffer DC has no font selected at start of render |
| L15 | 2985–2991 | `#` after block-comment close tokenized as hash comment |
| L16 | 2997–3004 | C++ raw string literals not recognized |
| L17 | 3111–3123 | Horizontal rule detection is fragile |
| L18 | 3763 | Minimap bar width hardcoded to 120 columns |
| L19 | 942, 6011 | `DwmSetWindowAttribute` uses magic constant 20 |
| L20 | 4482–4499 | Minimize sends WM_SIZE w=0,h=0; `CreateCompatibleBitmap(0,0)` returns NULL |
| L21 | 4297 | Arena `used + size` can overflow `size_t` |

---

## Recommendations (Prioritized)

### Immediate Fixes
1. **Add the missing `{` in the minimap for-loop** (C1) — compile/logic error
2. **Add NULL checks after all `malloc`/`calloc`/`realloc` calls** (C2, C3, C4, H7, H8, H18) — ~15 locations
3. **Add bounds validation to `gb_delete`** (C5) — check `pos >= 0`, `len >= 0`, `pos + len <= gb_length`
4. **Fix `tlen - qlen` underflow in search** (H1) — add `if (tlen < qlen) return;`
5. **Fix `hdr_buf` off-by-one** (H3) — change guard to `hdr_len > 1023`
6. **Fix `Ctrl+Backspace` at position 0** (H11) — guard `start` from going negative
7. **Fix bitmap deselect in WM_SIZE** (H12) — save and restore the original HBITMAP

### Short-term Fixes
8. **Change `match_positions` from `int *` to `bpos *`** (H2)
9. **Fix `Alt+Up` stale line cache** (H5) — capture `prev_ls` before `gb_delete`
10. **Fix `do_replace_all` undo ordering** (H6)
11. **Fix `lc_notify_insert`/`lc_notify_delete` off-by-one** (H9, H10)
12. **Fix arrow key selection collapse** (H19)
13. **Fix `write_file_atomic` buffer sizes** (H4) — use `MAX_PATH + 16`
14. **Fix DPI initialization** (H15) — use `GetDpiForWindow` after creation

### Medium-term Improvements
15. Implement save-point tracking for undo/redo modified flag (M5)
16. Fix `word_start` to skip non-word characters first (M6)
17. Implement Shift+Tab de-indent and Tab block-indent (M23, M24)
18. Fix all DPI-unaware pixel constants in search bar blink rect (M10)
19. Handle `malloc(0)` for empty document saves (M4)
20. Add bounds checks to `gb_copy_range` and `gb_move_gap` (M16, H17)
