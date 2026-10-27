// fonts: the ones installed on the system, choosing one and loading them
// into imgui

#pragma once

#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

struct ImFont;

namespace logic::ui {

// a font in a file, index is the font in a collection (.ttc). no file means
// the app's built-in font.
struct font_ref {
    std::filesystem::path file;
    int index = 0;

    bool builtin() const { return file.empty(); }
    bool operator==(const font_ref&) const = default;
};

// "index:file" for settings files, empty for the built-in font
std::string to_string(const font_ref& f);
font_ref parse_font_ref(std::string_view s);

struct font_face {
    std::string family;
    std::string style;
    font_ref ref;
    // all glyphs are equally wide
    bool mono = false;

    // "family style"
    std::string name() const;
};

// the installed fonts that can be drawn (truetype and opentype outlines),
// sorted by name. asks fontconfig if the build has it, scans the usual font
// folders otherwise. takes a moment, keep the result.
std::vector<font_face> system_fonts();

// the system's default monospace font, the built-in font if there is none
font_ref default_mono_font();

// the bytes of the font file. empty if it can't be read or holds no font
// that can be drawn, error says why.
std::vector<unsigned char> read_font(const font_ref& f, std::string& error);

// a combo box listing the fonts with a filter and a "monospace only" switch
class font_picker {
public:
    explicit font_picker(bool mono_only = false)
        : mono_only(mono_only)
    {
    }

    // builtin: the name shown for the built-in choice. true when f changed.
    bool gui(const char* label, font_ref& f, const std::vector<font_face>& fonts, const char* builtin);

private:
    std::string filter;
    bool mono_only;
};

// replaces imgui's fonts, call it between frames. ui becomes the default
// font, mono the one of mono_font(). the built-in ui font is builtin_ttf (not
// copied, must stay alive), the built-in mono font is the system's default
// monospace font or imgui's own. fonts that can't be loaded are replaced by
// the built-in ones, errors says why. glyphs are made when they are first
// drawn, at any size. icons_ttf (not copied either) is merged into the ui
// font, see icons.h. none if empty.
void load_fonts(const font_ref& ui, const font_ref& mono, std::string_view builtin_ttf, std::vector<std::string>& errors,
    std::string_view icons_ttf = {});

// height of the text in pixels from the next frame on, for all fonts
void set_font_size(float size);

// for code, addresses and memory dumps: all glyphs are equally wide. the
// default font until load_fonts.
ImFont* mono_font();

// draws with mono_font() while it lives
struct mono_scope {
    mono_scope();
    ~mono_scope();
    mono_scope(const mono_scope&) = delete;
    mono_scope& operator=(const mono_scope&) = delete;
};

}
