cmake_minimum_required(VERSION 3.14)

project(logic)

include(Hd/cmake/resource.cmake)
add_subdirectory(Hd)

add_executable(logic
    src/main.cpp
    src/draw/draw.cpp
    src/draw/grid.cpp
    src/draw/bezier.cpp
    src/draw/text.cpp
    src/sheet.cpp
    src/ui_elements/ui_element.cpp
    src/ui_elements/anchor.cpp
    src/ui_elements/pin.cpp
    src/ui_elements/line.cpp
    src/ui_elements/spline.cpp
    src/sim/circuit.cpp
    src/sim/wire.cpp
    src/sim/gate.cpp
    src/sim/gates/and_gate.cpp
    src/sim/gates/nand_gate.cpp
    src/sim/gates/or_gate.cpp
    src/sim/gates/input_gate.cpp
    src/sim/gates/output_gate.cpp
    src/sim/net.cpp
    src/sim/pin.cpp

)

add_resource_dir(logic res)

target_link_libraries(logic Hd)
