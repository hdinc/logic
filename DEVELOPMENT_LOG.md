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

## 2026-10-23 00:06 +0300

### Changes made

- **Canvas text** (schematics, symbols, component bar) is drawn three ways,
  by its size on screen:
  - **24 px and more:** exactly from the glyph outlines, computed per pixel
    in the shader. Correct at any zoom, including hairlines and corners.
  - **Smaller, with FreeType:** hinted bitmaps at the exact pixel size, on
    whole pixels, so labels look like interface text.
  - **Otherwise:** multi-channel signed distance fields (MSDF). They replace
    the single-channel stb_truetype SDF.
- **Bug fix:** with high-contrast fonts, zoomed-in canvas text lost strokes.
  The reported case was Noto Serif Display SemiCondensed Black Italic,
  where the `U`'s right stem and the joint of the `1` broke up into
  specks. Those are hairlines about 11 font units wide, 0.4 of a texel at
  the atlas's 48 px, which no texture-based field can hold.
- **Editors** show their framebuffer at whole-pixel sizes. A fractional window
  size scaled the image slightly and blurred everything on the canvas.
- **README:** FreeType is also used for small canvas text;
  `-DHD_USE_FREETYPE=OFF` turns it off.

### Features implemented

- Exact, resolution-independent canvas text at large sizes.
- Hinted small canvas text, matching the interface.
- MSDF canvas glyphs with sharp corners for the remaining cases.
- `tests/font_test.cpp` (ctest `font_test`), a headless check of the
  distance fields and the outline curves.

### How it works

- **Hd library:**
  - **`Hd/deps/msdfgen`:** msdfgen v1.13 core (MIT), unmodified, built as a
    static library with `-O2` even in Debug. It's linked privately into Hd.
  - **`Hd/src/Hd/Msdf.{h,cpp}` (no OpenGL):** converts stb_truetype outlines
    into `msdfgen::Shape` (closing open contours, dropping empty edges).
    Generation steps:
    1. `normalize`
    2. an orientation fix found with a distance probe outside the bounds
       (TrueType and CFF wind opposite ways)
    3. ink-trap edge coloring
    4. `generateMSDF` with overlap support
    5. `distanceSignCorrection` with the nonzero rule
    6. error correction

    `pack()` generates the glyphs on up to 8 threads and shelf-packs them
    into a 1024 px atlas, growing to 2048 or 4096 if needed. Atlas text
    height 48 px, range 6 px, `GL_RGB8`. Rows are bottom-up like OpenGL
    textures.
  - **`Hd/src/Hd/Outline.{h,cpp}` (no OpenGL):** turns each glyph's outline
    into quadratic curves:
    - lines get their control point in the middle
    - CFF cubics become 4 quadratics each, with tangent-based control points
    - every curve is split at its x and y extrema, so it's monotonic in both

    A monotonic curve crosses a horizontal or vertical line at most once,
    and only if its end points are on both sides. Shared end points
    therefore never count twice.
  - **`Hd::Font`:**
    - keeps its public API and adds `range()` and `curveTexture()`
    - `Glyph` gains `first`, `count`, `lo` and `hi`: the outline's curves
      and bounds, in units of the text height
    - curves live in an RGBA32F 2D texture, two texels per curve, rows of
      1024 (`Font::curveRow`)
    - with `HD_HAS_FREETYPE`, a private `Hinted` part (FreeType face from
      the font bytes, so `.ttc` indices work) adds `bitmap(cp, pixelHeight)`,
      `bitmapTexture()` and `newBatch()`:
      - light hinting, sized from stb's text height so both paths agree
      - an R8 cache, 1024 growing to 2048, uploading only dirty rows
      - at most 256 new glyphs per batch
      - a full cache is cleared only between batches
      - missing glyphs map to `?` in every path
  - **`Hd::Renderer2D`:**
    - `begin()` stores the viewport and the inverse of `vp`, notes whether
      the view is axis-aligned (needed for snapping), and starts a font
      batch
    - `text()` picks per string: `exact` when `round(height/px) >=
      hintBelow` (24); hinted when smaller, hinting is on, the view is
      axis-aligned and the size is at least 3 px; MSDF otherwise or on a
      cache miss
    - hinted glyphs keep the unhinted layout, with only the baseline and
      each glyph's start rounded to whole pixels, so text width and
      alignment are identical in all three paths
    - new public settings `hinting` and `hintBelow`
    - `end()` binds the MSDF atlas, the bitmap cache and the curve texture
      to units 0–2, keeping one batch and one draw call
  - **`shape_frag.glsl`:**
    - type 4 (MSDF): the median of three channels, the pixel range from
      `fwidth`, at least 1
    - type 5 (hinted bitmap): coverage from texel UVs
    - type 6 (outline): loops over the glyph's curves and casts a
      horizontal and a vertical ray. Each crossing adds its direction,
      weighted by how much of the pixel lies on its side. The nonzero
      coverage of the two rays is averaged. Hairlines thinner than a pixel
      get proportional, faint coverage.
  - **CMake:** FreeType is now found at Hd level
    (`option(HD_USE_FREETYPE ON)`), so ImGui and the canvas share the
    result. Hd defines `HD_HAS_FREETYPE` privately.
- **App:** `cell_editor.cpp` and `sim_view.cpp` floor the size of the
  editor image.
- **Decisions:**
  - Exact outline rendering for large text instead of a bigger atlas or
    FreeType bitmaps up to a size cap. The hairline needs about 190 px of
    atlas height to survive in a field. Bitmaps would re-rasterize
    constantly while zooming and still break past their cap.
  - Rays against monotonic pieces with the half-open endpoint rule, rather
    than solving full quadratics with root-range checks. That avoids the
    double-counted crossings that leave streaks where curves meet.
  - Curves are stored in a 2D float texture, not a texture buffer: Hd's
    `Shader` validates programs right after linking, when every sampler is
    on unit 0, and a `samplerBuffer` sharing a unit with `sampler2D`s fails
    validation.
  - MSDF stays, for builds without FreeType, for views that aren't
    axis-aligned, and for full bitmap caches.
  - Hinted layout uses unhinted advances with rounded glyph starts. Hinted
    advances would drift by up to half a pixel per glyph and break right
    alignment against symbol boxes.
- **Diagnosis along the way:** the first assumption, a sign error in
  stb_truetype's SDF, was wrong. That SDF and msdfgen both had correct signs
  in every texel more than 1.5 px from the outline. The outline from
  `stbtt_GetGlyphShape` matches fontTools point for point. A FreeType
  rendering at 400 pt shows the `U`'s right stem really is a hairline.

### Testing

- **`font_test`** (ctest, 4 fonts: Roboto, Noto Sans Condensed Bold Italic,
  C059 Bold Italic (CFF) and Noto Serif Display SemiCondensed Black Italic):
  - **Distance field signs:** every MSDF texel more than 1.5 px from the
    outline must match the outline's nonzero winding. 0 errors in
    1713 glyphs.
  - **Curves:** for every glyph, the winding of the monotonic curves must
    match stb_truetype's own rasterizer at 300 px, on pixels that are fully
    inside or outside along with their neighbors. 0 errors in all four
    fonts.
  - **Corner:** Roboto `L` keeps its outer corner a quarter texel inside.
  - Takes about 9 s.
- **Mutation checks** (scratch builds):
  - Always taking the first quadratic root fails on Roboto glyphs (U+021B
    by 345 pixels), so the curve test catches crossing errors.
  - Removing the monotonic split doesn't fail on the test fonts, because
    well-made TrueType fonts put points at extrema. The split protects
    fonts that don't.
- **Timing (Release, 24 cores):** building a font atlas takes 32 ms
  (Roboto), 49 ms (Noto Serif Display) and 69 ms (C059); the curves take
  under 1 ms.
- **GUI** (VNC display, XTest screenshots):
  - **Noto Serif Display Black Italic:** "U1" zoomed far in now matches
    FreeType's rendering: hairline stem, the `1`'s flag joint, smooth
    curves, about one pixel of anti-aliasing, no streaks or specks.
    Medium zooms are clean.
  - **Small text:** hinted labels are crisp below 24 px, with right-aligned
    port labels flush against the box at every step.
  - **CFF:** C059 Bold Italic zoomed in is clean.
- **Without FreeType** (scratch build with `-DHD_USE_FREETYPE=OFF`): ImGui
  and Hd build without FreeType defines, `font_test` passes, and the app
  draws small canvas text with MSDF.
- All 7 ctest suites pass.

### Recommended features

- A canvas setting for `hintBelow` and hinting on or off.
- Culling glyphs outside the view. Every label is still sent to the GPU when
  zoomed far in, which is cheap but grows with the size of the schematic.
- Band or grid acceleration for the outline shader if fonts with very many
  curves per glyph get slow.
- Using the exact outline path for the component bar icons too, for
  pixel-exact tiny text.

### Outstanding issues

- At exactly 24 px text switches from hinted to exact, which can shift a
  glyph by up to half a pixel for one zoom step.
- The MSDF distance field check skips texels within 1.5 px of the outline,
  so it can't see strokes thinner than 3 px at the atlas size. The curve
  test covers those.
- `font_test`'s corner check assumes an upright `L`. It runs only on the
  first font (Roboto).
- Large text is drawn per fragment against all of a glyph's curves. A full
  screen of huge CFF text costs about 70 curves × 2 rays per pixel.

### Next steps

- Check the Windows build: MSDF and outlines without FreeType, and with
  FreeType.
- Watch the outline shader's cost on weaker GPUs.

## 2026-10-23 11:23 +0300

### Changes made

- **Bundles**, like SystemVerilog interfaces: named wires that go together,
  carried by one port or wire.
- **Port fields:** `model::port` has a bundle type and a flipped view. A
  bundle port's `width` is the type's total width, kept up to date by the
  workspace. Its `dir` only says which side of the symbol it sits on; the
  fields have their own directions.
- **Netlist:** a bundle and plain bits of the same width, or two bundle
  types, on one net are a `type mismatch` (`net::type_error`). The wire is
  drawn in the width-error color. The bits still connect.
- **Simulator:**
  - The top cell's bundle ports drive only the fields that are inputs or
    inouts in the port's view.
  - Model ports of grouped external cells are joined to their bits of the
    bundle pin.
- **Waveform recorder:** a signal can be bits of a net. Its key holds the
  net and the bit range (`waveform_recorder::key_of`).
- **Cell undo** ignores the widths of bundle ports. They follow the type,
  which has its own undo.
- **Breakout labels:** the bundle type label sits below the bundle pin,
  clear of the instance name.
- **Bundle editor:** its button column is wide enough for the arrows and
  the remove button.
- **Plugin examples:** `logic_examples` has a fifth model,
  `handshake_sink`.
- **READMEs:** the main README and `sdk/README.md` describe bundles.

### Features implemented

- **Bundle types:**
  - Fields with a width and a direction, or a nested bundle (optionally
    flipped).
  - The flipped view swaps inputs and outputs.
  - Cycle detection.
  - Saved as `<library>/bundles/<name>.json`.
- **Library panel:**
  - Bundles appear under "Bundles" in each library.
  - New bundle type, open, rename (F2) and delete. Delete is refused while
    anything uses the type, and the users are listed.
- **Bundle editor tab:**
  - A field table: name, type (direction or bundle), width or flipped,
    description, reorder and remove.
  - The layout with bit ranges, problems (duplicate or invalid names,
    cycles) and "Used by".
  - Its own undo (Ctrl+Z/Y and the Edit menu).
  - Read-only while a simulation runs.
- **Bundle ports on schematic cells:**
  - The port properties get a Type (bits or a bundle type) and Flipped.
  - Labels read `m : ~hs`.
  - Generated symbols give bundle pins a thicker stub.
- **Bundle wires:**
  - Drawn hollow (a double stroke) with hollow junctions (filled with
    their own color since 12:04).
  - Tooltips on wires and pins list the fields with their bits and, while
    simulating, their values.
- **Bundle breakout:**
  - A Wiring part with one end per chosen field. The fields picker is a
    checkbox tree; empty means all top-level fields.
  - An end can be a nested bundle (a bundle port), so breakouts chain.
  - It works both ways, so it also puts a bundle together.
- **Bundle tap:** one field, placed on a bundle wire, labelled
  `.aw.valid`.
- **Simulation view:**
  - A bundle top port is a field list: checkboxes and hex inputs for the
    driven fields, values for the others.
  - Recording a bundle wire records each field as its own trace
    (`wire 30.valid`, ...).
- **HDL and C model cells:**
  - "Bundle ports" groups the model's ports `<prefix><field>` (nested paths
    joined with `_`, prefix `<name>_` by default) into one bundle port.
  - "Find bundle ports" suggests prefixes whose ports fit a bundle type in
    either view.
  - When ports are grouped, the ports table edits the model's own ports.
- **C SDK:** an optional `bundles` JSON field at the end of
  `logic_model_desc` declares bundle types and bundle ports. The types go
  into the plugin's library. No ABI break: it's read only if
  `struct_size` covers it.

### How it works

- **One wide net per bundle:**
  - A bundle wire is a net of the type's total width. The fields are bit
    ranges, in declaration order from bit 0.
  - The simulator needed no new net type. Breakouts and taps are
    splitter-like primitives whose `bit_link`s come from the layout.
  - **Rejected:** a net per field. It would have changed the netlist,
    simulator, waveforms and hover code throughout.
- **`model/bundle.{h,cpp}`:**
  - `bundle_type`, `bundle_field`, the flattened `bundle_layout` (leaves
    with path, direction, offset, width; nodes for the tree UI).
  - `bundle_ref`/`parse_bundle_ref` (ids as parameter text) and
    `split_fields`.
- **Workspace** (`model/workspace.cpp`):
  - `layout_of(id, flipped)` flattens recursively. A nested field's flip is
    combined with the outer view. A missing type, a cycle or more than
    `max_width` bits give an error and an empty layout.
  - `bundles_changed()` bumps `bundle_revision()`, sets the widths of
    bundle ports, and regroups external cells.
  - Also: `bundle_users`, `would_create_bundle_cycle` and
    `valid_bundle_name`.
  - **Layouts aren't cached:** computing one is cheap, and a cache could go
    stale when a type is edited without `bundles_changed()`.
- **Primitives that depend on bundle types:**
  - They override `build(values, build_context)` and `uses_bundles()`. The
    context gives the workspace, and builtin library cells get none.
  - `interface_of` adds the bundle revision to their cache key, so an edit
    rebuilds them.
  - Parameters stay text, with a `param_def::role` (`bundle`, `fields`,
    `field`) for the editor's pickers (`editor/param_editor.cpp`).
  - **Rejected:** new parameter types. They would have changed every
    switch over parameter types and the saved C-model parameter format.
- **Breakout and tap** (`builtin/wiring.cpp`, `bundle_view`):
  - They resolve the type and the chosen fields, and drop unknown or
    repeated ones. `check()` reports the problems.
  - The simulator doesn't call them "no simulation model" when they have no
    links yet.
- **Types live in libraries,** so they are shared across cells and saved
  like cells. Ids are random (plugin types use a stable hash of plugin and
  name), so renaming keeps references.
- **Port groups** (`model::port_group`, in `c_model_impl` and `hdl_impl`):
  - The cell's `ports` are the visible ones, bundle ports included, so the
    symbol, netlist and editors work unchanged.
  - The model's own ports go to `leaf_ports`, and `cell::model_ports()`
    returns them.
  - `workspace::group_ports` matches fields to ports by name, width and
    direction. A group that doesn't fit stays its plain ports, with a
    problem. The bundle port takes the place of its first field.
    `apply_groups` sets the ports through `set_ports`, which keeps ids by
    name.
  - **Rejected:** keeping `ports` as the model's ports and deriving the
    visible ones. Symbol pins, `check()` and the symbol editor would all
    have needed to know about groups.
  - **Rejected:** SystemVerilog `interface` ports on the HDL top. Verilator
    doesn't allow them on the top module, and Icarus and GHDL's VPI can't
    reach them.
- **Simulating port groups:**
  - `cell_interface::model_ports` / `interface_view::model_ports` give each
    model port its visible port and first bit. For default parameters they
    are cached per cell (`group_cache`). For other parameters they come
    from the provider's `ports_for`, grouped the same way.
  - In `simulator::elaborate` each model port gets its own net, aliased to
    its bits of the visible pin's net, and keeps its own in/out kind.
  - The Verilator, Icarus and GHDL components and the plugin registry
    compare against `cell::model_ports()`, so the models see their ports in
    their order.
- **Plugins** (`plugin/libraries.cpp`, `parse_bundles`): the types are
  created or updated in the plugin's read-only library. Groups are set
  before the symbol is chosen, so plugin symbols can name bundle ports.
- **Top-level bundle inputs** (`simulator::add_driver(net, -1, port, lo,
  width)`): one driver per input or inout field, intersected with the
  net's node slices. Output fields are left to the circuit.
- **Drawing:**
  - Bundle wires: `render::draw_wire(..., bundle)` draws the outline, then
    `draw_bundle_inside` the inside of all bundle wires in a second pass.
    Otherwise round caps would close the hollow where wires join.
  - `editor/bundle_ui.{h,cpp}` has the field table for tooltips and panels,
    and the input editor for the simulation view.
  - Wire tooltips show the bundle in its normal view: a net doesn't have a
    view, only its ports do.
- **Bundle editor** (`editor/bundle_editor.{h,cpp}`):
  - Snapshots of the type for undo. Renaming isn't an undo step.
  - Every edit calls `bundles_changed()`.
  - It is read-only while any simulation runs, because the simulator holds
    the cells, and so their widths, fixed.
- **Files:**
  - Ports save `"bundle"` (id) and `"flipped"`, and keep `"width"`, so an
    older program reads a bundle port as a plain bus.
  - External cells save `"groups"` and `"leaf_ports"` only when they have
    groups.
  - Loading resyncs bundle port widths without marking the libraries
    changed.

### Testing

- **model_test** (`test_bundles`):
  - Layout offsets and directions of a nested bus with one flipped
    handshake, the flipped view, cycles and a missing type.
  - Name rules.
  - Port widths following their type, and the revision counter.
  - Netlist type matching and mismatch, and a missing type.
  - Saving and loading bundles, bundle ports and an HDL cell's groups,
    widths resynced on loading, and removing the file of a deleted type.
- **sim_test:**
  - `test_bundles`:
    - A breakout splitting a bundle, with only the input fields driven by
      the top.
    - Chosen fields and a tap on a nested field.
    - A chained breakout on a nested bundle end, and a bundle through a
      subcircuit with a tap inside.
    - A type mismatch that still connects the bits.
    - Breakout and tap parameter problems, and a rebuild after a type
      change.
    - Waveform field signals (keys, values, capture, remove).
  - `test_bundle_groups`: an external cell whose model ports are
    interleaved (ready before valid) grouped into a flipped bundle port,
    simulated with a test component factory. Also a group that doesn't fit
    and removing the groups.
- **plugin_test:** the examples plugin's `handshake_sink` brings its
  `handshake` type and bundle port `s`. Placed and simulated with the real
  plugin.
- **hdl_test** (`test_bundle_port`): `tests/hdl/sink.sv` and `sink.vhd`
  grouped into a bundle port, kept across a refresh, simulated on
  Verilator 5.052, Icarus 13.0 and GHDL 6.0.
- **GUI** (scratch workspace, a private Xvnc display, xdotool and ffmpeg
  screenshots):
  - The library's Bundles group and the bundle editor table and layout.
  - Hollow bundle wires, the breakout, the tap and `m : ~hs` port labels.
  - The wire tooltip with fields, and the breakout's bundle picker and
    fields picker (choosing only `data` left one end).
  - Simulating: field inputs, `ready` coming back into the bundle, and
    tapped and broken-out values.
  - Waveforms of a bundle wire as three field traces.
  - An HDL cell's "Find bundle ports" suggestions and the generated symbol
    with a bundle stub.
  - The GUI check found the clipped buttons in the bundle editor and the
    overlapping breakout label. Both are fixed.
- All 7 ctest suites pass.

### Recommended features

- Arrays of fields or bundles (`lanes[4]`).
- A generated SystemVerilog wrapper, so HDL tops can use real `interface`
  ports with Verilator.
- Mapping VHDL records to bundles.
- Net labels (connecting by name), which go well with bundles.
- Checking views: a warning when two drivers of the same output field share
  a bundle net.
- A bundle port tool in the schematic editor (like I/O), and dragging a
  type from the library onto a port.

### Outstanding issues

- Wire tooltips show the normal view of a bundle. The view of the port a
  wire comes from isn't tracked per net.
- Editing a bundle type rebuilds every breakout and tap with a new cache
  entry. Old entries stay in `interface_cache` until the program exits,
  because pointers into it may still be held during a frame.
- If a type edit makes a port group stop fitting, the cell falls back to
  its plain ports, and `set_ports` regenerates a symbol whose port names
  changed. A customized symbol of that cell is lost.
- Undo in a cell doesn't undo bundle type changes, and the other way
  round. The two histories are separate.
- The plugin bundle types are matched only by plugin and name. Two plugins
  declaring a `handshake` give two different types.
- The breakout's end labels use an approximate text width (no font
  metrics), like the generated symbols.

### Next steps

- Try bundles on a larger design (e.g. a memory bus between the 6502 and a
  RAM) and adjust the breakout and tap layouts from that use.
- Add a bundle port to the schematic editor's tools and the component bar's
  search.
- Look at arrays of bundles and the SystemVerilog wrapper for HDL tops.

## 2026-10-23 12:04 +0300

### Changes made

- **Wire colors:** bundle wires and buses have their own colors, so the
  two are easy to tell apart.
  - **While editing:** bundles are violet, buses blue and single wires grey
    as before. Junction dots take the color of their wires.
  - **While simulating:** a bundle with a known value is light violet,
    including the value shown at a bundle port. A known bus stays blue.
    Floating, unknown and single-bit values keep their colors.
- **Bundle wires are filled:** a solid line as thick as the bundle stubs of
  symbols (`model::bundle_stroke`). They were hollow before. The hollow
  look and its second drawing pass (`draw_bundle_inside`) are gone.
- **Test:** building a bundle from single signals with a mirrored
  breakout.

### How it works

- `render::theme` has new colors `bus_wire` and `bundle_wire` (editing) and
  `bundle` (a known bundle value), in the dark and light themes.
- `render::value_color(v, bundle)` picks the bundle color for known
  values. `draw_port_value` passes it for bundle ports.
- `schematic_editor::draw` picks a wire's base color from its local net
  (bundle, bus or single). Selection and width or type errors still
  override it.
- **Why color and not only thickness:** a bundle and a wide bus could look
  alike at small zoom. Color tells them apart at any size, and filled lines
  read better than the hollow ones, which looked like two thin wires when
  zoomed out.

### Testing

- `sim_test`, new case in `test_bundles`:
  - A mirrored breakout gets `valid` and `data` from single input ports.
  - The bundle on an output port carries them.
  - `ready` set from the bundle port reaches its single output port, and
    doesn't override `valid` and `data`.
- **GUI** (private Xvnc, screenshots):
  - **Editing:** the bundle wire is filled violet, buses blue, single
    wires grey.
  - **Simulating:** the bundle wire and its port value are light violet,
    buses light blue.
- All ctest suites pass except `font_test`, which wasn't run again: no font
  code changed.

### Recommended features

- Colors for bundles and buses in the Settings window.

### Outstanding issues

- Symbol stubs of bus and bundle pins are still drawn in the symbol color,
  not the wire colors.

### Next steps

- Use the wire colors for the bus and bundle stubs of instance symbols, so
  pins match their wires.

## 2026-10-23 14:03 +0300

### Changes made

- **Right side labels of generated symbols end at the box.** Before, they
  were placed by an estimated text width (0.6 × size per character), so
  with real fonts they stopped short of the box or ran into it. For
  example, in Murax's symbol `io_gpioA_write` was far from the right edge.
- **Symbol text has an alignment:** left (the default), right or center.
  It is set in the symbol editor's text properties ("Align") and saved as
  `"align"` only when it isn't left. Older files load as left aligned.
- **"Find bundle ports" suggests new bundle types.** It used to match only
  existing types, so a model like Murax (VexRiscv), with no types defined,
  got no suggestions.
  - The popup now has two lists: existing types that fit the ports, and
    "New bundle types" from ports sharing a prefix up to their last `_`.
    For Murax these are `io_jtag` (tms, tdi, tdo, tck), `io_gpioA`
    (read, write, writeEnable), `io_uart` (txd, rxd) and `io`
    (asyncReset, mainClk).
  - Picking one creates the type in the cell's library and groups the
    ports into one bundle port.

### How it works

- **Alignment:** `model::text_align` in `model::text`; `pos` is the end
  of the baseline the alignment names.
  - **Renderer:** moves the text box left by its measured width (or half
    of it) before the transform. Mirrored symbols therefore still keep
    labels on the same side of their lines.
  - **Symbol editor:** the hit box moves the same way, with the estimated
    width.
  - `cell::generate_symbol` anchors right side labels 3 units inside the
    box, right aligned, so no font metrics are needed in the model.
  - **Rejected:** measuring the text in `generate_symbol`. The model
    doesn't know the font, and the canvas font can change in the settings.
- **New types from prefixes:**
  - `model::common_prefixes(ports)` groups ports by their name up to the
    last `_`. A prefix needs two or more ports, and something has to be
    left after it.
  - `workspace::bundle_from_ports(lib, ports, prefix)` makes a type named
    after the last part of the prefix (`io_jtag_` → `jtag`, numbered if
    taken). It has a field per port with the port's width, direction and
    description, so the model's view is the normal one.
  - The group is named after the prefix (`io_jtag`). Its side is left
    when most fields are inputs.
  - Ports already in a group aren't suggested again.
- **Note for existing cells:** a symbol generated before this change keeps
  its old label positions until "Generate symbol" is used again.
  Regrouping ports regenerates it too.

### Testing

- **model_test:**
  - `test_bundles_from_prefixes` uses Murax's eleven ports:
    - the four prefixes and their ports
    - types created for jtag, gpioA and uart, with field directions
      (`tdo` an output) and gpioA's width of 96 bits
    - five visible ports after grouping, with jtag on the left and gpioA
      on the right
    - a second `jtag` type named `jtag2`
  - `test_label_alignment`: right side labels are right aligned and left
    side ones left aligned, and the JSON round trip keeps the alignment.
- **GUI** (private Xvnc, screenshots): an HDL cell with Murax's ports.
  - The popup lists the four prefix suggestions.
  - Picking jtag, gpioA and uart creates the three types in the library
    and the bundle port rows.
  - The generated symbol shows `io_gpioA` and `io_uart` ending at the box
    edge.
- All ctest suites pass except `font_test`, which wasn't run again: no font
  code changed.

### Recommended features

- Nested suggestions: `io_` with its sub-bundles (jtag, gpioA, uart) as
  one bundle.
- Choosing the type name and the view (flipped) before creating it.

### Outstanding issues

- The prefix suggestion `io` (asyncReset, mainClk) is offered even though
  a clock and a reset rarely make a useful bundle.
- The symbol editor's hit box for right aligned text still uses the
  estimated width.

### Next steps

- Add the nested suggestions and a small dialog for the new type's name
  and view.

## 2026-10-23 14:16 +0300

### Changes made

- **Bug fix:** bus and bundle stubs in the component bar's icons were drawn
  far too thick, as blobs next to the thin lines.

### How it works

- **Cause:** icons draw at a small zoom, so the component bar raises
  `renderer::stroke_scale` until thin lines are about 1.5 pixels wide.
  `draw_shape` multiplied every width by that factor, so a bus (2.5×) or
  bundle (5×) stroke grew by the same large factor.
- **Fix:** while `stroke_scale` is above 1, a shape with its own width
  grows by only a quarter of its extra width over a thin line:
  `max(width, thin × (1 + 0.25 × (width / stroke − 1)))`. With the icon
  scale, buses come out about 1.4× and bundles 2× a thin line. At the
  normal scale (1) every width is drawn as it is, as before.

### Testing

- **GUI** (private Xvnc, screenshots of the component bar): bus pins of
  the gates, registers, splitter and the plugin models are slim. The
  `handshake_sink` bundle stub is a bit thicker than a bus.
- All ctest suites pass except `font_test`, which wasn't run again: no font
  code changed.

## 2026-10-23 14:43 +0300

### Changes made

- **Category "Arithmetic":**
  - Adder/subtractor (`addsub`)
  - Multiplier (`multiplier`)
  - Divider (`divider`)
  - Negator (`negator`)
  - Comparator (`comparator`)
  - Shifter (`shifter`, a barrel shifter)
  - Bit extender (`extender`)
- **Category "Memory":** Counter (`counter`).
- **Category "Plexers":**
  - Multiplexer (`mux`)
  - Demultiplexer (`demux`)
  - Decoder (`decoder`)
  - Priority encoder (`priority`)
  - Bit counter (`popcount`)
  - Bit selector (`bitselect`)
- **Cell categories:**
  - Every cell has a `category`, saved as `"category"` when set.
  - Builtin cells take their primitive's category, and plugin models their
    `logic_model_desc::category`. `library::categories` (the old map for
    plugin models) is gone; `library::categories()` lists the categories
    in use.
  - The library panel shows each library's categories as tree nodes (open,
    sorted), followed by the cells without one, then the bundles.
  - The category is set in the cell's properties (schematic and model
    editors) or from the library panel's context menu (Category): a text
    input with a list of the library's categories.
  - The component bar groups every cell by its category, or by its library
    if it has none, with builtins first.
- **README:** lists the new parts and categories.

### Features implemented

All widths go up to 4096 bits, except the multiplier at 2048 (its full
product is twice as wide).

- **Adder/subtractor:**
  - Width; mode add, subtract or input (a `sub` pin chooses).
  - Optional carry in and carry out, which are borrow in and out when
    subtracting.
  - Title `+`, `-` or `+/-`.
- **Multiplier:** width; signed; output the low half or the full product.
- **Divider:** width; signed (rounding towards 0, the remainder has the sign
  of a); optional remainder. Dividing by 0 gives x.
- **Negator:** the two's complement.
- **Comparator:** width; signed; outputs lt, eq, gt.
- **Shifter:**
  - Width; operation shift left, shift right, arithmetic right, rotate
    left or rotate right.
  - The amount pin has the bits for 0..width−1. A larger amount shifts
    everything out, and rotations go modulo the width.
- **Bit extender:** input and output widths; zero, sign or one extension;
  narrower keeps the low bits.
- **Counter:**
  - Width; maximum (0 = all ones); direction up or down; at the end wrap or
    stay.
  - Ports clk, en, load, d, rst and q, plus carry (1 at the end value).
  - On the rising edge rst comes first, then load, then en.
  - Its count is saved with the simulation state and shown in the
    inspector.
- **Multiplexer / demultiplexer:**
  - Select bits (1–6, up to 64 inputs or outputs); width; optional enable.
  - Enable 0 makes the outputs 0.
  - The demultiplexer's other outputs are 0.
- **Decoder:** select bits (1–12); optional enable; a one-hot output bus.
- **Priority encoder:** input bits; index of the highest 1; valid.
- **Bit counter:** the number of 1 bits.
- **Bit selector:**
  - Width; output bits; `sel` picks the group of bits starting at
    `sel × out`.
  - Bits beyond the bus are 0.

### How it works

- **`src/sim/arith.{h,cpp}`:** add, sub, negate, mul, divmod, shl, shr,
  sar, rotl, rotr, compare, popcount and extend, on known values of any
  width. They work on 64-bit words with `unsigned __int128` for carries
  and partial products. Division is shift and subtract, one bit at a time.
  Signed operations work on magnitudes and fix the sign at the end.
  **Rejected:** limiting the parts to 64 bits. The simulator supports up to
  4096 bits everywhere else.
- **Unknown inputs:**
  - Any x or z bit in an operand makes the arithmetic outputs x (your
    choice). It's simple and predictable, like Verilog's arithmetic.
  - Optional control inputs that aren't connected (all z) count as their
    inactive value: carry in 0, enable 1, load 0, rst 0. An unknown control
    makes the outputs x. For the counter, an unknown control on the clock
    edge makes the count x.
- **One shared symbol layout,** `builder::box`:
  - Inputs on the left and outputs on the right, one grid apart, with bus
    stubs for buses. Selects and enables go at the bottom.
  - Labels sit inside, the right ones right aligned, and a title under the
    top edge, clear of the instance name above the box.
  - **Rejected:** trapezoid mux symbols. The box keeps pins on the grid for
    any input count, and looks like the other parts.
- **Files and registration:** the parts live in
  `src/builtin/{arith,plexers}.cpp` and are registered by
  `add_arith`/`add_plexers`. Port order follows the box: left, bottom,
  right.
- **Categories:** `cell::category` isn't part of the cell's undo state,
  like the name, so changing it is immediate and not an undo step.

### Testing

- **sim_test:**
  - `test_arith_values`: carries across 64-bit words, 100-bit subtraction,
    signed and unsigned products (also 70×70 bits), division (−7 / 2 = −3
    r −1, −128 / −1 wrapping, 100-bit), shifts by the width and more,
    rotations, signed compare, popcount, extension, negation.
  - `test_arith_parts`: the adder with carry, x propagation and a 100-bit
    input-selected subtract; signed full product; division by 0 giving x;
    the comparator; shifter amount widths (3 bits for 8, 4 for 12) and a
    rotation by 13 on 12 bits; the extender with x bits.
  - `test_counter_part`:
    - Counting to 5 and wrapping, with carry.
    - Enable, load, reset priority and an unknown enable.
    - Down with stay and with wrap.
    - Unconnected en, load and rst (counts).
    - Saving and loading its state.
  - `test_plexers`:
    - Mux select, an x select and enable 0; demux outputs.
    - Decoder one-hot and an x select.
    - Priority encoder index width and valid, bit counter width.
    - Bit selector groups, including the cut last group.
  - A `part_fixture` helper wires each pin of a part to a top port by name,
    or leaves the listed pins unconnected.
- **model_test:**
  - `test_builtins` builds every new part at its defaults and extremes:
    pins on the grid, ports and pins matching, no pins on top of each
    other.
  - `test_categories`: builtin categories, `library::categories()`, and
    the save/load round trip (no key when empty).
- **plugin_test:** plugin models get their category on the cell.
- **GUI** (private Xvnc, screenshots):
  - The library panel's category trees (`demos` in work; Arithmetic,
    Input/Output, ... in builtin) and the Category field.
  - All 14 parts placed in one schematic: boxes with labels, titles, bus
    stubs, and the mux's select and enable at the bottom.
- All ctest suites pass except `font_test`, which wasn't run again: no font
  code changed.

### Recommended features

- An ALU combining the arithmetic, logic and shift operations with an op
  input and flags.
- Per-bit x propagation for the adder (low bits that don't depend on
  unknown ones).
- Separate decoder outputs as an option, instead of one bus.
- Logisim-style trapezoid symbols for the plexers, as an option.
- Renaming or deleting a category for all its cells at once.

### Outstanding issues

- Any x in an operand makes the whole arithmetic result x, even bits it
  can't affect.
- The counter's maximum parameter is a 64-bit integer. Wider counters can
  only use all ones or a value below 2^63 as their end.
- Category names are free text: two spellings make two categories.

### Next steps

- Use the new parts in an example (e.g. a counter, adder and mux datapath)
  in the workspace examples.

## 2026-10-23 14:57 +0300

### Changes made

- **Multiplexer and demultiplexer** are drawn as trapezoids, like common
  textbook mux symbols, instead of labelled boxes.
  - **Mux:** tall on the inputs' side, short on the output's side.
  - **Demux:** mirrored.
  - **Inputs/outputs:** numbered inside the tall side.
  - **Select (and enable):** on the slanted bottom edge, labelled "sel"
    and "en" outside.

### How it works

- `plexer_symbol` in `src/builtin/plexers.cpp` draws both. The demux uses
  the same geometry with x negated.
  - **Geometry:** the tall side spans one grid beyond the first and last
    pin. The short side is one grid shorter at each end (half a grid for 2
    pins).
  - **Pins:** the n pins are one grid apart without a gap, and the single
    pin is at y 0, the grid line at or just below their middle. Pins stay
    on the grid for any count.
  - **Bottom pins:** the select sits below the middle and the enable one
    grid towards the short side. Their stubs end on the slanted edge.
- The port order is unchanged, so the simulation components and saved
  schematics are unaffected. Pin positions changed, so wires to existing
  muxes and demuxes need reconnecting.
- **Rejected:** the gates' input spacing, which leaves out the middle row
  for an even count. It centred the output exactly but left a gap between
  inputs 1 and 2 that doesn't match the reference style.

### Testing

- model_test's `test_builtins` checks the new symbols at the defaults and
  extremes (pins on the grid, no overlapping pins), and the sim tests of
  the plexers pass unchanged.
- **GUI** (private Xvnc screenshots): 4- and 2-input muxes and demuxes,
  with and without enable, bus and single-bit.
- All ctest suites pass except `font_test`, which wasn't run again: no font
  code changed.

### Outstanding issues

- With an even number of inputs the output is half a grid below the
  trapezoid's middle.

## 2026-10-23 15:01 +0300

### Changes made

- **Mux and demux trapezoids are narrower:** two grids wide by default,
  three with an enable, so the enable gets its own place on the bottom
  edge.
- **They are symmetric around the single pin's line** (the mux output, the
  demux input). The shape reaches as far below y 0 as above it, plus a
  grid.
  - With an even number of inputs, the evenly spaced pins can't straddle
    y 0 on the grid, so there is one grid of extra space under the last
    pin.
  - This fixes the outstanding issue of the previous entry: the output was
    half a grid below the middle of the shape.

### How it works

- In `plexer_symbol` (`src/builtin/plexers.cpp`):
  - `top`/`bottom` are ±(the farthest pin from y 0 + a grid), and the
    short side is `tall + 2 or 3 grids`.
  - The select stays one grid from the tall side, which is the middle when
    there is no enable, and the enable one grid further.
- **Rejected:** leaving out the middle input row to centre the pins as
  well. That brings back the gap between inputs that the reference style
  doesn't have.

### Testing

- `test_builtins` (pins on the grid, no overlaps) and the plexer sim tests
  pass.
- **GUI screenshot:** a 4-input mux with enable (three grids, symmetric)
  and a 2-input mux (two grids, symmetric).
- All ctest suites pass except `font_test`, which wasn't run again: no font
  code changed.

## 2026-10-23 15:23 +0300

### Changes made

- **The mux and demux trapezoids follow your sketch:**
  - **Pins:** one grid apart and symmetric around the single pin at y 0.
    An even count leaves out the middle row, like the gates' inputs, so a
    2-input mux has its inputs at ±1 grid.
  - **Shape:** the body reaches one grid beyond the outer pins, and the
    short side is one grid shorter at each end, so the shape is symmetric
    around y 0. It is two grids wide, three with an enable.
  - **Select and enable pins:** on the grid line of the bottom corner
    (before, one grid below it). Short stubs go up to the slanted edge,
    with the select one grid from the tall side.

### How it works

- `side_ys` in `src/builtin/plexers.cpp` is the gates' spacing again
  (`input_ys`), which keeps symmetry and the grid together.
  - The previous entries' even spacing (no gap) needed extra space under
    the last pin to keep the shape symmetric. Your sketch shows the gap
    instead.
- The top and bottom are a grid beyond the outer pins.
- The shrink of the short side is always one grid (half a grid for two
  pins before).
- The input numbers and the sel/en labels are kept from the first
  reference image. They sit outside under the bottom edge.

### Testing

- `test_builtins` (pins on the grid, no overlaps) and the plexer sim tests
  pass.
- **GUI screenshot:**
  - A 2-input mux matching the sketch.
  - A 4-input mux with enable: three grids wide, a gap between inputs 1
    and 2, symmetric.
- All ctest suites pass except `font_test`, which wasn't run again: no font
  code changed.

## 2026-10-23 15:50 +0300

### Changes made

- **Rotating and mirroring while placing:** pasted elements (Ctrl+V)
  follow the mouse and can be turned before the click.
  - R (or Ctrl+R) turns them a quarter turn about the point under the
    mouse, Shift+R the other way.
  - M (or Ctrl+M) mirrors them.
  - Placing a single cell (Space, the component bar) takes the same keys.
    Before, only R worked there, and only counter-clockwise.
- **Q duplicates the element under the mouse and starts placing it:**
  - an instance, with its parameters and orientation
  - a port connector
  - a wire segment

  The copy follows the mouse until a click places it (Shift keeps
  placing). It can be turned with R and M first. The clipboard isn't
  touched.
- The Help > Keys window and the schematic editor's key list describe the
  new keys.

### How it works

- **Keys:** `placing_transform_shortcut` (`src/editor/tool.h`) reads R,
  Shift+R and M, with or without Ctrl.
  - Without Ctrl, R and M only transform while placing or pasting. The
    select tool keeps Ctrl+R/Ctrl+M, so a stray R doesn't turn a selection.
- **Pasting:** `paste_tool::transform` turns the copied ports, instances
  (position and `xf.then`) and wire ends about the copy's origin, which is
  the point that follows the mouse.
- **Placing a cell:** `instance_tool` applies `xf.then(turns, mirror)` to
  its preview.
- **Q:** `schematic_editor::copy_of(handles, origin)` is the old
  `copy_selection` generalized to any handles.
  - `duplicate_hovered` copies the `hit_test` result under the mouse. A
    wire end counts as its whole segment.
  - The origin is the instance's or port's position, so the copy hangs on
    the mouse like a newly placed part. For a wire it's the grid point
    under the mouse.
  - It then calls `begin_paste`, so placing, turning, naming
    (`unique_instance_name` on clashes) and cycle checks are the paste
    tool's.
- **Rejected:** Q placing through the system clipboard. It would overwrite
  what was copied with Ctrl+C.

### Testing

- **GUI** (private Xvnc, xdotool):
  1. Hovered the bundle breakout B1 in a schematic and pressed Q: the copy
     followed the mouse by its bundle pin.
  2. Pressed R: it turned a quarter turn about that point.
  3. Clicked: placed and selected as U1, rotated, with the original
     unchanged.
- All ctest suites pass except `font_test`, which wasn't run again: no font
  code changed. The tools need an ImGui frame and are not unit tested.

### Recommended features

- Q in the symbol editor (duplicate a shape or pin).
- Showing the keys for R/M/Esc in a hint line while placing.

## 2026-10-23 15:58 +0300

### Changes made

- **Bug fix:** a port being pasted or duplicated with Q was drawn without
  the dot on its connection point, while instances being placed showed
  dots on their pins.
- **Bug fix:** the pasted copy of a bundle port was labelled with its width
  (`m[10]`) instead of its type (`m : ~hs`).

### How it works

- `paste_tool::draw` drew ports as connected (`draw_port(..., true)`), and
  connected pins get no dot. Instances are drawn without connection info,
  so all their pins get dots.
- The ports are now drawn unconnected, like the port tool's preview. They
  are passed the bundle label like the ports of the schematic.

### Testing

- **GUI:** Q on the bundle port `m` in a schematic shows the ghost with
  its dot. The label fix was made after that screenshot (which still read
  `m[10]`) and builds, but wasn't looked at again.
- All ctest suites pass except `font_test`, which wasn't run again: no font
  code changed.

## 2026-10-23 16:05 +0300

### Changes made

- **Instance names option:** View > Instance names, and Settings >
  Canvas, choose which names schematics show above instances:
  - **All** (the default, as before)
  - **Custom only:** names you gave, not the default U1, U2, ...
  - **None**

  It applies to every schematic, editing and simulating, and is saved in
  `imgui.ini` (`InstanceNames=all|custom|none`) like the grid settings.
- **README:** describes the option.

### How it works

- `render::instance_names` is on the renderer (`renderer::names`), next
  to the grid settings. `render::draw_instance` skips the label by it.
- **Default names:** `model::is_default_instance_name`
  (`src/model/schematic.cpp`) is true for "U" followed by one or more
  digits, the names `schematic_editor::unique_instance_name` gives.
- **Your decisions:**
  - Three values in one setting instead of two toggles.
  - A name counts as custom when it doesn't look like U<number>. Existing
    schematics need no changes, but a name typed as e.g. U7 counts as
    default. **Rejected:** a saved "renamed by the user" flag, under which
    every existing name would count as default.
  - A global view setting rather than per cell or per instance.
- The app's settings tuple includes the option, so changing it marks
  `imgui.ini` for saving.

### Testing

- **model_test** (`test_default_instance_names`): U1 and U123 are default;
  U, U1a, cpu, u1 and the empty name are not.
- **GUI:** with `InstanceNames=custom` in `imgui.ini`, a schematic with
  U1, U2, dmx and U4 shows only "dmx" above its demux. This also checks
  that the setting loads.
- All ctest suites pass except `font_test`, which wasn't run again: no font
  code changed.

### Recommended features

- A per-instance override (always or never show), saved in the file.

## 2026-10-23 16:21 +0300

### Changes made

- **New builtin part: Tunnel** (Wiring, kind `tunnel`).
  - It has one pin and a name (parameter `name`, default `net`). All
    tunnels with the same name in a schematic are connected, like an
    invisible wire.
  - It takes the width of its net, so it carries single bits, buses and
    bundles.
  - It's drawn as a tag pointing at its pin, as long as its name. An empty
    name shows "?" and the instance problem "give the tunnel a name".
- **`primitive::is_wiring()`:** primitives that only connect nets
  (splitter, bundle breakout and tap, pull resistor, tunnel) now say so.
  The simulator uses it instead of guessing from links and pulls when
  deciding whether to report "has no simulation model".
- **Pin tooltips:** a pin that takes its net's width says "takes the width
  of its net" instead of "0 bits".
- **Warning fix:** `schematic_editor::duplicate_hovered` no longer
  triggers a GCC `-Warray-bounds` false positive (seen in a full rebuild).
- **README:** mentions tunnels.

### How it works

- **Your decisions:**
  - A tunnel part rather than names on wires.
  - Tunnels connect within one schematic: the same name in a subcircuit
    is a different net, and signals cross levels through ports, like
    Logisim.
  - The width comes from the net.
- **Joining:** `cell_interface::tunnel` (exposed as
  `interface_view::tunnel`) holds the tunnel's name.
  `netlist::extract` joins the pin nodes of all instances with the same
  non-empty name in its union-find, before nets are numbered. Everything
  after that (nets, widths, simulation, waveforms, hover) works unchanged.
  **Rejected:** checking for the "tunnel" kind in the netlist. The
  interface field keeps the netlist free of particular primitives.
- **Width:** a port of width 0 takes its net's width. The netlist skips
  width-0 terminals in the width and type checks, so the net's width and
  bundle type come from the real ports. Two different widths on
  same-named tunnels are the usual width mismatch.
- **Simulation:** tunnels have no component and no links. The simulator
  just sees their pins on joined nets.

### Testing

- **sim_test** (`test_tunnels`):
  - Two tunnels named `data` pass an 8-bit value without a wire, and a
    tunnel with another name stays floating.
  - The tunnels' pins share the 8-bit net.
  - 8 and 4 bits on one name give a width mismatch.
  - A bundle passes through tunnels with its type.
  - A tunnel in a subcircuit doesn't connect to the parent's tunnel of the
    same name.
  - An empty name gives the instance problem.
- **model_test:** `test_builtins` checks the tunnel's interface at its
  defaults.
- **GUI** (private Xvnc): the tag symbol with its name, and while
  simulating an 8-bit input through two tunnels with no wire between them
  reaches the output (0xAB).
- All ctest suites pass except `font_test`, which wasn't run again: no font
  code changed.

### Recommended features

- Highlighting all tunnels of a name when one is hovered or selected.
- A warning for a tunnel name used only once in a schematic.
- Net labels on wires sharing the tunnels' names.
- Global tunnels (e.g. for clock and reset) across the hierarchy as an
  option.

### Outstanding issues

- The tag's length comes from an estimated text width, like the other
  generated symbols.

## 2026-10-23 16:43 +0300

### Changes made

- **Smaller tunnel tag:** 8 units high, less than a grid, so tunnels
  stacked one grid apart don't overlap. Before it was 12 units, and
  stacked tags crossed.
- **Name beside the tag:** the name is written next to the tag, on the
  side away from the pin. The tag is a fixed small arrow (one and a half
  grids long) instead of growing with the name.

### How it works

- In `tunnel::build` (`src/builtin/wiring.cpp`):
  - The tag runs from the pin at (0, 0) to x = grid + 5, ±4 high.
  - The name is a left-aligned text at x = grid + 8.
  - Text keeps its side of the lines under a mirror (see `draw_shape`),
    so a mirrored tunnel shows its name on the left, still away from the
    pin.
- **Rejected:** a full grid (±5) high. Stacked tags would share their
  edges and look like one shape. ±4 leaves a small gap.

### Testing

- **GUI** (private Xvnc): two pairs of tunnels named "we" and "re", one
  grid apart, normal and mirrored. The tags don't touch, and the names are
  beside them on the outer side.
- model_test (`test_builtins`) and `test_tunnels` in sim_test pass. All
  ctest suites pass except `font_test`, which wasn't run again: no font
  code changed.

## 2026-10-23 16:52 +0300

### Changes made

- **While simulating, hovering a net highlights all of it:**
  - Its wires get a glow (the selection color, faint, 2 units wider than the
    wire), drawn under them so the value colors stay visible.
  - The cell's port connectors and the tunnels on the net are drawn in the
    selection color.
  - A net is hovered through a wire, a pin, a port connector or a
    tunnel's tag, so pointing at one tunnel also lights up the wires and
    tunnels of the same name elsewhere.

### How it works

- **`schematic_editor::highlight_net`:** a net of the shown scope's
  netlist, or -1.
  - `draw()` first draws the glow for the wires of that net, then
    everything as before.
  - Ports whose `port_nets` entry is the net, and instances with a tunnel
    name whose pin is on it, take the selection color.
- **`schematic_editor::net_at(point)`** finds the net under the mouse, in
  this order: a port connector, an instance pin, a tunnel's body (its
  bounds), a wire.
- **`sim_view::gui`** sets `highlight_net` every frame from the mouse
  while the canvas is hovered, else -1.
  - Only while simulating, as asked.
  - The editor is still free to use it later.
- Tunnels of a name share one net (the netlist joins them), so
  highlighting by net index covers them without knowing names.

### Testing

- **GUI** (private Xvnc, simulating the tunnel demo: input → tunnel "data"
  … tunnel "data" → output):
  - Hovering the input wire lights up both wires, both tunnels and both
    port connectors.
  - Moving away clears it.
- All ctest suites pass except `font_test`, which wasn't run again: no font
  code changed. The drawing needs a GL context and isn't unit tested.

### Recommended features

- The same highlight while editing.
- Highlighting the pins of instances on the net.

## 2026-10-23 17:20 +0300

### Changes made

- **Multi-bit inputs are edited in a popup while simulating.** Clicking a
  wide input (or inout) port connector of the simulated cell opens it at
  the port; so does the **...** button next to the input in the
  properties. It holds:
  - the value as text in the port's format (Enter sets it)
  - the format, with the fraction bits for fixed point
  - a button per bit (up to 128 bits), a click flips it
  - for inouts, Drive on or off
- **Number formats per port,** saved in the cell file (`"format": {"kind":
  ..., "frac": ...}`, only when not hex):
  - hex, unsigned and signed decimal, binary, octal
  - unsigned and signed fixed point with a number of fraction bits
  - IEEE float (half, single, double by the width), bfloat16
  - ASCII

  They are used for:
  - the value drawn at the port while simulating
  - the Inputs and Outputs panels
  - the port's tooltip
  - typing values

  The format is chosen in the port's properties or in the popup.
- **The Inputs panel** edits wide inputs as text in the port's format,
  instead of hex only.

### Features implemented

- **`sim::format_number` / `sim::parse_number`** (`src/sim/numbers.{h,cpp}`):
  - **Integers:** any width. Parsing accepts 0x, 0b and 0o prefixes, `_`
    separators, and rejects values that don't fit (unsigned 0..2^w−1,
    signed −2^(w−1)..2^(w−1)−1).
  - **Fixed point:** formatted exactly (whole part, then fraction digits
    by repeated ×10 with a 128-bit remainder), parsed rounding to the
    nearest step. Up to 64 bits.
  - **Floats:** the shortest text that reads back the same
    (`std::to_chars`). Half and bfloat16 conversions round to nearest even
    and handle subnormals, infinities and NaN.
  - **ASCII:** the first character is the top byte. Non-printable bytes
    show as \xNN, \" and \\ are escaped, and quotes are optional when
    typing.
  - **Unknown values:** with x or z bits, a value shows its bits in every
    format. A format that doesn't fit the width (a float on 8 bits) shows
    hex.
- **`model::number_format`** (`src/model/number_format.{h,cpp}`): the kind
  and fraction bits, labels and file ids, and which kinds fit a width.
  Unknown kinds from newer files load as hex.
- **`editor/number_ui.{h,cpp}`:** `edit_number_format` (combo of the kinds
  that fit, plus fraction bits), `edit_number` (the text field: red while
  invalid text is typed, applied on Enter) and `edit_bits` (the bit
  buttons, rows of 16, groups of 4).

### How it works

- **Your decisions:**
  - A popup at the port. **Rejected:** a separate window like the
    picture, which needs OK and Apply.
  - All four format groups.
  - The format saved per port.
- **Opening the popup:** both triggers set `sim_view::port_popup` and
  `open_port_popup`, and the popup is opened and drawn in the editor's
  window, so it works from either place. Its format edits the cell's port
  (found non-const through the workspace) and marks the library changed.
  The simulator doesn't read the format.
- **Reused code:** the integer kinds reuse the waveform viewer's
  `wave::format_value`, so ports and traces show integers the same way.
  The parsing uses `sim/arith`.
- **Where formats are drawn:** `render::draw_port_value` formats with the
  port's format. The schematic tooltip of a cell's port does too, and
  adds the decimal number unless the format is decimal.

### Testing

- **sim_test** (`test_number_formats`):
  - Integer show and parse, with prefixes, separators and ranges, and
    100-bit decimal both ways.
  - Fixed point exact (1.25, −1.25, 2^−8, the most negative 64-bit value)
    and rounding when parsing.
  - Half, single, double and bfloat16 both ways, including the half
    maximum, overflow to infinity and the smallest subnormal.
  - Floats refused for other widths.
  - ASCII both ways with escapes.
  - Unknown bits.
- **model_test:** the format saved and loaded, no key for hex, and an
  unknown kind loading as hex.
- **GUI** (private Xvnc), simulating an 8-bit input to an output:
  1. Clicking the input port opened the popup with the value field
     focused.
  2. Typing C3 and Enter set it, and the output showed 0xC3.
  3. The format list for 8 bits had no floats.
  4. Signed fixed showed −61, and −3.8125 with 4 fraction bits, on the
     canvas too.
  5. Clicking bit 7 gave 4.1875, and the output showed 0x43.
- All ctest suites pass except `font_test`, which wasn't run again: no font
  code changed.

### Recommended features

- Formats for wires and instance pins (per net), and the port's format as
  the default for its waveform trace.
- The fixed point and float formats in the waveform viewer.
- Formats for the fields of bundle ports.

### Outstanding issues

- The bit buttons are left out above 128 bits; wider inputs are set as
  text.
- Fixed point and floats are limited to 64 bits.

## 2026-10-23 17:29 +0300

### Changes made

- **Thin buses and bundles by default:** bus and bundle signals are drawn
  as thin as single wires, and only their colors tell them apart:
  - while editing, buses blue and bundles violet
  - while simulating, the value colors
- **Option "Thick buses and bundles"** (View menu and Settings > Canvas,
  saved in `imgui.ini` as `ThickBuses`, off by default) brings back the
  thick lines. It covers:
  - the wires and their junction dots
  - the glow of a highlighted net
  - the bus and bundle stubs of symbols (generated symbols, splitters,
    breakouts, taps, gates' bus pins) and component bar icons
- **README:** describes the option.

### How it works

- `renderer::thick_buses` and `renderer::wire_line(bus, bundle)` give the
  width of a wire. `draw_wire`, the highlight glow and the junction dots
  (`schematic_editor::draw`) use them.
- **Symbol stubs:** `draw_shape` draws a shape whose width is exactly
  `model::bus_stroke` or `model::bundle_stroke` at the normal stroke when
  the option is off.
  - Those widths are what the program uses to mark bus and bundle stubs.
    A symbol line the user made thick with another width stays thick.
  - **Rejected:** a flag on shapes, which would change the symbol format
    and every builtin.
- **Saved files are unchanged:** the stubs keep their widths and only the
  drawing differs.

### Testing

- **GUI** (private Xvnc), a schematic with a bundle, buses, a breakout and
  a tap:
  - By default every wire and stub is thin, the bundle violet and the
    buses blue.
  - Turning on View > Thick buses and bundles draws them thick as before.
- All ctest suites pass except `font_test`, which wasn't run again: no font
  code changed.

### Outstanding issues

- A symbol line drawn on purpose at exactly a bus's or bundle's stroke
  width is also thinned when the option is off.

## 2026-10-23 17:35 +0300

### Changes made

- **ImGui style:** frames (inputs, combos, buttons, the component bar's
  icons) have a 1 pixel border, and tree nodes (library panel, hierarchy,
  bundle field trees) are connected by lines to their children.
- It applies to both themes, and to the standalone waveform viewer
  (`logic-wave`), which shares `ui::apply_style`.

### How it works

- `ui::apply_style` (`src/ui/style.cpp`) sets `FrameBorderSize = 1` and
  `TreeLinesFlags = ImGuiTreeNodeFlags_DrawLinesToNodes` after the theme's
  colors.
- **Line style:** `DrawLinesToNodes` draws the vertical line only down to
  the last child. **Rejected:** `DrawLinesFull`, which runs to the end of
  the node's contents. ImGui documents ToNodes as slower for large trees,
  which these aren't.

### Testing

- **GUI screenshot:** the library panel shows the tree lines under work,
  demos, builtin and its categories, and frames and component bar buttons
  have borders.
- No code paths changed besides the style, so the tests weren't run again.

## 2026-10-23 17:38 +0300

### Changes made

- **The bundle tap has no circle on its bundle pin any more.** It was a
  ring of radius 2.5 that looked like an unconnected pin on the wire. The
  tap is now its diagonal from the bundle wire, the field's line and its
  label.

### How it works

- `tap::build` (`src/builtin/wiring.cpp`) no longer adds the circle
  shape. A tap without a field shows only "?" next to its pin.
- A tap placed on a bundle wire splits the wire there (`normalize`), so
  it still connects. Its pin dot shows only while nothing is connected,
  like any pin.

### Testing

- **GUI screenshot:** a tap on a bundle wire shows its diagonal joining
  the wire without the ring.
- model_test (`test_builtins`) and sim_test pass.

## 2026-10-23 18:02 +0300

### Changes made

- **Filled symbols:** the closed outlines of symbols are filled, like the
  light-blue gates in your screenshot. That covers gates (and their
  bubbles), the mux trapezoids, registers, boxes, generated symbols and
  your own drawings, in schematics (editing and simulating), the symbol
  editor and the component bar icons.
  - **Fill color:** the theme's `symbol_fill` (#DCE8F5 in the light theme,
    #1F2B38 in the dark).
  - **View option:** "Fill symbols" (View menu and Settings > Canvas,
    `imgui.ini` `FillSymbols`), on by default.
  - **Per cell:** a cell can have its own color (symbol editor
    properties, "Own fill color", saved as `"fill": "#RRGGBBAA"`).
    Alpha 0 means no fill.
- **Checkers background:** a third grid style, faint squares of one grid
  that fade into five-grid squares while zooming out. In View > Grid
  style and Settings, saved as `GridStyle=checkers`.
- **Rectangle tool in the symbol editor** ("Rect", key R): two corners
  make four lines, which fill like any closed outline.
- **README:** describes filling and the grid styles.

### How it works

- **Finding the outlines** (`model::fill_outlines`,
  `src/model/symbol.cpp`):
  1. Lines, arcs (`arc_points`) and Béziers (17 points) make a graph.
     Straight lines are split where another shape's end lies on them, so
     T-junctions become vertices (an AND gate's long back line, pin stubs
     ending on a box edge).
  2. Vertices of degree 1 are removed until none is left. That drops pin
     stubs, leads, output lines, the XOR's separate back curve and open
     shapes.
  3. The outer boundary of each remaining group is traced from its
     leftmost point, always taking the most clockwise turn, so inner lines
     (a register's clock triangle on its box edge) don't change the fill.
  4. Circles are outlines of their own.
- **Your decisions:**
  - Automatic fills of closed outlines, so no symbol needs redrawing.
    **Rejected:** a filled shape type, which would have meant redrawing
    builtins and existing symbols.
  - A theme color with a per-cell override.
  - Checkers as a grid style.
- **Drawing** (`src/render/renderer.{h,cpp}`):
  - `renderer::fill` triangulates by ear clipping (either winding,
    concave shapes like OR gates).
  - `fill_symbol` caches the triangles per symbol (by address, checked
    against a copy of the symbol) and moves them to the instance.
  - `draw_fills` draws them with `res/shaders/fill_*.glsl`, in
    `renderer::end()` before Hd's batch, so fills lie under all lines and
    text. The component bar icon pass calls it before its own `g.end()`.
  - The fill outlines are covered by the strokes, so the fill needs no
    antialiasing of its own.
  - **Hd is unchanged:** `Renderer2D` has no polygons, so the fill path
    lives in the app.
- **Fill color:** `render::fill_of(r, cell)` returns the cell's own color,
  else the theme's if the option is on, else none. `draw_symbol` takes
  the fill, `draw_instance` passes the instance's cell's, and the
  selection only recolors strokes.
- **Bug found while testing:** re-allocating the fill vertex buffer with
  `glBufferData` for every component bar icon (about 60 times per frame)
  hung the WSL D3D12 GL driver inside a mutex. The buffer now only grows
  and is updated with `glBufferSubData`.
- **Checkers:** `grid_frag.glsl` mode 2 draws alternating squares at the
  grid and at five grids, mixed with the same `u_fine` as the dots, at
  12 % of the grid color.

### Testing

- **model_test** (`test_fill_outlines`):
  - AND, OR, XOR and NAND give one outline (NAND: plus its bubble).
  - The 9-input AND's outline stops at the body (±30), not the extended
    back line (±40).
  - The register gives one outline of exactly its box (40×60).
  - A generated box gives one; an open polyline none.
  - A cell's own fill color and "no fill" round-trip through JSON.
- **GUI** (private Xvnc):
  - **Dark theme:** filled AND, OR, XOR (back curve not filled), NOT
    triangle and bubble, 9-input AND, register.
  - **Light theme with checkers:** the gates look like the screenshot, the
    icons are filled, and the checkers are faint.
  - **Symbol editor:** the rectangle tool drew a filled box, and an own
    color (#FFE0A0) filled it orange.
  - The driver hang above showed up as a black window and was found with a
    gdb backtrace. It's fixed, and icons are filled.
- All ctest suites pass except `font_test`, which wasn't run again: no font
  code changed.

### Recommended features

- A per-instance fill color, to group parts in a schematic.
- Filling holes (outlines inside outlines) as holes.

### Outstanding issues

- The fill cache keys on the symbol's address. Entries of symbols that are
  gone stay until 2000 entries, when the cache starts over.
- Outlines that cross each other without a shared point aren't joined;
  they fill separately.

## 2026-10-23 18:24 +0300

### Changes made

- **The component bar looks like the reference simulator's:**
  - Every builtin part has a small hand-drawn icon of its body only: no
    pin leads or labels, monochrome in the text color, thin even strokes.
  - The vertical bar has collapsible category headers with separators.
    The horizontal bar has framed buttons.
- **Only builtin parts are listed,** by category. User cells, HDL and
  plugin models are placed from the library panel or with Space / Cell...
- **Icon sizes:** Small (24 px), Medium (32) and Large (44) in the bar's
  right-click menu, saved in `imgui.ini` as `ComponentIcons`. The docked
  bar resizes for the chosen size.
- **README:** describes the bar.

### Features implemented

- `model::primitive::icon()` returns a pin-free drawing in a 24×24 box.
  It's empty by default, and then the symbol is drawn.
- `src/builtin/icons.cpp` has the 40 icons, built with `builtin::builder`
  and looked up by kind (`builtin::icon(kind)`, used by
  `builtin::primitive::icon()`):
  - **Gates:** AND, NAND, OR, NOR, XOR, XNOR, NOT, buffer and the tristates
    (with the enable stub), as bodies with bubbles.
  - **Sources:** constant (circled 1), clock (circled square wave).
  - **Memory and counter:** chips with pin ticks and short letters (D, R,
    RW, CT).
  - **Arithmetic:** boxed signs, and the extender as a narrow box into a
    wide one.
  - **Plexers:** mux and demux trapezoids, chips for the decoder and
    priority encoder, "#1", a bracket for the bit selector.
  - **Wiring:** a splitter fan, a breakout comb, the tap, the tunnel tag, a
    pull-resistor zigzag to ground.
  - **I/O:** LED with rays, "8." display, hex box "F", push button, switch,
    terminal ">_".

### How it works

- **Your decisions:**
  - Hand-drawn icons in code for every builtin. **Rejected:** image files
    (no theme colors, blurry when scaled, artwork for every new part) and
    automatic body-only icons from the symbols.
  - Builtins only in the bar.
  - The size as a menu setting.
- **Pins:** the reference leaves them out, and so do the icons. At icon
  size the leads and labels are noise, and the body shape is what tells
  parts apart.
- **Drawing** (`component_bar::draw_icons`):
  - Icons are drawn into the atlas at the chosen size, from ±13 world units
    (the 24-unit box and a margin).
  - Strokes are 1.8 px at the small size, scaling with larger ones
    (`px / 14`).
  - Atlas slots are 48 px so every size fits.
  - A part without an icon falls back to its symbol, scaled to its bounds.
- **Text:** the icons use plain ASCII signs (+-, x, /, <>, <<), so any
  canvas font has them. Chip labels are one or two letters because longer
  ones aren't readable at 24 px.
- **Dimmed icons:** icons are drawn at 35% alpha while there's nothing to
  place into (no schematic open), as before. That's why they look grey on
  a fresh start.

### Testing

- **model_test** (`test_builtins`): every builtin has an icon without pins
  inside its 24×24 box. This caught the XNOR icon being 1.5 units too wide,
  now tightened.
- **GUI** (private Xvnc screenshots):
  - **Horizontal:** framed icon buttons with separators between categories.
  - **Vertical:** collapsible categories in both themes.
  - **Large:** the icons and the re-docked, wider bar.
  - Only builtins are listed, and the user cells and plugin models are
    gone from it.
- All ctest suites pass except `font_test`, which wasn't run again: no font
  code changed.

### Recommended features

- Icons for plugin models (an optional icon field in the SDK), if they
  return to the bar.
- A favorites or recently used group.

### Outstanding issues

- The small text in some icons (chip letters, two-character signs) is
  only just readable at the small size.

## 2026-10-23 18:26 +0300

### Changes made

- **Thinner component bar icons:** strokes are about 1.3 px at the small
  size and at most 1.8 px at the large one. Before, they grew with the
  size to about 3 px at large, which looked too thick.

### How it works

- `component_bar::draw_icons`: `line_px = clamp(px / 24, 1.3, 1.8)`, where
  it was `max(1.8, px / 14)`.

### Testing

- **GUI screenshot:** the large icons have thin even strokes, like the
  reference.

## 2026-10-23 23:35 +0300

### Features implemented

- **Origin indicator in the schematic editor:** the cross and ring at
  0, 0 that the symbol editor had is drawn in schematics too.
- **Toggle:** View > Origin and a "Show origin" checkbox in the settings'
  Canvas section turn it on and off in both editors. It's on by default and
  saved in imgui.ini (`Origin=0/1`).

### How it works

- `renderer::show_origin` (next to `show_grid`) and
  `renderer::draw_origin()`, which draws the cross in the preview color at
  hairline width when the flag is on. The drawing code moved there from
  `symbol_editor::draw`, and both editors call it right after `begin()`,
  so it lies under wires, symbols and pins (fills are drawn first, so it
  shows over instance fills).
- The flag is part of `app::settings`, so changing it marks imgui.ini for
  saving like the other view options.
- No keyboard shortcut: G toggles the grid, and the origin is rarely
  switched, so the menu is enough.

### Testing

- Builds; all 7 ctest suites pass.
- **GUI (VNC :1):** the cross shows at the origin of counter_6502's
  schematic over the 6502's fill. View > Origin hides it there and in the
  symbol editor, turning it on again brings it back, and imgui.ini stores
  `Origin=0`/`Origin=1`.

### Recommended features

- An option to also mark the schematic's bounds or the symbol's ports'
  bounding box.

## 2026-10-24 00:05 +0300

### Features implemented

- **7-segment display redrawn like the reference picture:**
  - a 5×7-grid box
  - slanted, filled, hexagonal segments, the chosen color when lit and grey
    when dark
  - a round decimal point at the lower right
- **Input modes** (instance property "Input"):
  - **bus** (default, as before): `seg`, a = bit 0 … g = bit 6, dp = bit 7
  - **pins:** 1-bit `a`…`g` on the left with their labels inside, and a
    `dp` pin on the right next to the dot
  - **hex:** a 4-bit `in` shown as 0–F (built-in decoder), with a separate
    `dp` pin
- **Pin side:** left, right, top or bottom, for the bus and hex pin.
  Separate pins are always on the left.
- **Enable pin** (option): an `en` input at the bottom. The display is
  dark unless it's 1, for multiplexed displays.
- **Dark segments while editing:** the display shows its grey segments on
  the canvas, not only while simulating.
- **Colors:** orange and cyan added; the LED gets them too.

### How it works

- **Layout:** `seg7_display::build` (`src/builtin/io.cpp`).
  - Box x ∈ [-20, 30], y ∈ [-35, 35]. The top and bottom edges are on half
    grids like the picture, so the pins a–g sit on grid rows.
  - Bus/hex pin positions: left (-30, 0), right (40, 0), top (10, 50),
    bottom (10, -50). The top and bottom stubs are 1.5 grids long so the
    pin lands on the grid.
  - `dp` is at (40, -20), `en` at (0, -50). No combination puts two pins
    in the same place (checked by the test).
- **Hex decoding:** `builtin::seg7_hex(digit)` (`builtin.h`) returns the
  segment bits of 0–F, using the standard table.
- **Drawing:** `render::draw_live` (`src/render/live.cpp`).
  - It now takes the instance's ports and finds values by port name,
    because the port layout depends on the mode.
  - `seg7_polygons()` builds the seven hexagons once (thickness 4.2,
    0.6 gap, slant 0.1), and they're drawn with `renderer::fill`. They lie
    over the symbol fill and under the outline and labels.
  - **Lit:** the chosen color.
  - **Dark or floating:** grey, `mix(symbol_fill, symbol, 0.2)`, which
    follows the theme.
  - **x:** the theme's unknown color.
  - **Hex input:** a fully floating input is dark, a partly unknown one
    makes every segment unknown.
  - **Enable:** a floating `en` keeps the display dark, x makes it
    unknown.
- **Editing:** `schematic_editor::draw` calls `draw_live` for every
  instance, also while editing. It passes no values then, and only the
  7-segment display draws anything.
- **Decisions:**
  - Bus stays the default (the user's choice), so the part's old
    interface is unchanged.
  - Pin side applies only to the bus and hex pin; separate pins stay on
    the left as in the picture.
  - The params system has no conditional visibility, so "Pin side" always
    shows and its description says when it applies.
  - The bus pin has no label inside the box; its tooltip names it.

### Testing

- **model_test `test_seg7_display`:** every input × side × dp × enable
  combination builds:
  - pins on the grid and apart
  - the expected ports and widths
  - an empty `check()`
  - the default is a bus on the left

  It also checks `seg7_hex` for 0, 8, A and F. All 7 ctest suites pass.
- **GUI (VNC :1, scratch workspace with test cells):**
  - dark segments while editing
  - pins mode with constants showing "3." in green, matching the picture's
    layout
  - hex 0xA on a top pin showing "A"
  - bus 0x86 on a right pin showing "1." in cyan
  - a bottom bus pin
  - hex with an unconnected `en` staying dark
  - the light theme

  Moving the `en` label to x = 0 fixed it crowding the `g` label.

### Recommended features

- Conditional parameters in the properties panel (hide "Pin side" for
  separate pins).
- A multi-digit display part, and a common-anode or common-cathode choice
  for the enable level.

### Outstanding issues

- Cyan lit segments have little contrast on the light theme's pale box.

## 2026-10-24 00:45 +0300

### Changes made

- **7-segment display, thicker segments:** 5 units (half a grid) instead
  of 4.2, with a 0.5 gap, as measured from the reference picture.
- **Antialiased segments:** fills have hard edges, so every segment edge
  is also traced with a one-pixel antialiased line in the segment's color.
- **Q / place previews show the digit:** `instance_tool` and `paste_tool`
  call `schematic_editor::draw_live`, which is now public, for their
  previews.

### Features implemented

- **Glow:** lit segments and the point get a soft halo in their color, as
  strong as they are bright.
- **Ghosting / multiplexing:** a display shows how long each segment was
  lit, not just its state at the frame. Multiplexed digits all stay
  visible, and segments fade out after going dark.
  - **Brightness:** the share of simulated time a segment was lit since
    the last update, through a cube root (Stevens' brightness law): 50 %
    on looks 79 % bright, 25 % looks 63 %. The user chose proportional
    with a boost.
  - **Persistence (ms):** a new parameter, 100 by default. The afterglow
    fades to about 5 % over that much real time, and 0 turns the afterglow
    off.

### How it works

- **Segment logic:** it moved to `builtin::seg7_levels` (`io.cpp`,
  declared in `builtin.h`). It covers the bus, pins and hex modes, active
  low and enable. The renderer and the sim component both use it.
- **Sim side:**
  - New optional `component::has_view()` and
    `sample_view(now, out)`.
  - `seg7_component` (replacing `display_component` for this part) adds up
    each segment's lit time on every eval. `sample_view` returns lit time ÷
    window length and starts a new window. With no time passed, e.g. while
    paused, it returns the current 0/1 state.
  - The simulator keeps a list of components with a view (`viewers`) and
    `sample_views()`.
  - `runner::publish` stores the samples in `state::views`, and
    `snapshot::view(component)` reads them. Each window is one publish
    interval: about 15 ms while running, one command while stepping.
- **Editor side:**
  - `schematic_editor::draw_live` looks up the view through
    `scope::components[index]`.
  - The fade is kept in `ghosts`, by (scope, instance):
    `level = max(cbrt(duty), level · exp(−3·dt / persistence))`. It's
    cleared when not simulating.
  - The result goes to `render::draw_live` as a new optional `brightness`
    argument (8 values).
- **Drawing** (`live.cpp` `draw_seg7`):
  - `seg7_segments()` gives each hexagon plus its centerline.
  - **Color:** `mix(unlit, lit, brightness)`, or the unknown color for x.
  - **Glow:** six round-capped lines along the centerline, each 0.9 wider,
    at alpha 0.06 × brightness, so they add up to a smooth falloff. Three
    stronger layers showed visible bands. The halo is in Renderer2D's
    batch after the fills, so it also tints neighbouring dark segments a
    little, like a real display.
- **Decisions:**
  - **Why on-time and not only fading:** frames sample the circuit at
    random points of a fast multiplex. A fade alone would flicker, while
    the averaged on-time is steady and fits the physics.
  - **Wall-time fade on top:** it covers slow multiplexing and pausing.
  - **Edge strokes, not a new shader:** they're cheap and need no change
    in the Hd library.

### Testing

- **sim_test `test_seg7_view`:** a hex display gated by a 1 kHz clock at
  25 % duty.
  - After 10 ms, segments b and c read 0.25 and the rest 0.
  - A second sample with no time passed gives the current state.
  - After `reset()`, a 1 ms window reads 0.25 again.
- All 7 ctest suites pass.
- **GUI (VNC :1, scratch workspace):**
  - Two digits multiplexed by a clock and its inverse. At 1 kHz both "7"
    and "2" show steadily with glow.
  - At 3 Hz they alternate and fade over about 100 ms.
  - Q on a display shows its segments in the preview.
  - Zoomed in, the edges are smooth and the glow falls off evenly.

### Recommended features

- A brightness or gamma setting, for displays whose digits look too dim
  at a high multiplex factor.
- On-time averaging for the LED, so PWM-dimmed LEDs show their
  brightness.

### Outstanding issues

- With persistence 0 the brightness still follows the on-time of the last
  publish interval, so a fast multiplex stays visible. That is intended,
  and the parameter description says "0: no afterglow".

## 2026-10-24 01:00 +0300

### Changes made

The user changed their mind about the look of the 7-segment display:
- **Glow removed.**
- **Full brightness, held:** a segment lit at any time since the last
  update shows at full brightness, and stays lit for the display's
  persistence time after it was last lit. This replaces the brightness
  proportional to on-time and the fade.
- **Wider gap:** 0.9 units between segments, up from 0.5.

### How it works

- **Sim side, unchanged:** the sim still reports each segment's share of
  lit time per publish interval (`sample_view`). A share above 0 now just
  means "was lit", so brief pulses between frames still count.
- **`schematic_editor::ghost`:** it keeps a `lit_until` time per segment,
  set to now + persistence whenever the segment was lit. A segment is
  shown while it's lit or before that time.
- **`render::draw_live`:** it takes `const bool* shown` instead of float
  brightness.
- **`seg7_polygons()`:** back to plain hexagons. The glow's centerlines
  are gone.
- **Persistence parameter description:** "how long a segment stays lit
  after it goes dark, so multiplexed digits stay visible".

### Testing

- All 7 ctest suites pass, including `test_seg7_view`, which still checks
  the on-time shares.
- **GUI:**
  - 1 kHz two-digit multiplex: both digits at full brightness.
  - 3 Hz: each digit stays lit 100 ms after its enable drops.
  - The gaps are visibly wider and there's no glow.

## 2026-10-24 01:20 +0300

### Features implemented

- **Theme editor:** every color the apps use has a name and can be edited.
  - **Opening it:** View > Settings → **Edit theme...**, or View > Theme →
    **Edit theme...**.
  - **Groups:** Canvas, Signals, Waveforms, Messages, Displays and
    Interface. Interface holds all of ImGui's colors under their ImGui
    names.
  - **Built-in themes:** Dark and Light are read-only.
  - **Custom themes:**
    - **Duplicate...** makes a custom theme. Custom themes can be renamed
      and deleted; deleting falls back to the theme's base.
    - **Each color** has a picker with alpha, changes show live, and a
      Reset button appears when it differs from the base theme. A filter
      box opens the groups that match.
    - **Saving:** they're saved as JSON in `~/.config/logic/themes`
      (`$XDG_CONFIG_HOME`; `%APPDATA%\logic` on Windows;
      `~/Library/Application Support/logic` on macOS).
- **Choosing a theme:** the theme combo in Settings and View > Theme list
  Dark, Light and the custom themes. imgui.ini keeps the choice as
  `Theme=<name>`; old `Theme=dark` and `Theme=light` lines still work.
- **Viewer:** the waveform viewer (`logic-wave`) has a View > Theme menu
  with the same themes, stored in `logic-wave.ini`.

### Changes made

- **Shared names:** uses that mean the same thing now share one color.
  - **Unknown and Floating:** used on the canvas and in waveforms. The
    waveform unknown color was orange; it is now the canvas's red.
  - **Error:** canvas missing-cell marks and the red message texts.
  - **Warning:** the orange texts, including the paused simulation and
    HDL warnings.
  - **Info:** the blue "building" texts.
  - **Success:** "built".
  - **Program counter:** the disassembly line.
  - **Displays:** the LED and 7-segment colors, plus "Unlit segment",
    which was computed from the symbol colors before.
- **Message colors:** about 30 literal `TextColored` and `PushStyleColor`
  colors in the editors became `ui::color(ui::color_id::...)`.
- **Files:** `src/ui/style.h/.cpp` were removed; `ui::apply_current()`
  takes their place.

### How it works

- **`src/ui/theme.h/.cpp`** (in `logic_ui`, used by both apps):
  - **`color_id`:** with a table of key, name, group, description and the
    dark and light defaults (today's values).
  - **`theme`:** app colors plus `ImGuiCol_COUNT` interface colors, the
    base ("dark"/"light") and the file. `builtin_theme(light)` takes
    ImGui's `StyleColorsDark` / `StyleColorsLight` into a scratch style,
    so it needs no ImGui context.
  - **Current theme:** `current()`, `set_current()`, `color()` and
    `color_u32()`. `apply_current()` sets ImGui's colors and the apps'
    roundings and borders.
  - **Files:** `config_dir()`, `themes_dir()`, `list_themes()`,
    `load_theme()`, `save_theme()`, `find_theme(name)`.
  - **Pickers:** `theme_combo()` and `theme_menu_items()`, shared by both
    apps.
  - **File format:** `{"name", "base", "colors": {key: "#rrggbbaa"},
    "interface": {ImGui name: "#rrggbbaa"}}`. Missing or invalid colors
    come from the base theme, so files stay valid when colors are added.
    Unknown keys are ignored.
- **Canvas:** `render::theme` keeps its fields, so drawing code is
  unchanged, plus the display colors. `render::set_theme(const ui::theme&)`
  fills it. The built-in `dark_theme()` and `light_theme()` are gone.
- **App:**
  - `app::theme_name` replaces `light_theme`.
  - `apply_theme()` loads by name, falling back to Dark with a message.
  - `use_theme()` pushes `ui::current()` to ImGui and the canvas.
- **`theme_editor`** (`src/app/theme_editor.h/.cpp`):
  - It edits a copy of the current theme and applies it every frame a
    color changes.
  - It saves on `IsItemDeactivatedAfterEdit`, or on Reset.
  - A name check refuses empty names, path characters, a leading dot,
    "dark" or "light", and existing names. A custom theme's name is its
    file name.
- **Waveform view:** the six `IM_COL32` constants became functions that
  read the theme.

### Testing

- **New `theme_test`:**
  - unique keys and names, and every group has colors
  - hex parsing and formatting
  - built-in values (dark background, light pin) and ImGui's light
    WindowBg
  - a save and load round trip with every color and interface color
  - a partial file: missing, invalid and unknown keys fall back to the
    base
  - broken and missing files fail with an error
- All 8 ctest suites pass.
- **GUI (VNC :1, `XDG_CONFIG_HOME` in the scratchpad):**
  - Settings shows **Edit theme...**, and Dark is read-only.
  - Duplicating as "Mine" wrote `Mine.json`.
  - Changing Background recolored the canvas live and was saved.
  - Filtering "windowbg" opened Interface, and changing WindowBg
    recolored every window.
  - Reset restored and saved the default.
  - A restart kept `Theme=Mine`, and View > Theme listed it.
  - `logic-wave` loaded "Mine" from the same folder and switched to Light
    from its menu.
  - Delete removed the file and fell back to Dark.

### Recommended features

- Import and export of a theme file from the editor, and an "open themes
  folder" button.
- Separate light-theme defaults for the message colors. They're the same
  as in the dark theme today, as before, and orange on white is weak.

### Outstanding issues

- Rounding, borders and sizes of the interface aren't part of themes.

## 2026-10-24 01:45 +0300

### Features implemented

- **Built-in Black theme:** a black canvas and black windows, listed after
  Dark and Light in Settings, View > Theme, the theme editor and the
  waveform viewer. Like the others it's read-only; Duplicate makes an
  editable copy, and its colors are the base of the copy's resets and
  missing colors.

### How it works

- **Black defaults:** `color_def` has a third default, `black`.
  - **Canvas:** background `#000000`, grid `#3a3f47` at 80 %, symbols
    `#f2f4f7`, symbol fill `#0d1420`, and slightly brighter wires and
    signal colors than Dark.
  - **Unlit segments:** `#1e242b`.
  - **Messages:** brighter variants of the message colors.
  - **Display colors:** the same as Dark.
- **Interface:** `make_black()` starts from ImGui's dark colors and
  overrides the surfaces:
  - WindowBg, ChildBg, TitleBg, ScrollbarBg and DockingEmptyBg are black
  - PopupBg and MenuBarBg are `#0a0a0a`
  - frames are `#141414`, with blue-tinted hover and active states
  - borders and separators are `#2e2e33`

  ImGui's blue accents for buttons, headers and tabs stay.
- **API:**
  - `ui::builtin_theme(key)` takes "dark", "light" or "black"; an
    unknown key gives Dark.
  - `ui::builtin_themes()` lists them for the pickers and the name check.
  - `theme::light()` was removed; code compares `base`.
  - `find_theme`, the pickers' separator position and the editor's name
    check use the list, so a later built-in theme only needs a table
    column.

### Testing

- **theme_test:**
  - Black's key and name, black background and WindowBg, and ImGui's dark
    button color kept
  - `find_theme("black")`
  - an unknown key gives Dark
  - a custom file based on black fills its gaps from Black
- All 8 ctest suites pass.
- **GUI:** View > Theme lists Dark, Light and Black. Black shows the black
  canvas and windows, with a simulated two-digit display. I switched back
  to Dark afterwards.

## 2026-10-24 21:13 +0300

### Changes made

- **Selection modifiers in the select tool** (schematic and symbol editors):
  - Ctrl+click adds the element under the mouse to the selection.
    Ctrl+clicking it again removes it.
  - Shift+click removes the element from the selection.
  - Ctrl+drag is a box selection that adds to the selection, and shift+drag
    is one that removes from it. This works from empty space, from inside
    the selection's bounds, and from an element.
  - Neither modifier moves anything.
- **Shift+click no longer adds to the selection.** It was undocumented
  except in the Keys window, and the user expected ctrl. In the placing
  and drawing tools, shift still means "keep placing".
- The Keys help window and the shortcut comments in `schematic_editor.h`
  and `symbol_editor.h` describe the new modifiers.

### How it works

- `select_tool<Editor>` in `src/editor/tool.h` stores a `select_mode`
  (`replace`, `add` or `remove`) from the keys held at the press. Ctrl wins
  over shift. The enum isn't called `mode_t` because POSIX already uses
  that name.
- **With a modifier, a press on an element** goes to the new state
  `modifier_click` and keeps the `hit_test` handles in `hits`:
  - If the mouse drags past ImGui's threshold, it becomes `box_select` from
    the press point.
  - On release, `add` calls `toggle(hits)`: if every handle is selected,
    they are all removed, otherwise added. `remove` calls `deselect(hits)`.
  - Toggling works on the whole hit group, so a wire's two ends, the ends at
    a junction, and a shape's points act together.
- **With a modifier, a press on empty space** goes straight to
  `box_select`, without clearing the selection.
- **Releasing a box** calls `deselect` on the handles inside it in `remove`
  mode. Otherwise it calls `select(..., true)`, as before; `replace` has
  already cleared the selection at the press.
- **Plain clicks and drags** (select, move, box, moving from inside the
  bounds) are unchanged.
- The helpers work on the editors' public `selection` vector and
  `Editor::handle`, so the editor interfaces don't change.
- Decided: shift+drag removes with a box, to mirror ctrl+drag. A ctrl+drag
  that starts on an element is a box rather than a move. The user asked for
  "ctrl+click and drag appends".

### Testing

- Build is clean, and all 8 ctest suites pass. There's no unit test: the
  tools depend on ImGui input.
- GUI, driven with xdotool on a second instance with
  `examples/counter_6502` (closed afterwards without saving):
  - ctrl+click on the CPU selected it, and a second ctrl+click deselected it
  - ctrl+click on the CPU, the clock and a wire selected all three, with
    both ends of the wire
  - shift+click on the CPU removed it
  - ctrl+drag around the inverter added it and the wire ends there
  - shift+drag around the clock removed it
  - ctrl+drag starting on the CPU drew a box and moved nothing; Undo stayed
    disabled
  - a plain click on the CPU replaced the selection with it
- The symbol editor uses the same template code. It wasn't checked in the
  GUI, because the example has no symbol to click on.

### Recommended features

- A box that selects an instance when it touches the instance, not only
  when it contains the instance's handle point. Today a ctrl+drag over part
  of an instance doesn't select it.

### Outstanding issues

- None known.

## 2026-10-24 21:29 +0300

### Changes made

- **The component bar is part of the Editor window** instead of a separate
  dockable "Components" window:
  - **Vertical:** a column left of the editor tabs. It starts four icons
    wide and can be resized from its right border.
  - **Horizontal:** a row above the tabs.
  - It can no longer be undocked, moved to another dock, or left behind
    when the Editor window moves.
- **Removed the bar's docking code:** `app::dock_component_bar`, `dock_bar`,
  `set_bar_orientation`, `component_bar::dir_changed`, and the bar's split
  in `default_layout`. Changing orientation or icon size takes effect in the
  next frame and needs no re-docking.
- README: the component bar description.

### How it works

- `component_bar::gui` draws a child region with borders
  (`BeginChild`/`EndChild`) in the current window, not its own `Begin`.
  - **Vertical:** `ImGuiChildFlags_ResizeX` with an initial width of four
    buttons plus padding and scrollbar. ImGui saves the resized width in
    `imgui.ini`.
  - **Horizontal:** the height fits one row of icons and its scrollbar.
  - The two orientations use different child ids (`components_column`,
    `components_row`). With a single id, the resizable column kept the
    full width the row had had.
- `app::editor_panel` calls `components_panel()` right after
  `Begin("Editor")`. For a vertical bar it then does `SameLine()` and puts
  the tab bar in a `tabs` child; a horizontal bar is followed directly by
  the tabs.
- `components_panel()` now returns true when an icon was clicked.
  `editor_panel` then calls `SetWindowFocus()` on the window that holds the
  tabs. The canvas only takes keys (Esc, R, M, ...) while its own window is
  focused, so focusing the outer Editor window, as before, would leave
  placing impossible to cancel with Esc when the bar is vertical.
- **Older layouts:** an `imgui.ini` that still has the Components dock
  node loads fine. Nothing opens a window in that node, so ImGui drops it
  and the editor takes the space.
- Rejected: keeping a dockable window and only docking it more firmly
  (e.g. `NoUndocking`). The window would still have its own tab and dock
  node, and the bar wouldn't belong to the editor.

### Testing

- Build is clean, and all 8 ctest suites pass.
- GUI, driven with xdotool, started in a scratch directory with a copy of
  the real `imgui.ini` (the old layout with the Components dock):
  - The old dock node disappeared, and the horizontal bar showed inside the
    Editor window above the tabs.
  - View > Component bar > Vertical put the bar left of the tabs, four
    icons wide. Dragging its right border widened it, and the width was
    still there after a restart.
  - Clicking the AND icon, in both orientations, started placing an AND
    gate on the canvas. Esc over the canvas cancelled it; before the focus
    fix it didn't with the vertical bar.
  - Right-clicking the bar opens its menu (orientation, icon size).

### Recommended features

- Collapse the vertical bar to a thin strip with a button, to give the
  canvas the full width when the bar isn't needed.

### Outstanding issues

- Existing `imgui.ini` files keep an unused `[Window][Components]` entry,
  because ImGui keeps the settings of windows that no longer exist. It's
  harmless.

## 2026-10-24 22:17 +0300

### Changes made

- **The component bar is a translucent strip over the schematic canvas.**
  It replaces the bordered column or row inside the Editor window from the
  previous entry.
  - No search box, no scrollbar, and it can't be resized. Every side has the
    same layout: framed icon buttons by category, with a line between
    groups.
  - **Side:** top, bottom, left or right of the canvas, or off. It's chosen
    by right-clicking the strip (Top, Bottom, Left, Right, Hide), in
    View > Component bar, or in Settings. A hidden strip comes back from
    View or Settings.
  - **Size:** as long as its icons, at most the canvas minus 8 px margins,
    and centered along its edge.
  - **Look:** faint (55 % opacity) while you work on the canvas and nearly
    solid (95 %) while the mouse is over it, eased. Tooltips and the
    right-click menu stay fully opaque.
  - **Scrolling:** smooth when the mouse is within 1.5 buttons of either
    end, faster closer to the edge (up to 700 px/s). The wheel scrolls two
    buttons per notch and also eases. Fades at the ends show that more
    icons are there.
  - **Where it shows:** only over the schematic while editing. It's hidden
    in the symbol view, while simulating, in locked cells, on model pages,
    and in bundle and instance tabs.
- **Only a fixed subset of the builtins is on the bar:**
  - Constant, Clock
  - AND, OR, XOR, NOT, NAND, NOR, Buffer, Tri-state buffer
  - Multiplexer
  - Splitter, Tunnel
  - Register, Counter
  - LED, Button, Switch, 7-segment display, Hex display

  The others are placed from the library panel or with Space.
- **Settings:** `imgui.ini` stores `ComponentBar=top|bottom|left|right|off`.
  The old `horizontal` loads as top and `vertical` as left.
- README and the Keys help window describe the strip.

### How it works

- **`component_bar`** (`src/app/component_bar.{h,cpp}`):
  - `edge {top, bottom, left, right, off}` replaces `orientation`.
  - `groups()` keeps only the kinds in `shown_kinds`.
  - `gui(ws, target, canvas_min, canvas_max)` runs in the window that holds
    the canvas, after the canvas is drawn. It computes the strip's length
    from the button, spacing and separator counts, so the size is right in
    the first frame. It places the strip 8 px inside the chosen edge,
    centered along it at a whole pixel so the icons stay sharp.
  - The strip is one `BeginChild("component_bar")` with rounded corners, a
    border, and `ChildBg` = `WindowBg`. It uses
    `NoScrollbar | NoScrollWithMouse` and its own padding.
  - The translucency is `ImGuiStyleVar_Alpha`, eased toward the target
    based on last frame's hover. Tooltips and the menu push `Alpha` 1.
  - Icons are placed with `SetCursorPos` along the strip's axis. Separators
    are lines drawn in the middle of the gap between groups.
- **`scroll_strip()`:**
  - Edge zones and the wheel move `scroll_target`, which is clamped to
    `[0, GetScrollMax]`.
  - `scroll` eases toward it by `1 - exp(-15·dt)` and is applied with
    `SetScrollX/Y`.
  - The fades are `AddRectFilledMultiColor` in the strip's background
    color, from solid to clear, drawn over the icons.
- **Focus:** releasing the left button over the strip gives focus back to
  the canvas window (`SetWindowFocus()` after `EndChild`). So Esc, R and M
  work, and the next canvas click places the part.
- **Hover:** the strip is a child window drawn after the canvas, so the
  canvas `InvisibleButton` isn't hovered under it. Clicks and the wheel
  over the strip don't reach the editor, and the wheel doesn't zoom the
  canvas.
- **`cell_editor::placing_canvas()`:** the canvas's screen rectangle from
  the last `gui()`. It's set only while the schematic canvas is shown and
  editable, so the strip shows only where builtins can be placed.
  `app::editor_panel` calls `components_panel(e, min, max)` right after
  `e->gui()` in each cell tab.
- **Removed:**
  - the search box and filter
  - collapsible groups
  - the resizable column and the `components_row` / `components_column`
    children
  - the `tabs` child around the tab bar
  - the focus call after a click in `editor_panel`
- **Decided:**
  - A fixed, hard-coded subset (the user's choice) rather than one that can
    be edited from the menu.
  - The strip is as long as its icons, not the whole edge, so it covers
    less of the schematic.
  - Centered along its edge (the user's choice, after first trying it at
    the top or left end).

### Testing

- Build is clean, and all 8 ctest suites pass.
- GUI, driven with xdotool, started in a scratch directory with a copy of
  the real `imgui.ini` (`ComponentBar=vertical`, large icons) on
  `examples/counter_6502`:
  - **Old setting:** `vertical` loaded as a column on the left, with a fade
    at the bottom where more icons were.
  - **Edge scroll:** holding the mouse at the bottom end scrolled smoothly to
    the last icon.
  - **Wheel:** two notches eased the strip back up.
  - **Sides:** right-click > Top, Right and Bottom moved the strip to each
    edge. As a row, it fits its icons.
  - **Hide:** the strip disappeared. View > Component bar showed "Off"
    ticked, and Top brought it back.
  - **Placing:** clicking the AND icon started placing an AND gate, and Esc
    cancelled it.
  - **Centering:** with small icons the row is centered along the top edge.
  - **Wheel over the strip:** the canvas didn't zoom.
  - **Symbol view (Tab) and simulation (F5):** no strip.

### Recommended features

- Fade the strip out and let the mouse through while a drag or a wire
  passes over it, so it never blocks editing near its edge.
- Number keys 1–9 for the first strip items.

### Outstanding issues

- Parts that aren't in the subset can't be added to the bar without
  changing `shown_kinds` in `component_bar.cpp`.

## 2026-10-24 22:36 +0300

### Changes made

- **The decoder is drawn like the demultiplexer, without the input:** the
  same trapezoid with numbered outputs on the tall side, `sel` (and `en`)
  at the slanted bottom. It replaces the "DEC" box.
- **One 1-bit output per value of `sel`:** `out0` … `outN-1`, instead of a
  single `out` bus 2^select bits wide. The new pin is 1 and the others are 0.
  An unknown `sel` or `en` makes all outputs unknown.
- **Select bits are limited to 6 (64 outputs), as for the demux.** It was
  12, but 4096 separate pins aren't practical.
- **Component bar icon:** the demux shape instead of the "DE" chip.

### How it works

- In `src/builtin/plexers.cpp`, `plexer_symbol` takes a
  `plexer {mux, demux, decoder}` instead of `bool demux`.
  - The decoder takes the demux layout (mirrored) but skips the single
    input pin.
  - Its outputs are 1 bit wide, with the descriptions "1 when sel is k,
    else 0" and `sel` "which output is 1".
- `decoder_component` sets each output port `first + k` (after `sel` and
  the optional `en`).
- **Compatibility:** a saved decoder from before has an `out` bus that no
  longer exists, so its wires would lose their pin. No schematic in the
  workspace uses a decoder, so the decoder was changed rather than given
  an option. Decoders with more than 6 select bits are clamped by the
  parameter range.

### Testing

- **sim_test:** the decoder with 3 select bits has `out7` 1 bit wide and no
  `in`. `sel` = 5 sets `out5` = 1 and `out4` = `out0` = 0. An unknown `sel`
  makes `out5` unknown. All 8 ctest suites pass.
- **GUI**, on a scratch copy of the workspace: a Decoder and a
  Demultiplexer placed side by side. They have the same shape and numbered
  outputs, and the decoder has no input pin.

### Recommended features

- Optional inverted (active-low) outputs for the decoder, as on the
  74x138.

### Outstanding issues

- None known.

## 2026-10-24 22:41 +0300

### Features implemented

- **Decoder "Outputs" parameter:**
  - `separate` (default): a 1-bit pin per output, `out0` … `outN-1`, as in
    the previous entry.
  - `bus`: one output `out` with a bit per output (bit `sel` is 1, as the
    decoder was before today), drawn as a compact trapezoid with one bus
    pin in the middle of the tall side.
- **Select bits:** up to 12 again. Separate outputs with more than 6 select
  bits would be too many pins, so the decoder is shown as a bus. The
  properties warn: "more than 6 select bits are too many pins, shown as a
  bus".

### How it works

- In `src/builtin/plexers.cpp`, `plexer::decoder_bus`:
  - `plexer_symbol` draws the trapezoid at the size of a two-output
    decoder and puts one `out` port at y 0 on the tall side, with a bus
    stub and an "out" label.
  - The `sel` description is "which bit to set".
- `decoder::is_bus(params)`: outputs = bus, or select > `max_separate_bits`
  (6). `build`, `create` and `check` use it.
  - `check` returns the warning only when separate outputs were chosen but
    a bus is shown.
- `decoder_component` takes `bus`.
  - Bus: sets one bit in a value as wide as the outputs.
  - Separate: sets each pin.
  - In both, an unknown `sel` or `en` gives unknown.
- **Decided:**
  - Separate stays the default, as asked in the previous change.
  - A bus as the fallback for too many select bits (instead of clamping the
    select width), so the part keeps working with the select it was given.

### Testing

- **sim_test:**
  - Separate (3 bits): `out5` = 1, the others 0, and unknown with an
    unknown `sel`.
  - Bus (3 bits): no `out0`; `out` is 8 bits wide, 0x20 for `sel` = 5, and
    unknown with an unknown `sel`.
  - 8 select bits with the default: `out` is 256 bits wide.
  - All 8 ctest suites pass.
- **GUI**, on a scratch copy of the workspace:
  - Two decoders placed; Properties shows Select bits, Enable and Outputs
    (separate / bus).
  - Bus turned one into the compact trapezoid with a single `out` pin.
  - 8 select bits on the separate one showed it as a bus, with the warning
    in orange under the parameters.

### Outstanding issues

- None known.

## 2026-10-24 22:54 +0300

### Features implemented

- **The place popup (Space / "Cell...") works from the keyboard:**
  - The top entry is always selected, both when the popup opens and after
    every change to the text.
  - Up and Down move the selection. They stop at the ends, repeat while
    held, skip greyed-out cells (the ones that would contain themselves),
    and scroll the list to keep the selection visible.
  - Enter or keypad Enter places the selected cell, and Shift+Enter does
    the same. Esc closes the popup.
  - A mouse click places as before; hovering doesn't move the selection.
- **Recent cells:**
  - With an empty search, the recently placed cells come first, most
    recent first, then a separator, then all other cells in library order.
  - Up to 8 are kept. They're shared by all editors and saved in
    `imgui.ini` (`RecentCells=`), so they survive restarts.
- **Ranked search:**
  1. names that start with the text
  2. names where a word starts with it (after a space or punctuation, at
     a letter/digit change, or a lower-to-upper case change)
  3. names that contain it anywhere

  Within a rank, library order is kept. "and" lists AND, then NAND, then
  handshake_sink.
- **Look:** the search box has no "Search" label. It's full width with a
  faint "Search cells..." hint. Rows show `library / name`: the library
  faint, the matched text in an accent color (`ImGuiCol_PlotLinesHovered`).

### How it works

- **New `src/ui/search.{h,cpp}`** in `logic_ui`, without ImGui:
  - `match_text(name, text)` returns `{rank, pos}` of the best occurrence,
    ignoring case.
  - `recent_list` (`use`, `items`, `to_string`, `from_string`): ids most
    recent first, no duplicates, limited to 8.
- **`editor::context`:**
  - `ui::recent_list* recent`. The app owns `recent_cells` and passes it to
    every cell editor.
  - `schematic_editor::begin_place` calls `recent->use(cell)`, so the popup,
    the component bar and Q (duplicate) all count.
- **`editor::input_string`** takes an optional hint and then uses
  `InputTextWithHint`.
- **`cell_editor::place_popup`:**
  - Builds the entries each frame: recent ids looked up in the libraries
    (gone cells left out), then the rest; or the matches sorted with
    `std::stable_sort` by rank.
  - `selected` resets to the first selectable entry when the popup opens,
    when the text changes (`last_search`), or when the selected entry is
    no longer selectable.
  - Up and Down are read with `IsKeyPressed` (repeating). Enter and Escape
    without repeat.
  - A row is an empty `Selectable("##cell", selected)`, and the text is
    drawn over it with the draw list in parts, starting at the cursor
    position. The Selectable's rectangle starts half an item spacing
    further left, which clipped the first letter.
  - `SetScrollHereY` runs after the selected row when the keys moved it.
- **Settings:** the recent list is part of `app::settings`, so a change
  marks `imgui.ini` dirty.
- **Decided:**
  - A cell counts as used when placing starts, not when the instance is
    dropped. That's simpler, and choosing it is the use.
  - With an empty search, all cells are listed below the recent ones, so
    browsing still works.

### Testing

- **New `search_test`:**
  - ranks: prefix, word starts after space, `-`, `_`, digit, camel case,
    and middle of a word
  - the best position is chosen; an empty text matches
  - `recent_list` moves an id to the front, drops duplicates, keeps the
    limit, and its string round-trips, skipping bad parts
- All 9 ctest suites pass.
- **GUI** with xdotool, on a scratch copy of the workspace and `imgui.ini`:
  - The empty popup had the hint, no label, and the first row selected.
  - Typing "and" listed AND (selected), NAND, then handshake_sink, with
    "and" highlighted. Enter placed an AND; the same with "clock" placed a
    Clock.
  - The empty popup then showed Clock and AND first, above a separator.
  - Five Downs skipped the greyed `cell1` and reached Constant.
  - Ten Ups stopped at the top, and Esc closed the popup.
  - "and", Down, Enter placed a NAND.
  - 40 Downs scrolled the list with the selection in view.
  - After a restart, the recent list (NAND, Clock, AND) was still there.

### Recommended features

- The component bar icon in front of builtin rows.
- Tab to complete the search text to the selected name.
- Fuzzy (subsequence) matching, e.g. "rgs" finds "Register".
- Also match library and category names, e.g. "arith" for the arithmetic
  parts.

### Outstanding issues

- None known.

## 2026-10-24 23:23 +0300

### Features implemented

- **Automatic symbols:** a schematic cell with an empty symbol is shown as
  a generated box with a pin per port, inputs on the left, outputs and
  inouts on the right, labelled, with the name above.
  - Before, such a cell drew as nothing until "Generate from ports" was
    clicked.
  - The box isn't saved, and it follows the ports: add, remove or rename a
    port (or rename the cell) and every instance updates.
  - Existing cells with an empty symbol get the box at once; drawn symbols
    are unchanged.
- **Symbol editor window:** a dockable window with a tab per cell. It
  docks above Properties in the default layout; older layouts get it there
  the first time.
  - **An automatic symbol** shows as a read-only preview with an **Add
    symbol** button, which makes the box the cell's own symbol to draw on.
  - **A drawn symbol** has the symbol tools. Its properties have **Reset to
    automatic** (with a confirmation) next to "Generate from ports".
  - The page is read-only while any running simulation uses the cell,
    including the cell's own.
- **The Editor tab only shows the circuit:** the Schematic/Symbol radio and
  the Tab key are gone.
- **Libraries panel:**
  - **A click** places the cell in the schematic of the active tab; the
    next canvas click drops it. If it can't be placed, the tooltip says
    why: no schematic open, simulating, the schematic is used by a
    simulation, or it would contain itself. It also shows builtins'
    descriptions.
  - **A double click** opens the cell, as does the menu's **Edit** (was
    "Open").
  - **Right-click** shows **Add symbol** for automatic symbols, otherwise
    **Edit symbol**. Builtins have neither.
- **Undo, the Edit menu and the Properties panel** act on the Editor or the
  Symbol editor, whichever was focused last.

### How it works

- **Model:**
  - `model::generated_symbol(ports, name, order, new_id)` is the box
    generator, taken out of `cell::generate_symbol`, which now calls it.
  - `cell::auto_symbol()`: a schematic cell whose symbol has no shapes and
    no pins.
  - `cell::check()` skips the pin warnings for it.
  - `workspace::interface_of` returns `auto_symbol_of(c)` for such cells: a
    per-cell cache regenerated when the ports or the name differ, with
    local ids. Everything that draws or connects instances goes through
    `interface_of`, so the box works everywhere: canvas, netlist, sim view,
    bounds and hit tests.
  - Saving is unchanged: an empty symbol stays empty.
- **`editor::symbol_page`** (`src/editor/symbol_page.{h,cpp}`):
  - It owns a `symbol_editor`, the symbol toolbar (moved from
    `cell_editor::edit_toolbar`), its canvas, the properties, copy/paste
    and undo/redo.
  - `symbol_editor::draw` draws `auto_symbol_of` (shapes and labelled
    pins, no handles) for automatic symbols; the page then only pans and
    zooms.
  - The canvas code shared with the cell editor is
    `editor::canvas_widget` (`canvas_widget.h`): the size in whole pixels,
    the input, drawing into the framebuffer, and the image.
  - `tool_button` moved to `context.h`.
- **Shared undo:**
  - `app::histories` maps cell ids to `shared_ptr<model::history>`, given
    to both the cell editor and the symbol page and dropped when neither is
    open.
  - `history::restore_count()` counts undos and redos. Each side compares
    it every frame and revalidates its selection when the other one
    replaced the cell.
- **App:**
  - `symbol_pages`, `symbol_panel()`, `open_symbol(c, add)`,
    `close_symbol`, `find_symbol`.
  - A second texture/framebuffer (`symbol_tex/fb`), because both windows
    are visible in the same frame and draw at different sizes.
  - `last_focus` is set by `IsWindowFocused(RootAndChildWindows)` in each
    window. `focused_symbol()` routes Ctrl+Z/Y/C/X/V, the Edit menu and the
    Properties panel.
  - `simulated_by(cell, own)` gives the lock text for editors (others'
    simulations) and symbol pages (any).
  - Deleting a cell closes its symbol page too.
  - `dock_symbol_editor()` splits the Properties dock node when the window
    has no saved settings, waiting until Properties exists. On the first
    frame it doesn't yet, and the window would otherwise be created
    floating.
- **Libraries click:** `Selectable` → `e->place(c.id)` and
  `SetWindowFocus("Editor")`.
  - The row is highlighted while that cell is being placed.
  - A double click calls `cell_editor::cancel_place()` and then
    `open_editor`.
  - The release that ends a double click is ignored
    (`MouseClickedLastCount >= 2`). Otherwise it started placing again
    before the tab switch took effect: found in the GUI test.
- **Decided** (the user's choices):
  - The automatic symbol is live until customised, rather than generated
    and saved once.
  - The symbol editor is its own window with tabs, rather than following
    the active tab.
  - A click that can't place does nothing and the tooltip says why.

### Testing

- **model_test:**
  - New `test_auto_symbol`:
    - a box with one pin per port, not stored
    - it follows added and removed ports
    - a symbol of its own is used and stays
    - clearing it is automatic again
    - only schematic cells
  - `test_ports_and_check` and `test_builtins` were updated for automatic
    symbols.
  - All 9 ctest suites pass.
- **GUI** with xdotool on a scratch copy of the workspace and of the real
  `imgui.ini` (the layout from before the window):
  - The Symbol editor docked above Properties, and the Editor toolbar has
    no radio.
  - One click on `cell1` in Libraries and a canvas click placed it in
    `asfd` as a box with `in1`.
  - Right-click > Add symbol opened `cell1` in the Symbol editor with the
    box editable, and Properties showed the symbol properties.
  - A line drawn there showed on the instance at once. Ctrl+Z undid it,
    and Ctrl+Y redid it.
  - Reset to automatic brought the box back and the page showed Add symbol
    again.
  - Double-clicking `cell1` opened its tab and left `asfd` on the Select
    tool. Hovering it showed "'cell1' would contain itself".
  - Adding an output port in `cell1` added an `out1` pin to the instance in
    `asfd`.
  - With the simulation running, the symbol page was greyed out.
- **Note:** xdotool's `key ctrl+z` can send both keys in one frame, which
  the app doesn't take as the chord. Holding ctrl with
  `keydown`/`keyup` works.

### Recommended features

- A hover preview of a cell's symbol in the Libraries panel.
- Drag a cell from Libraries onto the canvas to place it.

### Outstanding issues

- External (C model and HDL) cells keep their generated, saved symbols.
  The automatic box only applies to schematic cells.

## 2026-10-24 23:39 +0300

### Changes made

- **Fix:** right-clicking a builtin cell in the Libraries panel opened an
  empty menu. Builtins can't be edited, have no symbol to add or edit, and
  their library is read-only, so the menu had no items.

### How it works

- `app::library_cell` opens the context menu only when it has something
  in it: the cell can be edited (schematic or external), or isn't a
  builtin (symbol items), or its library isn't read-only (rename,
  category, delete). Builtins only show their tooltip.

### Testing

- **GUI** with xdotool, on a scratch copy of the workspace: right-clicking
  AND showed no menu, only the tooltip. Right-clicking `mos6502` (a plugin
  cell) shows Edit and Edit symbol.
- All 9 ctest suites pass.

### Outstanding issues

- None known.

## 2026-10-24 23:46 +0300

### Changes made

- **Fix:** the component bar's buttons were spaced differently in a row
  and in a column, and weren't square.
  - The gap came from ImGui's `ItemSpacing`, which differs in x and y.
  - The padding came from `FramePadding`, also different in x and y.

### How it works

- `src/app/component_bar.cpp` uses its own constants for both directions:
  - `icon_pad` (4 px around the icon, pushed as `FramePadding` while the
    strip is drawn), so buttons are square
  - `gap` (6 px between buttons)
- The strip's length and thickness are computed from the same values.

### Testing

- **GUI** with xdotool, on a scratch copy of the workspace: the bar at the
  top and at the left, compared enlarged. The buttons are the same squares
  with the same gaps.
- All 9 ctest suites pass.

### Outstanding issues

- None known.

## 2026-10-24 23:49 +0300

### Changes made

- **Component bar spacing:** the gap between buttons is shorter (3 px,
  was 6). The distance from the strip's border to the buttons is the same
  3 px (it was 2 px: the 4 px padding minus the border line).

### How it works

- In `src/app/component_bar.cpp`, one constant `pad` (3 px) is both the
  gap between buttons and the padding inside the strip; the separate `gap`
  is gone.
- At the strip's edges the content is inset by `pad` plus
  `ChildBorderSize`, because the border line is drawn inside the strip's
  rectangle and would otherwise take a pixel of the padding. The strip's
  size, the button positions and the separator lines use that inset.

### Testing

- **GUI** with xdotool, on a scratch copy of the workspace: one pixel row
  through the strip measured border, 3 px background, button border, and
  between two buttons border, 3 px background, border.
- All 9 ctest suites pass.

### Outstanding issues

- None known.

## 2026-10-26 16:05 +0300

### Features implemented

- **Web build (WebAssembly):** the editor and the builtin simulator run in a
  browser, built with Emscripten (`emcmake cmake -B build-web`, see "Web
  build" in README.md). The schematic and symbol editors, the component bar,
  wiring, undo, simulation on its thread, waveforms and saving work. C model
  plugins and HDL cells (Verilator, Icarus, GHDL) aren't part of it: the
  "New C model cell", "New HDL cell", "Reload plugins" and "Exit" menu items
  are hidden there.
- **Hosting:** the output folder (`index.html`, `.js`, `.wasm`,
  `coi-serviceworker.js`) can be served as it is by any static host, from a
  sub-path too, so it's ready for GitHub Pages.
- **Saving in the browser:** the workspace and imgui.ini live in
  `/home/web_user`, kept in IndexedDB. The page asks before it's closed with
  unsaved changes.

### Changes made

- Hd: an Emscripten branch in its CMake (glfw from the `contrib.glfw3` port,
  FreeType from the `freetype` port, a `glad` header for GLES 3, no install
  rules), shaders turned into GLSL ES 3.00 when they're loaded, the WebGL 2
  context hints, no GL debug output or frame pacing in the browser, the
  canvas follows the window, `glFramebufferTexture2D` (on every platform),
  resources as a C array, and at most 4 threads for the canvas font's
  distance fields in the browser.
- logic: `app::run` is split into `run` and `frame`. `src/web` holds the
  page glue, and `web/` holds the page shell, the pre-js, the vendored
  coi-serviceworker (v0.1.7, MIT) and `serve.py`.
- Fixes found by clang/libc++: an ambiguous `std::to_string` of a file time
  (`src/hdl/job.cpp`, `__int128` in libc++) and an unused lambda capture
  (`src/cosim/component.cpp`).

### How it works

- **Threads kept:** the simulator's runner and the HDL manager keep their
  `std::thread`s (`-pthread`), so the simulator didn't need a single-threaded
  mode. Threads need SharedArrayBuffer, so the page has to be cross-origin
  isolated. GitHub Pages can't send the COOP/COEP headers, so
  coi-serviceworker adds them. The alternative, a single-threaded runner that
  steps in each frame, was rejected by the user's choice. It would have
  worked on any host without a service worker.
- **Thread pool:** browser threads are workers started with the page
  (`PTHREAD_POOL_SIZE=16`). A thread past the pool only starts once the main
  thread returns to the browser, so a main thread that waits for it (a
  join, or `runner::wait_idle`) never wakes up. This first hung at startup,
  when `Hd::Font` started 8 msdf threads besides the HDL manager's. In the
  browser msdf now uses 4. 16 covers the manager, the 4 msdf threads and 11
  simulations at once.
- **Service worker reload:** coi-serviceworker reloads by itself on
  `updatefound`, before the worker controls the page, so the page could come
  back without isolation. `index.js` also started on the first, non-isolated
  load and failed (`DataCloneError`). The shell turns coi's reload off and
  reloads itself once `navigator.serviceWorker.ready` resolves. It starts
  emscripten's script, kept in a `<template>` (emcc requires the
  `{{{ SCRIPT }}}` placeholder), only when `crossOriginIsolated` is true.
  After 3 tries, or with no service worker (private mode, plain http), the
  page shows why it can't run.
- **Shaders:** GLSL ES 3.00 has no `layout(location = n) uniform`.
  Emscripten's `-sGL_EXPLICIT_UNIFORM_LOCATION` removes these and maps the
  numbers to the real locations, so the C++ code keeps its literal uniform
  locations. `Hd::Shader` replaces `#version 330 core` with `#version 300 es`
  plus precision lines and drops the `GL_ARB_explicit_uniform_location`
  extension line.
- **Resources:** wasm-ld can't link a binary file (`ld -r -b binary`).
  `add_resource_dir` runs `myar-bin`, itself built to wasm, under node
  (`CMAKE_CROSSCOMPILING_EMULATOR`, `-sNODERAWFS`), and `cmake/bin2c.cmake`
  writes the archive as `<pfix>res_myar[]` and `<pfix>res_myar_size`, which
  `Resource.cpp` reads in the browser.
- **Storage:** `web/pre.js` mounts IDBFS at `/home/web_user` with
  `autoPersist`, which writes each file back to IndexedDB as it closes. It
  reads the folder back in a `preRun` that holds a run dependency, so `main`
  starts with the saved files. `main` changes into that folder (imgui.ini)
  and makes the workspace folder. Manual `FS.syncfs` calls after saving were
  dropped, since `autoPersist` covers them. The repo's `workspace/` isn't
  preloaded: its examples need the 6502 and AVR plugins.
- **Main loop:** in the browser `app::run` hands `frame` to
  `emscripten_set_main_loop_arg` and returns, and `main` makes the app with
  `new` so it stays alive. This avoids `simulate_infinite_loop`, whose
  unwinding would run destructors with `-fwasm-exceptions`. Each frame tells
  the page whether there are unsaved changes (`web::set_unsaved`, used by
  `beforeunload`).
- **Host headers:** glm and nlohmann_json are linked into
  `build-web/host_include` from the host instead of searching
  `/usr/include`, whose libc headers would shadow Emscripten's.
- **Hi-dpi:** the GLFW port scales the framebuffer by the device pixel
  ratio, like macOS. The interface keeps its size and text is sharp.
- **HDL and plugin code:** it still compiles in the browser build and only
  fails at run time (Emscripten stubs fork, sockets and dlopen), so
  `logic_core` didn't need any `#ifdef`s.

### Testing

- **Browser** (headless Chromium driven over the DevTools protocol,
  SwiftShader WebGL 2): the page loads with `crossOriginIsolated`, then a new
  cell, placing a switch and an LED, wiring them, simulating (the switch
  toggled lights the LED), Ctrl+S, reloading the page (the cell is still
  there, and IndexedDB holds the cell, `library.json` and imgui.ini), and
  the File menu without the plugin, HDL and Exit items.
- **Hi-dpi:** at device scale factor 2 the layout is the same, text is sharp
  and a part lands where it was clicked.
- **GitHub Pages setup:** the release build served from `/logic/` by a plain
  `python -m http.server` without COOP/COEP. In 3 of 3 runs the page
  reloaded once, ended up isolated and started without errors. Before the
  shell fix it could end up stuck on "Loading…".
- **Native:** builds, and all 9 ctest suites pass. The app starts and runs
  its frame loop on X11.

### Recommended features

- A GitHub Actions workflow that builds the web version and deploys it to
  Pages.
- Importing and exporting a workspace (a .zip) in the browser, and example
  cells that only use builtin parts.
- C model plugins as wasm side modules (`MAIN_MODULE`/`SIDE_MODULE`), and the
  bundled 6502 plugin linked in statically.
- HDL cells prebuilt on the desktop as wasm modules shipped with the
  workspace.

### Outstanding issues

- Tested only in headless Chromium. Firefox and Safari, and real GPUs,
  aren't tested yet.
- The page needs a service worker on hosts without COOP/COEP headers. It
  doesn't run where service workers are off (some private modes) or over
  plain http other than localhost.
- More than 11 simulations at once would exhaust the thread pool and hang
  the page.
- Emscripten warns that `-pthread` with memory growth can slow down JS code
  that touches memory (the GL calls). No slowdown was seen.
- File dialogs only see the browser's in-memory file system, and fonts can
  only be the bundled one (there are no system fonts).

### Next steps

- Try the web build in Firefox and Safari and on a real GPU.
- The Pages workflow and workspace import/export.

## 2026-10-26 20:22 +0300

### Changes made

- README's "Web build" section says how the site is deployed.

### Features implemented

- **Pages deployment:** `.github/workflows/pages.yml` builds the web version
  and publishes it to <https://hdinc.github.io/logic/> on every pushed tag,
  or when it's started by hand from the Actions tab (`workflow_dispatch`).

### How it works

- The workflow is in this repository and deploys with
  `actions/upload-pages-artifact` and `actions/deploy-pages`. GitHub serves a
  repository's Pages site at `hdinc.github.io/<repo>/`, so no
  `hdinc.github.io` repository is needed, and no token or deploy key.
  - Rejected: a workflow in a separate Pages repository. It can't see this
    repository's tags without a `repository_dispatch` sent from here, and it
    would still have to clone and build this repository.
  - Rejected: pushing the build into a `hdinc.github.io` repository. That
    only makes sense while this repository is private, and it needs a
    token. Both repositories are public now.
- **build job** (ubuntu-24.04): checkout, then the submodules over https. The
  Hd submodule URL is ssh (`git@github.com:hdinc/Hd/`), which needs a key in
  CI, so `url."https://github.com/".insteadOf` rewrites it. glm and
  nlohmann_json come from apt (`libglm-dev`, `nlohmann-json3-dev`) into
  `/usr/include`, the default `LOGIC_HOST_INCLUDE`. emsdk is cloned and
  `EMSDK_VERSION` (6.0.13, the version used locally) is installed. The
  Emscripten cache (the GLFW and FreeType ports, system libraries) is kept
  with `actions/cache`, keyed by that version. After
  `emcmake cmake` and `cmake --build`, the top-level `.html`, `.js`, `.wasm`
  and `.data` files of `build-web` are copied into `site/`, with
  `.nojekyll`, and uploaded. A pattern instead of a fixed list, so a new
  output file isn't missed.
- **deploy job:** deploys the uploaded files into the `github-pages`
  environment. `concurrency: pages` without cancelling, so a newer tag waits
  for a running deployment.
- `exeres.myar` in the build folder isn't needed by the page. On the web it's
  compiled into the wasm as a C array (`Hd/cmake/resource.cmake`).

### Testing

- A local release web build (Emscripten 6.0.13) outputs `index.html`,
  `index.js`, `index.wasm` and `coi-serviceworker.js`, which are the files
  the copy step picks up.
- The emsdk repository has the `6.0.13` tag, and the workflow file parses as
  YAML. The workflow hasn't run on GitHub yet.

### Recommended features

- Deploy only version tags (`v*`) if other tags are used later.
- A build-only check of the web version on pushes to master, without
  deploying.

### Outstanding issues

- The Pages source of the repository must be set to "GitHub Actions"
  (Settings → Pages) before the first deployment.
- The tagged commit's Hd submodule commit must be on GitHub, or the checkout
  fails.

### Next steps

- Set the Pages source, push a tag and check the site at
  <https://hdinc.github.io/logic/>.
- Try the web build in Firefox and Safari and on a real GPU.
- Workspace import/export in the browser.

## 2026-10-26 20:38 +0300

### Changes made

- The Pages workflow runs only for version tags (`v*`) instead of any tag.
  The `github-pages` environment allows `master` and `v*` tags to deploy,
  so another tag would have built and then failed to deploy.
- Updated the actions to versions that run on Node 24:
  `actions/checkout@v7`, `actions/cache@v6`,
  `actions/upload-pages-artifact@v5` (with `upload-artifact` v7) and
  `actions/deploy-pages@v5`. GitHub warned that the old versions target the
  deprecated Node 20.
- README: deploys come from version tags.

### Features implemented

- First deployment: `v0.0.1` is live at <https://hdinc.github.io/logic/>.

### How it works

- The first run of `v0.0.1` built, but the deploy was rejected: the
  `github-pages` environment only allowed `master`. A tag rule `v*` was
  added in Settings → Environments, and the failed job was re-run.
- Each action's `action.yml` at the new tag runs with `node24`.
  `upload-pages-artifact` is a composite action and uses
  `upload-artifact` v7.

### Testing

- The `v0.0.1` run passed after the environment rule was added (build
  2m42s, deploy 9s), and `https://hdinc.github.io/logic/` returns 200.
- The updated workflow hasn't run on GitHub yet.

### Recommended features

- A build-only check of the web version on pushes to master, without
  deploying.

### Outstanding issues

- None known for the deployment.

### Next steps

- Check the updated workflow with the next version tag, or with a manual run
  from the Actions tab.
- Try the web build in Firefox and Safari and on a real GPU.
- Workspace import/export in the browser.

## 2026-10-26 20:50 +0300

### Changes made

- **Sharp canvases on hi-dpi screens.** On a screen with a device pixel ratio
  above 1 (most phones, laptops and Retina Macs, and the browser when it
  zooms), the schematic, the simulation view, the symbol editor and the
  component bar's icons were blurry. ImGui's own text stayed sharp.
  - Cause: the canvases were drawn into their framebuffer at their size in
    ImGui units (CSS pixels on the web), then shown with `Image()`, which
    ImGui scales by `DisplayFramebufferScale` for the screen. At a scale of 2
    the texture was stretched over twice the pixels.
  - The canvases are now drawn at the screen's resolution and shown 1:1.

### Features implemented

- None, a fix.

### How it works

- `canvas_room()` in `src/editor/canvas_widget.h` returns the room left in
  the ImGui window in whole ImGui units, and the same in framebuffer pixels
  (times `io.DisplayFramebufferScale`). `canvas_widget` (cell editor, symbol
  editor) and `sim_view::gui` bind the framebuffer at the pixel size, draw,
  and show that corner of the texture at the ImGui size. The units are
  limited so the pixels fit the 3840×2160 textures.
- `editor::canvas` keeps working in ImGui units (`begin_frame` gets the ImGui
  size). Zoom, the mouse, and hit-test distances (`canvas::pixels`) stay the
  same on screen.
- `render::renderer::scale` holds the framebuffer pixels per interface
  pixel. `app::draw_gui` sets it every frame. `renderer::pixel()` is now an
  interface pixel (`g.pixel() * scale`). Its users mean a size the user
  sees: hairlines (origin, guides, selection box), the grid's fade by
  pixels per grid step, and the component bar's icon line widths.
  `Hd::Renderer2D` stays in framebuffer pixels, for antialiasing, the
  smallest drawn width and hinted text, so those are as sharp as the screen
  allows.
- The grid shader gets the framebuffer pixel (`u_px`) for its antialiasing,
  and `u_scale` (location 6) for the dots' and lines' size, so they keep
  their size on screen.
- The 7-segment display's fills are smoothed by a line one framebuffer pixel
  wide (`r.g.pixel()`), since it only covers the hard edge.
- The component bar draws each icon at `icon_pixels()` (the icon size times
  the scale) into slots of that size. The atlas grew from 1024 to 2048 (225
  large icons at a scale of 3). The fixed `slot_size` of 48 is gone.
- Rejected: scaling the whole canvas world (making `begin_frame` take
  framebuffer pixels). Every pixel-based size, hit tolerance and the mouse
  mapping would have had to change, and the native app would behave
  differently from the web.
- At a fractional scale (e.g. 2.625), the framebuffer size is rounded to
  whole pixels, so it may be up to a pixel off from the shown size. That is
  a much smaller resampling than before.

### Testing

- Native build. All 9 ctest suites pass.
- Native GUI at scale 1 (VNC, X11): `counter_6502` opens with the same grid,
  symbols, text and component bar as before, and no shader errors.
- Web build (Emscripten 6.0.13) in headless Chromium with
  `--force-device-scale-factor=2` (`devicePixelRatio` 2, canvas 2400 pixels
  for a 1200 pixel window), over the DevTools protocol:
  - New cell, then an AND gate and a switch placed from the component bar.
    Each landed where it was clicked.
  - Screenshots at device resolution: the gate's outline, the pins, the
    labels, the grid dots and the bar's icons have one-pixel antialiased
    edges, not stretched ones.
  - The simulation view (Simulate) is sharp too.

### Recommended features

- Snap the component bar's position to framebuffer pixels at fractional
  scales, so its icons fall on whole pixels.

### Outstanding issues

- Not tried on a real hi-dpi browser or a Retina Mac, only Chromium's forced
  scale factor.
- The symbol editor wasn't looked at in the browser test, but it goes
  through the same `canvas_widget` as the cell editor.

### Next steps

- Check the deployed site on a phone or a hi-dpi laptop after the next
  version tag.
- Try the web build in Firefox and Safari.

## 2026-10-27 11:36 +0300

### Changes made

- **Ports turn and mirror:** a cell's input, output and inout ports in its
  schematic can be rotated and mirrored like instances. Before, rotating a
  selection only moved the ports, which kept pointing left or right.
- **Shared rotation and mirror controls:** the selection panel's Rotation
  and Mirror fields are one helper for instances and ports.

### Features implemented

- **Placing a port** (Input or Output tool): R rotates the preview (Shift+R
  the other way) and M mirrors it, as when placing a cell.
- **Selected ports:** Ctrl+R, Ctrl+Shift+R and Ctrl+M turn them with the
  rest of the selection. A single selected port turns about its own
  position.
- **Pasting:** R and M turn pasted ports with the other pasted elements.
- **Port panel:** Rotation (0, 90, 180, 270) and Mirror. With several ports
  selected, the Ports section has them too, showing "mixed" where they
  differ.
- **Drawing:** the stub runs towards the turned connector. The label stays
  upright on the side away from it: right- or left-aligned for horizontal
  ports, centered above or below for vertical ones. While simulating, the
  value of a vertical bus port is shown to its right, since the stub and
  the label take the space above and below.

### How it works

- `model::port` has a `transform xf`, the same type instances use. It turns
  the connector about `pos`. Unturned, an input's wire leaves to the right
  and an output's to the left, as before.
- `model::connection_point` turns the ±2 grid offset with `p.xf.apply`.
  Everything that finds where wires meet a port goes through it (the
  netlist, `normalize`, hover and sim hit tests), so none of them needed
  changes.
- `render::draw_port` takes the stub's direction from `pos` to the
  connection point. `draw_port_value` puts a vertical bus port's value
  beside it.
- JSON: `rotation` and `mirror` on a port, written only when set, the same
  keys as on instances. Old files load unturned. A rotation from the file
  is reduced to 0–3.
- `schematic_editor`: `port_tool` uses `placing_transform_shortcut`.
  `paste_tool::transform` and `transform_selection` apply
  `xf.then(turns, mirror)` to ports. The new template `edit_transforms`
  (in `schematic_editor.cpp`) gives the Rotation and Mirror controls for
  several instances or ports.
- `cell::set_ports` (ports replaced by name, e.g. for HDL cells) keeps a
  port's `xf` along with its position.
- Why a full transform and not a "side" (left, right, up, down): it is the
  type instances already use, with `then` for turning selections, the same
  JSON keys and the same inspector. A mirrored port is the same as one
  turned 180°, which is fine.
- Only the port in the cell's own schematic turns. Where its pin sits on
  the cell's symbol is still set by the automatic symbol or the Symbol
  editor.

### Testing

- `model_test`, new `test_port_transform`:
  - The connection point for inputs and outputs at different rotations and
    with mirroring.
  - An input turned 90° and an output turned 90° below it, joined by a
    vertical wire, are on one net (`netlist::extract`).
  - JSON round trip, and no `rotation`/`mirror` keys when unturned.
  - `set_ports` keeps each port's transform.
- All 9 ctest suites pass.
- **GUI** (private Xvnc, screenshots) with a test cell holding inputs and
  outputs at every rotation, some mirrored, and 8-bit outputs:
  - Each port's stub, connector dot and label face the right way, and the
    labels stay readable.
  - The vertical a→y pair joined by a wire shows as connected (no loose
    pin dots).
  - Ctrl+R on a selected output turned it about its own position.
  - The Input tool's preview turned with R and mirrored with M.
  - In the simulation, the vertical 8-bit outputs show `zzzzzzzz` to their
    right, not over the stub.
- Not looked at in the GUI: the port panel's new Rotation and Mirror
  fields. They are the same widgets as the instance panel's.

### Recommended features

- Turn a port with the R and M keys while dragging it, as in some other
  schematic tools.

### Outstanding issues

- None known.

### Next steps

- None planned for this feature.

## 2026-10-27 13:15 +0300

### Features implemented

- **Framebuffer** (Input/Output, kind `framebuffer`): a picture of width ×
  height pixels. A circuit writes one pixel per clock. While simulating,
  the picture is shown in its own window.
  - **Parameters:**
    - Width and Height: 1–1024 pixels, default 64 × 64.
    - Format:
      - `rgb888`: 24-bit data, red in bits 23–16, green 15–8, blue 7–0.
      - `grey8`: 8 bits, black to white.
      - `mono`: 1 bit, lit or black.
    - Mono color: the lit color for `mono`, one of the LED colors, white by
      default.
  - **Pins** (box "FB", all on the left): `clk`, `we`, `rst`, `x`, `y`,
    `data`. `x` and `y` are just wide enough for the size (e.g. 7 and 6
    bits for 100 × 37). `data` has the format's width.
  - **On the rising edge of clk:**
    - `rst` 1 clears the picture to black and writes nothing. This is a
      synchronous reset, like the Counter's.
    - Otherwise `we` 1 writes `data` at (x, y). `we` not connected also
      writes, so simple circuits need fewer wires.
    - Nothing is written for x or y outside the picture, or for an unknown
      x, y, `we` or `rst`.
    - Data with unknown bits is written as magenta, so bad data shows up
      instead of looking black.
  - **Simulation reset** clears the picture. The properties panel shows how
    many pixels were written.
- **Framebuffer window** "Framebuffer: <instance path>":
  - One for each framebuffer in the hierarchy. It opens centered when the
    simulation starts.
  - **Zoom:** Fit (the largest whole scale that fits, smaller only if it
    must) or 1×, 2×, 4× or 8×. Pixels stay sharp.
  - **Hover:** shows the pixel's x, y and color (#RRGGBB).
  - **Reopen:** after closing it, the instance's right-click menu has "Show
    framebuffer".

### Changes made

- README: the framebuffer is listed with the builtin parts.
- Component bar: the framebuffer has an icon, but isn't in the bar's
  shortlist (`shown_kinds`), the same as the Terminal. It is in the library
  and the place popup.

### How it works

- `sim::pixel_buffer` (`src/sim/component.h`): width, height, RGBA pixels
  (red in the low byte, row 0 at the top), a mutex and a version counter.
  - The component writes it on the simulation thread under the mutex and
    bumps the version on every change.
  - New virtual `component::pixels()` returns the component's buffer, null
    by default. It is the same buffer for the component's whole life.
- `simulator::displays()`: `add_instance` collects `{flat component index,
  instance path, buffer}` for components with pixels. The list is fixed
  once built, so the UI reads it directly, like scopes.
- `framebuffer_component` and `framebuffer` (`src/builtin/io.cpp`):
  - The edge detection is the same as `register_component`.
  - `address_width(n)` is `bit_width(n - 1)`, at least 1.
  - `mono_rgba` holds its own RGB table for the LED color names, since
    builtins don't depend on the render theme.
- `sim_session` (`src/editor/sim_session.*`):
  - Keeps a `display_window` for each display: an `Hd::Texture` with
    `GL_NEAREST` filtering, made on the first draw (the GL context exists
    then), the uploaded version, a local copy of the pixels, `open` and
    `zoom`.
  - `displays_gui()` is called from `app.cpp` next to `waveforms_gui`. It
    copies the pixels under the lock only when the version changed, then
    uploads with `glTexSubImage2D`.
  - `show_display` and `has_display` serve the instance menu in
    `sim_view::instance_menu`.
- **Why not `sample_view`:** it copies a `vector<float>` into every
  published snapshot, about 60 times a second. For up to 1024 × 1024 pixels
  that is far too much. With a shared buffer and a version number, the UI
  copies only when something changed, once per frame at most.
- **Why `we` defaults to writing when not connected:** RAM reads with an
  unconnected `we`, but a framebuffer has nothing to read, and a part that
  ignored every clock until `we` is wired would look broken.
- **Rejected:**
  - An asynchronous reset: it would differ from the Counter.
  - Drawing the picture inside the symbol: the canvas renderer has no
    textured quads yet.

### Testing

- `sim_test`, new `test_framebuffer`:
  - Port widths for several sizes and formats.
  - Writing only on the rising edge. A floating `we` writes; `we` 0 or x
    doesn't.
  - Nothing written outside the picture or at an unknown address (the
    version doesn't change).
  - Magenta for unknown data. RGB888 byte order, grey8 and mono in green.
  - `rst` clears at the edge without writing, and `reset()` clears too.
  - The "pixels written" count.
  - `simulator::displays()` lists a framebuffer inside a subcircuit with
    its path (`U2/U1`) and flat component index.
- `model_test`: the icon passes the builtin icon checks.
- All 9 ctest suites pass. The web build (Emscripten) compiles.
- **GUI** (private Xvnc, screenshots), with a 1 kHz clock, x and y counters
  (y counting on x's carry) and an 8-bit counter as data, into a 16 × 16
  grey8 framebuffer:
  - The window opened centered when the simulation started.
  - Running, it filled with the expected black-to-white gradient.
  - Hovering x 13, y 12 showed #CDCDCD (index 205).
  - Reset cleared it.
  - Closing it and choosing "Show framebuffer" from the instance's menu
    reopened it.
  - Fit (10×) and 1× zoom work.

### Recommended features

- **VGA monitor:** a variant driven by a pixel clock with hsync, vsync and
  RGB, for designs that generate video timing.
- **Address-bus mode:** a single `addr` = y · width + x input, so a CPU
  can write it like memory. The 6502 example could then draw.
- **Indexed color:** a palette (2, 4 or 8-bit index) loaded from a file.
- **Double buffering:** a swap/vsync input, so frames don't tear while
  they are drawn.
- **Save picture:** save the window's picture as a PNG.
- **On-canvas preview:** a live thumbnail inside the symbol.
- **Plugins:** `pixels()` in the plugin SDK, so C models can have screens.
- An example cell with a framebuffer in `workspace/examples`.

### Outstanding issues

- Not run in a browser, only compiled for the web.
- Snapshots (`save_state`) don't include the picture. They are only used
  by plugins so far.
- A very large framebuffer written every clock copies its whole picture
  once per UI frame (4 MB at 1024 × 1024). That is fine at 60 fps, but a
  dirty-rectangle upload would be lighter.

### Next steps

- Decide which of the recommended variants to add first. The address-bus
  mode is the smallest and makes the CPU examples able to draw.

## 2026-10-27 13:23 +0300

### Changes made

- **Framebuffer window, sharp pixels:** the picture is drawn without
  texture filtering. Before, it was blurred by linear filtering, which
  made e.g. a 16-step grey ramp look like a smooth gradient.
- **Fit follows the window:** Fit scales the picture to fill the window
  (keeping its aspect ratio) as the window is resized. Before, it used the
  largest whole-number scale, so the picture only changed size at 2×, 3×
  and so on, and seemed not to follow the window.

### How it works

- **Why the old setting didn't work:** the texture's `GL_NEAREST`
  parameters were overridden by the ImGui OpenGL backend. Since its
  2026-04 update, it binds its own sampler objects (`glBindSampler`, linear
  by default) when GL 3.3 or ES 3 is available. A bound sampler takes
  precedence over the texture's own parameters.
- `sim_session::display_gui` adds `platform_io.DrawCallback_SetSamplerNearest`
  to the window's draw list before the `Image`, and
  `DrawCallback_SetSamplerLinear` after it, so the rest of the UI keeps
  linear sampling. Both are checked for null, for backends without them.
  The texture keeps `GL_NEAREST` for contexts without samplers.
- Fit: `scale = min(avail.x / width, avail.y / height)`. The fixed zooms
  (1×, 2×, 4×, 8×) keep whole-number scales. At a non-whole Fit scale,
  pixel columns can differ by one screen pixel in width. That is the cost
  of filling the window exactly.

### Testing

- **GUI** (private Xvnc, screenshot while dragging the window's corner):
  - The 16 × 16 picture grew with the window to about 19× (not a whole
    number).
  - Each pixel row is a sharp band.
- All 9 ctest suites pass.

### Outstanding issues

- The editor and sim canvases (`canvas_widget.h`, `sim_view.cpp`) are also
  sampled through the backend's linear sampler. They are drawn 1:1, so
  there is no visible effect.

## 2026-10-27 13:40 +0300

### Features implemented

- **Ports take the settings of the pin they're connected to:** when one of
  the cell's ports gets connected to an instance pin, it copies the pin's
  settings into the fields that still have their defaults. This happens
  whether the port is placed on the pin, a wire is drawn, or something is
  moved or pasted.
  - **Width, bundle type and view:** copied if the port is still plain 1
    bit and all connected pins agree.
  - **Name:** copied if the port still has a default name (`in1`, `out2`,
    `io3`) and exactly one pin is connected. It is made unique, e.g. `d2`.
  - **Description:** copied if the port has none and exactly one pin is
    connected.
  - **Only on a new connection:** it happens once, when the port gains a
    connection. Changing an instance's width afterwards, or setting the
    port back to 1 bit yourself, doesn't change it again.
  - Pins that take the width of their net (e.g. a tunnel's) don't count.
- **Add ports for pins:** right-click an instance (Select tool) or use the
  button in its properties. For each pin that isn't connected, it adds a
  port with the pin's direction, name, width, bundle type and view,
  description and number format.
  - The port sits four grids out from the side of the symbol the pin is
    on, turned to face the pin, with a two-grid wire to it.
  - The new ports are selected and are one undo step.

### How it works

- New `src/netlist/port_setup.h/.cpp` (in `logic_core`, so it is unit
  tested):
  - `default_port_name(name)`: `in`, `out` or `io` followed by digits, the
    names `cell::add_port` gives.
  - `linked_pins(cell, netlist)`: the (instance id, pin index) pairs each
    port's net reaches, by port id.
  - `adopt_pin_settings(ws, cell, port ids)`: the copying above, for the
    listed ports only.
  - `add_ports_for_pins(ws, cell, instance)`:
    - The side is the edge of the symbol's bounds nearest to the pin,
      turned with the instance's transform.
    - The port's rotation is the one that puts its connection point on the
      wire's end.
- `schematic_editor`:
  - `draw` keeps `linked` (the pins each port was connected to in the last
    frame) next to `nets`.
  - At the end of `normalize()`, after every edit, it extracts the netlist
    again and collects the ports that gained a pin. It runs
    `adopt_pin_settings` on them and marks the cell modified, so placing a
    port and copying the settings are one undo step.
  - The right-click menu (`instance_menu`) opens on an instance when the
    Select tool is active. Before this, the editor had no right-click
    menu; a right drag still pans.
- **Why "newly connected" and not "has default values":** with defaults
  alone, a port you set to 1 bit on purpose would jump back to the pin's
  width at the next edit anywhere in the schematic. Comparing with the
  previous frame's connections needs no new field in the file format.
  Undo is handled too, since `linked` is refreshed every frame.
- **Why copy and not inherit:** a cell port can't take its width from the
  net (as a tunnel's pin does), because the cell's interface has to be
  fixed for its symbol and for the parent schematics.
- Bundle views are copied as they are: a port wrapping an instance's
  master pin is a master pin of the outer cell too.

### Testing

- `model_test`, new `test_port_setup`:
  - `default_port_name` cases.
  - Add ports for pins on an 8-bit register: names, directions, widths and
    descriptions. Positions and connection points on the left and right.
    Every port is on its pin's net. A second run adds nothing, and a
    second register gets `d2`.
  - A port placed on a pin takes the width, name and description, and a
    second run changes nothing.
  - A port's own name, width and description are kept. Only listed ports
    change.
  - One port fanning out to two pins: an 8 and 8 bit pair gives the width
    but no name. An 8 and 4 bit pair gives nothing.
- All 9 ctest suites pass.
- **GUI** (private Xvnc, screenshots, on the framebuffer demo):
  - Right-clicking the framebuffer shows the menu.
  - Add ports for pins added wired `we` and `rst` ports for its two free
    pins.
  - An Input port placed with its connection on a counter's free 8-bit
    `d` pin became `d[8]`.
  - One Undo removed the port completely, not just the copied settings.

### Recommended features

- **Inputs on the left, outputs on the right:** an option for Add ports for
  pins to put every input on the left and every output on the right in a
  column, for a tidier wrapper cell.
- **Selection:** "Add ports for pins" for every instance in a selection.
- **Port panel:** a "Match connected pin" button to copy the settings
  again on request, e.g. after an instance's width parameter changed.

### Outstanding issues

- Add ports for pins doesn't avoid what's already there. In a crowded
  schematic, a new port can sit on another wire (seen in the GUI test,
  without a connection).
- Ports added for closely spaced top or bottom pins (one grid apart) have
  overlapping labels.
- Width changes of a port aren't propagated to the other side of the
  connection (e.g. a tunnel); only the port takes settings.

### Next steps

- Lay ports out in columns when wrapping a whole instance (see recommended
  features).

## 2026-10-27 13:47 +0300

### Features implemented

- **Random** (Logic sources, kind `random`): a new pseudo-random number on
  each rising clock edge, from xoshiro256**.
  - Parameters: Bit width (1 to `max_width`, default 8) and Seed (default
    1).
  - Wider than 64 bits, each 64-bit word gets its own number.
- **LFSR** (Logic sources, kind `lfsr`): a linear feedback shift register.
  - Each rising clock edge shifts it left by one. Bit 0 gets the XOR of the
    tap bits.
  - It goes through all 2^n − 1 values except 0 before repeating.
  - Parameters: Bit width (2–64, default 8) and Seed, its first value. Bits
    above the width are ignored, and 0 is taken as 1 (a register of zeros
    would stay at zero).
- **Both:**
  - **Pins:** `clk`, `en`, `rst` on the left, `q` on the right.
  - **On the rising edge of clk:** `rst` 1 starts over from the seed.
    Otherwise `en` 1, or `en` not connected, steps to the next number. An
    unknown `en` or `rst` at the edge makes `q` unknown until the next
    step.
  - **Repeatable:** `q` holds the first number right after a reset. The
    same seed gives the same numbers on every run and after Reset.
  - The properties panel shows `q`. Each has a component bar icon (a chip
    with "?" or "LF"), though neither is in the bar's shortlist.

### How it works

- New `src/builtin/random.cpp` with `add_random`, registered in
  `builtin.cpp`.
  - `generator_component` holds the clock edge and control logic shared by
    the two parts (the same rules as the Counter). Subclasses give
    `restart`, `step` and `current`.
  - `random_component` seeds xoshiro256**'s four words with splitmix64 from
    the seed, as its authors recommend.
  - `lfsr_component` is a Fibonacci register:
    `state = (state << 1 | parity(state & taps)) & mask`.
- **LFSR taps:** the maximal-length table for 2 to 64 bits from Xilinx
  XAPP052.
  - **Checked offline:** a script tested every polynomial for primitivity.
    It checked that x^(2^n − 1) = 1 mod p and that x^((2^n − 1)/q) ≠ 1 for
    every prime factor q of 2^n − 1, with Miller–Rabin and Pollard rho for
    the factoring. All 63 pass.
  - A polynomial and its reciprocal are both primitive, so the shift
    direction doesn't affect the period.
- **Why xoshiro256\*\*:** it is fast, passes the usual statistical tests and
  needs no library. `std::mt19937` would also work, but its state is large
  and its seeding from one number is weak.
- **Rejected:**
  - Seeding from system entropy: runs wouldn't be repeatable.
  - A seed pin: the user chose the seed parameter only.

### Testing

- `sim_test`, new `test_random`:
  - Random: two instances with the same seed give the same 20 numbers, and
    a different seed gives different ones. The first number is there after
    reset.
  - A clock that stays high doesn't step; `en` 0 holds; an unknown `en`
    gives an unknown `q`.
  - `rst` and `reset()` start over.
  - All 256 byte values appear within 4096 draws, and the bits are
    balanced within 5%.
  - A 130-bit width gives different words.
  - LFSR:
    - The full period 2^n − 1, never 0, for every width from 2 to 20 bits.
    - Seed masking, and 0 taken as 1.
    - The exact 4-bit sequence 1, 2, 4, 9, 3.
    - A 64-bit register with an all-ones seed.
- `model_test`: the new icons pass the builtin icon checks.
- All 9 ctest suites pass. The web build compiles.
- **GUI** (private Xvnc, screenshot): a 24-bit Random feeding the data of a
  16 × 16 rgb888 framebuffer, with counters for x and y, fills it with
  random colors.

### Recommended features

- **Seed pin:** a load input that takes a new seed from a pin on the clock
  edge.
- **LFSR serial output:** a 1-bit output with the bit shifted out, for
  noise or PRBS test patterns.
- **Range:** a parameter for numbers from 0 to max − 1 (rejection
  sampling), e.g. for dice.

### Outstanding issues

- None known.

### Next steps

- None planned for these parts.

## 2026-10-27 13:53 +0300

### Features implemented

- **LFSR Taps parameter:** you can set the feedback taps yourself. Two
  forms:
  - **Bit numbers** from 1, separated by spaces or commas, e.g. `8 6 5 4`.
    This is the numbering of the built-in table and of Xilinx XAPP052.
  - **A hex mask**, e.g. `0xB8` (bit 0 = tap 1).
  - **Empty** (the default) keeps the maximal-length taps for the width.
  - **A mistake** shows a problem on the instance and the maximal-length
    taps are used. Mistakes are:
    - a bit number of 0 or above the width;
    - something that isn't a number;
    - a mask with bits above the width, or no bits.
  - The properties panel shows the taps in use (`taps`) as well as `q`.
- **Full cycle:** taps that aren't a primitive polynomial give a shorter
  cycle, e.g. `4 2` repeats after 6 values. The part's description now says
  the full 2^n − 1 cycle is for the default taps.

### How it works

- `parse_taps(text, width, error)` in `src/builtin/random.cpp` turns the
  text into a mask: an optional for a mistake, the default mask for empty
  text. `default_taps(width)` builds the mask from the table.
- `lfsr_primitive::check` reports the error with ", the maximal-length taps
  are used". `create` falls back to the default mask, the same way other
  parts' `check` describe what `build` works around. The instance panel
  shows the message (`workspace::check_instance`).
- `lfsr_component` takes the mask in its constructor and adds a `taps`
  inspect item.
- **Why both forms:** bit numbers match the tables in data sheets and
  application notes. Masks are what code and other tools often use.
- **Not checked:** whether custom taps give a maximal cycle. That needs
  factoring 2^n − 1, which is too slow to do while editing for wide
  registers. The description says so instead.

### Testing

- `sim_test`, in `test_random`:
  - `4 3`, `4,3`, ` 3, 4 `, `0xC`, `0xc` and empty all give mask 0xC and a
    period of 15.
  - `4 2` gives mask 0xA and a period of 6.
  - `0xB8` and `8 6 5 4` give the same mask.
  - Each mistake gives a problem and falls back to 0xB8: `9`, `0`, `8 x`,
    `0x1FF`, `0x`, `0x0`, `0xG1`, `-3`, `1234`.
- All 9 ctest suites pass.
- GUI: not looked at in the properties panel; the private display's
  default layout doesn't show it. The message goes through the existing
  `check_instance` path, the same as e.g. the decoder's.

### Recommended features

- A "maximal?" note for custom taps of up to about 24 bits, found by
  running the register through its cycle.
- **Galois form:** a Form choice of Fibonacci or Galois, since some
  designs need the Galois sequence.

### Outstanding issues

- None known.

### Next steps

- None planned.

## 2026-10-27 14:22 +0300

### Features implemented

- **Toolbar icons:** the editor and simulation toolbar buttons show icons
  from Google's Material Icons font instead of words. Every button is a
  square icon button with a tooltip that names it and gives its shortcut.
  - **Editor toolbar:**
    - Simulate (`play_circle`) and Stop (`stop_circle`), and Cancel
      (`cancel`) while HDL models build.
    - Select (`highlight_alt`), Wire (`polyline`), Input (`input`), Output
      (`output`) and Cell (`memory`, a chip).
    - Undo (`undo`) and Redo (`redo`).
  - **Simulation toolbar:**
    - Waveforms (`ssid_chart`) and Signals (`playlist_add`).
    - Reset (`skip_previous`) and Step (`skip_next`).
    - Run (`play_arrow`) and Pause (`pause`), highlighted while running.
- The Symbol editor's tool buttons and the menus keep their words.

### Changes made

- New `res/fonts/MaterialIcons-Regular.ttf` (349 KB, about 2,100 icons)
  with its licence, `res/fonts/MaterialIcons-LICENSE.txt` (Apache 2.0).
  - Both are packed into the program and the web build with the other
    resources. The web build's resource archive is now 539 KB.
- README: a "Third-party assets" section names Roboto and Material Icons.
- Tooltips start with the button's name, e.g. "Wire: draw wires, ..." and
  "Undo (Ctrl+Z)", since the word is no longer on the button.

### How it works

- `ui::load_fonts` (`src/ui/fonts.cpp`) has a new optional `icons_ttf`. The
  icon font is merged (`MergeMode`) into the interface font after it is
  loaded, whichever font that is: the built-in Roboto or one chosen in
  Settings. A font change reloads both. The waveform viewer passes no
  icons and is unchanged.
- **Vertical position:** Material Icons sit on the baseline and are a
  whole em high. Roboto leaves 0.24 em below the baseline, so the icons
  sat about 0.14 em too high.
  - `GlyphOffset.y = 0.14 × 20` lowers them. ImGui scales that offset by
    the font size over the base font's `SizePixels`.
  - So the interface fonts are now added with a reference size of 20
    (`reference_size`). With imgui 1.92's dynamic fonts that size only
    serves as this reference; the shown size is still
    `style.FontSizeBase`.
- New `src/ui/icons.h`:
  - `ui::icon::*`: the UTF-8 of each icon's codepoint, with the icon's
    Material name in a comment. The header says where to look up more.
  - `ui::icon_button(icon, tip, active, enabled)`: a square button
    (`GetFrameHeight()` on each side), highlighted while active.
    - Only the button is disabled when `enabled` is false. The tooltip is
      set after `EndDisabled`, so it isn't drawn faded on a disabled Undo
      or Redo.
- `editor::icon_tool_button` (`context.h`) is `icon_button` followed by
  `SameLine`, the icon version of `tool_button`, which the Symbol editor
  still uses.
- **Decisions, made with the user:**
  - Material Icons, the static font: the newer Material Symbols ships only
    as a 10 MB variable font, which ImGui can't vary.
  - The whole font, not a subset: adding an icon later only needs its
    codepoint.
  - Icons only, with tooltips.
- **Rejected:** drawing the icons with ImDrawList, and Font Awesome or
  Lucide. Both were compared on a mock-up page first. An icon font makes
  more icons cheap later.

### Testing

- All 9 ctest suites pass. The web build compiles and packs the font.
- **GUI** (private Xvnc, screenshots enlarged 2×):
  - The editor toolbar shows all nine icons centered in their buttons,
    after the vertical offset was added. Before it, the icons sat about 3
    pixels too high.
  - The simulation toolbar shows Stop, Waveforms, Signals, Reset, Step and
    Run.
  - While running, Run turns into a highlighted Pause, and its tooltip
    says "Pause".
- Not checked in the GUI:
  - A system font picked as the interface font. The icons are merged the
    same way after it.
  - The light theme. Icons are glyphs, so they take the text color.

### Recommended features

- Icons for the Symbol editor's tools (pin, line, curve, arc, circle,
  text, rectangle) and in the menus (e.g. File > Save).
- A setting to show words next to the icons, for new users.
- Subset the font with `pyftsubset` once the set of icons settles, to
  save about 340 KB.

### Outstanding issues

- The 0.14 em offset was measured with Roboto. A system font with very
  different ascent and descent may put the icons a pixel or two off
  center.

### Next steps

- Use the icons in the Symbol editor and the menus (see recommended
  features).

## 2026-10-27 14:31 +0300

### Changes made

- **Icons centered exactly in their buttons:** reported from the
  Simulate button. Measured pixel by pixel in the app, the icons were about
  one pixel right and one pixel up. Now the gaps on opposite sides are
  equal: 4 or 5 px on each side of every icon.

### How it works

- **Why they were off:**
  - ImGui centers a button label by its advance width and the font's line
    height, both rounded to pixels, not by the visible shape.
  - The 0.14 em `GlyphOffset` from the previous entry only roughly
    compensated vertically.
  - Centering the glyph's quad isn't exact either: FreeType sizes the
    bitmap from the outline's control points, which stick out past curves.
    The undo arrow's arc made its quad taller than its ink, so a centered
    quad left it one pixel low.
- **Fix:** `ui::icon_button` moved from `icons.h` into a new
  `src/ui/icons.cpp`.
  - The button is drawn with an empty label (`##icon`, under `PushID` of
    the icon).
  - The icon is drawn with `AddText` so that the middle of its visible
    pixels lands on the middle of the button, rounded to whole pixels.
  - `ink_box(glyph)` scans the glyph's few hundred pixels in the font
    atlas (`io.Fonts->TexData`, which imgui 1.92 keeps in memory). It
    counts pixels over a quarter covered and maps the box back to the
    quad's coordinates.
  - It falls back to the quad if the atlas has no pixels. It is done every
    frame, about 15 icons of about 20 × 20 pixels, so nothing is cached
    that the atlas could invalidate.
- `load_fonts` no longer sets `GlyphOffset` or a reference size, since
  `icon_button` centers the icons itself. Icons used inside running text
  would sit high again, but none are.
- **Rejected:**
  - Tuning `GlyphOffset` further: it is the same for every icon, and the
    error depends on the icon and the font.
  - Using the font's own glyph bounding boxes: they are the control boxes
    again.

### Testing

- **GUI** (private Xvnc, gaps of the light pixels inside each button,
  measured from the raw screen pixels):
  - Simulate (play in a circle): L4 R4 T4 B4. Before: left 5, right 3,
    top 3, bottom 5.
  - Select, Wire, Input, Output and Cell: equal left and right, equal top
    and bottom (4 or 5 px).
  - Undo, enabled after an edit: L4 R4 T9 B9.
  - Redo, greyed out: within a pixel. Its faint edges fall around the
    brightness threshold.
- All 9 ctest suites pass. The web build compiles.

### Outstanding issues

- None known.

## 2026-10-27 14:54 +0300

### Features implemented

- **Toolbar icon size:** the toolbar buttons are bigger and have three
  sizes, like the component bar: Small (30 px), Medium (38 px) and Large
  (48 px). Before, a button was as tall as a text button, 26 px at the
  default font size.
  - **Choosing it:** right-click any toolbar icon, or use View > Toolbar
    icons.
  - **Saved:** as `ToolbarIcons=small|medium|large` with the other
    settings.
  - A button is never smaller than a text button, so a large interface
    font still fits.
- **Closer buttons:** 3 px between the buttons of a group, instead of the
  usual 8. The wider gap or the separator line stays between groups
  (Simulate | tools | Undo/Redo, and Waveforms/Signals | Reset/Step/Run).
- **Centered line:** other items on a toolbar line stay their normal size,
  centered on the taller buttons. These are the time, warnings, "Shift:
  keep placing", the Max checkbox and the speed field.

### How it works

- `src/ui/icons.*`:
  - `toolbar_size` with `get_toolbar_size`/`set_toolbar_size` (a global, as
    the font size is), `toolbar_button_size()`, `toolbar_gap()` and
    `toolbar_size_menu()`.
  - `icon_button`:
    - The icon's em is three quarters of the button. The glyph is baked at
      that size (`GetFont()->GetFontBaked(size)`), so it is rasterized
      sharp, not scaled.
    - It is still centered by its visible pixels (`ink_box`).
    - A right click opens the size menu (`BeginPopupContextItem`).
  - **Text alignment:** while drawing the button, `icon_button` pushes a
    `FramePadding.y` of (button − font size) / 2. ImGui then sets the
    line's text baseline from it, so `Text` items later on the same line
    land in the middle without any extra code.
  - **Framed controls:** a checkbox or an input isn't moved that way.
    `toolbar_center()` moves the next one down by half the difference in
    height.
- `editor::icon_tool_button` and the toolbars use
  `SameLine(0, toolbar_gap())` between buttons of a group.
- `app`: the `ToolbarIcons` setting is read and written, added to the
  settings tuple (so a change marks the settings as unsaved), and a View >
  Toolbar icons submenu.
- **A drawn-icons trial:** the same buttons were also tried with icons
  drawn by the draw list (16 × 16 grid, the port symbols for Input and
  Output, a square wave for Waveforms), with the same size setting. That
  version is kept in a git stash ("drawn toolbar icons with size
  setting") for comparison; this entry is the icon font version.

### Testing

- **GUI** (private Xvnc, screenshots):
  - Small: 3 px gaps, with "Shift: keep placing" centered on the buttons.
  - The right-click menu shows Small, Medium and Large icons with a check
    mark.
  - Large in the editor and simulation toolbars: the Max checkbox, speed
    field and time are centered.
  - Measured at Large, the icons have equal gaps on opposite sides (9–10
    px), within a pixel for anti-aliased edges.
  - In the drawn trial, which uses the same settings code, the choice was
    written as `ToolbarIcons=large` and restored after a restart.
- All 9 ctest suites pass. The web build compiles.

### Recommended features

- The same size setting for the Symbol editor's toolbar once it has
  icons.

### Outstanding issues

- None known.

## 2026-10-27 15:02 +0300

### Changes made

- **Font Awesome instead of Material Icons:** the toolbar icons come from
  Font Awesome Free 6, solid style, with icons that fit the app better:

  | Button | Material Icons | Font Awesome |
  | --- | --- | --- |
  | Select | `highlight_alt` | `arrow-pointer` (a mouse cursor) |
  | Wire | `polyline` | `pencil` |
  | Input / Output | `input` / `output` | `right-to-bracket` / `right-from-bracket` |
  | Cell | `memory` | `microchip` |
  | Waveforms | `ssid_chart` | `wave-square` (a digital square wave) |
  | Signals | `playlist_add` | `square-plus` |
  | Undo / Redo | `undo` / `redo` | `arrow-rotate-left` / `arrow-rotate-right` |
  | Simulate / Stop / Cancel | `play_circle` / `stop_circle` / `cancel` | `circle-play` / `circle-stop` / `circle-xmark` |
  | Reset / Step / Run / Pause | `skip_previous` / `skip_next` / `play_arrow` / `pause` | `backward-step` / `forward-step` / `play` / `pause` |

- **Files:**
  - `res/fonts/fa-solid-900.ttf` (416 KB) and
    `res/fonts/FontAwesome-LICENSE.txt` replace the Material Icons font and
    its licence.
  - The web build's resource archive is now 605 KB.
- **Icon size:** an icon's em is 60% of the button, down from 75%. Font
  Awesome's icons fill most of their em; at 75% the square wave nearly
  touched the button's edges.
- **README:** the third-party section names Font Awesome Free. The font is
  under the SIL OFL 1.1, the icons under CC BY 4.0.

### How it works

- Only the data changed: the codepoints in `ui::icon` (`src/ui/icons.h`),
  the font's path in `app::update_font`, and the size factor in
  `ui::icon_button`.
- Centering by the visible pixels (`ink_box`) and the size setting work for
  any icon font unchanged.
- **Checked before use:** every codepoint is in the free solid font. The
  font's cmap was read with a small script, because IconFontCppHeaders'
  `IconsFontAwesome6.h`, used to look up the codepoints, also lists Pro
  icons.
- **Kept in git stashes for comparison:** the Material Icons version
  ("material icons toolbar with size setting") and the drawn version
  ("drawn toolbar icons with size setting").

### Testing

- **GUI** (private Xvnc, screenshots enlarged 2×):
  - The editor toolbar at Small and Large.
  - The simulation toolbar at Large: Stop, the square wave, the plus,
    Reset, Step and Run.
  - The right-click size menu. Icons are centered and sharp at both sizes.
- All 9 ctest suites pass. The web build compiles and packs the font.

### Outstanding issues

- None known.

## 2026-10-27 15:12 +0300

### Changes made

- **Toolbar icons centered to the pixel:** reported as slightly to the
  right (Step, Play and others).
  - Measured in the app, many icons had one more pixel on the left than on
    the right, and one more above than below.
  - Now the left and right gaps are equal for every icon, at all three
    sizes, in both toolbars.
  - The top and bottom gaps are equal too, except for Select (Large) and
    Input/Output (Medium, Large). Their width and height are odd and even
    at every size, so a square button leaves them one pixel to spare
    vertically.

### How it works

- **Why they were off:**
  1. **Parity:** when the button's size minus the icon's drawn size is
     odd, one pixel is left over. The rounding in `icon_button` always gave
     it to the left/top, which moved the icon right and down.
  2. **Sub-pixel drawing:** the icon font was oversampled. stb_truetype
     then places glyphs at fractions of a pixel (`sub_x`), and its
     prefilter smears the bitmap, so the measured ink box and the drawn
     pixels differed by a fraction.
  3. **Threshold:** `ink_box` counted pixels from a quarter covered. On
     anti-aliased edges that is wider than where the eye sees the edge.
- **Fixes:**
  - `load_fonts`: the icon font is merged with `PixelSnapH` and
    `OversampleH = OversampleV = 1`. Its glyphs are drawn on whole pixels,
    so the ink box is exact. Text fonts keep their oversampling.
  - `ink_box`: pixels at half coverage or more count as drawn.
  - New `icon_size(codepoint, box, want)` in `src/ui/icons.cpp`:
    - It tries the wanted size (60% of the button) and up to 2 pixels
      smaller or larger.
    - It takes the first size whose drawn width (and, if possible, height)
      has the same parity as the button, so the spare space splits evenly.
      Horizontal parity counts double.
    - The result is cached per codepoint, button size and wanted size. A
      pixel's difference in icon size isn't visible; an uneven gap is.
- **Rejected:**
  - Drawing at fractional positions (`ImDrawListFlags_TextNoPixelSnap`):
    centered on average, but every edge would be blurred.
  - Rounding the other way: it only moves the error to the other side.

### Testing

- **GUI** (private Xvnc, gaps of the pixels at least half as bright as the
  icon's peak, measured from the raw screen pixels for each button):
  - **Small, Medium and Large, editor and simulation toolbars:** left gap
    equals right gap for every icon.
  - **Top and bottom:** equal except the three noted above, which are one
    pixel lower.
  - **Before the fix (Small):** left was one more than right on Signals,
    Reset, Step and Run, and on Reset and Step at Medium.
  - A temporary debug print showed the sizes tried for the three
    exceptions: e.g. `arrow-pointer` at 26–30 px is 15×21, 16×23, 17×24,
    17×25 and 18×25, never even in both directions.
- All 9 ctest suites pass. The web build compiles.

### Outstanding issues

- Select, Input and Output can be one pixel low at some sizes (see above).

## 2026-10-27 15:22 +0300

### Changes made

- **Fixed a crash when closing an editor tab while its simulation runs.**
  The same could happen when closing the visible instance tab ("Open in
  new tab") of a simulation.

### How it works

- **Cause, a use after free:**
  - `app::editor_panel` remembers the visible tab's simulation view in
    `active_view` (and an instance tab's in `active_instance_tab`) while it
    draws the tabs.
  - When a tab's × is clicked, the tab is closed after the loop.
    `close_editor` destroys the `cell_editor`, and its `sim_view` with it,
    but `active_view` still pointed at the view.
  - Later in the same frame, `app::hierarchy_panel` called
    `active_view->hierarchy_panel()` on freed memory, and
    `properties_panel` could do the same with `active_instance_tab`.
  - gdb backtrace: `scope_label` ← `sim_view::hierarchy_panel` ←
    `app::hierarchy_panel` ← `app::draw_gui`.
- **Fix** in `app::editor_panel`:
  - When the closed editor is the visible one, `active_cell` and
    `active_view` are cleared before `close_editor`.
  - When the closed instance tab is the visible one, `active_view` and
    `active_instance_tab` are cleared before it is erased.
  - The panels then show nothing for that frame and pick up the next tab in
    the following one.
- The Symbol editor's tabs keep the active page as an id and look it up
  (`find_symbol`), so they don't have this problem.
- Instance tabs of a simulation whose editor is closed are already removed
  at the start of the next frame (`update_simulations`). Until then they
  share the session, which stays alive through its `shared_ptr`.

### Testing

- **GUI under gdb** (private Xvnc):
  - **Before:** opening the framebuffer demo, simulating, running and
    closing its tab gave a SIGSEGV with the backtrace above.
  - **After:** the same steps, no signal; the app keeps running and shows
    the empty editor.
  - With a top cell holding a subcircuit (a clock inside), U1 opened in a
    new tab while running: closing the instance tab, then the top tab
    (still running), gave no crash.
- All 9 ctest suites pass. The web build compiles.

### Recommended features

- Keep the active views as ids or as a `weak_ptr` to the session,
  instead of raw pointers that can outlive their tab.

### Outstanding issues

- None known.

## 2026-10-27 16:10 +0300

### Features implemented

- **Sanitizer build:** `-DLOGIC_SANITIZE=ON` builds everything with
  AddressSanitizer and UndefinedBehaviorSanitizer:
  - **Coverage:** the app, the tests, the Hd submodule and the plugins.
  - **What it reports:** use after free, buffer overflows, leaks and
    undefined behavior, where they happen, even when they wouldn't crash.
    For example in the web build, freed memory stays readable, which is
    why the tab-closing crash didn't show there.
  - **Usage** (now in README > Tests > Sanitizers):
    `cmake -B build-asan -DCMAKE_BUILD_TYPE=RelWithDebInfo -DLOGIC_SANITIZE=ON`,
    `cmake --build build-asan -j4`,
    `ctest --test-dir build-asan --output-on-failure`.

### Changes made

- **Test bug found by ASan:** `test_bundles` in `model_test` read
  `f.bus->id` after `ws.delete_bundle` had freed the bundle. The id is now
  kept before deleting.
- `.gitignore`: `build-asan/`.

### How it works

- **CMake:**
  - `option(LOGIC_SANITIZE)` adds
    `-fsanitize=address,undefined -fno-omit-frame-pointer` with
    `add_compile_options`/`add_link_options` before `add_subdirectory(Hd)`,
    so every target gets them.
  - **The resource packer:** `myar`/`myar-bin` (Hd) run during the build.
    They get the flags removed again, because LeakSanitizer failed the
    build on what the packer doesn't free before exiting.
  - **Test environment:** at the end of `CMakeLists.txt`, every test gets
    `LSAN_OPTIONS=suppressions=tests/lsan.supp` and
    `UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1:suppressions=tests/ubsan.supp`.
- **Suppressions for code we don't change:**
  - `tests/lsan.supp`: `libsimavr.so`. simavr doesn't free its irqs and
    their names (`avr_init_irq`, `avr_alloc_irq`) in `avr_terminate`, about
    50 KB in 720 allocations in `plugin_test`. The AVR plugin frees what it
    owns.
  - `tests/ubsan.supp`: `shift-exponent` in `imgui_widgets.cpp`. imgui's
    `TreeNodeBehavior` shifts by `TreeDepth - 1`, which is −1 for a clipped
    top-level node drawing tree lines (the library panel). It stopped the
    sanitized app at startup.
- **Why undefined behavior is recoverable:** UBSan suppressions only apply
  to recoverable checks, so `-fno-sanitize-recover` was dropped. The tests
  still stop at the first report that isn't suppressed (`halt_on_error=1`).
- **Memory:**
  - A sanitized compile needs several times the memory of a normal one.
    The first try with `-j16` used up WSL's 7 GB and froze it.
  - The README says to keep the jobs low (`-j4` with 8 GB). The rebuild
    here used `-j3`.
- **Rejected:**
  - Patching imgui in the Hd submodule: the submodule is changed only on
    request.
  - Turning leak checking off for all tests: leaks in our own code would go
    unnoticed.

### Testing

- `build-asan`:
  - **First run:** model_test failed with the use after free above, and
    plugin_test with simavr's leaks. The other 7 suites passed.
  - **After the fixes and suppressions:** all 9 suites pass (font_test
    takes about 2 minutes sanitized).
- **The sanitized app** (private Xvnc, run as in the README):
  - It starts, simulates and closes a running tab with no report.
  - **Proof:** with the old tab-closing code put back for a moment, ASan
    reported the bug from the previous entry exactly:
    `heap-use-after-free` in `sim_view::current` ←
    `sim_view::hierarchy_panel` (`sim_view.cpp:793`) ←
    `app::hierarchy_panel`, freed by `cell_editor::stop_simulation` in
    `~cell_editor`. The fix was then restored and the app rebuilt.
- The normal build's 9 suites pass.

### Recommended features

- A CI job that runs the sanitizer build's tests on every push (the
  workflow in `.github/workflows` only deploys the web build).
- The web build with `-fsanitize=address` (Emscripten supports it) for
  browser-only paths.
- Report the imgui shift upstream, then drop the suppression.

### Outstanding issues

- The imgui shift by −1 is real undefined behavior, suppressed rather than
  fixed.
- simavr's leaks are suppressed; they grow with every AVR instance
  created.

## 2026-10-27 16:34 +0300

### Changes made

- **Fixes in the Hd submodule's resource packer (`Hd/deps/myar`)**, found by
  the sanitizer build:
  - **Leaks:** `archiver::read_file` read each file into a `new char[]`
    that was never deleted, and never closed the file. That was 15
    allocations, one per file in `res/`. It now reads straight into the
    returned `std::vector` and closes the file. `main.cpp` also closes the
    two `FILE*` it opened on the "file exists" and "extract" paths.
  - **Misaligned reads:** `reader::load` read the file count at offset 4
    and each 24-byte header from offset 12 by casting the byte buffer to
    `long*` and `myar_header*`. Those addresses aren't 8-byte aligned,
    which is undefined behavior (fine on x86, not everywhere). It now
    copies them out with `memcpy`. The app hit this at startup, loading
    its resources; the tests don't load them.
- **The packer is sanitized like everything else:** the CMake block that
  removed the sanitizer flags from `myar`/`myar-bin` (previous entry) is
  gone.
- **imgui is left unchanged, as third-party code (decided with the
  user):** its shift by −1 stays suppressed in `tests/ubsan.supp`. For
  that, UBSan reports stay recoverable at compile time, and the tests use
  `halt_on_error=1`. A short attempt to fix it in
  `imgui_widgets.cpp` (a `TreeDepth > 0` guard) was reverted.

### How it works

- Only Hd's working tree is changed (`deps/myar/main.cpp` and
  `deps/myar/myar.cpp`). The submodule commit and its bump in this
  repository are left for when they are asked for.

### Testing

- `build-asan`, rebuilt with `-j3`:
  - The packer, now sanitized, ran during the build and generated
    `libres.myar` without a LeakSanitizer report.
  - All 9 suites pass.
- **The sanitized app** (private Xvnc, with `tests/ubsan.supp`):
  - **Before the `memcpy` change:** misaligned-load reports in
    `myar.cpp:42`.
  - **After:** no reports. It loads its fonts, opens a cell, simulates and
    closes the running tab.
- The normal build's 9 suites pass. The web build compiles (it packs and
  reads resources the same way).

### Outstanding issues

- The imgui shift by −1 is suppressed, not fixed (third-party code).

## 2026-10-27 16:44 +0300

### Changes made

- **The resource packer is renamed from `myar` to `hdres`** (in the Hd
  submodule). The name was chosen by the user from a list of suggestions:
  "Hd resources", matching `Hd::Resource`, which reads the archives.

  | What | Before | After |
  | --- | --- | --- |
  | Folder | `Hd/deps/myar` | `Hd/deps/hdres` |
  | Library / tool targets | `myar` / `myar-bin` | `hdres` / `hdres-bin` (files `libhdres-0.1.0.a`, `hdres-0.1.0`) |
  | Sources | `myar.h`, `myar.cpp` | `hdres.h`, `hdres.cpp` |
  | Namespace, types | `myar::`, `myar_file`, `myar_header` | `hdres::`, `hdres_file`, `hdres_header` |
  | Archive files | `exeres.myar`, `libres.myar` | `exeres.hdres`, `libres.hdres` |
  | Signature | `MYAR` | `HDRS` |
  | Linked symbols | `_binary_exeres_myar_start`, `exeres_myar` (web), ... | `_binary_exeres_hdres_start`, `exeres_hdres`, ... |

### How it works

- `git mv` keeps the files' history; the contents were changed with a
  plain `myar` → `hdres` replacement.
- **Changed together:**
  - Hd's `CMakeLists.txt` (`add_subdirectory`, linking, install).
  - `cmake/resource.cmake`: the archive's file name, and the array name
    given to `bin2c.cmake` for the web build.
  - `src/Hd/Resource.cpp` and `ResourceDefault.cpp`.
- **Why the symbols changed:** natively, `ld -r -b binary` names the
  symbols after the archive's file name. On the web, `bin2c` gets the
  array name. Renaming the file without the symbols wouldn't link.
- **The signature** (`HDRS`) and the error text ("this is not an hdres
  archive") changed too. Archives are generated on every build, so no old
  `.myar` file is ever read.
- **This repository:** only the submodule bump. Its own CMake no longer
  mentions the packer. Older log entries keep the old names, as they were
  written then.

### Testing

- **Normal build**, reconfigured and rebuilt:
  - `hdres-bin` generated `exeres.hdres` and `libres.hdres`.
  - `nm` shows `_binary_exeres_hdres_*` and `_binary_libres_hdres_*` in
    the app.
  - All 9 ctest suites pass.
- **The app** (private Xvnc) loads everything from the new archives:
  Roboto text, the Font Awesome toolbar icons, and the canvas with the
  grid and the component bar (shaders).
- **Web build:** `exeres.hdres` is embedded as `exeres_hdres` and links.
- **Sanitizer build**, rebuilt with `-j3`: the sanitized `hdres-bin`
  generated the archives without a report, and all 9 suites pass.

### Outstanding issues

- Old `exeres.myar` and `libres.myar` files stay in existing build folders
  until they are cleaned. Nothing reads them.

## 2026-10-27 23:10 +0300

### Features implemented

- **The 6502 and AVR plugins in the web build:** `logic_chips` (MOS 6502)
  and `logic_avr` (ATmega328P, ATtiny85) are linked into `index.wasm`. They
  show up as read-only libraries and in the component bar, as on the
  desktop. "New C model cell" is back in the browser's File menu.
- **Uploading files in the browser:**
  - An upload button (Font Awesome "upload") next to the "..." of every file
    parameter, e.g. an MCU's firmware. It opens the browser's file picker,
    copies the file into the library's folder and sets the parameter to it.
  - Files dropped on the page are copied into the folder of the library of
    the cell being edited (`work` for a read-only one). The Messages panel
    says where they went.
  - A file with the same name is replaced, so a rebuilt firmware can be
    uploaded again under its name.
  - Uploads go to IndexedDB like the rest of the workspace and survive a
    reload.
- **Examples in the browser:** the repo's `workspace/examples` (the 6502
  counter and the ATmega328P blink) is packed into `index.data`. `main`
  copies it into the workspace when the workspace has no `examples` folder.

### Changes made

- `deps/simavr`: a new submodule (buserror/simavr, tag v1.8, the same
  version as the system's simavr here).
- `deps/simavr_config`, for the web build only:
  - `sim_core_config.h` and `sim_core_decl.h` are written by hand (simavr's
    Makefile generates them), with only the mega328 and tiny85 cores.
  - `sim_elf_stub.c` stubs `elf_read_firmware` and `avr_load_firmware`.
    There's no libelf, and the plugin gets its firmware from the program.
- `CMakeLists.txt` (web branch): `simavr_web`, `logic_chips` and `logic_avr`
  as static libraries linked into `logic_core`, the examples preloaded, and
  `$stringToNewUTF8` for the page glue.
- `src/plugin/registry.cpp`: plugins linked into the program are found by
  name before the plugin folders, and `scan()` lists them. The abi and model
  checks moved into `enter()`, which both paths use.
- `src/web`: `upload`/`take_upload` and `set_drop_dir`/`take_dropped`.
  `web/pre.js` has the JS side.
- `src/editor/param_editor.cpp`: the upload button (web only).
  `src/app/app.cpp`: plugins are synced in the browser too, and each frame
  sets the drop folder and reports dropped files.
- README: the "Web build" section (plugins, GPL-3, examples, uploading) and
  `deps` in the layout table.

### How it works

- **Linked in, not loaded:**
  - Both plugins export `logic_plugin_entry`. In the web build each one is
    compiled with `-Dlogic_plugin_entry=<target>_plugin_entry`, so the
    plugin sources don't change.
  - `registry::load` checks a static list (`static_plugins()`, filled under
    `LOGIC_STATIC_PLUGINS`) before it resolves a file.
  - A static plugin has no `lib`, `copy` or mtime. It's cached in `loaded`
    under `static:<name>` and never reloaded.
  - On the desktop the list is empty, so nothing changes there.
- **Rejected: wasm side modules (`MAIN_MODULE`/`SIDE_MODULE`).** They would
  let users load their own plugins, but they make the main wasm much bigger
  and need emsdk to build a plugin. They're left for later.
- **simavr from source:**
  - Built from `simavr/sim/*.c` without `run_avr.c` (its `main`),
    `sim_elf.c` (libelf) and `sim_dwarf.c` (only used by `sim_elf.c`).
  - `sim_gdb.c` stays, since `sim_avr.c` calls it. It compiles against
    Emscripten's socket stubs and is never started.
  - The cores need their shared halves too (`sim_megax8.c`,
    `sim_tinyx5.c`). simavr ships the avr-libc register headers it needs in
    `cores/avr`, so avr-libc isn't needed.
- **GPL-3:** simavr is GPL-3, so the published page is a combined GPL-3
  work. The user accepted this, and the README says so. The desktop plugin
  stays a separate library, as before.
- **Uploads:**
  - `Module.logicUpload` makes a detached `<input type=file>` and calls
    `click()`. When the file is chosen it writes it with `FS.writeFile`,
    which closes the file, so IDBFS `autoPersist` stores it.
  - It records the path under the caller's ImGui id. The button polls
    `take_upload` each frame.
  - The click happens in the frame after ImGui sees the mouse release, not
    inside the browser's event handler. Chrome allows it through the
    transient user activation that follows a click.
  - The upload button is only on file parameters, not on the plugin and HDL
    file choosers that share `browse_button`. Uploading a `.so` would be
    useless.
- **Drops:**
  - The drop handler copies the `FileList` before its first `await`, since
    the browser empties it once the handler returns.
  - The program sets `Module.logicDropDir` only when it changes.
  - Dropped files aren't matched to the parameter under the mouse. The user
    picks them with "...".

### Testing

- **Native:** builds without warnings, and all 9 ctest suites pass
  (plugin_test loads the plugins from files as before).
- **Web build:** compiles and links. `index.wasm` is 4.73 MB, against
  4.50 MB for the published one, and `index.data` (the examples) is 90 KB.
- **Browser** (headless Chromium 156 over the DevTools protocol, SwiftShader
  WebGL 2, a fresh profile):
  - The `examples` library was in the new workspace.
  - `counter_6502` showed the mos6502 part with its ports and simulated.
  - In `blink_328p`, the ATmega328P's properties showed Firmware with "..."
    and the upload button. Uploading a renamed copy of the firmware
    (`my_blink.elf`, chosen with `Page.setInterceptFileChooserDialog` and
    `DOM.setFileInputFiles`) set the parameter to `my_blink.elf` and marked
    the library modified.
  - Simulating, the LED's pixel switched between lit and dark every
    ~250 ms, as the firmware does.
  - Save all, then a `.hex` file dropped with `Input.dispatchDragEvent`
    landed in `workspace/examples`, and the Messages panel said so.
  - After a reload, IndexedDB had `my_blink.elf`, `dropped.hex` and the cell
    with `"firmware": "my_blink.elf"`, and the cell simulated again with
    the LED blinking.

### Recommended features

- Importing and exporting a workspace or library (a .zip), to move projects
  between the desktop and the browser and to get files back out.
- User plugins as wasm side modules.
- A download button next to file parameters.

### Outstanding issues

- Only tested in headless Chromium. Safari may not open the file picker
  from a frame callback. If it doesn't, the picker has to be opened from a
  `pointerup` listener in `pre.js`.
- The examples come back on the next load if the `examples` folder is
  deleted.
- Uploading over a file that a running simulation uses doesn't reload it.
  The simulation has to be restarted.

### Next steps

- Try uploads in Firefox and Safari.
- Workspace import/export as a .zip.

## 2026-10-27 23:43 +0300

### Features implemented

- **A Files panel in the browser** (View > Files), for the workspace's files
  in IndexedDB. It replaces the upload button next to every file parameter.
  - It shows one folder at a time with a path bar (`workspace / examples`)
    and a `..` row. A double click opens a folder.
  - Columns: name, size and modified date. The date column is shown only
    when the panel is wide enough.
  - A click on a header sorts. Folders come first, and names sort in
    natural order (`file2` before `file10`).
  - Toolbar:
    - Upload: several files at once, into the folder shown.
    - New folder.
    - Download: the selected file, through a browser download. A double
      click on a file downloads it too.
    - Rename and Delete. Delete asks first, and deletes a folder with
      everything in it.
- **Drops follow the panel:** dropped files go into the folder the panel
  shows. With the panel closed, they go to the library of the cell being
  edited, as before, and the panel opens there with the last file selected.
  Uploads also select their last file.
- **Library files are protected:** a library's folder (one with a
  `library.json`), its `library.json`, `cells` and `bundles`, and what's in
  them can't be renamed or deleted here. The buttons are greyed out with a
  tooltip pointing to the Libraries panel.
- The panel's open state is kept in imgui.ini (`Files=1` in the
  `[Logic][Settings]` section). It docks as a tab next to Libraries.

### Changes made

- `src/ui/file_list.{h,cpp}` (new, in `logic_ui`): the folder table, the path
  bar, sorting, `go()`, `refresh()` and `format_size()`. It doesn't depend
  on the web build, so the file chooser can use it in the next step.
- `src/app/file_manager.{h,cpp}` (new, web build only): the panel, its
  toolbar, and the New folder, Rename and Delete popups.
- `src/web`: `upload(dir)` takes several files and no caller id.
  `take_uploaded()` returns uploaded and dropped files from one queue. New:
  `download(file)`. `web/pre.js` changed to match (`logicUpload` with
  `multiple`, `logicWriteAll`, `logicDownload`).
- `src/editor/param_editor.cpp`: the upload button is removed. The file is
  back to how it was before the previous entry.
- `src/app/app.cpp/.h`: the `files` member, View > Files, the `Files=`
  setting, the drop folder, and Files docked with Libraries in the default
  layout.
- `src/ui/icons.h`: download, folder, folder-plus, file, pen-to-square and
  trash-can icons. Each codepoint was checked against
  `res/fonts/fa-solid-900.ttf` with `fc-query`.
- README: the "Web build" section describes the Files panel.

### How it works

- **Written from scratch, not on ImGuiFileDialog (decided with the user):**
  - ImGuiFileDialog's embedded mode always has a file name field and
    OK/Cancel, and it doesn't re-read a folder after changes without being
    reopened, which loses the selection.
  - It has no file operations, and it can't block renaming library files.
  - Replacing ImGuiFileDialog in the file chooser with the same `file_list`
    is planned as step 2. Every use goes through `src/ui/file_dialog.h`.
- **Always current:** `file_list` reads its folder when `dir` changes, after
  `refresh()`, and once a second. So uploads, drops and deletes show up
  without notifying it. Reading a folder each second is cheap on MEMFS and
  on a local disk.
- **Upload and drop share one path:** both push to `Module.logicUploaded`.
  `app::frame` takes the queue each frame, reports each file in Messages and
  shows the panel at the last one. The panel's Upload button only calls
  `web::upload(folder())`.
- **Focus between frames:** `file_manager::show` is called from
  `app::frame` after the ImGui frame has ended. So it only sets a flag, and
  `gui()` calls `SetNextWindowFocus` before `Begin`.
- **Docking:** in the default layout Files docks with Libraries. For an
  existing imgui.ini without a place for Files, `gui()` calls
  `SetNextWindowDockID(<Libraries' dock>, FirstUseEver)`.
- **Bug found while testing:** Enter in the name popups did nothing.
  `editor::input_string` returns true only when the text changed, which
  hides `EnterReturnsTrue`. The popups now check `IsItemDeactivated()` with
  Enter, like the library rename.
- **Alignment:** the folder and file icons have different widths. Each row
  draws a label-less Selectable and then places the icon and the name at
  screen positions. `SameLine(x)` with `GetCursorPosX()` measures from
  different origins inside a table cell, which was tried first.
- Hidden entries (names starting with `.`) aren't listed.

### Testing

- **Native:** builds without warnings, and all 9 ctest suites pass.
- **Web build:** compiles without warnings.
- **Browser** (headless Chromium 156 over the DevTools protocol, a fresh
  profile):
  - View > Files opened the panel as a tab next to Libraries. It was still
    open after a reload (`Files=1`).
  - In `examples`, selecting `cells` greyed out Rename and Delete with
    "Part of a library, it's managed in the Libraries panel".
  - Upload with two files (`file10.txt`, `file2.txt`) wrote both, listed
    `file2.txt` before `file10.txt`, and selected the last one.
  - Renamed `file2.txt` to `renamed.txt` (typed, then Enter).
  - Made a folder `fw`, then deleted it after the confirmation.
  - Download saved `file10.txt` with its contents into a folder set with
    `Page.setDownloadBehavior`.
  - A drop with the panel showing `examples` went into `examples`. With the
    panel closed, a drop went into `work`, and the panel opened there with
    the file selected.
  - After a reload, IndexedDB had `renamed.txt` and `dropped.hex`, and no
    `file2.txt` or `fw`.

### Recommended features

- Step 2: the file chooser on `file_list`, without ImGuiFileDialog. In the
  browser it could have an upload button too.
- Downloading a folder or the whole workspace as a .zip, and importing one.
- Keyboard navigation in the list (arrows, Enter, Delete, F2).

### Outstanding issues

- A library folder that hasn't been saved yet (e.g. `work` before the first
  save) has no `library.json`, so it can be renamed or deleted in the panel
  until then.
- Renaming or deleting a file that a cell uses (e.g. its firmware) isn't
  checked. The cell reports the missing file when it's simulated.
- Only tested in headless Chromium. The file picker and downloads aren't
  tested in Firefox and Safari yet.

### Next steps

- Step 2: the file chooser on `file_list`, and ImGuiFileDialog removed.

## 2026-10-28 00:17 +0300

### Changes made

- **Our own file chooser replaces ImGuiFileDialog** (step 2 of the plan
  agreed with the user). `src/ui/file_dialog.cpp` is rewritten on top of
  `ui::file_list`. Its interface stays the same (`browse_button`,
  `open_dialog`, `dialog_result`), so none of the 14 callers changed:
  - file parameters (firmware)
  - HDL sources and the plugin file in the model editor
  - Export VCD in the simulation
  - Open, Open session and Save session in `logic-wave`
- The `deps/ImGuiFileDialog` submodule is removed, along with its CMake
  lines and its entry in `.gitmodules`.
- `logic-wave` loads the Font Awesome icons too (`res/fonts/fa-solid-900.ttf`
  was already packed into it). Without them its dialogs showed "?" for the
  icons.
- `ui::file_list::path_bar` hides the folders nearest the root behind a
  "..." button when the path doesn't fit. The button goes to the last
  hidden folder, and its tooltip names it.
- README: the "Web build" section describes the dialogs' upload button, and
  the `deps` row no longer lists ImGuiFileDialog.

### Features implemented

- **The dialog:**
  - A modal over `file_list`: Home, the path bar, the folder table, then a
    name field, a filter combo and Open/Save and Cancel.
  - Filters keep ImGuiFileDialog's syntax
    (`Description{.a,.b},.*`), so the filter strings didn't change. The
    combo shows each filter's extensions.
  - Clicking a file puts its name in the field. Double-clicking a file
    chooses it.
- **The name field takes a name or a path,** relative to the folder shown.
  Enter or Open:
  - a folder: goes there
  - in an open dialog: an existing file is chosen; otherwise an error says
    it isn't there
  - in a save dialog: adds the filter's first extension to a name without
    one, and asks "Replace?" before overwriting
- **Esc or Cancel** closes the dialog.
- **Windows:** a drive combo.
- **Browser** (set up by the app with `ui::set_dialog_root` and
  `ui::set_dialog_upload`):
  - The dialogs stay in the workspace; Home goes there.
  - An upload button uploads into the folder shown and selects the uploaded
    file, so a firmware is uploaded and chosen in one dialog. This brings
    back the one-step upload without a button on every parameter.
  - Files dropped while a dialog is open go into its folder and are
    selected.
  - If the filter would hide an uploaded file, the dialog switches to "All
    files".

### How it works

- **Dialog state:** one `dialog` per key in a map: the browse button's ImGui
  id, or the id given to `open_dialog`. It holds the title, save flag,
  filters, a `file_list`, the name, an error and the file it's asking to
  replace.
- **Opening the popup:** `start()` only marks it pending. `display()`, which
  the callers run every frame where they draw, calls `OpenPopup`. So the
  popup is opened and drawn in the same ID stack, even when `open_dialog` is
  called from inside a menu. The entry is erased when the dialog closes.
- **Name field bug, found while testing:** clicking or dropping a file
  didn't change the name field. ImGui keeps its own buffer while a text
  field is active, and this field gets keyboard focus when the dialog opens.
  The dialog now sets `name_set` when it changes the name, and the field's
  buffer is reloaded with `ReloadUserBufAndSelectAll()`
  (`GetInputTextState`, from imgui_internal).
- **The app's frame** gives an open dialog priority:
  - drops go to `ui::dialog_folder()` first, then to the Files panel's
    folder, then to the edited cell's library
  - the last upload goes to `ui::dialog_show_file()`, and to the Files panel
    only if no dialog is open
- **Starting folder:** a dialog's first folder must be inside the dialog
  root, or it starts at Home. In the browser, Export VCD starts in the
  current folder, `/home/web_user`, which is outside the workspace, so it
  starts in the workspace.

### Testing

- **Native:** clean reconfigure and build without warnings. All 9 ctest
  suites pass.
- **Web build:** compiles without warnings. `index.wasm` is 4.60 MB, about
  129 KB less than with ImGuiFileDialog.
- **Browser** (headless Chromium over the DevTools protocol, a fresh
  profile):
  - Firmware "...":
    - It opened in `workspace / examples` with `blink_328p.elf` selected,
      listing only firmware files.
    - The dialog's upload button uploaded `my_blink.elf` and selected it;
      Open set the parameter to `my_blink.elf`.
  - The name field followed a clicked file (`counter_6502.bin`).
  - A `.txt` file dropped on the open dialog switched it to "All files" and
    selected it.
  - Typing `cells` and Enter went into that folder. A missing name showed
    "'...' isn't there".
  - Export VCD:
    - It opened in the workspace with `blink_328p.vcd`. Typing `wave` and
      Enter saved `wave.vcd`.
    - Saving `wave` again asked "Replace?", and Replace wrote it.
- **Desktop** (a private Xvnc, a copy of the workspace):
  - The firmware dialog has no upload button. A deep path showed `...` and
    the last folders.
  - A double click on `counter_6502.bin` set the parameter.
- **`logic-wave`:**
  - Ctrl+O opened the dialog in the VCD's folder, with the icons after the
    font fix.
  - Escape closed it.
  - Ctrl+S opened "Save the session" with `a.json`, and Enter wrote
    `a.json`.

### Recommended features

- Keyboard navigation in the list (arrows, Enter, Backspace for up).
- Recent folders or bookmarks in the dialog.
- Showing hidden files on request. Today they aren't listed, but a typed
  path reaches them.

### Outstanding issues

- Not tested on Windows (the drive combo) or in Firefox and Safari.
- Esc while typing in the name field also closes the dialog.

### Next steps

- Keyboard navigation in `file_list`, shared by the dialog and the Files
  panel.

## 2026-10-28 13:38 +0300

### Changes made

- **Fixed the "conflicting ID" error in the model editor's Ports table for
  cells with bundle ports** (e.g. the AVR's ATmega328P). In that case the
  table lists the model's own ports (the leaf ports). They come straight
  from the plugin's `describe` and are never given ids, so all of them have
  id 0. The table used `PushID(p.id)`, so every row had the same ImGui IDs.
- The same cause broke the remove button there: it erased leaf ports by id,
  so removing one port removed all of them.

### How it works

- `model_editor::ports_table` (`src/editor/model_editor.cpp`) now keys
  each row by its index. A remove deletes that index from the leaf list,
  or calls `cell::remove_port` with the row's id for an ungrouped cell.
- Rejected: giving leaf ports ids when they're read or described. That
  needs changes in the plugin loader, `refresh`, the JSON reader and
  `apply_groups`, and only the editor uses the ids anyway. The cell's real
  ports, which pins and wires refer to, keep getting ids from `set_ports`.

### Testing

- Built. All 9 ctest suites pass. The GUI wasn't checked again.

### Next steps

- None for this fix.

## 2026-10-28 16:55 +0300

Parameterized bundles, step 1 of 2. Step 2 adds parameters to schematic
cells.

### Features implemented

- **Bundle parameters.** A bundle type has integer parameters (name,
  default, min, max, description), e.g. `DATA_W` and `USER_W` on an
  `axis` bundle.
- **Field widths are expressions** of the parameters: numbers, parameter
  names, `+ - * / %`, parentheses, `clog2()`, `min()`, `max()`. For
  example, `tkeep` is `DATA_W / 8`.
- **A field of width 0 is left out**, which makes fields optional. For
  example, `tuser` is `USER_W`, and USER_W defaults to 0.
- **Every use of a bundle gives the values of its parameters**:
  - ports: Properties has one integer input per parameter;
  - nested fields: expressions of the outer bundle's parameters;
  - breakouts and taps: a new "Arguments" parameter;
  - port groups of HDL/C cells: expressions of the model's parameters,
    e.g. `DATA_W = TDATA_WIDTH`. A Verilog/VHDL instance placed with
    `TDATA_WIDTH=64` gets a bundle port with 64 data bits.
- **The same bundle with other values is another type.** Wiring
  `axis<DATA_W=64>` to `axis` gives a width or type error. Labels show only
  the values that differ from the defaults: `axis<32>` for a bundle with
  one parameter, `axis<DATA_W=64,USER_W=2>` for more.
- Bundle editor: a Parameters table; the width column is a text field
  that turns orange, with the reason as a tooltip, when it can't be
  evaluated at the defaults; nested bundle fields get an input per
  parameter of the nested type.

### Changes made

- `bundle_field::width` is now `std::string` (an expression), no longer
  `int`. Careful: assigning an int to it still compiles (as a char). The
  tests were updated for that.
- `workspace::layout_of`, `bundle_label` and `group_ports` take the
  values.
- `netlist::net` carries `bundle_args`, and the type check compares them.
- `port_setup` copies and compares the values.
- The simulator's top ports, the sim view (fields, waveforms) and all
  editor labels and tooltips pass the values.

### How it works

- `src/model/expr.{h,cpp}` (new): `eval_expr(text, scope, error)` is a
  recursive-descent evaluator over `int64_t` with overflow checks
  (`__builtin_*_overflow`). Also `is_literal`, `expr_names` and
  `valid_param_name`.
- Model (`bundle.h`, `cell.h`):
  - `bundle_type::params` holds integer `param_def`s.
  - `bundle_field::args` maps the nested bundle's parameters to
    expressions.
  - `bundle_node::args` holds a nested bundle's resolved values.
  - `port::bundle_args` holds resolved integers, only those that differ
    from the defaults.
  - `port_group::args` holds expressions over the model's parameters.
- Workspace:
  - `bundle_values` fills in the defaults and `bundle_args` drops them.
  - `check_bundle_args` reports unknown names and out-of-range values
    instead of clamping like `resolve_params`.
  - `same_bundle` is the type test and `group_args` evaluates a port
    group.
  - In `layout_of` each bundle level evaluates its widths in its own scope
    and evaluates the nested args in the outer scope; width 0 skips the
    field.
- Port groups:
  - `apply_groups` and the group cache evaluate with the model's default
    parameters.
  - `external_interface` (instances with other parameters) uses the
    instance's values, so the bundle port follows the HDL parameter.
- Breakout and tap:
  - A new text parameter `args` with the new `param_def::bundle_args` role
    (`"DATA_W=32, USER_W=0"`, see `parse_bundle_args`/`format_bundle_args`).
  - The parameter editor shows it as one integer input per parameter,
    and only for a bundle that has parameters.
  - The fields chooser lays the bundle out with those values.
- JSON:
  - Bundles get `params`, and a field's `width` is a number when it's a
    literal and a string otherwise. Old files load unchanged, and files
    whose widths are all numbers stay readable by older versions.
  - Fields get `args`, ports get `bundle_args` and groups get `args`.
  - The format version stays 1: everything new is optional.
- Decisions, made with the user:
  - Values come from every use site, from HDL parameters and (step 2)
    from schematic parameters.
  - Widths are expressions, and width 0 drops a field.
  - Other values make a different type, labeled `name<…>`.
  - Parameters are integers only.
  - The work is delivered in two steps.
- Rejected:
  - Storing each specialization as a synthesized bundle type with its own
    id: every consumer would need to know about the generated types, and
    they would have to be kept in sync.
  - Clamping out-of-range values like instance parameters do: a silent
    clamp would hide wrong widths.

### Testing

- model_test:
  - `test_expr`: precedence, functions, errors (unknown names, division
    by zero, overflow, syntax), `parse_bundle_args`.
  - `test_bundle_params`:
    - layouts for the defaults and other values, the dropped `tuser`,
      out-of-range and unknown values;
    - nested args, including the flipped view and an unknown name;
    - widths that fail or go negative, labels, `bundle_args`/`same_bundle`;
    - port widths following their values, and a type error between
      `two<A=8,B=4>` and `two<A=4,B=8>` (same width);
    - `add_ports_for_pins` copying the values;
    - an HDL cell's port group with `DATA_W = TDATA_WIDTH` and its errors;
    - a JSON round trip, and an old-format bundle file.
- sim_test: a breakout with `args = "W=16"` on a parameterized bundle
  port. The fields sit at the right offsets, a zero-width field is left
  out, other values on the port give a type mismatch, and an out-of-range
  value shows on the instance.
- hdl_test: a new `stream_sink` (.sv and .vhd) with `TDATA_WIDTH`, grouped
  with `DATA_W = TDATA_WIDTH`. The instance with 32 gets a 34-bit bundle
  port and simulates on Verilator, Icarus and GHDL.
- All 9 ctest suites pass. The web build (build-web) compiles.
- GUI, on VNC with a scratch workspace:
  - the bundle editor shows the Parameters table and expression widths;
    an unknown name turns the width orange with a tooltip and a problem
    line;
  - an `axis<DATA_W=64>` port shows its label, layout tooltip and a
    width mismatch with a default `~axis` pin;
  - setting DATA_W back to 32 in the port's Properties makes it `~axis`
    and the wire a normal bundle wire;
  - a breakout's Arguments inputs: USER_W=2 adds `tuser` and the label
    becomes `axis<USER_W=2>`.

### Recommended features

- "Find bundle ports" in the model editor only matches bundles at their
  default values. It could infer the values from the ports' widths, e.g.
  `DATA_W = 64` from a 64-bit `s_tdata`.
- Bundles declared by plugins (the `bundles` JSON of a plugin) could take
  `params` and expression widths too.

### Outstanding issues

- Renaming a bundle parameter doesn't rename the values that ports,
  fields, breakouts and groups give for it. They then report "it has no
  parameter …".
- An expression is checked only at the defaults in the bundle editor;
  other values are checked where they're used.

### Next steps

- Step 2: parameters on schematic cells. Port widths, bundle values and
  child instance parameters become expressions of the cell's parameters,
  and each instance is specialized for its values.

## 2026-10-28 17:03 +0300

Parameterized schematic cells, step 2 of 2: subcircuits with parameters.

### Features implemented

- **Schematic cells have integer parameters.** They're edited in the cell
  properties (nothing selected) in the same kind of table the bundle
  editor uses. Each instance sets its values in its Properties, like the
  parameters of builtins.
- **Expressions of the cell's parameters** can be used for:
  - a port's width (e.g. `W`, `W + 1`);
  - a bundle port's values (e.g. `DATA_W = W`);
  - the integer parameters of instances inside (e.g. a NOT gate's
    `width = W`, a sub-cell's `N = W * 2`).
- In a cell with parameters these fields become text inputs that take a
  number or an expression. They turn orange with the reason as a tooltip
  while the expression fails, and the cell properties list the problems.
- **Every instance is specialized for its values.** Its pins have the
  widths for its values, and the simulator uses a copy of the cell for each
  set of values, nested generics included. The sim view shows each copy
  with its own widths and values.
- An open generic cell is shown, checked and simulated (F5 as top) with
  its default values.

### Changes made

- `schematic::params`, `port::width_expr` / `port::bundle_arg_exprs` and
  `instance::param_exprs` are new.
- `workspace::params_of` returns a schematic cell's parameters, so the
  instance parameter editor shows them without changes.
- `edit_params` takes an optional map of expressions and a scope.
- `bundle_editor::params_table` and the expression inputs of the bundle
  and model editors now use the shared `edit_int_params` / `edit_expr`
  (param_editor).
- The sim view keeps its per-cell editors by cell pointer instead of id,
  because instances with other values have their own copies with the same
  id.
- The "how its value is shown" tooltip of a port's Format went to the
  Width input when the format wasn't shown (1-bit ports). It now attaches
  only to Format.
- README: parameterized bundles and subcircuits.

### How it works

- **The stored cell is the specialization for the defaults.** Each
  expression sits next to a plain value (`port.width`, `port.bundle_args`,
  `instance.params`), and that value is kept evaluated at the cell's
  defaults. So the netlist, the editors, the symbol and simulating the cell
  as top work unchanged.
- `workspace::specialize(cell&, values, ports_only)` evaluates all of a
  cell's expressions for the given values:
  - port widths (1..max_width);
  - bundle values (checked with `check_bundle_args`, the width taken from
    the layout);
  - instance parameters (must be an integer parameter of the instance's
    cell, in range, stored with `set_param`).

  It returns the problems, and anything that fails keeps its value.
- `apply_param_defaults(cell&)` runs `specialize` with the defaults. The
  schematic editor runs it every frame (`update_param_values`) for cells
  that use parameters or expressions. That way undo, and changes to cells
  that instances use, are followed. Only a real change marks the library
  dirty. `load_workspace` runs it after loading too.
- `interface_of` of an instance whose values aren't the defaults:
  - uses `schematic_interface`, which specializes only the ports;
  - keeps the cell's symbol (pins refer to port ids) or regenerates the
    automatic box;
  - is cached by cell id and `params_key`, and checked against the
    cell's ports, symbol, parameters, name and the bundle revision.

  `check_instance` reports that specialization's problems.
- `simulator::specialize(cell, instance, where)` gives each child scope
  the cell itself for the defaults. For other values it gives a copy,
  specialized once per (cell id, values), owned by the simulator; its
  problems are reported once with the instance path. Netlists are already
  cached per cell pointer, so each copy gets its own. `scope::cell` points
  to the copy, which has the cell's id.
- JSON: the schematic gets `params`, ports get `width_expr` and
  `bundle_arg_exprs`, and instances get `param_exprs`. All are optional,
  and the format version stays 1.
- Decisions, made with the user: generic schematics are part of this
  feature; parameters are integers only; an open generic cell uses its
  defaults (no preview values).
- Rejected:
  - Evaluating the expressions inside `interface_of` and the netlist each
    time instead of keeping the default values stored: every consumer would
    need the parent's scope, and simulating a generic cell as top would
    need special handling.
  - An "=" toggle per field: a single text input that takes a number or an
    expression is simpler and works the same way in the bundle editor.

### Testing

- model_test `test_cell_params`:
  - a cell with an `axis` port (`DATA_W = W`), a `W + 1` port and a
    register with `width = W`;
  - its defaults, and the interface of an instance with W=64 (port
    widths, label `~axis<DATA_W=64>`, the same pins);
  - the cache following a changed expression;
  - the problems: an out-of-range bundle value, an unknown name, a
    parameter the instance's cell doesn't have; what fails keeps its value;
  - out-of-range widths for an instance's values, and a JSON save/load
    round trip.
- sim_test `test_cell_params`:
  - a generic `inv` (W-bit NOT) at W=4 and inside `inv2` (N, `W = N * 2`)
    at N=8, so 16 bits; both invert correctly;
  - the scopes point to specialized copies with the cell's id; the cell
    itself is unchanged;
  - an expression that goes out of range for an instance's values is
    reported with its path, and a failing port width shows on the instance
    and as a width mismatch.
- All 9 ctest suites pass. The web build compiles.
- GUI, on VNC with a scratch workspace:
  - `top` with `inv` at W=4 and W=16: no problems, bus widths right;
  - F5: the outputs are 0xF and 0xFFFF; after setting a16 = 0x1234,
    y16 = 0xEDCB;
  - descending into U2 shows a 16-bit NOT gate, U1 a 4-bit one;
  - in `inv`, the NOT's Bit width shows `W`; changing it to `W * 2` makes
    the gate 2 bits with a width mismatch, and undo restores it;
  - the port Width shows `W`; an unknown name turns it orange; the cell
    properties show the Parameters table.

### Recommended features

- Boolean/choice parameters together with conditional parts (generate
  if), e.g. an optional output register.
- Preview values for an open generic cell, to look at it with other values
  without placing it.
- When a cell's parameter is renamed, rename the values and expressions
  that use it.
- Infer bundle values in "Find bundle ports" from the ports' widths.

### Outstanding issues

- Renaming or removing a parameter doesn't update the instances' values
  and expressions that use it. They show as problems ("unknown name",
  "the cell has no integer parameter of that name").
- The symbol of a generic cell is the same for all values. A pin's stub
  thickness (bit, bus, bundle) follows the defaults.
- The cell properties' Parameters table is cramped in a narrow Properties
  panel.
- Only integer parameters can be expressions. A child's boolean or choice
  parameters are set per instance as before.

### Next steps

- None planned. See the recommended features.

## 2026-10-28 17:37 +0300

AXI bundles, an AXI crossbar, an AXI RAM and an AXI test master.

### Features implemented

- **Builtin bundle types `axi4` and `axi4_lite`.** They live in the
  read-only builtin library with stable ids (`model::builtin_bundle_id`),
  so they can be used for ports, HDL/C port groups and breakouts in every
  workspace.
  - Parameters: `ID_W` (0..32, default 4), `ADDR_W` (12..64, 32), `DATA_W`
    (8..1024, 32; Lite 32..64), `USER_W` (0..64, 0), and `SIDE` (0/1, 1),
    which turns lock/cache/prot/qos/region on or off (Lite: prot).
  - The fields are flat with the usual names (`awid`, `awaddr`, `awlen`,
    …, `rready`), so an HDL port group with a prefix like `m_axi_` matches
    a Verilog module's ports. The normal view is the master's.
- **AXI crossbar** (`axi_crossbar`):
  - parameters: the protocol (AXI4 or AXI4-Lite), 1–16 masters, 1–16
    slaves, and an address map such as `"0x0000/4K, 0x1000/4K"`
    (`_` between digits, K/M/G/T sizes; empty splits the address space
    into equal power-of-two slices);
  - ports: `s0..` for the masters (flipped view), `aclk`, `aresetn`, and
    `m0..` for the slaves;
  - each slave is arbitrated round robin, separately for writes and reads,
    and masters on different slaves run in parallel;
  - each master has one outstanding write and one outstanding read, so
    responses stay in order and ids pass through unchanged;
  - every channel goes through a 2-entry skid buffer, so there are no
    combinational paths from a master to a slave;
  - unmapped addresses get DECERR from an internal error slave, and a burst
    read gets all of its beats.
- **AXI RAM** (`axi_ram`):
  - 2^size bytes (4 B to 16 MiB), optionally loaded from a firmware file;
  - FIXED/INCR/WRAP bursts, narrow sizes and byte strobes;
  - independent write and read paths at one beat per cycle;
  - its contents show and can be edited in the debugger's memory view.
- **AXI test master** (`axi_master`):
  - the simulation's Properties panel has an "AXI master" section with
    the address, data, length (INCR beats write data + k) and size, plus
    Write and Read buttons;
  - its state shows the state name, busy, done, the response
    (OKAY/SLVERR/DECERR), the beat count, and the first and last read
    data.
- **Example `examples/axi_demo`**: two test masters, a 2×2 crossbar
  (`0x0000/4K, 0x1000/4K`) and two 4 KiB RAMs. A 1 kHz clock reaches every
  `aclk` through tunnels.

### Changes made

- `model::primitive_registry` gets `add_bundle`/`bundles()`, and
  `workspace::add_builtin_library` adds those bundle types to the builtin
  library.
- `builder::box_pin::bundle` draws a box pin as thick as a bundle wire.
- The sim view's state table now shows text items (e.g. "state: idle").
  Before, every text item except a terminal's screen was dropped.
- README: the AXI parts.

### How it works

- `src/builtin/axi.{h,cpp}`:
  - One X-macro table, `LOGIC_AXI_FIELDS` (name, direction, AXI4 width,
    Lite width or none), is the single source for both the bundle types
    (`axi::bundle`) and the layout the components use (`axi::make_layout`,
    evaluated with `eval_expr`). model_test checks that the layout is the
    one `workspace::layout_of` gives for several sets of values.
  - A channel's payload is the fields before its `valid`, and they are
    contiguous. So a beat is one `sim::value` slice (`channel{off, w,
    valid, ready}`), and the crossbar passes ids, user, qos etc. through
    untouched.
- The parts are `clocked` components. Inputs are sampled on the rising
  edge of `aclk`; `aresetn` = 0 at an edge resets, and unconnected means
  running. Outputs are written on every eval from the state only.
- The bundle ports are `inout`: a part reads the net and drives only its
  own fields, and the others are left floating.
- Skid buffers (`fifo`):
  - `ready = size < 2` and `valid = !empty`;
  - at an edge, the pops the other side accepted go first, then the
    internal moves (responses, W beats, arbitration, decode errors), then
    the beats accepted from outside, pushed using the ready seen before
    the edge;
  - this gives one beat per cycle per channel. A single write takes 8
    cycles end to end through the crossbar.
- Crossbar state:
  - per master: input buffers AW/W/AR, output buffers B/R, busy flags, the
    slave its W beats go to, and the error-slave state with separate write
    and read ids;
  - per slave: output buffers AW/W/AR, input buffers B/R, the write and
    read owners, and round-robin pointers.
  - A slave's B goes back only after all the W beats have gone to it. A
    read is released at `rlast` (Lite: every beat).
- RAM: one write and one read burst at a time. The address of each beat
  comes from `beat_address` (fixed/incr/wrap). Bytes are written by strobe
  at the bus-aligned address, and a read returns the whole bus word.
  Addresses wrap at the size, so a RAM at 0x1000 needs no offset.
- Test master: AW → W beats → B and AR → R beats, with `interact` actions
  (`addr`, `data`, `len`, `size`, `write`, `read`). Narrow sizes set the
  strobes of the lanes their address covers.
- The example is generated by a throwaway program against the model API,
  with pin positions from `interface_of`. Its wires don't end on other
  wires, so editing won't join nets.
- Decisions, made with the user:
  - AXI4 and AXI4-Lite;
  - a crossbar with one outstanding transaction per master and direction;
  - registered channels (skid buffers);
  - a text address map;
  - the bundles in the builtin library;
  - a RAM, a test master and an example on top.
- Rejected:
  - Several outstanding transactions per master. That needs id extension
    and response reordering.
  - Combinational routing.
  - Per-slave integer parameters: a primitive's parameter list is fixed,
    so they can't follow the slave count.

### Testing

- model_test `test_axi_bundles`:
  - both bundles are in the builtin library with stable ids;
  - `make_layout` matches `layout_of` for both protocols and several
    widths, including ID_W 0, SIDE 0, 1024 bits and USER_W 64;
  - the channel offsets;
  - the crossbar's ports (s flipped, m normal, `~axi4<DATA_W=64>`) and its
    `check()` messages;
  - `parse_map`: suffixes, spaces, default slices including a 64-bit
    space, overlap, out of range, bad syntax, size 0.
- sim_test `test_axi`, with test masters, a crossbar and RAMs on a 1 MHz
  clock:
  - two masters writing to different RAMs take as long as one alone
    (8 cycles);
  - a read back across the crossbar, an INCR burst of 4 written and read
    back;
  - both masters on the same RAM finish (15 cycles), and a one-byte write
    in lane 1 changes only that byte;
  - DECERR for an unmapped write and for a 3-beat burst read, and the RAMs
    still work afterwards;
  - AXI4-Lite through a 1×3 crossbar with the default map, including
    DECERR in the unmapped fourth slice;
  - a master straight on a RAM, reset in the middle of a burst, then
    writing again;
  - an overlapping map shown on the crossbar.
- All 9 ctest suites pass. The web build compiles.
- GUI on VNC:
  - the AXI parts are in the builtin library's AXI group, and
    `examples/axi_demo` opens without problems;
  - with F5 and Run, M0's Properties show the master section; writing
    0xDEADBEEF to 0x1004 gives done 1, OKAY;
  - RAM1's memory view shows `EF BE AD DE` at 4.

### Recommended features

- Several outstanding transactions per master (id extension, reordering)
  for more throughput.
- Data width and protocol converters (AXI4 ↔ Lite, 64 ↔ 32 bits) as
  parts.
- A scriptable master, e.g. a list of transactions from a file, and a
  monitor that logs transactions.
- An hdl_test with a Verilog AXI-Lite slave grouped with the builtin
  `axi4_lite` bundle behind the crossbar (planned as optional, not done).

### Outstanding issues

- All ports of a crossbar share one set of widths.
- The test master writes at most 64 bits of distinct data per beat: it
  repeats them across wider buses. It shows only the low 64 bits of read
  data.
- The RAM doesn't check that a WRAP burst is aligned and has a length of
  2, 4, 8 or 16, nor exclusive access (it never answers EXOKAY).
- Bundle wires are thin unless View > Thick buses and bundles is on, so
  the AXI wires look like plain wires apart from their color.

### Next steps

- See the recommended features.

## 2026-10-28 18:44 +0300

Bundles as groups in the waveform view, single fields on their own, and
icons on buttons.

### Features implemented

- **A bundle is one row in the waveforms.**
  - The row is named after the net, and its time line shows a summary of
    its fields (`awid=0 awaddr=00000040 …`, as much as fits in each
    segment).
  - Its arrow (or "Show fields" in the row menu) expands it into an
    indented line per field, each with its own trace. The name and the
    value at cursor A sit on one line.
  - Nested bundles are sub-groups with their own arrow and summary.
  - The whole bundle is recorded as one signal, so it counts once toward
    the 64-signal limit, not once per field.
- **A single field can be added on its own.** On a bundle net, "Add to
  waveforms" (wire menu, the instance's pins menu and the Signals menu) is
  now a submenu: "Whole bundle", then every field with its width, nested
  fields indented. A nested bundle added alone keeps its own fields.
  Entries already recorded are checked and greyed out.
- **The VCD export writes a bundle's fields as signals of their own** in a
  scope named after the bundle, e.g. `top.bus.data[7:0]`.
- **Icons:**
  - The waveform window: Run/Pause, Step, Clear (eraser) and Export VCD
    (download); Fit, zoom out and in, Add marker (flag), Markers (list) and
    Find previous/next (chevrons), all with tooltips. Removing a row is ×,
    and a marker's Go/Delete are → and the trash can.
  - In the app, every small "x" remove button is now an × icon with a
    tooltip (bundle fields, parameters, ports, port groups, list entries,
    breakpoints, the start error). The "Add field", "Add parameter",
    "Add port" and "Add source/include folder/define" buttons get a + icon.
    The messages and terminal "Clear" buttons get an eraser.
  - Dialog buttons stay as text.

### Changes made

- `wave::field` and `signal::fields` are new; signals without fields are
  unchanged.
- `waveform_recorder::add` takes the fields, and `has(key)` is new.
- `sim_view`:
  - `record_signal(net, name, node)`;
  - a new `signal_item` replaces the three menu-item lambdas;
  - `bundle_of(net)` holds the bundle lookup.
- The view: `row::closed` (closed sub-groups); `line::depth`; `bits` is
  renamed `parts` and also caches field slices and summaries; text traces
  that don't fit their segment are drawn clipped, where before they were
  left out.
- `ui/icons.h` gets xmark, plus, zoom in/out, fit, eraser, flag, list,
  prev/next and go. All were checked to be in res/fonts/fa-solid-900.ttf.

### How it works

- `search.{h,cpp}`:
  - `slice_trace(t, lo, width)` gives a field's changes;
  - `group_end(fields, k)` gives the end of a group;
  - `summary_trace(t, fields, first, end)` gives a text trace of
    `name=value` for the leaf fields (one bit as 0/1/x/z, wider ones in
    hex, names relative to the group), with only the changes where the
    text changes;
  - `expand_fields(set)` replaces signals that have fields by one signal
    per leaf, named `name.path`, for the VCD writer, which turns the dots
    into scopes.
- `view::line_trace`:
  - for a signal with fields, `line_id.second` is a field index;
  - a leaf gives its slice, a group the summary of its fields, and the
    row's own line (-1) the summary of all fields;
  - all are cached by trace version.
- Lines: an expanded row with fields adds a line per field, and skips
  the fields of a closed group (`group_end`). Plain wide rows still expand
  into bits.
- Recording:
  - `sim_view::record_signal` turns the bundle layout's nodes into
    `wave::field`s (nested bundle nodes become groups);
  - a single field is recorded as bits of the net
    (`waveform_recorder::key_of(net, lo, width)`), so it's a separate
    signal next to the whole bundle;
  - a nested bundle added alone gets its sub-fields with offsets and
    depths relative to it.
- Decisions, made with the user:
  - a collapsible group, recorded once;
  - the header shows a field summary;
  - single fields come from the schematic menus;
  - icons on the waveform toolbar and on the common small app buttons,
    not on dialogs.
- Rejected: keeping a signal per field and grouping the rows by name. It
  uses up the signal limit, and a bundle's fields couldn't be told apart
  from a field added on its own.

### Testing

- vcd_test `test_fields`:
  - slices change only where their bits do;
  - the summary text for all fields, with x/z, and for a sub-group with
    relative names and fewer changes;
  - `group_end`;
  - `expand_fields` gives names, widths and values;
  - writing a VCD and reading it back gives `top.bus.data[7:0]` and
    `top.bus.s.x` with the right values.
- waveform_test `test_bundle_rows`:
  - the recorder keeps a bundle with its fields and a field recorded on
    its own, and `has(key)` works;
  - the view has two rows, and expanding the bundle adds at least four
    field lines of height.
- All 9 ctest suites pass, and the web build compiles.
- GUI on VNC with `examples/axi_demo`:
  - the wire menu shows "Add to waveforms ▸" with "Whole bundle" and all
    the AXI fields;
  - adding the whole bundle and then `awaddr` gives two rows, `wire 7`
    with its summary and `wire 7.awaddr`;
  - after a test-master write, the summary changes at the handshake;
  - expanding shows indented field lines (awid, awaddr, awlen, …) with
    their values; the toolbar icons show with their tooltips.
  - First try, a field's value overlapped the next line, so name and value
    now share one line, which was then checked.

### Recommended features

- Pulling a field out of an expanded group into a row of its own from the
  waveform (drag or menu), in addition to the schematic menus.
- A summary that lists the fields that changed first, or only the
  handshake signals, for wide bundles like AXI.
- Keeping groups' open/closed state in saved sessions (only recordings
  have fields today; VCD files don't).

### Outstanding issues

- Field lines can't be expanded into bits.
- A bundle's summary is long for wide bundles, so each segment shows only
  its first fields.
- The standalone viewer reads the exported fields back as separate
  signals (VCD has no bundles), not as a group.

### Next steps

- See the recommended features.

## 2026-10-28 19:30 +0300

Waveform view follow-ups: same-size row buttons, field bits, activity on
collapsed bundles, text-sized icons, a whole-line selection, and row
resizing.

### Changes made

- **The row buttons are the same size.** The ×, the row's expand arrow,
  and the group and field arrows are all square icon buttons
  (`small_icon_button`, the font size + 4) with caret icons. Before, the ×
  was a `SmallButton` and the arrow an `ArrowButton` of another size.
- **The waveform viewer's icon buttons are as high as text buttons**
  (`GetFrameHeight`), so they line up with the Follow checkbox, the combos
  and the search field. The icon-size right-click menu is gone there; it
  stays on the editor toolbars.
- **A collapsed bundle (and a closed nested group) shows only activity**:
  a bus shape that changes wherever any field changes, x/z coloured, with
  no labels and an empty value column. The field summary is removed (the
  user changed their mind), and so is `summary_trace` with its tests.
- The standalone viewer's notes use the × icon.

### Features implemented

- **Fields expand into bits.** A field wider than one bit has a caret that
  shows its bits below it, msb first, indented one more level.
- **The selected line is highlighted across the whole line**: the names
  column as before, plus a faint band (the header colour at 25%) behind
  the trace. This works for rows, fields and bits.
- **Row heights resize by dragging the lower border of a line**, in the
  names column or over the traces:
  - a row's own line sets the row's height;
  - a field or bit line sets the height of all the lines under that row
    (`part_height`, 0.5–8 rows, saved in sessions);
  - the cursor is a vertical resize arrow, and the drag doesn't select,
    reorder or move cursor A.

### How it works

- `ui::icon_button_at(icon, tip, size, active, enabled)` holds the drawing
  that used to be in `icon_button`. `icon_button` (the editor toolbars)
  adds the size menu around it, and `small_icon_button(icon, tip, size)`
  (size 0 = frame height) doesn't. New icons: `caret_right` and
  `caret_down`.
- `view::line_id` is now a struct `{key, part, bit}` instead of a pair:
  part = the field index (-1 for the row) and bit = the bit (-1 for the
  whole).
  - `line_trace` gives:
    - (-1, -1): the row's trace, which is the activity for a bundle;
    - (-1, b): a bit of a plain row;
    - (f, -1): the field's slice, or a group's bits, since its fields are
      contiguous;
    - (f, b): a bit of the field's slice.
  - All are cached in `parts` by trace version.
  - `row::open_fields` holds the fields shown as bits and
    `row::part_height` the height of the lines under the row.
  - `line` has `part` and `bit`.
- `draw_digital(..., labels)` draws the bus shape without text for
  activity lines.
- The border hit test is done by hand in `plot()` before the timeline
  handles the mouse: within 3 px of a line's bottom. It starts
  `resize_row`/`resize_parts` on a click, and while it's active, cursor
  setting, selection and row dragging are skipped.
- Decisions, made with the user: resize row heights (not the window or
  the divider); activity as a bus shape without text; highlight the whole
  line.

### Testing

- waveform_test `test_bundle_rows`:
  - clicking the data field's caret adds exactly 8 bit lines;
  - dragging the bundle row's lower border by 60 px makes the content
    60 px taller, and doesn't set cursor A.
  - The content size follows a frame after a click, so the test lets a
    few frames pass before measuring.
- vcd_test: the summary checks are removed; the slice and
  `expand_fields`/VCD checks stay.
- All 9 ctest suites pass, and the web build compiles.
- GUI on VNC with axi_demo:
  - the × and the arrow are the same size;
  - the waveform toolbar icons are as high as the checkbox and the combos;
  - right-clicking an icon gives only its tooltip, no size menu;
  - the collapsed bundle shows a bus shape without text;
  - `awaddr`'s caret shows [31], [30], …;
  - the selected `awaddr` is highlighted over the trace too;
  - dragging the bundle row's border by 40 px made it 40 px taller.

### Outstanding issues

- A long value next to a field name gets clipped by the names column; the
  names column can be widened with its divider.
- The caret glyphs are small inside their buttons, since the icon is drawn
  at 60% of the button.

### Next steps

- None planned for these points.

## 2026-10-28 20:01 +0300

Zoom mode: one window fills the app and the other windows become tabs at
the bottom, like Questa's zoom/unzoom.

### Features implemented

- **Zooming a window**: double-click its tab (a floating window's title
  bar), use the right-click menu on its tab, or View > **Zoom window**,
  which zooms the window that had focus (the Editor if none).
- While zoomed:
  - the window fills the app below the menu bar;
  - a **tab bar at the bottom** lists every open window: the docked panels
    (Libraries, Editor, Symbol editor, Properties, Hierarchy, Messages,
    Files on the web) and the floating ones (Waveforms, framebuffer
    displays);
  - clicking a tab zooms that window instead;
  - the **Unzoom** button (compress icon) at the right of the bar, a
    double-click on the zoomed window's title, its menu "Unzoom", or the
    View menu go back to the layout, which is exactly as it was.
- Closing the zoomed window (e.g. a waveform window, or by stopping its
  simulation) ends the zoom. The zoom isn't remembered after a restart.
- Help > Keys has a line about it.

### How it works

- `src/ui/zoom.{h,cpp}` (logic_ui):
  - `zoom::begin(name, open, flags)` / `end()` replace `Begin/End` for the
    windows that can be zoomed, and `shown()` tells whether the window was
    made.
  - **While not zoomed** it calls `Begin`, so the docked layout works as
    before. After it, the last item is the dock tab or the title bar, so a
    double-click there or `BeginPopupContextItem` ("Zoom") zooms the
    window. The window with focus is remembered for the View menu.
  - **While zoomed**:
    - the other windows aren't submitted at all; docked ones keep their
      dock place, so the layout comes back unchanged;
    - their `SetNextWindow*` data is cleared, so it doesn't go to the next
      window;
    - the zoomed window's content goes into a window of its own: no
      docking, moving, resizing or collapsing, placed at the main
      viewport's work area minus the bar.
  - Its ID is `###zoom_` plus the name with `#` replaced. ImGui hashes
    what follows the **last** `###`, so with the name as it is, a floating
    window like `Waveforms: top###waveforms1` would have been the layout's
    own window. Its size then stuck after unzooming; this was found in the
    GUI and fixed, and a test checks it.
  - `zoom::end_frame()`, after all windows, ends a zoom whose window wasn't
    submitted and draws the bar: a tab bar in a child with room for the
    Unzoom button, with the tab labels being the window names before
    `###`.
- app.cpp, file_manager.cpp and sim_session.cpp use it for their windows.
  The Waveforms and framebuffer windows get `NoCollapse`, so a
  double-click on the title zooms instead of collapsing. Dialogs such as
  Settings and the theme editor, and modals, are unchanged.
- Panels that drew their content even when their tab was hidden still do;
  they skip it only while another window is zoomed.
- Decisions, made with the user:
  - zoom from the tab row (double-click or the tab's menu) and the View
    menu, with no button inside windows and no key;
  - every open window as a tab;
  - the toggle zooms the focused window, and the zoom isn't remembered.
- Rejected:
  - Moving the window into a second dockspace, or saving and reloading the
    dock layout (`SaveIniSettingsToMemory`): a node that loses its last
    window is deleted, and reloading the layout in the middle of a run is
    fragile.
  - Undocking the window.

### Testing

- New `zoom_test` (headless ImGui frames):
  - zooming A runs only A's content, in a window filling the work area
    above the bar, and A's layout window isn't drawn;
  - clicking B's tab zooms B, which has its own window and not the
    layout's;
  - closing B ends the zoom, and B keeps its own place and size;
  - `toggle` zooms the focused window and unzooms again.
- All 10 ctest suites pass, and the web build compiles.
- GUI on VNC:
  - double-clicking the Editor tab fills the app, with the tabs Libraries,
    Editor, Symbol editor, Properties and Hierarchy below;
  - clicking Properties shows it full size;
  - F5 in the zoomed editor and View > Waveforms add "Waveforms: axi_demo"
    as a tab, and it zooms;
  - View > Zoom window shows its check mark;
  - a double-click on the floating Waveforms title zooms it, and Unzoom
    restores both the docked layout and the window's own size;
  - the right-click menu on a docked tab shows Zoom.

### Outstanding issues

- Scroll positions and open tree nodes of a zoomed window are its own, not
  those of the window in the layout.
- `imgui.ini` from a run before the ID fix can keep a full-screen size for
  a waveform window. Resizing it once fixes that.

### Recommended features

- A key to zoom the focused window, if one is wanted later.

## 2026-10-29 20:36 +0300

RISC-V phase 1 of 7: the hart and the ISA tests. The plan, agreed with the user:
an RV32IMA CPU, CLINT, PLIC and 16550 UART as builtin parts on the AXI4 bundles,
ported from the `smode` branch of github.com/uwrk9amtdn/riscv-simulator (a fork of
hdinc/riscv-simulator with S mode and Sv32). The goal is to boot OpenSBI, Linux
and busybox in a schematic, with the UART in a VT100 terminal window. Phases:
1 hart + ISA tests, 2 CPU part with AXI, caches and run-ahead, 3 CLINT/PLIC/UART
parts, 4 terminal window, 5 Linux example, 6 performance, 7 later extras.

### Features implemented

- `src/riscv/` (in logic_core, no UI dependencies):
  - `hart`: RV32IMA + Zicsr + Zifencei, M/S/U modes, Sv32 paging, trap
    delegation, the counters (cycle, time, instret and their machine
    versions; hardware performance counters read as zero), mcounteren and
    scounteren, mcountinhibit, mstatus TVM/TW/TSR, MPRV/MXR/SUM, LR/SC and the
    AMOs, WFI.
  - `decode`: instructions decoded into a struct (op, rd, rs1, rs2, imm), a
    disassembler with ABI register and CSR names.
- New ctest `riscv_test`: the riscv-tests rv32ui/um/ua/mi/si `-p` suites on a
  hart with a flat memory. 81 pass. rv32mi-p-pmpaddr is skipped because there
  are no PMP entries.
- `tests/riscv/build.sh <riscv-tests checkout>` builds the suites with
  `riscv32-unknown-linux-gnu-gcc` (`module load riscv-toolchain`). The
  binaries (82 files, 1.1 MB, BSD licensed) are committed in tests/riscv/isa,
  so the tests don't need the toolchain.

### How it works

- Memory is a `riscv::bus` (load/store of 1–4 bytes at any alignment) that
  answers ok, fault, stall or misaligned. Stall means "not done yet, e.g. a
  cache miss": the hart returns `step_result::stalled` without having changed
  anything and runs the same instruction again later. Every instruction makes
  its architectural changes only after its last memory access, and the page
  walk's A/D updates are idempotent, so retrying is safe. The phase 2 CPU part
  builds its caches and AXI bus unit on this.
- Traps are return values (`status::trap`), taken at once by `raise()`. The
  port used C++ exceptions, which are slow for Linux's frequent page faults
  and ecalls, and one of them threw a plain `u32`, so a bad branch funct3
  called std::terminate.
- Accesses that cross a page are split and each part is translated; the bus
  decides whether it can do the misaligned part (`misaligned` becomes an
  address-misaligned exception, which OpenSBI emulates).
- Interrupt lines (`set_lines`: msip, mtip, meip, seip) are or'ed into mip.
  The software-writable mip bits (SSIP, STIP, SEIP) are kept apart from the
  lines, as in the port. Interrupts are taken in the spec's order, machine
  level first; a pending interrupt ends WFI even if it can't be taken.
- `time` is a field the owner keeps up to date; in the CPU part it comes from
  the simulation time, so Linux and OpenSBI can read the time CSR directly
  instead of trapping to have it emulated.
- Decisions:
  - Ported the architecture (CSR masks, delegation, the PTW, xRET) but wrote
    the code anew in this repo's style, as one `execute()` switch over decoded
    instructions, so phase 6 can cache decoded instructions per I-cache line.
  - No PMP entries (the CSRs read as zero, writes are ignored), which the spec
    allows and OpenSBI detects. Storing PMP values without enforcing them was
    rejected: software would believe it is protected.
  - No triggers: tselect stays 0 and tdata1 reads 0, so the breakpoint test
    sees the trigger type isn't supported.
  - No ASIDs (satp's ASID field reads 0) and physical addresses stay below
    4 GiB, as in the port.

### Testing

- `riscv_test`: the 81 ISA tests pass. The disassembler is checked for a few
  instructions.

### Outstanding issues

- No TLB yet; every access under Sv32 walks the page table (phase 6).
- The `cycle` counter counts one per instruction; stall cycles are added by
  the CPU part (phase 2).

### Next steps

- Phase 2: the `rv32_cpu` part: AXI4 master, I- and D-caches with burst line
  fills, uncached ranges (`cacheable` parameter), the core/bus clock ratio and
  run-ahead, debugger and inspect views; the ISA tests through
  `axi_crossbar` + `axi_ram`.

## 2026-10-29 22:10 +0300

RISC-V phase 2 of 7: the `rv32_cpu` part, with caches, an AXI4 master and a
core that runs ahead of the simulation.

### Features implemented

- **`rv32_cpu`** (component bar category "RISC-V"):
  - Ports: `aclk`, `aresetn`, `msip`, `mtip`, `meip`, `seip` and the AXI4
    master `m`.
  - Parameters:
    - the AXI widths;
    - reset pc (0x80000000), hart id (also a0 at reset), a1 at reset
      (0x87000000, the device tree);
    - core clock ratio (8 core cycles per aclk cycle);
    - run ahead (1000 ns);
    - timebase (10 MHz, the time CSR);
    - I-cache and D-cache size in KiB (16 each, 0 for none), ways (4), line
      bytes (64);
    - cacheable ranges (`0x80000000/2G`).
  - Debugger:
    - pc, disassembly, breakpoints and single step;
    - a memory view of what the caches hold (physical addresses).
  - Properties:
    - state (running, waiting for the bus, wfi, reset), pc, mode, instret,
      cycle;
    - x1–x31 with ABI names, the trap CSRs and satp;
    - cache hit rates, TLB misses, bus transactions.
- `axi_ram` puts a file's contents where it sees their addresses, which wrap at
  its size: an ELF linked at 0x80000000 starts at offset 0 (before, everything
  above the RAM's size was dropped).
- The hart has a 64-entry TLB, flushed by `sfence.vma` and satp writes.

### How it works

- **Memory system** (`memory_system`, the hart's `riscv::bus`):
  - Accesses in a cacheable range go to the I-cache (fetches) or the D-cache.
    The D-cache is write-back and write-allocate.
  - On a miss it records a request and answers `stall`:
    - a line fill, or first a write-back of a dirty victim;
    - an uncached access, for addresses outside the ranges.
  - Uncached accesses must be naturally aligned (otherwise `misaligned`, which
    OpenSBI emulates). They go out one at a time in program order.
- **Retry protocol.** A stalled instruction runs again once the bus is done.
  - Uncached results (and failed fills) are kept as "completed" transactions
    of the instruction, so a retry doesn't read a UART register twice.
  - They are matched by kind, address and size, not by position: a retry may
    skip accesses, e.g. a page walk once the TLB has the page. That bug made
    rv32si-p-dirty hang uncached; matching by position was rejected.
  - No interrupt is taken while an instruction is under way
    (`hart::in_progress`), or a completed MMIO read would be lost.
- **Coherence.**
  - An I-cache miss takes the line from the D-cache if it has it, so code that
    was just written is fetched correctly.
  - `fence.i` invalidates the I-cache.
  - The CPU is the only master, so nothing else is needed.
- **TLB.** It keeps leaf PTEs and checks permissions on every use; a store to a
  page that isn't dirty walks again. Without it, a retried instruction would
  walk again, and the walk's lines could evict the data line it waits for
  (a livelock with few ways).
- **Bus unit.** One transaction at a time:
  - INCR bursts of a line, with `cache` 0b1111;
  - single narrow beats on the right byte lanes with strobes for uncached
    accesses, `cache` 0b0000;
  - `prot` privileged and instruction bits set.

  AW and W go out together, and the handshakes are taken at rising aclk edges
  as the signals were before the edge, like the other AXI parts. The bundle
  value is rebuilt only when the bus state changes.
- **Timing.**
  - The core period is the measured aclk period / ratio; the core starts after
    aclk rose twice.
  - The core keeps its own time and runs instructions until it is `run_ahead`
    ahead of the simulation.
  - An access that stalls is issued when the simulation reaches the core's
    time (wake_at), so bus traffic happens at the right times. The core goes
    on from the time the transaction ended, and the wait is added to `cycle`.
  - Interrupt lines are sampled at every eval; they're seen at most run_ahead
    late.
  - WFI waits until an input changes.
  - `time` = core time × timebase.
  - Breakpoints pause and pass once on resume; nothing runs again at the time
    of the pause.
- Shared AXI helpers (drive, fifo, clocked, field access, port helpers) moved
  from axi.cpp into `src/builtin/axi_parts.h`. `parse_number` and a new
  `parse_ranges` (any number of base/size ranges) are public in axi.h; the
  crossbar map uses `parse_ranges`.

### Testing

- `riscv_test` runs the 81 ISA tests through the simulator with `rv32_cpu` and
  `axi_ram` (64 KiB) on a 100 MHz clock, in four configurations:
  - default caches;
  - 1 KiB direct-mapped caches with 16-byte lines (many write-backs);
  - no caches (ma_data skipped: misaligned accesses trap without a handler);
  - a 64-bit bus.

  All pass, in 0.6 s for the four runs.
- A test through a 1×1 `axi_crossbar`:
  - nothing runs before aclk rose twice;
  - the test passes;
  - fewer than 40 bus transactions for over 400 instructions (line bursts);
  - cycle ≥ instret.
- The rv32_cpu icon. All 11 ctest suites pass. Not checked in the GUI yet
  (planned with the Linux example).

### Outstanding issues

- The debugger's memory and disassembly see only what the caches hold, and
  disassembly under Sv32 needs the page in the TLB.
- The RAM's debugger view doesn't show dirty D-cache lines until they're
  written back.
- One transaction at a time: a dirty miss writes back, then fills.

### Next steps

- Phase 3: `riscv_clint`, `riscv_plic`, `uart16550` with AXI slave ports and
  interrupt outputs; bare-metal firmware tests for the timer, PLIC and UART.

## 2026-10-29 23:05 +0300

RISC-V phase 3 of 7: the CLINT, PLIC and 16550 UART parts.

### Features implemented

- **`riscv_clint`**:
  - AXI slave `s` (AXI4 or AXI4-Lite) and a `msip`/`mtip` output pair per
    hart (`harts` 1–8; numbered msip0.. when more than one).
  - `timebase` (10 MHz).
  - Registers as riscv,clint0: msip at 0x0, mtimecmp at 0x4000, mtime at
    0xBFF8, 32 or 64 bit accesses.
  - 64 KiB window.
- **`riscv_plic`**:
  - AXI slave, inputs `irq1`..`irqN` (`sources` 1–31, default 8), outputs
    `meip` and `seip`.
  - Registers as riscv,plic0 with a machine and a supervisor context:
    priorities (0–7), pending, enables, threshold, claim/complete.
  - Level triggered: a claimed source stays out until completed.
  - 4 MiB window.
- **`uart16550`**:
  - AXI slave and an `irq` output; `clock` (10 MHz, the device tree's
    clock-frequency) for the baud rate.
  - ns16550a registers: RBR/THR, IER, IIR/FCR, LCR, MCR (loop mode), LSR,
    MSR, SCR, the divisor latch.
  - 16-byte receive FIFO with trigger levels and the character timeout.
  - Sending takes no time. What's sent is shown in the Properties panel's
    terminal (the same "screen" as the terminal builtin), and typed
    characters arrive one character time apart at the set baud rate.
- Icons for the four RISC-V parts.

### How it works

- `axi_regs` (riscv.cpp) is the slave base on `axi::clocked`, with the same
  skid buffers and handshake rules as `axi_ram`:
  - A beat at a time; bursts are split into beats.
  - A write's strobes give the bytes (first to last strobe).
  - A read's size comes from arsize, or for AXI4-Lite from the device's
    register size, so a UART read never pops RBR for a neighbouring byte.
  - The device sees the address modulo its window, so bases must be aligned
    to it. The PLIC window is 4 MiB rather than the device tree's 64 MiB,
    because the example puts the PLIC at 0x03000000, which isn't 64 MiB
    aligned.
  - Register errors answer SLVERR (an access fault in the cpu).
- CLINT:
  - mtime is computed from the simulation time (plus an offset that writes
    to mtime change), not counted per tick.
  - It wakes itself at the time mtime reaches the nearest mtimecmp, so timers
    cost no events.
  - mtimecmp resets to all ones.
- UART:
  - The THRE interrupt follows the 16550: raised when THR empties (at once
    after a write) or when ETBEI is enabled while empty, cleared by reading
    IIR while it's the reported interrupt.
  - The port took the THRE interrupt as a plain level.
  - The character timeout fires after 4 character times without FIFO
    activity, so single keys reach Linux, which uses trigger level 8.
  - Fixed from the port: FCR with the FIFO disabled clears it, there is no
    host terminal and no polling thread.

### Testing

- New bare-metal firmware `tests/riscv/src/devices.c` (+ start.S, link.ld),
  built by `tests/riscv/build.sh` into `tests/riscv/firmware/devices.elf`
  (committed).
  - On the Linux example's map: UART 0x01000000 (PLIC source 1), CLINT
    0x02000000, PLIC 0x03000000, RAM 0x80000000.
  - It checks a timer interrupt 100 µs ahead, a software interrupt through
    msip, and UART receive interrupts through the PLIC with FIFO trigger
    level 14 (so they come from the character timeout). It echoes typed
    characters in upper case.
- `riscv_test` `test_devices`:
  - cpu + 1×4 crossbar + UART + CLINT + PLIC + RAM on 100 MHz;
  - the screen shows hello/timer/soft/ready;
  - "abc\n" typed through interact comes back as "ABC";
  - tohost is 1 and the cpu ends in WFI.
- All 11 ctest suites pass.

### Outstanding issues

- The cpu's `time` CSR is computed from its own time and timebase, so a write
  to the CLINT's mtime isn't seen by it. OpenSBI and Linux don't write mtime.
- No baud pacing for sending (output is immediate).

### Next steps

- Phase 4: a VT100 terminal window per UART (`ui/vt100`), with keys sent as
  escape sequences.

## 2026-10-29 23:50 +0300

RISC-V phase 4 of 7: a VT100 terminal window for each UART.

### Features implemented

- **Terminal windows**: each terminal part (the `uart16550`) of a simulation
  has a window "Terminal: <path>", open when the simulation starts, zoomable
  like the framebuffer windows.
  - Opened again from the canvas right-click menu (**Show terminal**) or the
    Properties panel's **Show terminal** button.
  - A VT100/xterm subset:
    - cursor movement, erase, insert/delete of lines and characters;
    - scroll regions;
    - 16, 256 and 24-bit colors, bold, dim, underline, inverse;
    - the alternate screen (vi, less), DECCKM, autowrap, insert mode;
    - save/restore cursor;
    - cursor position and device attribute reports (answered to the
      program);
    - UTF-8; OSC titles are skipped.
  - Keys while the window has focus:
    - Enter sends CR, Backspace DEL, Tab;
    - arrows, Home/End, Insert/Delete and PageUp/PageDown, F1–F12 send
      escape sequences (application cursor mode honoured);
    - Ctrl+letter sends control characters, also Ctrl+[ \ ] Space;
    - the app's shortcuts (Ctrl+S, F5, ...) don't fire while the terminal
      has the keys.
  - 5000 lines of history (scroll with the wheel or Shift+PageUp/PageDown;
    the view follows new output while at the bottom).
  - Mouse selection; Copy (Ctrl+Shift+C), Copy all, Paste (Shift+Insert,
    Ctrl+Shift+V, the toolbar), Clear.
  - 80 × 24, or **Fit window**.

### How it works

- `ui::vt100` (src/ui/vt100.{h,cpp}, logic_ui, no ImGui): a state machine
  over bytes (ground, escape, CSI with parameters and private markers,
  strings, charsets) into a main and an alternate screen of cells
  (character, fg, bg, attributes). Lines scrolled off the top of the whole
  main screen go into the history. Answers to queries are collected for
  `take_reply()`.
- New component hook `component::terminal()` returning a
  `sim::byte_stream` (bytes + mutex + reset count), collected by the
  simulator into `simulator::terminals()` like the displays. The UART appends
  what it sends; its reset bumps the count and the window clears.
- `sim_session::terminals_gui()` (called by the app with the displays):
  - takes each stream's bytes every frame, also for closed windows, so the
    screen is current when a window opens again;
  - feeds the emulator and sends replies back;
  - typed bytes go to the part with the new
    `simulator::interact(component, "key", byte)` (flat component index)
    through the runner.
- Keys: while the window is focused it takes every keyboard key with
  `SetKeyOwner(..., LockUntilRelease)`, so the app's shortcut checks (which
  ask for any owner) see the keys as locked; the terminal reads them with its
  own owner id. Characters come from `io.InputQueueCharacters`.
- Drawing: runs of cells with one style as text on the draw list, only the
  visible lines; the cursor is a block while focused.
- The UART has no "screen" inspect item any more (escape sequences made the
  Properties text unreadable); the stream replaces it.
- New icons: copy, paste, terminal.

### Testing

- New ctest `vt100_test`:
  - text, CR/LF, backspace, tab, deferred wrap and scrolling into the
    history;
  - UTF-8;
  - CUP/ED/EL/DCH/ICH and cursor moves;
  - SGR colors and attributes;
  - the cursor position report;
  - OSC skipping;
  - scroll regions with reverse index and insert/delete lines;
  - the alternate screen, DECCKM, cursor visibility, reset;
  - resizing and copying text.
- `riscv_test` reads the UART's stream.
- All 12 ctest suites pass. The window is checked in the GUI with the Linux
  example (next entry).

### Next steps

- Phase 5: bigger `axi_ram`, an `images` parameter, the Linux example.

## 2026-10-30 00:05 +0300

RISC-V phase 5 of 7: Linux boots in a schematic. Also most of the planned
phase 6 performance work, done here because the first boot showed where the
time went, and two hart bugs that only Linux found.

### Features implemented

- **Example `examples/riscv_linux`** (category RISC-V):
  - parts: CPU (64 KiB I/D caches, ratio 4), a 1×4 crossbar, UART, CLINT,
    PLIC and a 128 MiB RAM on a 64-bit AXI4 bus with a 50 MHz clock, so the
    core runs at 200 MHz;
  - the clock and interrupt lines go through tunnels.
  - F5, **Max**, run: OpenSBI, Linux 7.2 and the busybox prompt show in the
    UART's terminal after about 15 s.
  - In `workspace/examples/riscv_linux/`:
    - `fw_jump.bin`, `Image` and `rootfs.cpio`, the prebuilt S-mode images of
      the fork, committed as agreed (3.7 MB);
    - `soc.dts` (the fork's tree with the initrd in /chosen, zicntr, and the
      CLINT/PLIC windows of the parts) and the `soc.dtb` built from it;
    - a README with the memory map and where the images come from.
  - Generated by a throwaway program against the model API, like axi_demo.
- **`axi_ram`**:
  - `size` up to 28 bits (256 MiB);
  - new **Images** parameter: `file@address, ...`. Binaries go at the
    address, ELF/hex files where they say; addresses wrap at the size.
- **Faster simulation**:
  - The first boot ran at 0.38 MIPS and didn't reach the prompt in 60 s; it
    now takes 11.8 s (2.6 MIPS, 31M instructions).
  - An idle Linux runs at about 0.14× real time.

### How it works

- **Sensitivity** (engine): `context::set_sensitive(port, on)`. Changes on a
  port that is off don't call eval; node readers are now (instance, port)
  pairs. All ports are on after reset.
- **Idle parts ignore their clock**: `axi::clocked` asks its subclasses
  `idle(ports)`, meaning no state that moves and no valid offered (RAM,
  crossbar, test master, the RISC-V slaves). While idle it turns its clock
  off and only takes up the clock's value at the next eval, so an edge in the
  same time step isn't treated as one. It must be computed before
  `outputs()`, which overwrites the inouts.
- **The cpu**:
  - ignores aclk while its bus is idle and runs on its own wakes;
  - puts a new transaction on the bus only right after a rising edge, like a
    register output. Otherwise a request from a wake at an edge's time was
    taken by the cpu at that edge while an idle slave only saw the value: the
    first version hung this way.
- **Outputs cached**: clocked parts rebuild their outputs only after an edge,
  a reset or `changed()` (interact, the devices' time and inputs), and copy
  the cached values otherwise.
- **`sim::value`** keeps up to 320 bits inline (an AXI4 bundle with 64 data
  bits is 290), and `insert` works a source word at a time on the words it
  touches. Profiling (callgrind) had shown `insert` at 39% of the time.
- The cpu's cache index uses shifts, and fetches from the line of the last
  fetch skip the lookup.
- Measured on the boot:

  | Change | Boot |
  |---|---|
  | sensitivity + outputs cached + 256-bit inline | 35.7 s |
  | 64-bit bus | 28.5 s |
  | 64 KiB caches | 14.0 s |
  | fast insert | 12.2 s |
  | 50 MHz / ratio 4 (the example's clocks) | 13.0 s |
  | 320-bit inline | 11.8 s |

  128 KiB caches or a 128-bit bus help a little more; the example keeps
  64/64. The D-cache stays near 98.6% (initramfs unpacking and page
  clearing stream through memory).
- **Hart bugs found by Linux**:
  - A failing `sc.w` kept the reservation. Every SC must end it: Linux's
    exception return does a dummy SC so that an interrupted LR/SC can't
    succeed after a handler changed the word. With it kept, typing (many UART
    interrupts) corrupted a lock or seqcount.
  - `csrrs`/`csrrc` on mip wrote the PLIC's seip line into the
    software-writable SEIP bit. When OpenSBI forwarded a timer
    (`csrs mip, STIP`) while the UART interrupt was pending, SEIP stuck at 1.
    WFI then never slept and the kernel spun in its idle loop; the shell
    stopped echoing in the GUI. The spec says the read-modify-write uses the
    software bit only.
- Terminal window:
  - the 16 base colors are VS Code's dark terminal's (xterm's blue was
    unreadable on black);
  - the background is opaque;
  - the mono font is popped before EndChild (an ImGui assert in the first GUI
    run).

### Testing

- `riscv_test`:
  - new `test_lr_sc` (a failing SC ends the reservation);
  - new `test_mip_seip` (csrs mip with the seip line high leaves only STIP);
  - `test_linux_boot` loads `examples/riscv_linux` from the workspace and
    boots to the prompt; opt-in with `LOGIC_RISCV_LINUX=workspace` because it
    takes ~12 s (passes, 11.8 s).
- All 12 ctest suites pass, and the web build compiles.
- GUI on VNC:
  - opened the example, F5, Max, run: the terminal shows the boot and the
    prompt;
  - `uname -a`, `ls /` (colors), `echo -e` with SGR codes (red, bold green),
    `cat /proc/cpuinfo` (rv32ima_zicntr_zicsr_zifencei_zaamo_zalrsc, sv32);
  - `vi /etc/inittab` uses the alternate screen and returns to the shell;
  - Up recalls history;
  - `cat -v` shows ^A and Tab, and Ctrl+D ends it.
  - Ctrl+C doesn't interrupt the shell: it runs on /dev/console without job
    control (`tty` says /dev/console), not a terminal problem.
  - Before the two hart fixes, input stopped after a few commands; found
    with the CPU's Properties (mip = 0x220 with nothing pending in the PLIC)
    and the debugger's disassembly.

### Outstanding issues

- Idle speed: with Linux in WFI, the 50 MHz clock's own events dominate
  (about 0.14× real time), so typed characters echo with some delay.
- Ctrl+C has no effect in the example's console shell (no job control on
  /dev/console). A getty on ttyS0 would give one.
- The cpu's `cycle`/`time` CSRs come from its own time; writes to the
  CLINT's mtime aren't seen by it.

### Recommended features

- Let the clock skip events while every reader ignores it (the remaining
  idle cost).
- A generated device tree from the crossbar map and the parts' parameters
  (phase 7).

### Next steps

- Phase 6 (the rest): the decoded-instruction cache per I-cache line and
  allocation-free settle(); then phase 7 extras.
