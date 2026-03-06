#include "prose_code.h"

const Theme THEME_DARK = {
    .bg          = RGB(28,  28,  34),
    .bg_dark     = RGB(22,  22,  28),
    .surface0    = RGB(38,  38,  46),
    .surface1    = RGB(52,  52,  62),
    .surface2    = RGB(70,  70,  82),
    .overlay0    = RGB(120, 120, 135),
    .text        = RGB(205, 208, 218),
    .subtext     = RGB(145, 150, 168),
    .lavender    = RGB(148, 160, 220),
    .blue        = RGB(110, 168, 230),
    .sapphire    = RGB(96,  195, 178),
    .sky         = RGB(145, 200, 240),
    .teal        = RGB(96,  195, 178),
    .green       = RGB(158, 203, 155),
    .yellow      = RGB(220, 200, 140),
    .peach       = RGB(215, 160, 130),
    .maroon      = RGB(200, 140, 148),
    .red         = RGB(230, 120, 110),
    .mauve       = RGB(188, 148, 200),
    .pink        = RGB(195, 155, 220),
    .flamingo    = RGB(200, 172, 172),
    .rosewater   = RGB(210, 195, 195),
    .cursor      = RGB(148, 160, 220),
    .selection   = RGB(48,  58,  90),
    .activeline  = RGB(34,  34,  42),
    .gutter      = RGB(22,  22,  28),
    .gutter_text = RGB(72,  74,  88),
    .tab_active  = RGB(28,  28,  34),
    .tab_inactive= RGB(22,  22,  28),
    .scrollbar_bg= RGB(28,  28,  34),
    .scrollbar_th= RGB(58,  58,  72),
    .scrollbar_hover = RGB(90,  92, 108),
    .accent      = RGB(148, 160, 220),
    .misspelled  = RGB(230, 120, 110),
    .focus_dim   = RGB(28,  28,  34),
    .search_hl   = RGB(72,  65,  36),
    .is_dark     = 1,
};

const Theme THEME_LIGHT = {
    .bg          = RGB(255, 255, 255),
    .bg_dark     = RGB(243, 243, 243),
    .surface0    = RGB(228, 228, 228),
    .surface1    = RGB(210, 210, 210),
    .surface2    = RGB(185, 185, 185),
    .overlay0    = RGB(130, 130, 140),
    .text        = RGB(30,  30,  30),
    .subtext     = RGB(100, 100, 110),
    .lavender    = RGB(220, 0, 120),
    .blue        = RGB(0,   102, 204),
    .sapphire    = RGB(0,   122, 105),
    .sky         = RGB(38,  120, 178),
    .teal        = RGB(0,   122, 105),
    .green       = RGB(22,  126, 64),
    .yellow      = RGB(120, 100, 0),
    .peach       = RGB(163, 21,  21),
    .maroon      = RGB(163, 21,  21),
    .red         = RGB(205, 49,  49),
    .mauve       = RGB(136, 23,  152),
    .pink        = RGB(160, 50,  170),
    .flamingo    = RGB(140, 100, 100),
    .rosewater   = RGB(130, 90,  90),
    .cursor      = RGB(0,   0,   0),
    .selection   = RGB(173, 214, 255),
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

Theme g_theme;
int   g_theme_index = 0;
float g_dpi_scale = 1.0f;

void apply_theme(int index) {
    g_theme_index = index;
    if (index == 0) g_theme = THEME_DARK;
    else            g_theme = THEME_LIGHT;

    if (g_editor.hwnd) {
        BOOL dark = g_theme.is_dark;
        DwmSetWindowAttribute(g_editor.hwnd, 20, &dark, sizeof(dark));
        SetWindowPos(g_editor.hwnd, NULL, 0, 0, 0, 0,
            SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_FRAMECHANGED);
        InvalidateRect(g_editor.hwnd, NULL, FALSE);
    }
}
