cmake_minimum_required(VERSION 3.20)

project(logic C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# the web build: the editor and the builtin simulator, without plugins and
# hdl cells. see "Web build" in README.md.
if(EMSCRIPTEN)
    # the simulator and the hdl manager have threads, and exceptions are
    # caught. every object, the ports too, is built for both.
    add_compile_options(-pthread -fwasm-exceptions)
    add_link_options(-pthread -fwasm-exceptions)
    # glm and nlohmann_json are header only, taken from the host. not the
    # whole host include folder, its libc headers would shadow emscripten's.
    set(LOGIC_HOST_INCLUDE /usr/include CACHE PATH "Where the host's glm and nlohmann headers are")
    set(host_headers ${CMAKE_BINARY_DIR}/host_include)
    file(MAKE_DIRECTORY ${host_headers})
    foreach(d glm nlohmann)
        file(CREATE_LINK ${LOGIC_HOST_INCLUDE}/${d} ${host_headers}/${d} SYMBOLIC)
    endforeach()
    add_library(glm::glm INTERFACE IMPORTED)
    target_include_directories(glm::glm INTERFACE ${host_headers})
    add_library(nlohmann_json::nlohmann_json INTERFACE IMPORTED)
    target_include_directories(nlohmann_json::nlohmann_json INTERFACE ${host_headers})
    set(LOGIC_USE_FONTCONFIG OFF CACHE BOOL "" FORCE)
endif()

# memory errors (use after free, overflows, leaks) and undefined behavior
# are reported where they happen instead of maybe crashing later, e.g.
# cmake -B build-asan -DLOGIC_SANITIZE=ON. every target, Hd and the plugins
# too, so set before them. see "Tests" in README.md.
option(LOGIC_SANITIZE "Build with AddressSanitizer and UndefinedBehaviorSanitizer" OFF)
if(LOGIC_SANITIZE)
    # undefined behavior is recoverable so known cases in third party code
    # can be suppressed (tests/ubsan.supp), the tests still stop at the
    # first one that isn't (UBSAN_OPTIONS below)
    set(sanitize_flags -fsanitize=address,undefined -fno-omit-frame-pointer)
    add_compile_options(${sanitize_flags})
    add_link_options(${sanitize_flags})
endif()

add_subdirectory(Hd)

if(NOT EMSCRIPTEN)
    find_package(glm REQUIRED)
    find_package(nlohmann_json REQUIRED)
endif()
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
    src/model/number_format.cpp
    src/model/library.cpp
    src/model/workspace.cpp
    src/model/history.cpp
    src/model/param.cpp
    src/model/expr.cpp
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
    src/builtin/axi.cpp
    src/builtin/io.cpp
    src/builtin/arith.cpp
    src/builtin/plexers.cpp
    src/builtin/icons.cpp
    src/builtin/random.cpp
    src/builtin/riscv.cpp
    src/riscv/decode.cpp
    src/riscv/hart.cpp
    src/netlist/netlist.cpp
    src/netlist/port_setup.cpp
    src/sim/simulator.cpp
    src/sim/arith.cpp
    src/sim/numbers.cpp
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

# gui parts shared by the apps: the waveform view, file dialogs, fonts and
# style
add_library(logic_ui STATIC
    src/ui/file_dialog.cpp
    src/ui/file_list.cpp
    src/ui/fonts.cpp
    src/ui/icons.cpp
    src/ui/search.cpp
    src/ui/theme.cpp
    src/ui/zoom.cpp
    src/wave/ui/timeline.cpp
    src/wave/ui/view.cpp
)
target_link_libraries(logic_ui PUBLIC logic_wave imgui stb)
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
    src/app/theme_editor.cpp
    src/render/renderer.cpp
    src/render/live.cpp
    src/editor/canvas.cpp
    src/editor/schematic_editor.cpp
    src/editor/symbol_editor.cpp
    src/editor/cell_editor.cpp
    src/editor/symbol_page.cpp
    src/editor/model_editor.cpp
    src/editor/param_editor.cpp
    src/editor/bundle_ui.cpp
    src/editor/number_ui.cpp
    src/editor/bundle_editor.cpp
    src/editor/sim_session.cpp
    src/editor/sim_view.cpp
)

include(Hd/cmake/resource.cmake)
add_resource_dir(logic res)

target_link_libraries(logic logic_core logic_ui Hd)
target_compile_options(logic PRIVATE ${LOGIC_WARNINGS})

if(EMSCRIPTEN)
    # index.html, .js and .wasm with coi-serviceworker.js: a folder that any
    # static host can serve, github pages too
    target_sources(logic PRIVATE src/web/web.cpp src/app/file_manager.cpp)
    # kept in the browser's IndexedDB, see web/pre.js
    target_compile_definitions(logic PRIVATE LOGIC_DEFAULT_WORKSPACE="/home/web_user/workspace")
    set_target_properties(logic PROPERTIES OUTPUT_NAME index SUFFIX .html)
    target_link_options(logic PRIVATE
        -sMIN_WEBGL_VERSION=2 -sMAX_WEBGL_VERSION=2
        # layout(location = n) uniforms, glsl es 3.00 has none
        -sGL_EXPLICIT_UNIFORM_LOCATION=1
        -sALLOW_MEMORY_GROWTH=1
        -sSTACK_SIZE=8MB -sDEFAULT_PTHREAD_STACK_SIZE=2MB
        # threads are started with the page, one past these can't start
        # while the main thread waits for it. the hdl manager, 4 for the
        # canvas font (Hd/src/Hd/Msdf.cpp) and one per running simulation.
        -sPTHREAD_POOL_SIZE=16
        -sENVIRONMENT=web,worker
        -sFORCE_FILESYSTEM=1 -lidbfs.js
        "-sDEFAULT_LIBRARY_FUNCS_TO_INCLUDE=[\$addRunDependency,\$removeRunDependency,\$stringToNewUTF8]"
        # copied into a new workspace by main, now that the plugins they
        # use are linked in
        --preload-file ${CMAKE_SOURCE_DIR}/workspace/examples@/examples
        --pre-js ${CMAKE_SOURCE_DIR}/web/pre.js
        --shell-file ${CMAKE_SOURCE_DIR}/web/shell.html)
    file(GLOB_RECURSE example_files CONFIGURE_DEPENDS workspace/examples/*)
    set_property(TARGET logic APPEND PROPERTY LINK_DEPENDS ${CMAKE_SOURCE_DIR}/web/pre.js ${CMAKE_SOURCE_DIR}/web/shell.html ${example_files})
    configure_file(web/coi-serviceworker.js ${CMAKE_BINARY_DIR}/coi-serviceworker.js COPYONLY)

    # the bundled plugins are linked in, there's no dlopen. each one's entry
    # gets its own name, src/plugin/static_plugins.cpp lists them.
    function(logic_add_static_model target)
        add_library(${target} STATIC ${ARGN})
        target_include_directories(${target} PRIVATE sdk/include)
        target_compile_definitions(${target} PRIVATE logic_plugin_entry=${target}_plugin_entry)
        target_link_libraries(logic_core PUBLIC ${target})
    endfunction()
    logic_add_static_model(logic_chips plugins/chips/chips.cpp plugins/chips/m6502.cpp)

    # simavr from deps/simavr with only the cores of the avr plugin. elf
    # files are read by the program (src/io/firmware.cpp), not by simavr.
    file(GLOB simavr_sources deps/simavr/simavr/sim/*.c)
    list(FILTER simavr_sources EXCLUDE REGEX "/(run_avr|sim_elf|sim_dwarf)\\.c$")
    add_library(simavr_web STATIC ${simavr_sources}
        deps/simavr/simavr/cores/sim_megax8.c
        deps/simavr/simavr/cores/sim_mega328.c
        deps/simavr/simavr/cores/sim_tinyx5.c
        deps/simavr/simavr/cores/sim_tiny85.c
        deps/simavr_config/sim_elf_stub.c)
    # sim_core_config.h and sim_core_decl.h, simavr's makefile generates them
    target_include_directories(simavr_web PUBLIC deps/simavr_config deps/simavr/simavr/sim)
    target_include_directories(simavr_web PRIVATE deps/simavr/simavr/cores)
    target_compile_options(simavr_web PRIVATE -std=gnu99 -w)
    target_compile_definitions(simavr_web PRIVATE NO_COLOR)
    logic_add_static_model(logic_avr plugins/avr/avr.cpp plugins/avr/mcu.cpp plugins/avr/disasm.cpp)
    target_link_libraries(logic_avr PRIVATE simavr_web)
    target_compile_options(logic_avr PRIVATE ${LOGIC_WARNINGS})
    target_compile_definitions(logic_core PRIVATE LOGIC_STATIC_PLUGINS)

    # no viewer or tests
    return()
endif()

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

add_executable(riscv_test tests/riscv_test.cpp)
target_link_libraries(riscv_test logic_core)
target_compile_options(riscv_test PRIVATE ${LOGIC_WARNINGS})
add_test(NAME riscv_test COMMAND riscv_test ${CMAKE_SOURCE_DIR}/tests/riscv/isa ${CMAKE_SOURCE_DIR}/tests/riscv/firmware)

add_executable(vcd_test tests/vcd_test.cpp)
target_link_libraries(vcd_test logic_wave)
target_compile_options(vcd_test PRIVATE ${LOGIC_WARNINGS})
add_test(NAME vcd_test COMMAND vcd_test)

add_executable(theme_test tests/theme_test.cpp)
target_link_libraries(theme_test logic_ui)
target_compile_options(theme_test PRIVATE ${LOGIC_WARNINGS})
add_test(NAME theme_test COMMAND theme_test ${CMAKE_CURRENT_BINARY_DIR})

add_executable(search_test tests/search_test.cpp)
target_link_libraries(search_test logic_ui)
target_compile_options(search_test PRIVATE ${LOGIC_WARNINGS})
add_test(NAME search_test COMMAND search_test)

add_executable(waveform_test tests/waveform_test.cpp)
target_link_libraries(waveform_test logic_core logic_ui)
target_compile_options(waveform_test PRIVATE ${LOGIC_WARNINGS})
add_test(NAME waveform_test COMMAND waveform_test)

add_executable(zoom_test tests/zoom_test.cpp)
target_link_libraries(zoom_test logic_ui)
target_compile_options(zoom_test PRIVATE ${LOGIC_WARNINGS})
add_test(NAME zoom_test COMMAND zoom_test)

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

# the sanitizers' reports: leaks and undefined behavior in third party code
# are suppressed, anything else fails the test where it happens
if(LOGIC_SANITIZE)
    get_directory_property(logic_tests TESTS)
    set_tests_properties(${logic_tests} PROPERTIES ENVIRONMENT
        "LSAN_OPTIONS=suppressions=${CMAKE_SOURCE_DIR}/tests/lsan.supp;UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1:suppressions=${CMAKE_SOURCE_DIR}/tests/ubsan.supp")
endif()
