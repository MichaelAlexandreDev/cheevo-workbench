# Module memory (docs/planning/refactor-plan.md, D1-A): reading emulated memory, Memory Search,
# Assisted Search and the Watch List model. Standard library only, no other module.
add_library(cw_memory STATIC
    assisted_search.cpp
    memory.cpp
    memory_search.cpp
    watch_list.cpp)
target_include_directories(cw_memory PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(cw_memory PRIVATE cw_warnings)
