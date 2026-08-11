#pragma once

#include <FL/Fl_Double_Window.H>
#include <FL/Fl_Text_Buffer.H>

#include <string>
#include <vector>

enum class LineState : unsigned char { Clean, Modified };

struct Document {
    std::string path;
    Fl_Text_Buffer *text;
    Fl_Text_Buffer *styles;
    std::vector<LineState> lines;
    std::vector<bool> saving_lines;
    bool modified;
    bool untitled;
    unsigned save_progress;
    int cursor_position;
};

void create_editor_ui(Fl_Double_Window *window, int argc, char **argv);
void shutdown_editor_ui();
