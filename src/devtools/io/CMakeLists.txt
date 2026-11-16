# Module io (docs/planning/refactor-plan.md, D1-A): file checks and the text file store every
# writer of RA and application files goes through. No dependency on other modules.
add_library(cw_io STATIC
    file_checks.cpp
    text_file_store.cpp)
target_include_directories(cw_io PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(cw_io PRIVATE cw_warnings)
