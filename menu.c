#include "prose_code.h"

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
    { L"Toggle Spellcheck",  L"F7",            MENU_ID_SPELLCHECK },
    { L"Focus Mode",         L"Ctrl+D",        MENU_ID_FOCUS },
    { L"Session Stats",      L"Ctrl+I",        MENU_ID_STATS },
    { NULL, NULL, MENU_ID_SEP },
    { L"Toggle Theme",       L"Ctrl+T",        MENU_ID_THEME },
    { L"Zoom In",            L"Ctrl++",        MENU_ID_ZOOM_IN },
    { L"Zoom Out",           L"Ctrl+-",        MENU_ID_ZOOM_OUT },
};

const MenuDef g_menus[MENU_COUNT] = {
    { L"File",   g_file_items,   sizeof(g_file_items)/sizeof(g_file_items[0]) },
    { L"Edit",   g_edit_items,   sizeof(g_edit_items)/sizeof(g_edit_items[0]) },
    { L"Search", g_search_items, sizeof(g_search_items)/sizeof(g_search_items[0]) },
    { L"View",   g_view_items,   sizeof(g_view_items)/sizeof(g_view_items[0]) },
};

void menu_execute(int id) {
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
    case MENU_ID_SPELLCHECK: g_editor.spellcheck_enabled = !g_editor.spellcheck_enabled; break;
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
