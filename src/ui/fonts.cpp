#include "fonts.h"

#include <imgui.h>
#include <stb/stb_truetype.h>

#ifdef IMGUI_ENABLE_FREETYPE
#include <imgui_freetype.h>
#endif

#ifdef LOGIC_HAVE_FONTCONFIG
#include <fontconfig/fontconfig.h>
#endif

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <optional>
#include <system_error>

namespace logic::ui {

namespace fs = std::filesystem;

namespace {

ImFont* g_mono = nullptr;

bool contains_nocase(std::string_view text, std::string_view part)
{
    auto it = std::search(text.begin(), text.end(), part.begin(), part.end(),
        [](char a, char b) { return std::tolower((unsigned char)a) == std::tolower((unsigned char)b); });
    return it != text.end();
}

bool less_nocase(const std::string& a, const std::string& b)
{
    return std::lexicographical_compare(a.begin(), a.end(), b.begin(), b.end(),
        [](char x, char y) { return std::tolower((unsigned char)x) < std::tolower((unsigned char)y); });
}

// reads the names and flags of the fonts in a file without loading all of
// it: the table directory and a few tables
class sfnt_reader {
public:
    explicit sfnt_reader(const fs::path& p)
        : in(p, std::ios::binary)
    {
    }

    // whether the font's latin letters and digits are equally wide
    bool mono(int index)
    {
        if (!in) return false;
        auto f = face(u32(0) == tag("ttcf") ? u32(12 + 4 * (uint64_t)index) : 0);
        return f && f->mono;
    }

    std::vector<font_face> faces(const fs::path& file)
    {
        std::vector<font_face> r;
        if (!in) return r;
        if (u32(0) == tag("ttcf")) {
            uint32_t n = std::min<uint32_t>(u32(8), 256);
            for (uint32_t k = 0; k < n && in; k++)
                if (auto f = face(u32(12 + 4 * k))) r.push_back({f->family, f->style, {file, (int)k}, f->mono});
        } else if (auto f = face(0)) {
            r.push_back({f->family, f->style, {file, 0}, f->mono});
        }
        return r;
    }

private:
    static uint32_t tag(const char* t) { return (uint32_t)t[0] << 24 | (uint32_t)t[1] << 16 | (uint32_t)t[2] << 8 | (uint32_t)t[3]; }

    bool read(uint64_t at, void* buf, size_t n)
    {
        in.clear();
        in.seekg((std::streamoff)at);
        in.read((char*)buf, (std::streamsize)n);
        return (size_t)in.gcount() == n;
    }

    uint16_t u16(uint64_t at)
    {
        unsigned char b[2] = {};
        read(at, b, 2);
        return (uint16_t)(b[0] << 8 | b[1]);
    }

    uint32_t u32(uint64_t at)
    {
        unsigned char b[4] = {};
        read(at, b, 4);
        return (uint32_t)b[0] << 24 | (uint32_t)b[1] << 16 | (uint32_t)b[2] << 8 | b[3];
    }

    struct info {
        std::string family, style;
        bool mono;
    };

    std::optional<info> face(uint32_t offset)
    {
        uint32_t version = u32(offset);
        if (version != 0x00010000 && version != tag("OTTO") && version != tag("true")) return std::nullopt;
        uint32_t name = 0, post = 0, cmap = 0, hhea = 0, hmtx = 0;
        bool outlines = false;
        uint16_t tables = u16(offset + 4);
        for (uint16_t t = 0; t < tables; t++) {
            uint64_t rec = offset + 12 + 16 * t;
            uint32_t id = u32(rec);
            if (id == tag("name")) name = u32(rec + 8);
            else if (id == tag("post")) post = u32(rec + 8);
            else if (id == tag("cmap")) cmap = u32(rec + 8);
            else if (id == tag("hhea")) hhea = u32(rec + 8);
            else if (id == tag("hmtx")) hmtx = u32(rec + 8);
            // glyf: truetype, CFF: opentype. stb_truetype can't draw CFF2
            // and fonts with only bitmaps (e.g. color emoji)
            else if (id == tag("glyf") || id == tag("CFF ")) outlines = true;
        }
        // most fonts of other scripts can't show the interface
        if (!name || !outlines || (cmap && glyph(cmap, 'A') == 0)) return std::nullopt;

        info r;
        // the flag in 'post' isn't always set, e.g. fonts with some wide
        // symbols: compare a few advances too
        r.mono = post && u32(post + 12) != 0;
        if (!r.mono && cmap && hhea && hmtx) {
            uint16_t metrics = u16(hhea + 34);
            int first = -1;
            r.mono = metrics > 0;
            for (char c : {'i', 'W', 'M', '0'}) {
                int g = glyph(cmap, c);
                if (g <= 0) continue;
                int advance = u16(hmtx + 4 * (uint64_t)std::min<int>(g, metrics - 1));
                if (first < 0) first = advance;
                if (advance != first || advance == 0) r.mono = false;
            }
            if (first < 0) r.mono = false;
        }
        // typographic names (16, 17) group the weights under one family
        r.family = string(name, 16);
        if (r.family.empty()) r.family = string(name, 1);
        r.style = string(name, 17);
        if (r.style.empty()) r.style = string(name, 2);
        if (r.family.empty()) return std::nullopt;
        return r;
    }

    // glyph of a character in the unicode subtable of 'cmap', 0 if it has
    // none, -1 if the font has no subtable this can read
    int glyph(uint32_t table, uint32_t ch)
    {
        uint16_t count = u16(table + 2);
        for (uint16_t k = 0; k < count; k++) {
            uint64_t rec = table + 4 + 8 * k;
            uint16_t platform = u16(rec), encoding = u16(rec + 2);
            if (platform != 0 && !(platform == 3 && (encoding == 1 || encoding == 10))) continue;
            uint64_t sub = table + u32(rec + 4);
            uint16_t format = u16(sub);
            if (format == 4) {
                // segments sorted by their last character
                uint16_t segments = u16(sub + 6) / 2;
                for (uint16_t i = 0; i < segments; i++) {
                    if (u16(sub + 14 + 2 * i) < ch) continue;
                    uint16_t start = u16(sub + 16 + 2 * segments + 2 * i);
                    if (start > ch) return 0;
                    uint16_t delta = u16(sub + 16 + 4 * segments + 2 * i);
                    uint64_t range = sub + 16 + 6 * segments + 2 * i;
                    uint16_t offset = u16(range);
                    if (!offset) return (ch + delta) & 0xFFFF;
                    uint16_t g = u16(range + offset + 2 * (ch - start));
                    return g ? (g + delta) & 0xFFFF : 0;
                }
                return 0;
            }
            if (format == 12) {
                uint32_t groups = std::min<uint32_t>(u32(sub + 12), 1 << 20);
                for (uint32_t i = 0; i < groups; i++) {
                    uint64_t g = sub + 16 + 12 * i;
                    if (u32(g) <= ch && ch <= u32(g + 4)) return (int)(u32(g + 8) + (ch - u32(g)));
                }
                return 0;
            }
        }
        return -1;
    }

    // a name from the 'name' table as utf-8: english windows names first,
    // then other windows names, then mac roman ones
    std::string string(uint32_t table, uint16_t id)
    {
        uint16_t count = u16(table + 2);
        uint32_t storage = table + u16(table + 4);
        int best = 0;
        uint16_t best_platform = 0, len = 0, off = 0;
        for (uint16_t k = 0; k < count; k++) {
            uint64_t rec = table + 6 + 12 * k;
            if (u16(rec + 6) != id) continue;
            uint16_t platform = u16(rec), encoding = u16(rec + 2), language = u16(rec + 4);
            int score = 0;
            if (platform == 3 && (encoding == 1 || encoding == 10)) score = language == 0x409 ? 3 : 2;
            else if (platform == 1 && encoding == 0 && language == 0) score = 1;
            if (score > best) {
                best = score;
                best_platform = platform;
                len = u16(rec + 8);
                off = u16(rec + 10);
            }
        }
        if (!best) return {};

        std::string raw(len, '\0');
        if (!read(storage + off, raw.data(), len)) return {};
        std::string r;
        auto put = [&](uint32_t cp) {
            if (cp < 0x80) {
                r += (char)cp;
            } else if (cp < 0x800) {
                r += (char)(0xC0 | cp >> 6);
                r += (char)(0x80 | (cp & 0x3F));
            } else if (cp < 0x10000) {
                r += (char)(0xE0 | cp >> 12);
                r += (char)(0x80 | (cp >> 6 & 0x3F));
                r += (char)(0x80 | (cp & 0x3F));
            } else {
                r += (char)(0xF0 | cp >> 18);
                r += (char)(0x80 | (cp >> 12 & 0x3F));
                r += (char)(0x80 | (cp >> 6 & 0x3F));
                r += (char)(0x80 | (cp & 0x3F));
            }
        };
        if (best_platform == 1) {
            // close enough for font names
            for (unsigned char c : raw)
                put(c);
            return r;
        }
        // utf-16 big endian
        for (size_t k = 0; k + 1 < raw.size(); k += 2) {
            uint32_t cp = (unsigned char)raw[k] << 8 | (unsigned char)raw[k + 1];
            if (cp >= 0xD800 && cp < 0xDC00 && k + 3 < raw.size()) {
                uint32_t lo = (unsigned char)raw[k + 2] << 8 | (unsigned char)raw[k + 3];
                if (lo >= 0xDC00 && lo < 0xE000) {
                    cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
                    k += 2;
                }
            }
            put(cp);
        }
        return r;
    }

    std::ifstream in;
};

#ifdef LOGIC_HAVE_FONTCONFIG

std::string pattern_string(FcPattern* p, const char* object)
{
    FcChar8* s = nullptr;
    return FcPatternGetString(p, object, 0, &s) == FcResultMatch ? (const char*)s : "";
}

// a face stb_truetype can draw, as fontconfig describes it
std::optional<font_face> fontconfig_face(FcPattern* p)
{
    FcBool color = FcFalse;
    if (FcPatternGetBool(p, FC_COLOR, 0, &color) == FcResultMatch && color) return std::nullopt;
    auto format = pattern_string(p, FC_FONTFORMAT);
    if (format != "TrueType" && format != "CFF") return std::nullopt;
    int index = 0;
    FcPatternGetInteger(p, FC_INDEX, 0, &index);
    // the upper bits are a named instance of a variable font, stb_truetype
    // only draws its default instance
    if (index >> 16) return std::nullopt;
    // most fonts of other scripts can't show the interface
    FcCharSet* chars = nullptr;
    if (FcPatternGetCharSet(p, FC_CHARSET, 0, &chars) == FcResultMatch && !FcCharSetHasChar(chars, 'A')) return std::nullopt;

    font_face f;
    f.ref.file = pattern_string(p, FC_FILE);
    f.ref.index = index;
    f.family = pattern_string(p, FC_FAMILY);
    f.style = pattern_string(p, FC_STYLE);
    int spacing = FC_PROPORTIONAL;
    FcPatternGetInteger(p, FC_SPACING, 0, &spacing);
    if (f.ref.file.empty() || f.family.empty()) return std::nullopt;
    // fontconfig calls fonts with a few wide symbols proportional
    f.mono = spacing >= FC_MONO || sfnt_reader(f.ref.file).mono(index);
    return f;
}

std::vector<font_face> fontconfig_fonts()
{
    std::vector<font_face> r;
    if (!FcInit()) return r;
    auto pattern = FcPatternCreate();
    FcPatternAddBool(pattern, FC_OUTLINE, FcTrue);
    auto objects = FcObjectSetBuild(FC_FAMILY, FC_STYLE, FC_FILE, FC_INDEX, FC_SPACING, FC_FONTFORMAT, FC_COLOR, FC_CHARSET, nullptr);
    if (auto set = FcFontList(nullptr, pattern, objects)) {
        for (int k = 0; k < set->nfont; k++)
            if (auto f = fontconfig_face(set->fonts[k])) r.push_back(std::move(*f));
        FcFontSetDestroy(set);
    }
    FcObjectSetDestroy(objects);
    FcPatternDestroy(pattern);
    return r;
}

font_ref fontconfig_mono()
{
    font_ref r;
    if (!FcInit()) return r;
    auto pattern = FcNameParse((const FcChar8*)"monospace");
    FcConfigSubstitute(nullptr, pattern, FcMatchPattern);
    FcDefaultSubstitute(pattern);
    FcResult result;
    if (auto match = FcFontMatch(nullptr, pattern, &result)) {
        if (auto f = fontconfig_face(match)) r = f->ref;
        FcPatternDestroy(match);
    }
    FcPatternDestroy(pattern);
    return r;
}

#else

std::vector<fs::path> font_dirs()
{
    std::vector<fs::path> r;
    auto env = [](const char* name) -> fs::path {
        auto v = std::getenv(name);
        return v ? fs::path(v) : fs::path();
    };
#ifdef _WIN32
    auto windir = env("WINDIR");
    r.push_back((windir.empty() ? fs::path("C:\\Windows") : windir) / "Fonts");
    if (auto local = env("LOCALAPPDATA"); !local.empty()) r.push_back(local / "Microsoft" / "Windows" / "Fonts");
#elif defined(__APPLE__)
    r = {"/System/Library/Fonts", "/Library/Fonts"};
    if (auto home = env("HOME"); !home.empty()) r.push_back(home / "Library" / "Fonts");
#else
    r = {"/usr/share/fonts", "/usr/local/share/fonts"};
    if (auto data = env("XDG_DATA_HOME"); !data.empty()) r.push_back(data / "fonts");
    if (auto home = env("HOME"); !home.empty()) {
        r.push_back(home / ".local" / "share" / "fonts");
        r.push_back(home / ".fonts");
    }
#endif
    return r;
}

std::vector<font_face> scanned_fonts()
{
    std::vector<font_face> r;
    for (auto& dir : font_dirs()) {
        std::error_code ec;
        for (auto it = fs::recursive_directory_iterator(dir, fs::directory_options::skip_permission_denied, ec); !ec && it != fs::recursive_directory_iterator(); it.increment(ec)) {
            auto ext = it->path().extension().string();
            std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return std::tolower(c); });
            if (ext != ".ttf" && ext != ".otf" && ext != ".ttc" && ext != ".otc") continue;
            for (auto& f : sfnt_reader(it->path()).faces(it->path()))
                r.push_back(std::move(f));
        }
    }
    return r;
}

font_ref scanned_mono()
{
    // the usual monospace fonts of each system, by file name
    static const char* const names[] = {"consola.ttf", "cour.ttf", "Menlo.ttc", "Monaco.ttf", "DejaVuSansMono.ttf", "LiberationMono-Regular.ttf"};
    for (auto& dir : font_dirs())
        for (auto n : names) {
            std::error_code ec;
            if (fs::is_regular_file(dir / n, ec)) return {dir / n, 0};
        }
    // linux keeps them in folders: any regular monospace font
    for (auto& f : scanned_fonts())
        if (f.mono && (f.style == "Regular" || f.style == "Book")) return f.ref;
    return {};
}

#endif

}

std::string to_string(const font_ref& f)
{
    return f.builtin() ? std::string() : std::to_string(f.index) + ":" + f.file.generic_string();
}

font_ref parse_font_ref(std::string_view s)
{
    auto colon = s.find(':');
    if (colon == std::string_view::npos || colon == 0) return {};
    int index = 0;
    for (char c : s.substr(0, colon)) {
        if (c < '0' || c > '9') return {};
        index = index * 10 + (c - '0');
    }
    return {fs::path(std::string(s.substr(colon + 1))), index};
}

std::string font_face::name() const
{
    return style.empty() ? family : family + " " + style;
}

std::vector<font_face> system_fonts()
{
#ifdef LOGIC_HAVE_FONTCONFIG
    auto r = fontconfig_fonts();
#else
    auto r = scanned_fonts();
#endif
    std::sort(r.begin(), r.end(), [](auto& a, auto& b) {
        if (a.family != b.family) return less_nocase(a.family, b.family);
        return less_nocase(a.style, b.style);
    });
    return r;
}

font_ref default_mono_font()
{
    // asked once, the font settings ask every time they change
    static const font_ref r =
#ifdef LOGIC_HAVE_FONTCONFIG
        fontconfig_mono();
#else
        scanned_mono();
#endif
    return r;
}

std::vector<unsigned char> read_font(const font_ref& f, std::string& error)
{
    std::ifstream in(f.file, std::ios::binary);
    if (!in) {
        error = "can't open the file";
        return {};
    }
    std::vector<unsigned char> data((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());

    int offset = data.empty() ? -1 : stbtt_GetFontOffsetForIndex(data.data(), f.index);
    stbtt_fontinfo info;
    if (offset < 0 && f.index > 0 && stbtt_GetFontOffsetForIndex(data.data(), 0) >= 0) {
        error = "the file has no font number " + std::to_string(f.index);
        return {};
    }
    if (offset < 0 || !stbtt_InitFont(&info, data.data(), offset)) {
        error = "not a truetype or opentype font";
        return {};
    }
    if (!stbtt_FindGlyphIndex(&info, 'A')) {
        error = "the font has no latin letters";
        return {};
    }
    return data;
}

bool font_picker::gui(const char* label, font_ref& f, const std::vector<font_face>& fonts, const char* builtin)
{
    using namespace ImGui;

    std::string preview = builtin;
    if (!f.builtin()) {
        auto it = std::find_if(fonts.begin(), fonts.end(), [&](auto& face) { return face.ref == f; });
        preview = it != fonts.end() ? it->name() : f.file.filename().string();
    }

    if (!BeginCombo(label, preview.c_str(), ImGuiComboFlags_HeightLarge)) return false;
    if (!f.builtin()) SetItemTooltip("%s", f.file.string().c_str());

    bool changed = false;
    if (IsWindowAppearing()) SetKeyboardFocusHere();
    char buf[128];
    std::snprintf(buf, sizeof(buf), "%s", filter.c_str());
    SetNextItemWidth(GetFontSize() * 12);
    if (InputTextWithHint("##filter", "Filter", buf, sizeof(buf))) filter = buf;
    SameLine();
    Checkbox("Monospace only", &mono_only);

    if (Selectable(builtin, f.builtin())) {
        f = {};
        changed = true;
    }

    std::vector<const font_face*> shown;
    for (auto& face : fonts)
        if ((!mono_only || face.mono) && contains_nocase(face.name(), filter)) shown.push_back(&face);

    if (fonts.empty()) TextDisabled("No fonts found on the system");
    BeginChild("fonts", {GetFontSize() * 24, GetTextLineHeightWithSpacing() * 16});
    ImGuiListClipper clipper;
    clipper.Begin((int)shown.size());
    while (clipper.Step()) {
        for (int k = clipper.DisplayStart; k < clipper.DisplayEnd; k++) {
            auto face = shown[k];
            PushID(k);
            bool selected = face->ref == f;
            if (Selectable(face->name().c_str(), selected)) {
                f = face->ref;
                changed = true;
                // a child of the popup, selectables only close it directly
                CloseCurrentPopup();
            }
            if (selected && IsWindowAppearing()) SetScrollHereY();
            SetItemTooltip("%s", face->ref.file.string().c_str());
            PopID();
        }
    }
    EndChild();

    EndCombo();
    return changed;
}

void load_fonts(const font_ref& ui, const font_ref& mono, std::string_view builtin_ttf, std::vector<std::string>& errors,
    std::string_view icons_ttf)
{
    auto& io = ImGui::GetIO();
    io.FontDefault = nullptr;
    io.Fonts->ClearFonts();
    g_mono = nullptr;
#ifdef IMGUI_ENABLE_FREETYPE
    io.Fonts->FontLoaderFlags = ImGuiFreeTypeLoaderFlags_LightHinting;
#endif

    auto add = [&](const font_ref& f, const char* what) -> ImFont* {
        std::string error;
        auto data = read_font(f, error);
        if (data.empty()) {
            errors.push_back(std::string(what) + " font " + f.file.string() + ": " + error + ", using the default font");
            return nullptr;
        }
        // the atlas frees it
        void* mem = IM_ALLOC(data.size());
        std::memcpy(mem, data.data(), data.size());
        ImFontConfig config;
        config.FontNo = f.index;
        return io.Fonts->AddFontFromMemoryTTF(mem, (int)data.size(), 0.0f, &config);
    };

    // the first font added is the default
    if (ui.builtin() || !add(ui, "interface")) {
        ImFontConfig config;
        config.FontDataOwnedByAtlas = false;
        io.Fonts->AddFontFromMemoryTTF((void*)builtin_ttf.data(), (int)builtin_ttf.size(), 0.0f, &config);
    }
    // the icons are glyphs of the ui font, whichever it is. icon_button
    // centers them by their drawn shape.
    if (!icons_ttf.empty()) {
        ImFontConfig config;
        config.MergeMode = true;
        config.FontDataOwnedByAtlas = false;
        io.Fonts->AddFontFromMemoryTTF((void*)icons_ttf.data(), (int)icons_ttf.size(), 0.0f, &config);
    }

    if (!mono.builtin()) g_mono = add(mono, "monospace");
    if (!g_mono && !default_mono_font().builtin()) g_mono = add(default_mono_font(), "monospace");
    // proggy vector, imgui's own
    if (!g_mono) g_mono = io.Fonts->AddFontDefaultVector();
}

void set_font_size(float size)
{
    auto& style = ImGui::GetStyle();
    style.FontSizeBase = size;
    // NewFrame applies this one
    style._NextFrameFontSizeBase = size;
}

ImFont* mono_font()
{
    return g_mono;
}

mono_scope::mono_scope()
{
    // keeps the size
    ImGui::PushFont(g_mono, 0.0f);
}

mono_scope::~mono_scope()
{
    ImGui::PopFont();
}

}
