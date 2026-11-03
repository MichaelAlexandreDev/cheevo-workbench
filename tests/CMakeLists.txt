add_library(cw_test_core MODULE test_core/test_core.c)
target_link_libraries(cw_test_core PRIVATE libretro_api cw_warnings)
add_library(cw_not_a_core MODULE test_core/not_a_core.c)
target_link_libraries(cw_not_a_core PRIVATE cw_warnings)

add_executable(cw_tests
    test_assisted_search.cpp
    test_achievement_editor.cpp
    test_editor_live_test.cpp
    test_achievement_logic.cpp
    test_user_file.cpp
    test_user_file_store.cpp
    test_main.cpp
    test_bookmarks.cpp
    test_rcheevos.cpp
    test_libretro_core.cpp
    test_logic_tester.cpp
    test_authoring_runtime.cpp
    test_leaderboard_editor.cpp
    test_rich_presence_editor.cpp
    test_libretro_memory.cpp
    test_memory.cpp
    test_memory_search.cpp
    test_watch_list.cpp
    test_code_note_draft.cpp
    test_code_notes.cpp
    test_input_state.cpp
    test_file_checks.cpp
    test_frame_pacer.cpp
    test_game_hash.cpp
    test_game_ids.cpp
    test_save_state.cpp
    test_save_states.cpp
    test_recent_files.cpp
    test_session_state.cpp
    test_video_frame.cpp)
target_link_libraries(cw_tests PRIVATE cw_devtools cw_frontend doctest nlohmann_json cw_warnings)
target_compile_definitions(cw_tests PRIVATE CW_TEST_CORE_PATH="$<TARGET_FILE:cw_test_core>"
    CW_NOT_A_CORE_PATH="$<TARGET_FILE:cw_not_a_core>")
add_dependencies(cw_tests cw_test_core cw_not_a_core)

add_test(NAME cw_tests COMMAND cw_tests)

# Headless ImGui checks do not require a display server or an OpenGL context.
add_executable(cw_ui_tests
    test_main.cpp
    test_authoring_ui.cpp
    test_ui_appearance.cpp
    test_ui_fonts.cpp
    ../src/frontend/authoring_window.cpp
    ../src/frontend/ui_appearance.cpp
    ../src/frontend/ui_fonts.cpp
    ../src/frontend/ui_settings.cpp)
target_include_directories(cw_ui_tests PRIVATE ../src/frontend)
target_link_libraries(cw_ui_tests PRIVATE cw_devtools imgui doctest cw_warnings ${CMAKE_DL_LIBS})
add_test(NAME cw_ui_tests COMMAND cw_ui_tests)

# Run explicitly in a Release build; timings are informational, never a CI threshold.
add_executable(cw_assisted_search_benchmark EXCLUDE_FROM_ALL benchmark_assisted_search.cpp)
target_link_libraries(cw_assisted_search_benchmark PRIVATE cw_devtools cw_warnings)
