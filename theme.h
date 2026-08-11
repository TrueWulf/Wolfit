#pragma once

#include <FL/Fl.H>

enum class ThemePreset : unsigned char { KateBreezeDark, OneDark, Catppuccin };

struct Theme {
    Fl_Color surface;
    Fl_Color editor;
    Fl_Color text;
    Fl_Color muted;
    Fl_Color accent;
    Fl_Color tab_active;
    Fl_Color tab_hover;
    Fl_Color selection;
    Fl_Color scrollbar;
    Fl_Color scrollbar_hover;
    Fl_Color scrollbar_active;
    Fl_Color line_clean;
    Fl_Color line_modified;
};

struct Settings {
    Theme theme;
    int font_size;
    int indent_size;
    bool minimal_scrollbars;
};

Theme make_theme(ThemePreset preset);
Settings load_settings();
