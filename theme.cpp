#include "theme.h"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

static Theme kate_breeze_dark() {
    return {0x2A2E3200, 0x23262900, 0xFCFCFC00, 0xA0A5AA00, 0xFFFFFF00, 0x2D313600,
            0x32373C00, 0x3A5D7200, 0x6C737A00, 0xA7B2C000, 0xD7E1EB00, 0x59B77D00, 0xE74C3C00};
}

static Theme one_dark() {
    return {0x21252B00, 0x282C3400, 0xABB2BF00, 0x5C637000, 0x61AFEF00, 0x30364100,
            0x2C313C00, 0x3E587000, 0x7C859900, 0xAAB4C800, 0xC8D2E600, 0x5AAE7A00, 0xE74C3C00};
}

static Theme catppuccin() {
    return {0x18182500, 0x1E1E2E00, 0xCDD6F400, 0x7F849C00, 0x89B4FA00, 0x31324400,
            0x29293A00, 0x45475A00, 0xA6ADC800, 0xBAC2DE00, 0xD5DDF500, 0xA6D18900, 0xE74C3C00};
}

Theme make_theme(ThemePreset preset) {
    if (preset == ThemePreset::OneDark) return one_dark();
    if (preset == ThemePreset::Catppuccin) return catppuccin();
    return kate_breeze_dark();
}

Settings load_settings() {
    Settings settings = {make_theme(ThemePreset::KateBreezeDark), 17, 4, true};
    const char *home = std::getenv("HOME");
    if (home == nullptr) return settings;

    std::ifstream file((std::string(home) + "/.config/wolfit/config").c_str());
    std::string line;
    while (std::getline(file, line)) {
        if (line == "theme=one-dark") settings.theme = make_theme(ThemePreset::OneDark);
        if (line == "theme=catppuccin") settings.theme = make_theme(ThemePreset::Catppuccin);
        if (line == "theme=kate-breeze-dark") settings.theme = make_theme(ThemePreset::KateBreezeDark);
        if (line == "scrollbars=system") settings.minimal_scrollbars = false;
        if (line.rfind("font_size=", 0) == 0) {
            const int size = std::atoi(line.c_str() + 10);
            if (size >= 4 && size <= 2048) settings.font_size = size;
        }
        if (line.rfind("indent_size=", 0) == 0) {
            const int size = std::atoi(line.c_str() + 12);
            if (size >= 1 && size <= 8) settings.indent_size = size;
        }
    }
    return settings;
}

void save_font_size(int font_size) {
    const char *home = std::getenv("HOME");
    if (home == nullptr || font_size < 4 || font_size > 2048) return;
    const std::string directory = std::string(home) + "/.config/wolfit";
    std::filesystem::create_directories(directory);
    const std::string path = directory + "/config";
    std::ifstream input(path.c_str());
    std::vector<std::string> lines;
    std::string line;
    bool found = false;
    while (std::getline(input, line)) {
        if (line.rfind("font_size=", 0) == 0) {
            lines.push_back("font_size=" + std::to_string(font_size));
            found = true;
        } else lines.push_back(line);
    }
    if (!found) lines.push_back("font_size=" + std::to_string(font_size));
    std::ofstream output(path.c_str(), std::ios::trunc);
    for (const std::string &entry : lines) output << entry << '\n';
}
