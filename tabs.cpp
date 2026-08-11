#include "tabs.h"
#include "theme.h"

#include <FL/Fl.H>
#include <FL/fl_draw.H>

#include <cstring>

static constexpr int fixed_tab_width = 156;

TabStrip::TabStrip(int x, int y, int width, int height, std::vector<Document *> *documents, const Settings *settings)
    : Fl_Widget(x, y, width, height), documents_(documents), settings_(settings) {}

void TabStrip::callbacks(TabCallback select, TabCallback close, ReorderCallback reorder, void *data) {
    select_ = select;
    close_ = close;
    reorder_ = reorder;
    callback_data_ = data;
}

void TabStrip::active(int index) {
    active_ = index;
    redraw();
}

int TabStrip::tab_width(const Document &) const {
    return fixed_tab_width;
}

int TabStrip::tab_x(int index) const {
    int result = x() + 8;
    for (int i = 0; i < index; ++i) result += tab_width(*(*documents_)[i]) + 3;
    return result;
}

int TabStrip::tab_at(int mouse_x) const {
    for (unsigned i = 0; i < documents_->size(); ++i) {
        const int left = tab_x(static_cast<int>(i));
        if (mouse_x >= left && mouse_x < left + tab_width(*(*documents_)[i])) return static_cast<int>(i);
    }
    return -1;
}

bool TabStrip::close_at(int index, int mouse_x, int mouse_y) const {
    if (index < 0) return false;
    const int left = tab_x(index) + tab_width(*(*documents_)[index]) - 21;
    return mouse_x >= left && mouse_x < left + 15 && mouse_y >= y() + 9 && mouse_y < y() + 25;
}

void TabStrip::draw() {
    fl_color(settings_->theme.surface); fl_rectf(x(), y(), w(), h());
    fl_color(settings_->theme.muted); fl_rectf(x(), y() + h() - 1, w(), 1);
    fl_font(FL_HELVETICA, 12);
    for (unsigned i = 0; i < documents_->size(); ++i) {
        const int index = static_cast<int>(i);
        int left = tab_x(index);
        const int width = tab_width(*(*documents_)[i]);
        const bool active = index == active_;
        if (dragging_ && index == pressed_tab_) left += drag_offset_;
        else if (dragging_ && drag_offset_ > 0 && index > pressed_tab_ && index <= drop_target_)
            left -= tab_width(*(*documents_)[pressed_tab_]) + 3;
        else if (dragging_ && drag_offset_ < 0 && index >= drop_target_ && index < pressed_tab_)
            left += tab_width(*(*documents_)[pressed_tab_]) + 3;
        const int tab_y = y() + 4;
        const int tab_height = h() - 7;
        fl_color(active ? settings_->theme.tab_active : index == hover_tab_ ? settings_->theme.tab_hover : settings_->theme.editor);
        fl_rounded_rectf(left, tab_y, width, tab_height, 3);
        if (index == drop_target_) {
            fl_color(FL_WHITE);
            fl_rounded_rect(left, tab_y, width, tab_height, 4);
        }
        if (active) { fl_color(FL_WHITE); fl_rounded_rectf(left, y() + h() - 4, width, 3, 2); }
        if ((*documents_)[i]->modified) { fl_color(settings_->theme.line_modified); fl_pie(left + 10, y() + 14, 6, 6, 0, 360); }
        const std::string::size_type slash = (*documents_)[i]->path.find_last_of('/');
        const char *name = (*documents_)[i]->path.c_str() + (slash == std::string::npos ? 0 : slash + 1);
        const int text_width = width - 48;
        const int name_width = static_cast<int>(std::strlen(name)) * 8;
        std::string label = name;
        if (name_width > text_width) {
            const int visible = (text_width - 24) / 8;
            label = std::string(name, visible > 0 ? visible : 0) + "...";
        }
        fl_color(settings_->theme.text); fl_draw(label.c_str(), left + 22, y() + 22);
        const int close_x = left + width - 17;
        fl_color(index == hover_close_ ? settings_->theme.text : settings_->theme.muted);
        fl_line(close_x, y() + 14, close_x + 7, y() + 21);
        fl_line(close_x + 7, y() + 14, close_x, y() + 21);
    }
}

int TabStrip::handle(int event) {
    const int mouse_x = Fl::event_x();
    const int mouse_y = Fl::event_y();
    if (event == FL_MOVE || event == FL_ENTER) {
        const int next = tab_at(mouse_x);
        const int close = close_at(next, mouse_x, mouse_y) ? next : -1;
        if (next != hover_tab_ || close != hover_close_) { hover_tab_ = next; hover_close_ = close; redraw(); }
        return 1;
    }
    if (event == FL_LEAVE) { hover_tab_ = hover_close_ = -1; redraw(); return 1; }
    if (event == FL_PUSH && Fl::event_button() == FL_LEFT_MOUSE) {
        pressed_tab_ = tab_at(mouse_x);
        pressed_x_ = mouse_x;
        drag_x_ = mouse_x;
        drag_offset_ = 0;
        dragging_ = false;
        return pressed_tab_ >= 0;
    }
    if (event == FL_DRAG && pressed_tab_ >= 0) {
        drag_x_ = mouse_x;
        drag_offset_ = drag_x_ - pressed_x_;
        if (drag_x_ - pressed_x_ > 3 || pressed_x_ - drag_x_ > 3) dragging_ = true;
        const int target = tab_at(drag_x_);
        if (dragging_ && target >= 0 && target != pressed_tab_) {
            drop_target_ = target;
            redraw();
        }
        return 1;
    }
    if (event == FL_RELEASE && Fl::event_button() == FL_LEFT_MOUSE) {
        if (pressed_tab_ >= 0 && !dragging_) {
            if (close_at(pressed_tab_, mouse_x, mouse_y) && close_ != nullptr) close_(pressed_tab_, callback_data_);
            else if (tab_at(mouse_x) == pressed_tab_ && select_ != nullptr) select_(pressed_tab_, callback_data_);
        } else if (pressed_tab_ >= 0 && dragging_) {
            const int target = drop_target_ >= 0 ? drop_target_ : tab_at(mouse_x);
            if (target >= 0 && target != pressed_tab_ && reorder_ != nullptr) reorder_(pressed_tab_, target, callback_data_);
        }
        pressed_tab_ = -1;
        drop_target_ = -1;
        drag_offset_ = 0;
        redraw();
        return 1;
    }
    return 0;
}
