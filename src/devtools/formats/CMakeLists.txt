# Module formats (docs/planning/refactor-plan.md, D1-A): the RetroAchievements files the toolkit
# shares with RAIntegration (User.txt, bookmarks, code notes). Its headers expose io, logic and
# memory types, so those are public dependencies.
add_library(cw_formats STATIC
    bookmarks.cpp
    code_notes.cpp
    user_file.cpp
    user_file_store.cpp)
target_include_directories(cw_formats PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(cw_formats
    PUBLIC cw_io cw_logic cw_memory
    PRIVATE nlohmann_json rcheevos cw_warnings)
