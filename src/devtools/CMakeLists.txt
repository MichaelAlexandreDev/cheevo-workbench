# Developer tools: platform-independent logic (memory search, achievement editing, RA file
# formats). It must not depend on SDL, OpenGL or the front-end, so it can be unit-tested with
# synthetic memory buffers.
#
# Header-only for now; it becomes a STATIC library when the first source file lands (milestone 2).
add_library(cw_devtools INTERFACE)
target_include_directories(cw_devtools INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(cw_devtools INTERFACE rcheevos)
