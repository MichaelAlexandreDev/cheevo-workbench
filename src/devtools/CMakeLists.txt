# Developer tools: platform-independent logic (memory access, search, achievement editing, RA file
# formats). It must not depend on SDL, OpenGL or the front-end, so it can be unit-tested with
# synthetic memory buffers. The modules split out so far live in subdirectories; cw_devtools holds
# the rest and is removed once every module has its own target (refactor plan R8-R14).
add_subdirectory(io)
add_subdirectory(hash)
add_subdirectory(memory)

add_library(cw_devtools STATIC
    achievement_editor.cpp
    achievement_logic.cpp
    bookmarks.cpp
    code_note_draft.cpp
    code_notes.cpp
    editor_live_test.cpp
    logic_tester.cpp
    leaderboard_editor.cpp
    rich_presence_editor.cpp
    user_file.cpp
    user_file_store.cpp)
target_include_directories(cw_devtools PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(cw_devtools PUBLIC cw_io cw_memory rcheevos PRIVATE nlohmann_json cw_warnings)
