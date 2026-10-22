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

- **Hd submodule** (not committed there yet):
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
- The Hd changes need a commit in the Hd repository and a submodule bump.

### Next steps

- Commit the Hd changes and bump the submodule.
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

- Hd needs a commit of the ImGui update together with the earlier font
  changes, then a submodule bump here.
- ImGuiFileDialog stays at its pinned commit, which supports 1.92 (tested:
  the open dialog of `logic-wave`). Upstream has newer fixes.
- `logic-wave <folder>` reports `basic_string::_M_replace_aux` instead of
  saying the path is a folder. This predates the update.
- Corrections to the previous entry: font changes no longer rebuild a
  whole atlas, and the interface is no longer limited to Latin-1.

### Next steps

- Commit the Hd changes and bump the submodule.
- Plan the 1.93 update with a visual check of the waveform view.
