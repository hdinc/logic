cmake_minimum_required(VERSION 3.20)

project(logic CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_subdirectory(Hd)

find_package(glm REQUIRED)
find_package(nlohmann_json REQUIRED)

set(LOGIC_WARNINGS -Wall -Wextra -Wpedantic -Wsuggest-override -Wno-sign-compare)

# data model, file io, builtin primitives, netlist and simulator. no gui or
# opengl dependencies
add_library(logic_core STATIC
    src/model/types.cpp
    src/model/symbol.cpp
    src/model/schematic.cpp
    src/model/cell.cpp
    src/model/library.cpp
    src/model/workspace.cpp
    src/model/history.cpp
    src/model/param.cpp
    src/model/primitive.cpp
    src/io/json.cpp
    src/io/storage.cpp
    src/builtin/builtin.cpp
    src/builtin/builder.cpp
    src/builtin/gates.cpp
    src/builtin/sources.cpp
    src/builtin/memory.cpp
    src/netlist/netlist.cpp
    src/sim/value.cpp
    src/sim/simulator.cpp
)
target_include_directories(logic_core PUBLIC src)
target_link_libraries(logic_core PUBLIC glm::glm nlohmann_json::nlohmann_json)
target_compile_options(logic_core PRIVATE ${LOGIC_WARNINGS})

add_executable(logic
    src/main.cpp
    src/app/app.cpp
    src/render/renderer.cpp
    src/editor/canvas.cpp
    src/editor/schematic_editor.cpp
    src/editor/symbol_editor.cpp
    src/editor/cell_editor.cpp
    src/editor/param_editor.cpp
)

include(Hd/cmake/resource.cmake)
add_resource_dir(logic res)

target_link_libraries(logic logic_core Hd)
target_compile_options(logic PRIVATE ${LOGIC_WARNINGS})
target_compile_definitions(logic PRIVATE LOGIC_DEFAULT_WORKSPACE="${CMAKE_SOURCE_DIR}/workspace")

enable_testing()

add_executable(model_test tests/model_test.cpp)
target_link_libraries(model_test logic_core)
target_compile_options(model_test PRIVATE ${LOGIC_WARNINGS})
add_test(NAME model_test COMMAND model_test ${CMAKE_CURRENT_BINARY_DIR})

add_executable(sim_test tests/sim_test.cpp)
target_link_libraries(sim_test logic_core)
target_compile_options(sim_test PRIVATE ${LOGIC_WARNINGS})
add_test(NAME sim_test COMMAND sim_test)
