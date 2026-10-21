# logic

A digital logic simulator with a schematic editor, written in C++20 with
OpenGL and Dear ImGui. You draw circuits from gates, memories and I/O parts,
build hierarchies of cells, and mix in processors and chips simulated by
native code, Verilog/VHDL cells, and microcontrollers running real firmware.
A waveform viewer records signals while simulating, and also comes as a
standalone VCD viewer, `logic-wave`.

## Features

- **Schematic editor**: wires, buses and splitters, copy/paste, undo/redo,
  rotate and mirror, symbols for subcircuits. Pan and zoom with the right or
  middle mouse button and the wheel.
- **Builtin parts**: constants, clocks, logic gates, tristate buffers,
  registers, RAM, ROM, pull resistors, buttons, switches, LEDs, hex displays
  and a serial terminal.
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

```sh
git clone --recursive git@github.com:hdinc/logic.git
cd logic
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
```

If you cloned without `--recursive`, run `git submodule update --init`.

## Running

```sh
./build/logic [workspace folder]
```

Without an argument, the program opens the `workspace` folder of the
repository. It has two examples: `counter_6502`, a 6502 counting on a hex
display, and `blink_328p`, an ATmega328P blinking an LED. Open a cell from
the library panel and press F5 to simulate it.

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
