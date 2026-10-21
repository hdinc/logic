add_executable(logic-wave
    main.cpp
    viewer_app.cpp
)
add_resource_dir(logic-wave ${CMAKE_SOURCE_DIR}/res)
target_link_libraries(logic-wave logic_ui Hd)
target_compile_options(logic-wave PRIVATE ${LOGIC_WARNINGS})
# next to the logic app
set_target_properties(logic-wave PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})
