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
