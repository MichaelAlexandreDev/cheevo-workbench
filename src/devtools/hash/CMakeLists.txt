# Module hash (docs/planning/refactor-plan.md, D1-A): the game hash RetroAchievements identifies
# content by, computed with rcheevos' rhash.
add_library(cw_hash STATIC game_hash.cpp)
target_include_directories(cw_hash PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(cw_hash PRIVATE cw_io rcheevos cw_warnings)
