# Developer tools: platform-independent logic (memory access, search, achievement editing, RA file
# formats). It must not depend on SDL, OpenGL or the front-end, so it can be unit-tested with
# synthetic memory buffers. The modules split out so far live in subdirectories; cw_devtools holds
# the rest and is removed once every module has its own target (refactor plan R8-R14).
add_subdirectory(io)
add_subdirectory(hash)
add_subdirectory(memory)
add_subdirectory(logic)
add_subdirectory(formats)

add_library(cw_devtools STATIC
    achievement_editor.cpp
    code_note_draft.cpp
    editor_live_test.cpp
    logic_tester.cpp
    leaderboard_editor.cpp
    rich_presence_editor.cpp)
target_include_directories(cw_devtools PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(cw_devtools
    PUBLIC cw_formats cw_io cw_logic cw_memory rcheevos
    PRIVATE cw_warnings)
