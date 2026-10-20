# C model SDK

A C model is a part simulated by native code: a microcontroller core, a
peripheral chip, or anything else that's easier to write in C or C++ than to
draw as a schematic. Models live in plugins, shared libraries (`.so` / `.dll`)
that the program loads.

## Writing a plugin

Include `include/logic_model.h` (plain C99) or `include/logic_model.hpp`
(C++ helpers on top of it) and export one function:

```c
LOGIC_EXPORT const logic_plugin* logic_plugin_entry(uint32_t host_abi_version);
```

It returns the plugin's models, or null if it can't work with the program's
ABI version. Each model (`logic_model_desc`) has:

- a name (stored in the files, don't change it), a description and a category
- parameters, e.g. a bit width or a firmware file
- `get_ports`: the ports for given parameter values
- `create` / `destroy` / `reset` for instances
- `eval`: called when an input changes, when a wake time it asked for comes,
  and once after reset. It reads inputs and writes outputs.
- optional `inspect`, `save_state`, `load_state` for debugging views and
  snapshots

Values are 4-state: each bit is 0, 1, x or z, stored as three word arrays.
Use the helpers `logic_get_bit`, `logic_get_u64`, `logic_set_u64`,
`logic_set_z`, ... Time is in picoseconds. To do something later, set
`ctx->wake` (e.g. `ctx->now + LOGIC_US`) and act when `ctx->woken` is set.

Optional parts (a model built with an older header simply doesn't have them):

- `load_image`: the program reads the files of `LOGIC_PARAM_FILE` parameters
  (Intel HEX, S-records, ELF or binary) and passes them as segments, e.g. the
  firmware of a processor
- `interact`: user actions while simulating
- `debug`: memories, program counter, disassembly, breakpoints and single
  steps for the debugger in the program. To stop the simulation (e.g. at a
  breakpoint) set `ctx->pause` to a text in `eval`.
- `symbol`: the model's symbol as JSON text instead of a generated box (see
  `logic_model.h` for the format, `examples.cpp` has one for the 7 segment
  decoder). The easiest way to make one: edit the symbol of the model's
  cell in the program and copy the file it saves.

See `examples/` for a counter in C, a 7 segment decoder, a timed UART
transmitter and a tiny processor with a debugger interface in C++.

## Building

With CMake:

```cmake
include(path/to/sdk/cmake/LogicModel.cmake)
logic_add_model(my_models my_models.c)
```

That builds `my_models.so` / `my_models.dll` without a `lib` prefix and
with hidden symbols except the entry. Any other build system works too: build
a shared library that exports `logic_plugin_entry` with C linkage.

## Using it

Copy the plugin into a plugin folder:

- `<workspace>/plugins`
- `plugins` next to the program
- a folder listed in `LOGIC_PLUGIN_PATH` (`:` separated, `;` on Windows)

Each plugin found there shows up as a read only library named after it, with
a cell per model, ready to place (*File > Reload plugins* finds new ones and
reads rebuilt ones again). The symbol of a model's cell is, in this order:

1. one edited in the program, saved as
   `<workspace>/plugins/<plugin>/symbols/<model>.json`
2. the model's `symbol`
3. a generated box with the inputs on the left and the rest on the right

A plugin somewhere else can still be used through a cell of its own: *New C
model cell*, choose the plugin and model on its model page and press
*Refresh interface*. A plugin path relative to the
library folder works as well. A plugin that is rebuilt while the program runs
is loaded again the next time it's used; running simulations keep the old
version until they stop.
