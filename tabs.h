#pragma once

#include "editor.h"
#include "theme.h"

#include <FL/Fl_Widget.H>

class TabStrip : public Fl_Widget {
public:
    typedef void (*TabCallback)(int index, void *data);
    typedef void (*ReorderCallback)(int from, int to, void *data);

    TabStrip(int x, int y, int width, int height, std::vector<Document *> *documents, const Settings *settings);

    void callbacks(TabCallback select, TabCallback close, ReorderCallback reorder, void *data);
    void active(int index);
    void draw() override;
    int handle(int event) override;

private:
    int tab_at(int mouse_x) const;
    bool close_at(int index, int mouse_x, int mouse_y) const;
    int tab_x(int index) const;
    int tab_width(const Document &document) const;

    std::vector<Document *> *documents_;
    const Settings *settings_;
    TabCallback select_ = nullptr;
    TabCallback close_ = nullptr;
    ReorderCallback reorder_ = nullptr;
    void *callback_data_ = nullptr;
    int active_ = -1;
    int hover_tab_ = -1;
    int hover_close_ = -1;
    int pressed_tab_ = -1;
    int pressed_x_ = 0;
    int drag_x_ = 0;
    int drop_target_ = -1;
    int drag_offset_ = 0;
    bool dragging_ = false;
};
