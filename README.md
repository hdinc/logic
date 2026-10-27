# logic

A digital logic simulator with a schematic editor, written in C++20 with
OpenGL and Dear ImGui. You draw circuits from gates, memories and I/O parts,
build hierarchies of cells, and mix in processors and chips simulated by
native code, Verilog/VHDL cells, and microcontrollers running real firmware.
A waveform viewer records signals while simulating, and also comes as a
standalone VCD viewer, `logic-wave`.

## Features

- **Schematic editor**: wires, buses, splitters and tunnels (same-named
  tunnels are connected without a wire), copy/paste, undo/redo,
  rotate and mirror. Pan and zoom with the right or middle mouse button and
  the wheel. A new port connected to a pin takes the pin's width, type,
  name and description; right-click an instance > **Add ports for pins**
  wires a port to each of its free pins.
- **Symbols**: a subcircuit without a symbol of its own is shown as a box
  with its ports' pins that follows the ports. Right-click it in the
  library panel and choose **Add symbol** to draw one in the **Symbol
  editor** window (a tab per cell, next to the properties); **Reset to
  automatic** goes back to the box.
- **Library panel**: click a cell to place it in the open schematic,
  double-click (or right-click > **Edit**) to edit it.
- **Bundles**: named wires that go together, like SystemVerilog interfaces.
  A bundle type has fields with their own directions, nested bundles and a
  flipped view for the other side. Ports and wires can carry a whole
  bundle; a breakout part shows the chosen fields and a tap picks one field
  off a bundle wire. HDL and C model cells group their ports (`m0_valid`,
  `m0_data`, ...) into bundle ports.
- **Builtin parts**: constants, clocks, random number generators (random,
  LFSR), logic gates, tristate buffers,
  registers, counters, RAM, ROM, pull resistors, buttons, switches, LEDs,
  hex displays, a serial terminal and a framebuffer (pixels written at x,
  y on a clock, shown in a window while simulating). Arithmetic of any
  width: adder/
  subtractor, multiplier, divider, negator, comparator, barrel shifter and
  bit extender. Plexers: multiplexer, demultiplexer, decoder, priority
  encoder, bit counter and bit selector.
- **Component bar**: the most used builtin parts as icons by category, in
  a translucent strip over the schematic along its top, bottom, left or
  right edge, or hidden (right-click the bar, or View > Component bar). It
  scrolls smoothly when the mouse is near one of its ends or with the
  wheel, and has three icon sizes. Other cells are placed from the library
  panel or with Space.
- **Place popup** (Space or **Cell...**): opens with the recently placed
  cells first. Typing searches the names, ranked: the start of the name
  first, then the start of a word, then anywhere, with the match
  highlighted. The top entry is selected; Up and Down choose, Enter
  places, Esc closes.
- **Categories**: cells are grouped by category in the library panel. Set
  a cell's category in its properties or from the library panel's context
  menu.
- **Simulator**: 4-state values (0, 1, x, z) of any width, inouts, time in
  picoseconds. It runs on its own thread, in real time or as fast as
  possible. You can step it and look inside every instance of the hierarchy.
- **C models**: plugins (`.so` / `.dll`) with parts written in C or C++. See
  [sdk/README.md](sdk/README.md). Included:
  - a MOS 6502 built on the cores of [floooh/chips](https://github.com/floooh/chips)
  - ATmega328P and ATtiny85 using [simavr](https://github.com/buserror/simavr),
    see [plugins/avr/README.md](plugins/avr/README.md)
- **HDL cells**: Verilog and SystemVerilog through Verilator, and Verilog
  and VHDL through Icarus Verilog and GHDL. Builds are cached in
  `<workspace>/.cache`.
- **Debugger** for processors: memory views, disassembly, breakpoints and
  single stepping.
- **Waveforms**:
  - Recorded at every simulated time step.
  - Digital and analog display, with cursors that snap to transitions.
  - Time and frequency between two cursors.
  - Previous/next transition and value search.
  - Markers, zooming to a dragged range, and an overview bar of the whole
    recording.
  - Export to VCD.

## Building

You need:

- CMake 3.20 or newer and a C++20 compiler
- glfw3, glm and nlohmann_json
- OpenGL 3.3 core with `GL_ARB_explicit_uniform_location`, which most
  drivers have

Optional:

- simavr for the AVR plugin, which is skipped without it
- Verilator, Icarus Verilog and GHDL for HDL cells
- FreeType for hinted, cleaner text in the interface and small text on the
  canvas. Without it (or with `-DHD_USE_FREETYPE=OFF`) Dear ImGui draws text
  with stb_truetype, and small canvas text uses distance fields
- fontconfig to list the installed fonts. Without it (or with
  `-DLOGIC_USE_FONTCONFIG=OFF`) the usual font folders are scanned instead

```sh
git clone --recursive git@github.com:hdinc/logic.git
cd logic
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
```

If you cloned without `--recursive`, run `git submodule update --init`.

### Web build

The editor and the builtin simulator also run in a browser, built with
Emscripten. C model plugins and HDL cells aren't part of it (they need shared
libraries, child processes and a native compiler). You need Emscripten and
the host's glm and nlohmann_json headers (`-DLOGIC_HOST_INCLUDE=...` if they
aren't in `/usr/include`). The GLFW and FreeType ports are downloaded on the
first build.

```sh
emcmake cmake -S . -B build-web -DCMAKE_BUILD_TYPE=Release
cmake --build build-web -j
web/serve.py build-web    # http://localhost:8000/
```

`build-web` then has `index.html`, `index.js`, `index.wasm` and
`coi-serviceworker.js`, a folder any static host can serve as it is, from a
sub-path too (GitHub Pages). The simulator runs on a thread, which needs a
cross-origin isolated page. `serve.py` sends the headers for that. Where the
server can't, the service worker adds them and the page reloads once on the
first visit. The workspace is kept in the browser's IndexedDB and starts
with an empty `work` library.

Every pushed version tag (`v0.0.1`) deploys the web build to <https://hdinc.github.io/logic/>
(`.github/workflows/pages.yml`, Emscripten 6.0.13). It can also be run by
hand from the Actions tab. The repository's Pages source has to be "GitHub
Actions".

## Running

```sh
./build/logic [workspace folder]
```

Without an argument, the program opens the `workspace` folder of the
repository. It has two examples: `counter_6502`, a 6502 counting on a hex
display, and `blink_328p`, an ATmega328P blinking an LED. Double-click a
cell in the library panel to open it and press F5 to simulate it.

**View > Settings** sets the fonts: one for the interface, a monospace one
for the debugger, terminal and messages, and one for the text in schematics
and symbols. Any installed TrueType or OpenType font with Latin letters can
be chosen.

**Edit theme...** next to the theme in the settings (or **View > Theme**)
opens the theme editor. Every color has a name there, grouped by canvas,
signal values, waveforms, messages, displays and the interface. Dark,
Light and Black are built in; **Duplicate...** makes a custom theme you can
change, saved as a JSON file in `~/.config/logic/themes`
(`%APPDATA%\logic\themes` on Windows). The waveform viewer offers the same
themes.

While simulating, click a multi-bit input port (or use **...** next to
it in the properties) to set its value as text in the port's format, or
bit by bit. Ports show their values as hex, unsigned or signed decimal,
binary, octal, unsigned or signed fixed point, IEEE float (16, 32 or 64
bits), bfloat16 or ASCII, chosen in the port's properties or in that
popup.

Symbols are filled inside their closed outlines (**View > Fill
symbols**), in the theme's color or a cell's own (symbol properties, "Own
fill color"). **View > Grid style** has dots, lines or checkers.

Buses and bundles are drawn in their own colors. **View > Thick buses and
bundles** also draws them as thick lines.

**View > Instance names** shows all instance names, only the ones you gave
(not the default U1, U2, ...), or none.

To record signals, use **Signals...** in the simulation toolbar, or
right-click a wire, port or pin and choose **Add to waveforms**.

### Waveform viewer

```sh
./build/logic-wave [file.vcd | session.json]
```

It opens VCD files from any simulator, including timescales down to fs and
VHDL's std_logic states. You can also drop a file onto the window.

- **Adding signals:** pick them from the signal tree on the left: double-click
  a signal, drag it onto the waveforms, or right-click a scope to add
  everything in it.
- **Sessions:** a session file stores the rows, formats, cursors, markers and
  view. It still applies after the VCD file is regenerated.
- **Reload:** Ctrl+R reloads the file and keeps the layout.

Shortcuts, also listed under **Help > Shortcuts**:

| Keys | Action |
| --- | --- |
| Click / Shift+click | Cursor A / cursor B (Alt: no snapping) |
| Left / Right | Previous / next transition of the selected signals |
| Wheel, + / - , F | Zoom about the mouse, zoom in / out, fit everything |
| Right or middle drag | Pan |
| Drag over the ruler | Zoom to that range |
| M, Ctrl+[ / Ctrl+] | Marker at cursor A, previous / next marker |
| Delete | Remove the selected rows |

## Tests

```sh
ctest --test-dir build
```

The HDL tests are skipped when Verilator isn't installed.

## Source layout

| Folder | Contents |
| --- | --- |
| `src/model` | cells, schematics, symbols, libraries and undo history |
| `src/io` | library files and firmware images |
| `src/builtin` | builtin parts |
| `src/netlist`, `src/sim` | netlist extraction, the simulator and its thread |
| `src/plugin`, `src/hdl`, `src/cosim` | C model plugins, HDL tools and the VPI bridge |
| `src/wave` | waveform data, VCD files, search and sessions (`wave/ui`: the view) |
| `src/editor`, `src/render`, `src/app` | the editor, drawing and the main window |
| `src/ui` | GUI parts shared by both apps |
| `src/viewer` | the `logic-wave` app |
| `sdk` | the C model SDK and example models |
| `plugins` | the 6502 and AVR plugins |
| `Hd` | window, OpenGL and ImGui setup (a submodule) |
| `web`, `src/web` | the web build's page, service worker and glue |

## Third-party assets

- `res/fonts/Roboto.ttf`: [Roboto](https://fonts.google.com/specimen/Roboto), the
  interface and canvas font.
- `res/fonts/fa-solid-900.ttf`: [Font Awesome Free](https://fontawesome.com)
  6, solid style. The font is under the SIL OFL 1.1 and the icons under CC BY
  4.0 (`res/fonts/FontAwesome-LICENSE.txt`). Its icons are merged into the
  interface font for the toolbar buttons; the ones in use are named in
  `src/ui/icons.h`.
