# Developer tools: platform-independent logic (memory access, search, achievement editing, RA file
# formats). It must not depend on SDL, OpenGL or the front-end, so it can be unit-tested with
# synthetic memory buffers.
add_library(cw_devtools STATIC
    code_notes.cpp
    file_checks.cpp
    game_hash.cpp
    memory.cpp
    memory_search.cpp
    watch_list.cpp)
target_include_directories(cw_devtools PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(cw_devtools PUBLIC rcheevos PRIVATE nlohmann_json cw_warnings)
