cmake_minimum_required(VERSION 3.14)

project(logic)

add_subdirectory(Hd)

add_executable(logic
    # src/draw/draw.cpp
    # src/draw/grid.cpp
    # src/draw/bezier.cpp
    # src/draw/text.cpp
    # src/sheet.cpp
    # src/ui_elements/ui_element.cpp
    # src/ui_elements/anchor.cpp
    # src/ui_elements/pin.cpp
    # src/ui_elements/line.cpp
    # src/ui_elements/spline.cpp
    # src/sim/circuit.cpp
    # src/sim/wire.cpp
    # src/sim/gate.cpp
    # src/sim/gates/and_gate.cpp
    # src/sim/gates/nand_gate.cpp
    # src/sim/gates/or_gate.cpp
    # src/sim/gates/input_gate.cpp
    # src/sim/gates/output_gate.cpp
    # src/sim/net.cpp
    # src/sim/pin.cpp

    src/main.cpp
    src/logic.cpp
    src/editor/editor.cpp
    src/editor/item.cpp
    src/editor/symbol.cpp
    src/editor/circuit_editor/circuit_editor.cpp
    src/editor/circuit_editor/wire.cpp
    src/editor/circuit_editor/port.cpp
    src/editor/circuit_editor/component.cpp
    src/editor/symbol_editor/symbol_editor.cpp
    src/editor/symbol_editor/control_point.cpp
    src/editor/symbol_editor/pin.cpp
    src/editor/symbol_editor/line.cpp
    src/editor/symbol_editor/bezier.cpp
    src/editor/symbol_editor/text.cpp
    src/editor/symbol_editor/circle.cpp


)

include(Hd/cmake/resource.cmake)
add_resource_dir(logic res)

target_link_libraries(logic Hd)

target_compile_options(logic PRIVATE -Wall -Wextra -Wpedantic -Wsuggest-override -Wno-sign-compare)
