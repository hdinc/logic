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
