cmake_minimum_required(VERSION 3.20)

project(logic C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_subdirectory(Hd)

find_package(glm REQUIRED)
find_package(nlohmann_json REQUIRED)
find_package(Threads REQUIRED)

set(LOGIC_WARNINGS -Wall -Wextra -Wpedantic -Wsuggest-override -Wno-sign-compare)

# the sdk header as a string, generated wrappers include it
file(READ sdk/include/logic_model.h LOGIC_MODEL_H)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS sdk/include/logic_model.h)
configure_file(src/hdl/sdk_header.cpp.in ${CMAKE_BINARY_DIR}/generated/sdk_header.cpp @ONLY)
# the vpi bridge's sources, compiled by the hdl simulators' tools at run time
file(READ src/cosim/logic_vpi.c LOGIC_VPI_C)
file(READ src/cosim/cosim_protocol.h COSIM_PROTOCOL_H)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS src/cosim/logic_vpi.c src/cosim/cosim_protocol.h)
configure_file(src/cosim/bridge_source.cpp.in ${CMAKE_BINARY_DIR}/generated/bridge_source.cpp @ONLY)

# values and recorded waveforms: traces, vcd files, search and sessions.
# shared by the simulator and the waveform viewer, no gui.
add_library(logic_wave STATIC
    src/sim/value.cpp
    src/wave/trace.cpp
    src/wave/format.cpp
    src/wave/search.cpp
    src/wave/vcd.cpp
    src/wave/session.cpp
)
target_include_directories(logic_wave PUBLIC src)
target_link_libraries(logic_wave PUBLIC nlohmann_json::nlohmann_json)
target_compile_options(logic_wave PRIVATE ${LOGIC_WARNINGS})

# data model, file io, builtin primitives, netlist and simulator. no gui or
# opengl dependencies
add_library(logic_core STATIC
    src/model/types.cpp
    src/model/symbol.cpp
    src/model/schematic.cpp
    src/model/cell.cpp
    src/model/bundle.cpp
    src/model/library.cpp
    src/model/workspace.cpp
    src/model/history.cpp
    src/model/param.cpp
    src/model/primitive.cpp
    src/io/json.cpp
    src/io/storage.cpp
    src/io/firmware.cpp
    src/builtin/builtin.cpp
    src/builtin/builder.cpp
    src/builtin/gates.cpp
    src/builtin/sources.cpp
    src/builtin/memory.cpp
    src/builtin/wiring.cpp
    src/builtin/io.cpp
    src/builtin/arith.cpp
    src/builtin/plexers.cpp
    src/netlist/netlist.cpp
    src/sim/simulator.cpp
    src/sim/arith.cpp
    src/sim/runner.cpp
    src/sim/waveform.cpp
    src/plugin/dynlib.cpp
    src/plugin/registry.cpp
    src/plugin/libraries.cpp
    src/hdl/process.cpp
    src/hdl/job.cpp
    src/hdl/verilator.cpp
    src/hdl/icarus.cpp
    src/hdl/ghdl.cpp
    src/hdl/vhdl_parse.cpp
    src/hdl/manager.cpp
    src/cosim/socket.cpp
    src/cosim/component.cpp
    ${CMAKE_BINARY_DIR}/generated/sdk_header.cpp
    ${CMAKE_BINARY_DIR}/generated/bridge_source.cpp
)
target_include_directories(logic_core PUBLIC src sdk/include)
target_link_libraries(logic_core PUBLIC logic_wave glm::glm nlohmann_json::nlohmann_json Threads::Threads ${CMAKE_DL_LIBS})
if(WIN32)
    target_link_libraries(logic_core PUBLIC ws2_32)
endif()
target_compile_options(logic_core PRIVATE ${LOGIC_WARNINGS})

# file dialogs
add_subdirectory(deps/ImGuiFileDialog)
target_link_libraries(ImGuiFileDialog PUBLIC imgui)
target_compile_definitions(ImGuiFileDialog PUBLIC USE_STD_FILESYSTEM)

# gui parts shared by the apps: the waveform view, file dialogs, fonts and
# style
add_library(logic_ui STATIC
    src/ui/file_dialog.cpp
    src/ui/fonts.cpp
    src/ui/style.cpp
    src/wave/ui/timeline.cpp
    src/wave/ui/view.cpp
)
target_link_libraries(logic_ui PUBLIC logic_wave imgui ImGuiFileDialog stb)
target_compile_options(logic_ui PRIVATE ${LOGIC_WARNINGS})

# installed fonts from fontconfig, without it the font folders are scanned
find_package(PkgConfig QUIET)
option(LOGIC_USE_FONTCONFIG "Find installed fonts with fontconfig" ON)
if(LOGIC_USE_FONTCONFIG AND PkgConfig_FOUND)
    pkg_check_modules(FONTCONFIG QUIET IMPORTED_TARGET fontconfig)
endif()
if(FONTCONFIG_FOUND)
    target_link_libraries(logic_ui PRIVATE PkgConfig::FONTCONFIG)
    target_compile_definitions(logic_ui PRIVATE LOGIC_HAVE_FONTCONFIG)
endif()

add_executable(logic
    src/main.cpp
    src/app/app.cpp
    src/app/component_bar.cpp
    src/render/renderer.cpp
    src/render/live.cpp
    src/editor/canvas.cpp
    src/editor/schematic_editor.cpp
    src/editor/symbol_editor.cpp
    src/editor/cell_editor.cpp
    src/editor/model_editor.cpp
    src/editor/param_editor.cpp
    src/editor/bundle_ui.cpp
    src/editor/bundle_editor.cpp
    src/editor/sim_session.cpp
    src/editor/sim_view.cpp
)

include(Hd/cmake/resource.cmake)
add_resource_dir(logic res)

target_link_libraries(logic logic_core logic_ui Hd)
target_compile_options(logic PRIVATE ${LOGIC_WARNINGS})
target_compile_definitions(logic PRIVATE LOGIC_DEFAULT_WORKSPACE="${CMAKE_SOURCE_DIR}/workspace")

# the standalone waveform viewer, in its own directory for its resources
add_subdirectory(src/viewer)

# c model sdk and the example plugin, built into plugins/ next to the program
include(sdk/cmake/LogicModel.cmake)
logic_add_model(logic_examples sdk/examples/examples.cpp sdk/examples/counter.c sdk/examples/toy_cpu.cpp)
set_target_properties(logic_examples PROPERTIES
    LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/plugins
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/plugins)
add_dependencies(logic logic_examples)

# processors and chips with the cores of floooh/chips
logic_add_model(logic_chips plugins/chips/chips.cpp plugins/chips/m6502.cpp)
set_target_properties(logic_chips PROPERTIES
    LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/plugins
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/plugins)
add_dependencies(logic logic_chips)

# avr microcontrollers with the system's simavr, skipped without it
if(PkgConfig_FOUND)
    pkg_check_modules(SIMAVR QUIET IMPORTED_TARGET simavr)
endif()
if(SIMAVR_FOUND)
    logic_add_model(logic_avr plugins/avr/avr.cpp plugins/avr/mcu.cpp plugins/avr/disasm.cpp)
    target_link_libraries(logic_avr PRIVATE PkgConfig::SIMAVR)
    target_compile_options(logic_avr PRIVATE ${LOGIC_WARNINGS})
    set_target_properties(logic_avr PROPERTIES
        LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/plugins
        RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/plugins)
    add_dependencies(logic logic_avr)
    set(LOGIC_AVR_PLUGIN $<TARGET_FILE:logic_avr>)
else()
    message(STATUS "simavr not found, the avr plugin is skipped")
    set(LOGIC_AVR_PLUGIN "")
endif()

enable_testing()

add_executable(model_test tests/model_test.cpp)
target_link_libraries(model_test logic_core)
target_compile_options(model_test PRIVATE ${LOGIC_WARNINGS})
add_test(NAME model_test COMMAND model_test ${CMAKE_CURRENT_BINARY_DIR})

add_executable(sim_test tests/sim_test.cpp)
target_link_libraries(sim_test logic_core)
target_compile_options(sim_test PRIVATE ${LOGIC_WARNINGS})
add_test(NAME sim_test COMMAND sim_test)

add_executable(vcd_test tests/vcd_test.cpp)
target_link_libraries(vcd_test logic_wave)
target_compile_options(vcd_test PRIVATE ${LOGIC_WARNINGS})
add_test(NAME vcd_test COMMAND vcd_test)

add_executable(waveform_test tests/waveform_test.cpp)
target_link_libraries(waveform_test logic_core logic_ui)
target_compile_options(waveform_test PRIVATE ${LOGIC_WARNINGS})
add_test(NAME waveform_test COMMAND waveform_test)

# distance fields of the canvas font, with fonts of the system when they're there
add_executable(font_test tests/font_test.cpp)
target_link_libraries(font_test Hd)
target_compile_options(font_test PRIVATE ${LOGIC_WARNINGS})
add_test(NAME font_test COMMAND font_test ${CMAKE_SOURCE_DIR}/res/fonts/Roboto.ttf
    /usr/share/fonts/noto/NotoSans-CondensedBoldItalic.ttf /usr/share/fonts/gsfonts/C059-BdIta.otf
    /usr/share/fonts/noto/NotoSerifDisplay-SemiCondensedBlackItalic.ttf)
set_tests_properties(font_test PROPERTIES SKIP_RETURN_CODE 77)

# a plugin that refuses every abi version, only for the tests
logic_add_model(test_bad_plugin tests/plugins/bad_plugin.c)
set_target_properties(test_bad_plugin PROPERTIES
    LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/test_plugins
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/test_plugins)

add_executable(plugin_test tests/plugin_test.cpp)
target_link_libraries(plugin_test logic_core)
target_compile_options(plugin_test PRIVATE ${LOGIC_WARNINGS})
add_dependencies(plugin_test logic_examples logic_chips test_bad_plugin)
if(SIMAVR_FOUND)
    add_dependencies(plugin_test logic_avr)
endif()
add_test(NAME plugin_test COMMAND plugin_test $<TARGET_FILE:logic_examples> $<TARGET_FILE:test_bad_plugin> $<TARGET_FILE:logic_chips>
    ${CMAKE_SOURCE_DIR}/tests/roms "${LOGIC_AVR_PLUGIN}")

# needs verilator, skipped without it. builds are cached between runs.
add_executable(hdl_test tests/hdl_test.cpp)
target_link_libraries(hdl_test logic_core)
target_compile_options(hdl_test PRIVATE ${LOGIC_WARNINGS})
add_test(NAME hdl_test COMMAND hdl_test ${CMAKE_SOURCE_DIR}/tests/hdl ${CMAKE_BINARY_DIR}/hdl_test_cache)
set_tests_properties(hdl_test PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 900)
