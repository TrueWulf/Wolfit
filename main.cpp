#include "editor.h"
#include "theme.h"
#include "version.h"

#include <FL/Fl.H>

#include <string>

#ifdef __OpenBSD__
#include <unistd.h>
#endif

#ifdef __linux__
#include <sys/prctl.h>
#include <linux/seccomp.h>
#include <linux/filter.h>
#include <sys/syscall.h>
#endif

static void apply_sandbox() {
#ifdef __OpenBSD__
    if (pledge("stdio rpath wpath cpath fattr unix tmppath", nullptr) != 0) {
        return;
    }
#endif
#ifdef __linux__
    prctl(PR_SET_NO_NEW_PRIVS, 1, 0, 0, 0);
#endif
}

int main(int argc, char **argv) {
    apply_sandbox();
    Fl::lock();
    const Settings settings = load_settings();
    Fl::scheme("base");
    Fl::background((settings.theme.surface >> 24) & 0xff, (settings.theme.surface >> 16) & 0xff, (settings.theme.surface >> 8) & 0xff);
    Fl::background2((settings.theme.editor >> 24) & 0xff, (settings.theme.editor >> 16) & 0xff, (settings.theme.editor >> 8) & 0xff);
    Fl::foreground((settings.theme.text >> 24) & 0xff, (settings.theme.text >> 16) & 0xff, (settings.theme.text >> 8) & 0xff);
    Fl::set_color(FL_SELECTION_COLOR, settings.theme.tab_hover);

    std::string title = "Wolfit ";
    title += WOLFIT_VERSION;
    Fl_Double_Window window(1100, 720, title.c_str());
    create_editor_ui(&window, argc, argv);
    window.show(argc, argv);
    const int result = Fl::run();
    shutdown_editor_ui();
    return result;
}
