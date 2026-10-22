# Development log

Entries in chronological order, the newest last.

## 2026-10-21 16:55 +0300

### Changes made

- **OpenGL 3.3 core** instead of 4.5 (`8bd2cfd`, and `d0d53db` in the Hd submodule):
  - shaders are GLSL 330 with `GL_ARB_explicit_uniform_location`
  - array initializer lists became constructors
  - the context asks for 3.3 core
  - debug output falls back to `GL_KHR_debug`, or is turned off with a warning
- **Review of the waveform, font size and zoom commits** (`72a1197`):
  - dense waveforms are drawn as blocks, bounded by the width
  - Follow keeps up with stepping while paused
  - waveform commands no longer restart the real-time clock
  - the embedded font isn't copied on every rebuild
- **Simulator recording** (`6bbfd08`):
  - the waveform recorder stores the shared trace type
  - it syncs only new changes to the window instead of copying the history
    every frame, which cuts the window's CPU cost from about 23% to about
    9% of a core with 4 signals (Debug build)
- **Bug fixes found while testing:**
  - shrinking the waveform window permanently narrowed the names column
  - with no history yet, Fit zoomed to a 1 ps span

### Features implemented

- **`logic-wave`**, a standalone VCD viewer (`6bbfd08`), sharing its waveform
  view with the simulator's waveform window:
  - **`logic_wave` library** (no GUI):
    - traces with their own timebase (fs to s)
    - VCD reading, with std_logic states, reals, strings, `$dumpoff`, and
      line numbers in errors
    - VCD writing
    - transition and value search
    - JSON sessions
  - **`logic_ui` library:** the waveform view, file dialogs and the ImGui style.
  - **View:**
    - analog display of reals and buses (step or linear)
    - cursors A/B that snap to transitions, with the time and frequency
      between them
    - previous/next transition
    - markers
    - zooming to a dragged range
    - value search with `?` wildcards
    - an overview bar of the whole time range
    - rows that can be reordered, colored and sized
  - **Viewer app:**
    - a signal tree with a filter
    - loading on a thread with progress and cancel
    - reloading that keeps the layout
    - opening and saving sessions
    - dropping files onto the window
  - **Simulator window:** exports its recording to VCD.
- **README.md:** building, running, the viewer's shortcuts and the source
  layout.

### Recommended features

- FST files, for the large dumps of Verilator and GTKWave.
- Signal groups and divider rows.
- Named values, e.g. the states of a state machine.
- Counting edges or clock cycles between cursors A and B.
- Reloading automatically when the file changes.
- Comparing two files.
- Exporting the visible range as CSV or PNG.

### Outstanding issues

- Shaders need `GL_ARB_explicit_uniform_location`, which macOS doesn't
  have.
- The simulator keeps only the newest 10000 changes per signal, about half
  a second of a fast clock. Exported VCD files are cut off the same way.
- VCD files are read into memory whole, so a very large file needs memory
  about twice its size while loading.
- Fit spans from the earliest change of any signal. If one signal never
  changes, Fit zooms out to its start even when the others only have
  recent history.
- Right-clicking an instance's pin stub offers that instance's pins rather
  than the wire.
- The compute shader constructor of `Hd::Shader` needs GL 4.3. Nothing uses it.

### Next steps

- Look up uniform locations by name, to drop the extension requirement.
- Make the simulator's history limit configurable, or store it more
  compactly so it can be longer.
- Stream large VCD files, and add FST.

## 2026-10-22 22:40 +0300

### Changes made

- **Hd submodule:**
  - `Hd::Font` takes a face index for collections (`.ttc`)
  - `Hd::Font` throws `std::runtime_error` on data that isn't a font,
    instead of reading garbage
  - `Renderer2D::setFont()` swaps the canvas font at run time
- **The debugger, terminal and Messages panel** draw with the monospace
  font, so hex dumps and disassembly line up in columns.
- **The Settings window** fits its contents when opened. An older
  imgui.ini kept it too short for the new rows.
- **`logic-wave`** loads its fonts through the shared `ui::load_fonts`.
- **Bug fix, crash on exit:** `Hd::Gui` is the app's first member, so ImGui
  saved imgui.ini from its destructor after the members the settings handler
  reads were already destroyed. This was undefined behaviour before, and
  crashed (`std::bad_alloc` / SIGSEGV) once the handler read strings. Both
  apps now save the settings in their own destructor.

### Features implemented

- **Font settings** (View > Settings > Fonts), saved in imgui.ini as
  `UiFont`, `MonoFont` and `CanvasFont` (`index:path`, empty = built-in):
  - **Interface:** ImGui's default font. Built-in: Roboto.
  - **Monospace:** debugger, terminal, messages. Built-in: the system's
    monospace font (fontconfig's `monospace`, Consolas/Menlo on
    Windows/macOS), or ImGui's ProggyClean as a last resort.
  - **Canvas:** the SDF font of schematics, symbols and the component bar.
    Built-in: Roboto.
  - Changes apply right away, between frames.
- **Font discovery** (`src/ui/fonts.{h,cpp}` in `logic_ui`):
  - through fontconfig when CMake finds it (`LOGIC_USE_FONTCONFIG`, on by
    default)
  - otherwise by scanning the system and user font folders, reading only
    the sfnt table directory, `name`, `post`, `cmap`, `hhea` and `hmtx`
  - keeps only faces stb_truetype can draw: TrueType or CFF outlines, no
    color or bitmap-only fonts, no variable-font named instances, and only
    fonts with Latin letters (on the dev machine 457 of 2353 faces)
  - takes 60 ms with fontconfig and about 230 ms with the scan
- **Monospace detection:** fontconfig's spacing, or the `post`
  isFixedPitch flag, or equal advances of `i`, `W`, `M` and `0`. The last
  one catches Noto Sans Mono, which neither of the others flags.
- **Font picker:** a combo with a filter, a "Monospace only" switch (on by
  default for the monospace font), the file path as a tooltip, and a
  "Rescan fonts" button.
- **Errors:** a missing or invalid font file falls back to the built-in font
  with a message (can't open, not a font, no font number N, no Latin
  letters).

### Recommended features

- Font settings and a monospace waveform value font in `logic-wave`.
- Separate sizes for the monospace font, and for canvas text relative to
  the symbols.
- Previewing each font in its own face in the picker, loading on demand.
- More glyph ranges in the ImGui atlas (Turkish, Greek), matching what the
  canvas font already covers.
- Fallback fonts merged into the atlas for symbols the chosen font lacks.
- Saving fonts by family and style too, so the setting survives when the
  file moves.

### Outstanding issues

- The bundled ImGui (1.92.0 WIP) has no dynamic font atlas, so each font
  change rebuilds the atlas. That takes a moment with large fonts such as
  CJK collections.
- The ImGui atlas only covers Basic Latin and Latin-1, so other letters
  show as `?` in the interface, whatever the font.
- Without fontconfig, the default monospace font is found by file name, or
  by the first regular monospace face in the scan.
- The Windows and macOS discovery paths are untested. On Linux, the scan
  agrees with fontconfig (454 vs 457 faces, the same 12 monospace families).

### Next steps

- Test font discovery on Windows (MinGW).
- Add font settings to `logic-wave`.

## 2026-10-22 22:54 +0300

### Changes made

- **Dear ImGui 1.91.95 WIP → 1.92.9b (docking)** in `Hd/deps/imgui`. It's a
  plain file drop like the earlier updates, with no local patches. I chose
  1.92.9b over the week-old 1.93.0: 1.93 changes how `ImDrawList` places
  strokes (`AddLine` loses its half-pixel offset, `AddRect`/`AddCircle`
  draw inside), and the waveform view and timeline are built from those
  calls.
- **Fonts on the 1.92 system:**
  - `ui::load_fonts` replaces the fonts with `ClearFonts()`. The backend
    uploads and updates the textures, so `DestroyFontsTexture` is gone.
  - Fonts are added without a size. `ui::set_font_size` sets
    `style.FontSizeBase`, so the font size slider no longer rebuilds
    anything and resizes live while dragging.
  - Glyphs are rasterized when first drawn, at the size in use. Text in
    the interface is no longer limited to Basic Latin and Latin-1:
    Turkish, Greek and other letters now show.
  - The last-resort monospace font is ImGui's scalable
    `AddFontDefaultVector()` instead of the bitmap ProggyClean.
  - `mono_scope` uses `PushFont(font, 0.0f)`, which keeps the current size.
- **`AddPolyline` arguments** in the waveform view now use the 1.92.8 order
  (thickness, then flags).
- The app and `logic-wave` build cleanly with
  `IMGUI_DISABLE_OBSOLETE_FUNCTIONS`, so no deprecated ImGui calls are left.
- The Settings window labels the system monospace font by family only.

### Features implemented

- Live font size changes without rebuilding fonts.
- Any Unicode letter the chosen font has shows in the interface.

### Recommended features

- Update to ImGui 1.93 separately, comparing the waveform view and timeline
  drawing before and after.
- Merge a fallback font (e.g. fontconfig's match for missing characters)
  into the interface font. Roboto has no arrows (`→` shows as `?`).
- Let the canvas font load glyphs on demand too, instead of its fixed list.
- Use `style.FontScaleDpi` for high-DPI monitors.

### Outstanding issues

- ImGuiFileDialog stays at its pinned commit, which supports 1.92 (tested:
  the open dialog of `logic-wave`). Upstream has newer fixes.
- `logic-wave <folder>` reports `basic_string::_M_replace_aux` instead of
  saying the path is a folder. This predates the update.
- Corrections to the previous entry: font changes no longer rebuild a
  whole atlas, and the interface is no longer limited to Latin-1.

### Next steps

- Plan the 1.93 update with a visual check of the waveform view.

## 2026-10-22 23:02 +0300

This entry covers the font settings and the ImGui 1.92 update of the two
previous entries in the newer log format: how they work and how they were
tested. No code changed.

### Changes made

- `CLAUDE.md`: log entries also cover how features work and how they were
  tested, keep everything the final summary says, and are written before
  committing. Commits and submodule bumps aren't listed as next steps.

### How it works

- **Font choice (`ui::font_ref`)**: a file and a face index. No file means
  the built-in font. It's stored in imgui.ini as `index:path`; the path is
  the rest of the line, so it may hold colons and spaces. `UiFont`,
  `MonoFont` and `CanvasFont` are read by the app's settings handler.
- **Font discovery (`ui::system_fonts`, `src/ui/fonts.cpp`)**:
  - fontconfig (`FcFontList` with outline fonts): keeps TrueType and CFF
    formats, drops color fonts, named instances of variable fonts
    (`index >> 16`) and fonts whose charset lacks `A`.
  - Without fontconfig, `sfnt_reader` reads the files in the system and
    user font folders with seeks: the table directory (and the `ttcf`
    header of collections), `name` (typographic names 16/17 before 1/2;
    English Windows names, then other Windows ones, then Mac Roman),
    `post`, `cmap` (formats 4 and 12), `hhea` and `hmtx`. Fonts without
    `glyf` or `CFF ` (bitmap-only, CFF2) are skipped.
  - Monospace: fontconfig spacing ≥ `FC_MONO`, or the `post`
    isFixedPitch flag, or equal advances of `i`, `W`, `M` and `0`. Both
    paths use the advance check, which neither of the other two replaces:
    fontconfig and the flag both call Noto Sans Mono proportional.
  - The default monospace font is fontconfig's match for `monospace`.
    Without fontconfig it's Consolas, Courier New, Menlo, Monaco, DejaVu
    Sans Mono or Liberation Mono by file name, else the first regular
    monospace face in the scan.
  - The list is made when the Settings window first opens, and again on
    "Rescan fonts".
- **Validation (`ui::read_font`)**: reads the whole file, then checks it with
  stb_truetype (valid face index, `stbtt_InitFont`, has `A`) before handing
  it to ImGui or Hd, so a bad file gives a message rather than a crash.
- **Interface and monospace fonts (`ui::load_fonts`)**: between frames,
  `ClearFonts()`, then the interface font first (the default), then the
  monospace one, all without a size. `ui::set_font_size` sets
  `style.FontSizeBase` and `_NextFrameFontSizeBase`. ImGui 1.92 rasterizes
  glyphs on demand and the OpenGL backend updates the texture.
  `ui::mono_scope` pushes the monospace font at the current size around
  the disassembly, memory dump, terminal screen and messages.
- **Canvas font**: `Hd::Renderer2D::setFont()` builds a new SDF atlas
  (`Hd::Font`, 48 px glyphs) from the bytes. `Hd::Font` copies only up to
  the end of its last table, so the file buffer can be freed. A font that
  fails keeps its setting and draws with Roboto, like the other two.
- **Font picker (`ui::font_picker`)**: a combo with the built-in choice, a
  filter, a "Monospace only" switch and a clipped list (`ImGuiListClipper`)
  of the faces, with the file as a tooltip. The Settings window fits its
  contents on opening (`SetNextWindowSize` height 0 with
  `ImGuiCond_Appearing`).
- **Settings saved in the destructor**: `Hd::Gui` is constructed first, so
  it's destroyed last and `ImGui::DestroyContext()` saved imgui.ini after
  the members the handler reads were gone. `app::~app` and
  `viewer_app::~viewer_app` save the file and clear `io.IniFilename`.
- **Decisions:**
  - Fontconfig on Linux, because it follows the user's configuration and
    aliases. The scanner is for Windows and macOS and needs no library.
  - Fonts are stored by path rather than by family name: simple and exact,
    at the cost of breaking if the file moves.
  - The built-in monospace font comes from the system rather than a font
    bundled in `res/`, so no font file or license is added to the
    repository.
  - ImGui 1.92.9b rather than 1.93.0, see the earlier entry: 1.93 changes
    stroke placement in `ImDrawList`, which the waveform view relies on.
  - Fonts without Latin letters are hidden: they can't show the interface,
    and they were 1896 of the 2353 faces on the dev machine.

### Testing

- **Unit tests**: all 6 ctest suites pass after each step.
- **Font discovery**: a scratch program built from `src/ui/fonts.cpp`, once
  with fontconfig and once with the scanner, on the dev machine:
  - fontconfig: 457 faces, 54 families, 60 ms. Scanner: 454 faces,
    54 families, 230 ms.
  - Both find the same 12 monospace families. Default monospace font:
    Noto Sans Mono (fontconfig), Liberation Mono (scanner).
  - All listed faces load with `read_font`.
  - `parse_font_ref` / `to_string` round-trip a path containing `:` and
    spaces.
- **Obsolete API**: a separate build of `logic` and `logic-wave` with
  `IMGUI_DISABLE_OBSOLETE_FUNCTIONS` found the two old `AddPolyline` calls.
  After the fix it builds cleanly.
- **GUI** (VNC display, XTest):
  - Picked Noto Serif for the interface and Liberation Serif for the canvas
    through the filter. Both applied right away; the canvas changed alone.
  - Debugger disassembly and memory dump in Noto Sans Mono while running
    `counter_6502`.
  - Fonts restored after quitting and starting again.
  - Pasted "Ağaç şişe ıIİ öÖ ç Ω µ → …" into a text field: everything
    shows except `→`, which Roboto lacks.
  - The font size slider resizes the interface live, sharp at 32 px.
  - Waveform window after the update: hexagons, labels and tooltips are
    drawn as before.
  - `logic-wave`: the file dialog opens and lists files; missing files
    show the error popup.
- **Errors**: imgui.ini with a missing interface font, a monospace font with
  a face index the file doesn't have, and a text file as the canvas font
  gave three messages ("can't open the file", "the file has no font number
  3", "not a truetype or opentype font") and the default fonts.
- **Crash on exit**: reproduced under gdb (SIGSEGV in the settings handler
  from `Hd::Gui::~Gui`). After the fix, two runs that simulate and quit
  exit normally and imgui.ini keeps the fonts.
- **Not tested**: font discovery on Windows and macOS.

### Recommended features

- None beyond the earlier entries.

### Outstanding issues

- None beyond the earlier entries.

### Next steps

- None beyond the earlier entries.

## 2026-10-22 23:14 +0300

### Changes made

- **Interface text is drawn with FreeType** when CMake finds it, instead of
  stb_truetype. The spacing between letters is even and stems are
  consistently sharp. The problem was reported with Noto Serif Medium at
  20 px: in "Lines" there was a gap after the `L` while `i` and `n` nearly
  touched, and in "Libraries" `b`, `r` and `a` crowded each other.
- README: FreeType is listed as an optional dependency.

### Features implemented

- Hinted interface text in both apps (`logic` and `logic-wave`), with
  light hinting.

### How it works

- **Cause:** stb_truetype doesn't hint. ImGui places glyphs at fractional
  positions with a 2× horizontal oversample, so stems and edges land at
  different places within a pixel from letter to letter: some look sharp,
  others light and wide. Small serif text makes it obvious; Roboto hides it
  better. It isn't a regression of the 1.92 update: the same text rendered
  by 1.91.95 is pixel-for-pixel identical.
- **Fix:** `Hd/deps/imgui/misc/freetype/imgui_freetype.{h,cpp}` from the same
  1.92.9b release. Hd's `imgui` target looks for FreeType
  (`find_package(Freetype QUIET)`); when found it compiles the loader, links
  `Freetype::Freetype` and defines `IMGUI_ENABLE_FREETYPE` publicly, so ImGui
  picks the FreeType loader for every atlas. Without FreeType nothing
  changes.
- `ui::load_fonts` sets `ImGuiFreeTypeLoaderFlags_LightHinting` on the atlas
  when FreeType is in use. Light hinting aligns only vertical metrics
  (baseline, x-height) to the pixel grid and keeps the font's own advances.
  That's what most Linux desktops use (fontconfig `hintslight`), so the
  text matches other applications.
- **Decisions:**
  - Light hinting rather than FreeType's default full hinting. Both fix the
    spacing; full hinting also changes glyph widths, while light hinting
    keeps them as designed.
  - Optional rather than required: Windows (MinGW) builds don't have to
    provide FreeType, and they still get stb_truetype.
  - The canvas keeps its stb_truetype signed-distance-field font. It's drawn
    at any zoom, where hinting to a fixed pixel size doesn't apply.
- FreeType is already on Linux systems with fontconfig, which depends on it.

### Testing

- Before and after screenshots of the same text in the same font, enlarged
  4×: stb_truetype (both ImGui versions identical), FreeType with full
  hinting, and FreeType with light hinting. Both FreeType versions fix the
  uneven spacing.
- The app with the reported settings (Noto Serif Medium, Noto Sans Mono Thin,
  Noto Sans Condensed Bold Italic): the Settings window's "Grid style /
  Dots / Lines" row, which was in the report, now has even spacing.
- `nm` confirms `ImGuiFreeType::GetFontLoader` is defined in Hd's imgui
  library, and CMake found `/usr/lib/libfreetype.so` (FreeType 26.7.20).
- All 6 ctest suites pass.

### Recommended features

- A hinting choice in Settings (none, light, full) for people who prefer
  another look.
- Color emoji through FreeType's color glyphs (`LoadColor`), now that the
  loader is available.

### Outstanding issues

- Without FreeType (e.g. a Windows build without it) interface text keeps
  the uneven spacing of stb_truetype.
- `build/Hd/deps/imgui/libimgui.a` is a stale file from Oct 17. The build
  uses `libimgui-0.1.0.a`; the stale one can be deleted.

### Next steps

- Check the Windows build with and without FreeType.
