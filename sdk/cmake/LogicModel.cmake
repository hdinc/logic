# logic_add_model(<target> <sources>...)
#
# builds a c model plugin: a shared library loadable by logic, named
# <target>.so / <target>.dll without a lib prefix. copy it into a plugin
# folder: <workspace>/plugins, the plugins folder next to the program, or a
# folder in LOGIC_PLUGIN_PATH.

set(LOGIC_SDK_INCLUDE_DIR ${CMAKE_CURRENT_LIST_DIR}/../include)

function(logic_add_model target)
    add_library(${target} MODULE ${ARGN})
    target_include_directories(${target} PRIVATE ${LOGIC_SDK_INCLUDE_DIR})
    set_target_properties(${target} PROPERTIES
        PREFIX ""
        C_STANDARD 99
        CXX_STANDARD 17
        C_VISIBILITY_PRESET hidden
        CXX_VISIBILITY_PRESET hidden
        VISIBILITY_INLINES_HIDDEN ON)
    if(MINGW)
        # no dependency on the mingw runtime dlls
        target_link_options(${target} PRIVATE -static-libgcc $<$<LINK_LANGUAGE:CXX>:-static-libstdc++>)
    endif()
endfunction()
