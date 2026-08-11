#include "editor.h"
#include "detect.h"
#include "tabs.h"
#include "theme.h"
#include "version.h"

#include <FL/Fl.H>
#include <FL/Fl_Box.H>
#include <FL/Fl_Button.H>
#include <FL/Fl_Check_Button.H>
#include <FL/Fl_File_Chooser.H>
#include <FL/Fl_Group.H>
#include <FL/Fl_Input.H>
#include <FL/Fl_Menu_Bar.H>
#include <FL/fl_ask.H>
#include <FL/Fl_Native_File_Chooser.H>
#include <FL/Fl_Scrollbar.H>
#include <FL/Fl_Text_Buffer.H>
#include <FL/Fl_Text_Editor.H>
#include <FL/fl_draw.H>

#include <atomic>
#include <cctype>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <fstream>
#include <mutex>
#include <thread>
#include <string>
#include <vector>

#include <dirent.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/mman.h>
#include <unistd.h>

static Settings settings;
static std::vector<Document *> documents;
static int active_document = -1;
static std::vector<int> search_matches;
static int current_match = -1;
class StatusBar;
static StatusBar *status_bar = nullptr;
static Fl_Input *search_input = nullptr;
static Fl_Group *search_overlay = nullptr;
static Fl_Box *search_count = nullptr;
static Fl_Check_Button *search_case = nullptr;
static bool search_case_sensitive = false;

struct IpcRequest {
    std::string command;
    int client;
};

static std::vector<IpcRequest> ipc_requests;
static std::mutex ipc_mutex;
static std::atomic<bool> ipc_running{true};
static std::thread ipc_thread;
static int ipc_socket = -1;

static void update_ui();
static void save_active_document();
static void switch_document(int direction);
static void rehighlight(Document *document);
static void open_document(const char *path);
static void process_ipc(void *);
static void start_ipc();
static void change_font_size(int amount);
static void copy_active_selection(bool cut);
static void restore_session();
static void save_session();
static void show_search();
static void update_search();
static void search_cb(Fl_Widget *, void *);
static void navigate_search(int direction);
static void find_search_matches();
static void hide_search();
static bool search_matches_at(const char *text, const std::string &query, int position);
static bool ctrl_key(const char *latin, const char *russian);
static unsigned utf8_codepoint(const char *text);
static void save_active_document_as();
static void close_active_document();
static void select_line();
static void new_document();
static void open_file_dialog();

static Fl_Text_Display::Style_Table_Entry style_table[] = {
    {0xABB2BF00, FL_COURIER, 14, 0, 0}, {0xE06C7500, FL_COURIER_BOLD, 14, 0, 0},
    {0x61AFEF00, FL_COURIER_BOLD, 14, 0, 0}, {0x98C37900, FL_COURIER, 14, 0, 0},
    {0x5C637000, FL_COURIER_ITALIC, 14, 0, 0}, {0xD19A6600, FL_COURIER, 14, 0, 0},
    {0x23262900, FL_COURIER, 14, Fl_Text_Display::ATTR_BGCOLOR, 0xF6D36500}
};

static const char *const keywords[] = {"auto", "break", "case", "class", "const", "constexpr", "continue", "default",
    "do", "else", "enum", "extern", "for", "if", "namespace", "new", "operator", "private", "protected", "public",
    "return", "sizeof", "static", "struct", "switch", "template", "this", "typedef", "using", "virtual", "while", nullptr};
static const char *const types[] = {"bool", "char", "double", "float", "int", "long", "short", "signed", "unsigned", "void",
    "size_t", "std", "string", "vector", nullptr};

static bool is_in_list(const std::string &token, const char *const *list) {
    for (int index = 0; list[index] != nullptr; ++index) if (token == list[index]) return true;
    return false;
}

static bool is_text_file(const char *path) {
    const int fd = open(path, O_RDONLY);
    if (fd < 0) return false;
    struct stat st;
    if (fstat(fd, &st) != 0) { close(fd); return false; }
    const off_t size = st.st_size;
    if (size == 0) { close(fd); return true; }
    const size_t check_size = size > 512 ? 512 : static_cast<size_t>(size);
    if (size > 52428800) {
        void *mapped = mmap(nullptr, check_size, PROT_READ, MAP_PRIVATE, fd, 0);
        if (mapped == MAP_FAILED) { close(fd); return false; }
        const char *bytes = static_cast<const char *>(mapped);
        bool valid = true;
        for (size_t i = 0; i < check_size; ++i) if (bytes[i] == '\0') { valid = false; break; }
        munmap(mapped, check_size);
        close(fd);
        return valid;
    }
    char bytes[512];
    const ssize_t read_count = read(fd, bytes, check_size);
    close(fd);
    if (read_count < 0) return false;
    for (ssize_t i = 0; i < read_count; ++i) if (bytes[i] == '\0') return false;
    return true;
}

class WolfitScrollbar : public Fl_Scrollbar {
public:
    WolfitScrollbar(int x, int y, int width, int height) : Fl_Scrollbar(x, y, width, height), visual_thickness_(3), target_thickness_(3) {
        box(FL_NO_BOX);
        slider(FL_FLAT_BOX);
    }

    void draw() override {
        fl_push_clip(x(), y(), w(), h());
        const int range = maximum() - minimum();
        if (range > 0 && slider_size() < 1.0) {
            const bool vertical = h() > w();
            const int track = vertical ? h() : w();
            const int thickness = vertical ? static_cast<int>(visual_thickness_ + 0.5f) : h();
            int thumb = static_cast<int>(track * slider_size() + 0.5);
            if (thumb < 24) thumb = 24;
            if (thumb > track) thumb = track;
            const int travel = track - thumb;
            const double fraction = static_cast<double>(value() - minimum()) / range;
            const int offset = travel > 0 ? static_cast<int>(travel * fraction + 0.5) : 0;
            fl_color(dragging_ ? settings.theme.scrollbar_active : hover_ ? settings.theme.scrollbar_hover : settings.theme.scrollbar);
            if (vertical) fl_rounded_rectf(x() + (w() - thickness) / 2, y() + offset, thickness, thumb, thickness / 2);
            else fl_rounded_rectf(x() + offset, y(), thumb, h(), 4);
        }
        fl_pop_clip();
    }

    int handle(int event) override {
        if (event == FL_ENTER || event == FL_MOVE) { hover_ = true; target_thickness_ = 10; animate(); }
        if (event == FL_LEAVE && !dragging_) { hover_ = false; target_thickness_ = 3; animate(); }
        if (event == FL_PUSH && Fl::event_button() == FL_LEFT_MOUSE) { dragging_ = true; target_thickness_ = 10; animate(); }
        const int result = Fl_Slider::handle(event, x(), y(), w(), h());
        if (event == FL_RELEASE && dragging_) { dragging_ = false; hover_ = Fl::event_inside(x(), y(), w(), h()); target_thickness_ = hover_ ? 10 : 3; animate(); }
        return result;
    }

private:
    static void animate_cb(void *data) {
        static_cast<WolfitScrollbar *>(data)->animate();
    }

    void animate() {
        if (visual_thickness_ == target_thickness_) { redraw(); return; }
        if (visual_thickness_ < target_thickness_) ++visual_thickness_;
        else --visual_thickness_;
        redraw();
        if (visual_thickness_ != target_thickness_) Fl::repeat_timeout(0.016, animate_cb, this);
    }

    bool hover_ = false;
    bool dragging_ = false;
    int visual_thickness_;
    int target_thickness_;
};

class WolfitEditor : public Fl_Text_Editor {
public:
    WolfitEditor(int x, int y, int width, int height) : Fl_Text_Editor(x, y, width, height) {
        if (!settings.minimal_scrollbars) return;
        remove(mVScrollBar); remove(mHScrollBar);
        delete mVScrollBar; delete mHScrollBar;
        mVScrollBar = new WolfitScrollbar(0, 0, 12, 12);
        mHScrollBar = new WolfitScrollbar(0, 0, 8, 8);
        mVScrollBar->type(FL_VERT_SLIDER);
        mHScrollBar->type(FL_HOR_SLIDER);
        add(mVScrollBar); add(mHScrollBar);
        mVScrollBar->callback(reinterpret_cast<Fl_Callback *>(v_scrollbar_cb), this);
        mHScrollBar->callback(reinterpret_cast<Fl_Callback *>(h_scrollbar_cb), this);
    }

    void apply_theme() {
        color(settings.theme.editor);
        textcolor(settings.theme.text);
        cursor_color(FL_WHITE);
        cursor_style(SIMPLE_CURSOR);
        selection_color(settings.theme.selection);
        textsize(settings.font_size);
        linenumber_width(46);
        linenumber_bgcolor(settings.theme.editor);
        linenumber_fgcolor(0xC0C0C000);
        scrollbar_size(settings.minimal_scrollbars ? 12 : 0);
    }

    void draw() override {
        Fl_Text_Editor::draw();
        draw_line_indicators();
        draw_cursor_bar();
    }

    int handle(int event) override {
        if (event == FL_MOUSEWHEEL && (Fl::event_state() & FL_CTRL)) {
            const int delta = Fl::event_dy();
            change_font_size(delta > 0 ? -1 : delta < 0 ? 1 : 0);
            return 1;
        }
        if (event == FL_KEYBOARD && buffer() != nullptr && (Fl::event_state() & FL_CTRL)) {
            if (ctrl_key("s", "ы")) { save_active_document(); return 1; }
            if (ctrl_key("f", "а")) { show_search(); return 1; }
            if (ctrl_key("z", "я")) { if (buffer()->can_undo()) buffer()->undo(); return 1; }
            if (ctrl_key("y", "н")) { if (buffer()->can_redo()) buffer()->redo(); return 1; }
            if (ctrl_key("a", "ф")) { buffer()->select(0, buffer()->length()); return 1; }
            if (ctrl_key("c", "с")) { copy_selection(); return 1; }
            if (ctrl_key("x", "ч")) { cut_selection(); return 1; }
            if (ctrl_key("v", "м")) { Fl::paste(*this, 1); return 1; }
            if (ctrl_key("n", "т")) { new_document(); return 1; }
            if (ctrl_key("o", "щ")) { open_file_dialog(); return 1; }
            if (ctrl_key("w", "ц")) { close_active_document(); return 1; }
            if (Fl::event_key() == FL_Down) { navigate_search(1); return 1; }
            if (Fl::event_key() == FL_Up) { navigate_search(-1); return 1; }
            if (Fl::event_key() == FL_Tab) { switch_document((Fl::event_state() & FL_SHIFT) ? -1 : 1); return 1; }
        }
        if (event == FL_KEYBOARD && buffer() != nullptr && !(Fl::event_state() & (FL_CTRL | FL_ALT | FL_META))) {
            const char *text = Fl::event_text();
            const int length = Fl::event_length();
            if (length == 1 && std::strchr("([{\"'", text[0]) != nullptr) {
                const char opening = text[0];
                const char closing = opening == '(' ? ')' : opening == '[' ? ']' : opening == '{' ? '}' : opening;
                const int position = insert_position();
                char pair[3] = {opening, closing, '\0'};
                buffer()->insert(position, pair);
                insert_position(position + 1);
                show_insert_position();
                update_ui();
                return 1;
            }
        }
        const int handled = Fl_Text_Editor::handle(event);
        if (event == FL_KEYBOARD || event == FL_PUSH || event == FL_RELEASE) update_ui();
        return handled;
    }

private:
    static Fl_Color blend(Fl_Color from, Fl_Color to, unsigned amount, unsigned total) {
        const unsigned from_r = (from >> 24) & 0xff, from_g = (from >> 16) & 0xff, from_b = (from >> 8) & 0xff;
        const unsigned to_r = (to >> 24) & 0xff, to_g = (to >> 16) & 0xff, to_b = (to >> 8) & 0xff;
        return (((from_r * (total - amount) + to_r * amount / total) << 24) |
                ((from_g * (total - amount) + to_g * amount / total) << 16) |
                ((from_b * (total - amount) + to_b * amount / total) << 8));
    }

    void draw_line_indicators() {
        if (active_document < 0 || buffer() == nullptr) return;
        const Document *document = documents[active_document];
        const int gutter_x = mLineNumLeft + 3;
        const int gutter_width = 3;
        fl_push_clip(mLineNumLeft, y(), mLineNumWidth, h());
        fl_color(settings.theme.line_clean);
        fl_rectf(gutter_x, y(), gutter_width, h());
        int position = 0;
        for (unsigned line = 0; line < document->lines.size(); ++line) {
            int text_x = 0, baseline = 0;
            if (position_to_xy(position, &text_x, &baseline)) {
                Fl_Color color = document->lines[line] == LineState::Modified ? settings.theme.line_modified : settings.theme.line_clean;
                if (document->save_progress > 0 && line < document->saving_lines.size() && document->saving_lines[line])
                    color = blend(settings.theme.line_modified, settings.theme.line_clean, document->save_progress, 10);
                fl_color(color);
                const int line_top = baseline;
                const int next = buffer()->line_end(position);
                int next_x = 0, next_baseline = 0;
                const int line_bottom = next < buffer()->length() && position_to_xy(next + 1, &next_x, &next_baseline)
                    ? next_baseline : baseline + textsize();
                fl_rectf(gutter_x, line_top, gutter_width, line_bottom - line_top);
            }
            const int next = buffer()->line_end(position);
            if (next >= buffer()->length()) break;
            position = next + 1;
        }
        fl_pop_clip();
    }

    void draw_cursor_bar() {
        if (Fl::focus() != this || buffer() == nullptr) return;
        int cursor_x = 0, baseline = 0;
        if (!position_to_xy(insert_position(), &cursor_x, &baseline)) return;
        fl_color(FL_WHITE);
        const int cursor_height = textsize();
        fl_rectf(cursor_x, baseline, 2, cursor_height);
    }

    void copy_selection() {
        int start = 0, end = 0;
        if (!buffer()->selection_position(&start, &end)) return;
        char *selected = buffer()->text_range(start, end);
        Fl::copy(selected, end - start, 1);
        std::free(selected);
    }

    void cut_selection() {
        copy_selection();
        buffer()->remove_selection();
    }
};

class StatusBar : public Fl_Box {
public:
    StatusBar(int x, int y, int width, int height) : Fl_Box(x, y, width, height, "") {}

    void draw() override {
        Fl_Box::draw();
        fl_font(FL_HELVETICA, 11);
        const int control_width = 154;
        const int control_x = x() + w() - control_width - 10;
        fl_color(settings.theme.tab_hover);
        fl_rounded_rectf(control_x, y() + 4, control_width, h() - 8, 4);
        fl_color(settings.theme.text);
        fl_arc(control_x + 10, y() + 9, 8, 8, 0, 360);
        fl_line(control_x + 17, y() + 16, control_x + 21, y() + 20);
        fl_draw("-", control_x + 27, y() + 18);
        const std::string size = std::to_string(settings.font_size) + " px";
        fl_draw(size.c_str(), control_x + 58, y() + 18);
        fl_arc(control_x + 111, y() + 9, 8, 8, 0, 360);
        fl_line(control_x + 118, y() + 16, control_x + 122, y() + 20);
        fl_draw("+", control_x + 130, y() + 18);
    }

    int handle(int event) override {
        if (event != FL_PUSH || Fl::event_button() != FL_LEFT_MOUSE) return Fl_Box::handle(event);
        const int control_x = x() + w() - 164;
        const int mouse_x = Fl::event_x();
        if (mouse_x >= control_x && mouse_x < control_x + 44) change_font_size(-1);
        else if (mouse_x >= control_x + 105 && mouse_x < control_x + 154) change_font_size(1);
        else return 0;
        return 1;
    }
};

class SurfaceGroup : public Fl_Group {
public:
    SurfaceGroup(int x, int y, int width, int height) : Fl_Group(x, y, width, height) {}

    void draw() override {
        fl_push_clip(x(), y(), w(), h());
        fl_color(settings.theme.surface);
        fl_rectf(x(), y(), w(), h());
        fl_color(settings.theme.tab_hover);
        fl_rectf(x(), y() + h() - 1, w(), 1);
        fl_pop_clip();
        Fl_Group::draw();
    }
};

class SearchInput : public Fl_Input {
public:
    SearchInput(int x, int y, int width, int height) : Fl_Input(x, y, width, height) {}

    int handle(int event) override {
        if (event == FL_KEYBOARD) {
            if (Fl::event_key() == FL_Escape) { hide_search(); return 1; }
            if (Fl::event_key() == FL_Enter || Fl::event_key() == FL_KP_Enter) {
                navigate_search((Fl::event_state() & FL_SHIFT) ? -1 : 1);
                return 1;
            }
        }
        return Fl_Input::handle(event);
    }
};

static WolfitEditor *editor = nullptr;
static TabStrip *tabs = nullptr;
static Fl_Menu_Bar *menu_bar = nullptr;

static bool ctrl_key(const char *latin, const char *russian) {
    const int key = Fl::event_key();
    const int original = Fl::event_original_key();
    const char *text = Fl::event_text();
    const unsigned russian_key = utf8_codepoint(russian);
    return key == latin[0] || key == std::toupper(latin[0]) ||
           key == static_cast<int>(russian_key) || original == static_cast<int>(russian_key) ||
           original == latin[0] || original == std::toupper(latin[0]) ||
            (text != nullptr && (!std::strcmp(text, latin) || !std::strcmp(text, russian)));
}

static unsigned utf8_codepoint(const char *text) {
    const unsigned char first = static_cast<unsigned char>(text[0]);
    if (first < 0x80) return first;
    if ((first & 0xe0) == 0xc0) return ((first & 0x1f) << 6) | (static_cast<unsigned char>(text[1]) & 0x3f);
    if ((first & 0xf0) == 0xe0) return ((first & 0x0f) << 12) | ((static_cast<unsigned char>(text[1]) & 0x3f) << 6) | (static_cast<unsigned char>(text[2]) & 0x3f);
    return 0;
}

static int global_key_handler(int event) {
    if (event != FL_KEYBOARD || editor == nullptr || editor->buffer() == nullptr) return 0;
    const int state = Fl::event_state();
    if (!(state & FL_CTRL)) return 0;
    if (ctrl_key("s", "ы")) { save_active_document(); return 1; }
    if (ctrl_key("c", "с")) { copy_active_selection(false); return 1; }
    if (ctrl_key("x", "ч")) { copy_active_selection(true); return 1; }
    if (ctrl_key("v", "м")) { Fl::paste(*editor, 1); return 1; }
    if (ctrl_key("a", "ф")) { editor->buffer()->select(0, editor->buffer()->length()); update_ui(); return 1; }
    if (ctrl_key("f", "а")) { show_search(); return 1; }
    if (ctrl_key("z", "я")) { if (editor->buffer()->can_undo()) editor->buffer()->undo(); return 1; }
    if (ctrl_key("y", "н")) { if (editor->buffer()->can_redo()) editor->buffer()->redo(); return 1; }
    if (ctrl_key("n", "т")) { new_document(); return 1; }
    if (ctrl_key("o", "щ")) { open_file_dialog(); return 1; }
    if (ctrl_key("w", "ц")) { close_active_document(); return 1; }
    if (ctrl_key("q", "й")) { if (Fl::first_window() != nullptr) Fl::first_window()->hide(); return 1; }
    if (ctrl_key("l", "д")) { select_line(); return 1; }
    return 0;
}

static void mark_changed_lines(Document *document, int position, int inserted) {
    const int line_count = document->text->count_lines(0, document->text->length()) + 1;
    document->lines.resize(line_count, LineState::Clean);
    const int first = document->text->count_lines(0, position);
    const int last_position = position + inserted;
    const int last = document->text->count_lines(0, last_position > document->text->length() ? document->text->length() : last_position);
    document->saving_lines.resize(line_count, false);
    for (int line = first; line <= last && line < line_count; ++line) {
        document->lines[line] = LineState::Modified;
        document->saving_lines[line] = false;
    }
}

static void style_update_cb(int position, int inserted, int deleted, int, const char *, void *data) {
    if (inserted == 0 && deleted == 0) return;
    Document *document = static_cast<Document *>(data);
    document->modified = true;
    mark_changed_lines(document, position, inserted);
    rehighlight(document);
    update_ui();
}

static void rehighlight(Document *document) {
    char *text = document->text->text();
    const int length = document->text->length();
    std::string styles(length, 'A');
    for (int position = 0; position < length;) {
        if (text[position] == '/' && position + 1 < length && text[position + 1] == '/') while (position < length && text[position] != '\n') styles[position++] = 'E';
        else if (text[position] == '"' || text[position] == '\'') {
            const char quote = text[position]; styles[position++] = 'D';
            while (position < length && text[position] != '\n') { styles[position] = 'D'; if (text[position++] == '\\' && position < length) styles[position++] = 'D'; if (position > 0 && text[position - 1] == quote) break; }
        } else if (std::isalpha(static_cast<unsigned char>(text[position])) || text[position] == '_') {
            const int start = position;
            while (position < length && (std::isalnum(static_cast<unsigned char>(text[position])) || text[position] == '_')) ++position;
            const std::string word(text + start, position - start);
            const char style = is_in_list(word, keywords) ? 'B' : is_in_list(word, types) ? 'C' : 'A';
            for (int index = start; index < position; ++index) styles[index] = style;
        } else ++position;
    }
    if (search_input != nullptr && active_document >= 0 && document == documents[active_document]) {
        const std::string query = search_input->value();
        if (!query.empty()) {
            for (int position = 0; position + static_cast<int>(query.size()) <= length;) {
                if (search_matches_at(text, query, position)) {
                    for (unsigned index = 0; index < query.size(); ++index) styles[position + index] = 'G';
                    position += static_cast<int>(query.size());
                } else ++position;
            }
        }
    }
    document->styles->text(styles.c_str());
    std::free(text);
}

static void find_search_matches() {
    search_matches.clear();
    current_match = -1;
    if (active_document < 0 || search_input == nullptr) return;
    const std::string query = search_input->value();
    if (query.empty()) return;
    char *text = documents[active_document]->text->text();
    const int length = documents[active_document]->text->length();
    for (int pos = 0; pos + static_cast<int>(query.size()) <= length;) {
        if (search_matches_at(text, query, pos)) {
            search_matches.push_back(pos);
            pos += static_cast<int>(query.size());
        } else ++pos;
    }
    std::free(text);
}

static bool search_matches_at(const char *text, const std::string &query, int position) {
    for (unsigned index = 0; index < query.size(); ++index) {
        const unsigned char actual = static_cast<unsigned char>(text[position + index]);
        const unsigned char expected = static_cast<unsigned char>(query[index]);
        if (search_case_sensitive ? actual != expected : std::tolower(actual) != std::tolower(expected)) return false;
    }
    return true;
}

static void navigate_search(int direction) {
    if (search_matches.empty()) return;
    const int previous = current_match;
    if (current_match < 0) current_match = direction < 0 ? static_cast<int>(search_matches.size()) - 1 : 0;
    else current_match = (current_match + direction + static_cast<int>(search_matches.size())) % static_cast<int>(search_matches.size());
    const int pos = search_matches[current_match];
    editor->insert_position(pos);
    editor->buffer()->select(pos, pos + static_cast<int>(std::strlen(search_input->value())));
    editor->show_insert_position();
    if (search_count != nullptr) {
        std::string label = std::to_string(current_match + 1) + " / " + std::to_string(search_matches.size());
        if (previous >= 0 && ((direction > 0 && current_match < previous) || (direction < 0 && current_match > previous))) label += "  Wrapped";
        search_count->copy_label(label.c_str());
    }
    editor->redraw();
}

static void update_search() {
    if (active_document < 0) return;
    find_search_matches();
    if (search_count != nullptr) {
        const std::string label = search_matches.empty() ? "No matches" : std::to_string(search_matches.size()) + " matches";
        search_count->copy_label(label.c_str());
        search_count->redraw();
    }
    rehighlight(documents[active_document]);
    editor->redraw();
}

static void search_cb(Fl_Widget *, void *) {
    search_case_sensitive = search_case != nullptr && search_case->value() != 0;
    update_search();
}

static void search_action_cb(Fl_Widget *, void *data) {
    const int action = static_cast<int>(reinterpret_cast<std::intptr_t>(data));
    if (action == 0) { hide_search(); return; }
    navigate_search(action);
}

static void show_search() {
    if (search_input == nullptr || search_overlay == nullptr) return;
    int start = 0, end = 0;
    if (active_document >= 0 && documents[active_document]->text->selection_position(&start, &end) && end > start) {
        char *selected = documents[active_document]->text->text_range(start, end);
        search_input->value(selected);
        std::free(selected);
    }
    search_overlay->show();
    search_input->take_focus();
    search_input->insert_position(0, search_input->size());
    update_search();
}

static void hide_search() {
    if (search_overlay == nullptr) return;
    search_overlay->hide();
    search_matches.clear();
    current_match = -1;
    if (active_document >= 0) rehighlight(documents[active_document]);
    editor->take_focus();
    editor->redraw();
}

static void update_status() {
    if (active_document < 0) { status_bar->copy_label(""); status_bar->redraw(); return; }
    const int position = editor->insert_position();
    const int line = documents[active_document]->text->count_lines(0, position) + 1;
    const int column = position - documents[active_document]->text->line_start(position) + 1;
    std::string label = "  Ln " + std::to_string(line) + ", Col " + std::to_string(column);
    label += "    UTF-8    Spaces: " + std::to_string(settings.indent_size);
    label += "    " + std::string(detect_language(documents[active_document]->path));
    status_bar->copy_label(label.c_str());
}

static void update_ui() { tabs->redraw(); update_status(); status_bar->redraw(); }

static void change_font_size(int amount) {
    const int size = settings.font_size + amount;
    if (size < 10 || size > 28) return;
    settings.font_size = size;
    for (Fl_Text_Display::Style_Table_Entry &style : style_table) style.size = size;
    editor->textsize(size);
    editor->linenumber_width(size < 14 ? 42 : size < 18 ? 46 : 50);
    if (menu_bar != nullptr) menu_bar->textsize(size < 14 ? 12 : size < 20 ? 13 : 14);
    if (tabs != nullptr) tabs->redraw();
    if (status_bar != nullptr) status_bar->redraw();
    editor->redraw();
    update_ui();
}

static void select_tab(int index, void *) {
    if (index < 0 || index >= static_cast<int>(documents.size())) return;
    active_document = index;
    switch_document(0);
}

static void close_tab(int index, void *) {
    if (index < 0 || index >= static_cast<int>(documents.size())) return;
    Document *document = documents[index];
    document->text->remove_modify_callback(style_update_cb, document);
    delete document->text;
    delete document->styles;
    delete document;
    documents.erase(documents.begin() + index);
    if (documents.empty()) {
        active_document = -1;
        editor->buffer(nullptr);
        update_ui();
        return;
    }
    if (index < active_document) --active_document;
    else if (index == active_document && active_document >= static_cast<int>(documents.size())) active_document = static_cast<int>(documents.size()) - 1;
    switch_document(0);
}

static void send_ipc_response(int client, const char *text, std::size_t length) {
    while (length > 0) {
        const ssize_t sent = send(client, text, length, MSG_NOSIGNAL);
        if (sent <= 0) break;
        text += sent;
        length -= static_cast<std::size_t>(sent);
    }
    close(client);
}

static void process_ipc(void *) {
    std::vector<IpcRequest> requests;
    {
        std::lock_guard<std::mutex> lock(ipc_mutex);
        requests.swap(ipc_requests);
    }
    for (IpcRequest &request : requests) {
        if (request.command == "save") {
            save_active_document();
            send_ipc_response(request.client, "ok\n", 3);
        } else if (request.command.compare(0, 5, "open ") == 0 && request.command.size() > 5) {
            open_document(request.command.c_str() + 5);
            send_ipc_response(request.client, "ok\n", 3);
        } else if (request.command == "close") {
            close_tab(active_document, nullptr);
            send_ipc_response(request.client, "ok\n", 3);
        } else if (request.command == "get_text") {
            if (active_document < 0) {
                send_ipc_response(request.client, "", 0);
                continue;
            }
            char *text = documents[active_document]->text->text();
            const std::size_t length = std::strlen(text);
            send_ipc_response(request.client, text, length);
            std::free(text);
        } else {
            send_ipc_response(request.client, "error\n", 6);
        }
    }
}

static void ipc_loop() {
    unlink("/tmp/wolfit.sock");
    ipc_socket = socket(AF_UNIX, SOCK_STREAM | SOCK_NONBLOCK, 0);
    if (ipc_socket < 0) return;
    sockaddr_un address{};
    address.sun_family = AF_UNIX;
    std::strncpy(address.sun_path, "/tmp/wolfit.sock", sizeof(address.sun_path) - 1);
    if (bind(ipc_socket, reinterpret_cast<sockaddr *>(&address), sizeof(address)) != 0 || listen(ipc_socket, 8) != 0) {
        close(ipc_socket);
        ipc_socket = -1;
        return;
    }
    while (ipc_running) {
        const int client = accept4(ipc_socket, nullptr, nullptr, SOCK_NONBLOCK);
        if (client < 0) {
            usleep(10000);
            continue;
        }
        char command[4096];
        ssize_t length = recv(client, command, sizeof(command) - 1, 0);
        if (length <= 0) {
            close(client);
            continue;
        }
        command[length] = '\0';
        std::string text(command);
        while (!text.empty() && (text.back() == '\n' || text.back() == '\r')) text.pop_back();
        {
            std::lock_guard<std::mutex> lock(ipc_mutex);
            ipc_requests.push_back({text, client});
        }
        Fl::awake(process_ipc);
    }
    if (ipc_socket >= 0) close(ipc_socket);
    ipc_socket = -1;
    unlink("/tmp/wolfit.sock");
}

static void start_ipc() {
    ipc_running = true;
    ipc_thread = std::thread(ipc_loop);
}

static void reorder_tab(int from, int to, void *) {
    if (from < 0 || to < 0 || from >= static_cast<int>(documents.size()) || to >= static_cast<int>(documents.size())) return;
    Document *document = documents[from];
    documents.erase(documents.begin() + from);
    documents.insert(documents.begin() + to, document);
    if (active_document == from) active_document = to;
    else if (from < active_document && to >= active_document) --active_document;
    else if (from > active_document && to <= active_document) ++active_document;
    tabs->active(active_document);
    tabs->redraw();
}

static void switch_document(int direction) {
    if (documents.empty()) return;
    if (active_document >= 0 && editor->buffer() != nullptr) documents[active_document]->cursor_position = editor->insert_position();
    const int count = static_cast<int>(documents.size());
    active_document = (active_document + direction + count) % count;
    editor->buffer(documents[active_document]->text);
    editor->highlight_data(documents[active_document]->styles, style_table, 7, 'A', nullptr, nullptr);
    editor->insert_position(documents[active_document]->cursor_position);
    editor->show_insert_position();
    editor->take_focus();
    tabs->active(active_document);
    update_ui();
}

static Fl_Color soft_pulse_color(unsigned step, unsigned steps) {
    const unsigned base_r = (settings.theme.editor >> 24) & 0xff;
    const unsigned base_g = (settings.theme.editor >> 16) & 0xff;
    const unsigned base_b = (settings.theme.editor >> 8) & 0xff;
    const unsigned strength = (steps - step) * 14 / steps;
    return (((base_r + (255 - base_r) * strength / 255) << 24) |
            ((base_g + (255 - base_g) * strength / 255) << 16) |
            ((base_b + (255 - base_b) * strength / 255) << 8));
}

static void flash_frame(void *) {
    const unsigned steps = 10;
    bool animating = false;
    unsigned progress = steps;
    for (Document *document : documents) {
        if (document->save_progress == 0) continue;
        if (++document->save_progress >= steps) {
            for (unsigned line = 0; line < document->lines.size(); ++line)
                if (line < document->saving_lines.size() && document->saving_lines[line]) document->lines[line] = LineState::Clean;
            document->saving_lines.clear();
            document->save_progress = 0;
        } else {
            animating = true;
            if (document->save_progress < progress) progress = document->save_progress;
        }
    }
    editor->color(animating ? soft_pulse_color(progress, steps) : settings.theme.editor);
    editor->redraw();
    if (animating) Fl::repeat_timeout(0.035, flash_frame);
}

static void save_active_document() {
    if (active_document < 0) return;
    Document *document = documents[active_document];
    if (document->untitled) {
        save_active_document_as();
        return;
    }
    if (document->text->savefile(document->path.c_str()) != 0) return;
    document->modified = false;
    document->lines.assign(document->lines.size(), LineState::Clean);
    document->saving_lines.clear();
    document->save_progress = 0;
    Fl::remove_timeout(flash_frame);
    editor->color(settings.theme.editor);
    editor->redraw();
    update_ui();
}

static void save_active_document_as() {
    if (active_document < 0) return;
    Document *document = documents[active_document];
    Fl_Native_File_Chooser chooser(Fl_Native_File_Chooser::BROWSE_SAVE_FILE);
    chooser.title("Save document as");
    chooser.filter("Text files\t*.txt\nAll files\t*");
    if (!document->untitled) chooser.preset_file(document->path.c_str());
    if (chooser.show() != 0 || chooser.filename() == nullptr || chooser.filename()[0] == '\0') return;
    document->path = chooser.filename();
    document->untitled = false;
    if (document->text->savefile(document->path.c_str()) != 0) return;
    document->modified = false;
    document->lines.assign(document->lines.size(), LineState::Clean);
    update_ui();
}

static void open_document(const char *path) {
    if (!is_text_file(path)) return;
    for (unsigned index = 0; index < documents.size(); ++index) if (documents[index]->path == path) { active_document = static_cast<int>(index); switch_document(0); return; }
    Document *document = new Document{path, new Fl_Text_Buffer(), new Fl_Text_Buffer(), {}, {}, false, false, 0, 0};
    if (document->text->loadfile(path) != 0) { delete document->text; delete document->styles; delete document; return; }
    documents.push_back(document);
    document->text->add_modify_callback(style_update_cb, document);
    document->lines.assign(document->text->count_lines(0, document->text->length()) + 1, LineState::Clean);
    rehighlight(document);
    document->modified = false;
    active_document = static_cast<int>(documents.size()) - 1;
    switch_document(0);
}

static void new_document() {
    static unsigned untitled_count = 1;
    Document *document = new Document{"Untitled " + std::to_string(untitled_count++), new Fl_Text_Buffer(), new Fl_Text_Buffer(), {}, {}, false, true, 0, 0};
    document->text->add_modify_callback(style_update_cb, document);
    document->lines.assign(1, LineState::Clean);
    document->styles->text("");
    documents.push_back(document);
    active_document = static_cast<int>(documents.size()) - 1;
    switch_document(0);
}

static void save_session() {
    const char *directory = "/tmp/wolfit_session";
    mkdir(directory, 0700);
    DIR *entries = opendir(directory);
    if (entries != nullptr) {
        dirent *entry = nullptr;
        while ((entry = readdir(entries)) != nullptr) {
            if (!std::strcmp(entry->d_name, ".") || !std::strcmp(entry->d_name, "..")) continue;
            const std::string path = std::string(directory) + "/" + entry->d_name;
            unlink(path.c_str());
        }
        closedir(entries);
    }
    std::ofstream manifest(std::string(directory) + "/manifest", std::ios::trunc);
    if (!manifest) return;
    unsigned saved = 0;
    for (unsigned index = 0; index < documents.size(); ++index) {
        Document *document = documents[index];
        if (!document->modified) continue;
        const std::string dump = std::string(directory) + "/buffer-" + std::to_string(saved);
        if (document->text->savefile(dump.c_str()) != 0) continue;
        manifest << saved << '\t' << index << '\t' << (document->untitled ? 1 : 0) << '\t' << document->cursor_position << '\t' << document->path << '\n';
        ++saved;
    }
}

static void restore_session() {
    const char *directory = "/tmp/wolfit_session";
    std::ifstream manifest(std::string(directory) + "/manifest");
    std::string line;
    while (std::getline(manifest, line)) {
        const std::string::size_type first = line.find('\t');
        const std::string::size_type second = first == std::string::npos ? first : line.find('\t', first + 1);
        const std::string::size_type third = second == std::string::npos ? second : line.find('\t', second + 1);
        const std::string::size_type fourth = third == std::string::npos ? third : line.find('\t', third + 1);
        if (fourth == std::string::npos) continue;
        const unsigned dump_number = static_cast<unsigned>(std::strtoul(line.c_str(), nullptr, 10));
        const bool untitled = line.substr(second + 1, third - second - 1) == "1";
        const int cursor_position = std::atoi(line.substr(third + 1, fourth - third - 1).c_str());
        const std::string path = line.substr(fourth + 1);
        const std::string dump = std::string(directory) + "/buffer-" + std::to_string(dump_number);
        Document *document = new Document{path, new Fl_Text_Buffer(), new Fl_Text_Buffer(), {}, {}, true, untitled, 0, cursor_position};
        if (document->text->loadfile(dump.c_str()) != 0) {
            delete document->text;
            delete document->styles;
            delete document;
            continue;
        }
        document->lines.assign(document->text->count_lines(0, document->text->length()) + 1, LineState::Modified);
        rehighlight(document);
        document->text->add_modify_callback(style_update_cb, document);
        documents.push_back(document);
    }
    if (!documents.empty()) {
        active_document = static_cast<int>(documents.size()) - 1;
        switch_document(0);
    }
}

static void open_file_dialog() {
    Fl_Native_File_Chooser chooser(Fl_Native_File_Chooser::BROWSE_FILE);
    chooser.title("Open file");
    chooser.filter("Text files\t*.{txt,c,cc,cpp,cxx,h,hpp,py,rs,zig,sh,json,md}\nAll files\t*");
    if (chooser.show() == 0 && chooser.filename() != nullptr) open_document(chooser.filename());
}

static void copy_active_selection(bool cut) {
    if (active_document < 0) return;
    Fl_Text_Buffer *buffer = documents[active_document]->text;
    int start = 0, end = 0;
    if (!buffer->selection_position(&start, &end)) return;
    char *selected = buffer->text_range(start, end);
    Fl::copy(selected, end - start, 1);
    std::free(selected);
    if (cut) buffer->remove_selection();
}

static void close_active_document() {
    close_tab(active_document, nullptr);
}

static void select_line() {
    if (active_document < 0) return;
    Fl_Text_Buffer *buffer = documents[active_document]->text;
    const int position = editor->insert_position();
    buffer->select(buffer->line_start(position), buffer->line_end(position));
    update_ui();
}

static void menu_cb(Fl_Widget *, void *data) {
    const int action = static_cast<int>(reinterpret_cast<std::intptr_t>(data));
    if (action == 1) { new_document(); return; }
    if (action == 2) { open_file_dialog(); return; }
    if (action == 3) { save_active_document(); return; }
    if (action == 4) { if (Fl::first_window() != nullptr) Fl::first_window()->hide(); return; }
    if (action == 5) { if (active_document >= 0 && documents[active_document]->text->can_undo()) documents[active_document]->text->undo(); return; }
    if (action == 6) { if (active_document >= 0 && documents[active_document]->text->can_redo()) documents[active_document]->text->redo(); return; }
    if (action == 7) { copy_active_selection(true); return; }
    if (action == 8) { copy_active_selection(false); return; }
    if (action == 9) { if (active_document >= 0) Fl::paste(*editor, 1); return; }
    if (action == 10) { if (active_document >= 0) documents[active_document]->text->select(0, documents[active_document]->text->length()); return; }
    if (action == 11) { switch_document(1); return; }
    if (action == 12) { save_active_document_as(); return; }
    if (action == 13) { close_active_document(); return; }
    if (action == 14) { show_search(); return; }
    if (action == 15) { select_line(); return; }
    if (action == 16) { switch_document(-1); return; }
    if (action == 17) { change_font_size(1); return; }
    if (action == 18) { change_font_size(-1); return; }
    if (action == 19) { fl_message("Wolfit " WOLFIT_VERSION "\n\nA fast, lightweight text editor.\n\nCopyright (C) 2026 Wolfit contributors\nLicensed under GPL-3.0-or-later."); return; }
}

void create_editor_ui(Fl_Double_Window *window, int argc, char **argv) {
    settings = load_settings();
    style_table[0].color = settings.theme.text;
    for (Fl_Text_Display::Style_Table_Entry &style : style_table) style.size = settings.font_size;
    SurfaceGroup *top_container = new SurfaceGroup(0, 0, 1100, 66);
    menu_bar = new Fl_Menu_Bar(8, 3, 1084, 27);
    menu_bar->box(FL_FLAT_BOX); menu_bar->color(settings.theme.surface); menu_bar->textcolor(settings.theme.text); menu_bar->selection_color(settings.theme.tab_hover);
    menu_bar->add("File/New", FL_CTRL + 'n', menu_cb, reinterpret_cast<void *>(1));
    menu_bar->add("File/Open", FL_CTRL + 'o', menu_cb, reinterpret_cast<void *>(2));
    menu_bar->add("File/Save", FL_CTRL + 's', menu_cb, reinterpret_cast<void *>(3));
    menu_bar->add("File/Save As", FL_CTRL + FL_SHIFT + 's', menu_cb, reinterpret_cast<void *>(12));
    menu_bar->add("File/Close", FL_CTRL + 'w', menu_cb, reinterpret_cast<void *>(13));
    menu_bar->add("File/Quit", FL_CTRL + 'q', menu_cb, reinterpret_cast<void *>(4));
    menu_bar->add("Edit/Undo", FL_CTRL + 'z', menu_cb, reinterpret_cast<void *>(5));
    menu_bar->add("Edit/Redo", FL_CTRL + 'y', menu_cb, reinterpret_cast<void *>(6));
    menu_bar->add("Edit/Cut", FL_CTRL + 'x', menu_cb, reinterpret_cast<void *>(7));
    menu_bar->add("Edit/Copy", FL_CTRL + 'c', menu_cb, reinterpret_cast<void *>(8));
    menu_bar->add("Edit/Paste", FL_CTRL + 'v', menu_cb, reinterpret_cast<void *>(9));
    menu_bar->add("Edit/Find", FL_CTRL + 'f', menu_cb, reinterpret_cast<void *>(14));
    menu_bar->add("Selection/Select All", FL_CTRL + 'a', menu_cb, reinterpret_cast<void *>(10));
    menu_bar->add("Selection/Select Line", FL_CTRL + 'l', menu_cb, reinterpret_cast<void *>(15));
    menu_bar->add("Viem/Next Document", FL_CTRL + FL_Tab, menu_cb, reinterpret_cast<void *>(11));
    menu_bar->add("Viem/Previous Document", FL_CTRL + FL_SHIFT + FL_Tab, menu_cb, reinterpret_cast<void *>(16));
    menu_bar->add("Viem/Increase Font Size", FL_CTRL + '+', menu_cb, reinterpret_cast<void *>(17));
    menu_bar->add("Viem/Decrease Font Size", FL_CTRL + '-', menu_cb, reinterpret_cast<void *>(18));
    menu_bar->add("Settings/Theme: Kate Breeze Dark");
    menu_bar->add("Help/About Wolfit", 0, menu_cb, reinterpret_cast<void *>(19));
    Fl_Menu_Item *items = const_cast<Fl_Menu_Item *>(menu_bar->menu());
    for (int index = 0; index < menu_bar->size(); ++index) items[index].labelcolor(settings.theme.text);
    tabs = new TabStrip(8, 30, 1084, 32, &documents, &settings);
    tabs->callbacks(select_tab, close_tab, reorder_tab, nullptr);
    top_container->end();
    editor = new WolfitEditor(0, 66, 1100, 626);
    editor->box(FL_FLAT_BOX); editor->textfont(FL_COURIER); editor->apply_theme();
    SurfaceGroup *bottom_container = new SurfaceGroup(0, 692, 1100, 28);
    status_bar = new StatusBar(8, 692, 1084, 28);
    status_bar->box(FL_FLAT_BOX); status_bar->color(settings.theme.surface); status_bar->labelcolor(settings.theme.muted);
    status_bar->labelsize(11); status_bar->align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE);
    search_overlay = new Fl_Group(380, 686, 400, 36);
    search_overlay->box(FL_ROUNDED_BOX); search_overlay->color(settings.theme.tab_active);
    search_input = new SearchInput(390, 692, 164, 24);
    search_input->box(FL_BORDER_BOX); search_input->color(0x262A2E00); search_input->textcolor(FL_WHITE);
    search_input->selection_color(0x80808000); search_input->textsize(14); search_input->when(FL_WHEN_CHANGED);
    search_input->callback(search_cb);
    search_case = new Fl_Check_Button(560, 692, 32, 24, "Aa");
    search_case->box(FL_FLAT_BOX); search_case->color(settings.theme.tab_active); search_case->labelcolor(settings.theme.muted); search_case->labelsize(11);
    search_case->callback(search_cb);
    search_count = new Fl_Box(598, 692, 72, 24, "No matches");
    search_count->labelcolor(settings.theme.muted); search_count->labelsize(11); search_count->align(FL_ALIGN_CENTER);
    Fl_Button *previous = new Fl_Button(674, 692, 28, 24, "<");
    Fl_Button *next = new Fl_Button(704, 692, 28, 24, ">");
    Fl_Button *close = new Fl_Button(738, 692, 28, 24, "x");
    Fl_Button *buttons[] = {previous, next, close};
    for (Fl_Button *button : buttons) {
        button->box(FL_FLAT_BOX); button->color(0x383C4000); button->labelcolor(FL_WHITE); button->labelsize(14);
    }
    previous->callback(search_action_cb, reinterpret_cast<void *>(-1));
    next->callback(search_action_cb, reinterpret_cast<void *>(1));
    close->callback(search_action_cb, nullptr);
    search_overlay->end();
    search_overlay->hide();
    bottom_container->end();
    restore_session();
    for (int index = 1; index < argc; ++index) open_document(argv[index]);
    Fl::add_handler(global_key_handler);
    window->resizable(editor);
    window->end();
    start_ipc();
}

void shutdown_editor_ui() {
    save_session();
    ipc_running = false;
    if (ipc_socket >= 0) shutdown(ipc_socket, SHUT_RDWR);
    if (ipc_thread.joinable()) ipc_thread.join();
    std::vector<IpcRequest> requests;
    {
        std::lock_guard<std::mutex> lock(ipc_mutex);
        requests.swap(ipc_requests);
    }
    for (const IpcRequest &request : requests) close(request.client);
    unlink("/tmp/wolfit.sock");
}
