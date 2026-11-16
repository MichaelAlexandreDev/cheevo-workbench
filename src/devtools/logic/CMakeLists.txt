# Module logic (docs/planning/refactor-plan.md, D1-A): the RA logic string syntax, parsed and
# written through rcheevos. No other module.
add_library(cw_logic STATIC achievement_logic.cpp)
target_include_directories(cw_logic PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(cw_logic PRIVATE rcheevos cw_warnings)
