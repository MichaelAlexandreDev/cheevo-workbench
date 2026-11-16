add_library(cw_test_core MODULE test_core/test_core.c)
target_link_libraries(cw_test_core PRIVATE libretro_api cw_warnings)
add_library(cw_not_a_core MODULE test_core/not_a_core.c)
target_link_libraries(cw_not_a_core PRIVATE cw_warnings)

add_executable(cw_tests
    memory/test_assisted_search.cpp
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
    memory/test_memory.cpp
    memory/test_memory_search.cpp
    memory/test_watch_list.cpp
    test_code_note_draft.cpp
    test_code_notes.cpp
    test_input_state.cpp
    io/test_file_checks.cpp
    test_frame_pacer.cpp
    hash/test_game_hash.cpp
    test_game_ids.cpp
    test_save_state.cpp
    test_save_states.cpp
    test_recent_files.cpp
    test_session_state.cpp
    test_video_frame.cpp)
# Tests of a module live in tests/<module>/ (decision D12) and include the shared helpers from here.
target_include_directories(cw_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(cw_tests PRIVATE cw_io cw_hash cw_memory cw_devtools cw_frontend doctest nlohmann_json
    cw_warnings)
target_compile_definitions(cw_tests PRIVATE CW_TEST_CORE_PATH="$<TARGET_FILE:cw_test_core>"
    CW_NOT_A_CORE_PATH="$<TARGET_FILE:cw_not_a_core>")
add_dependencies(cw_tests cw_test_core cw_not_a_core)

add_test(NAME cw_tests COMMAND cw_tests)

# Dependency direction between modules (tools/check_layers.cmake): fails on a forbidden #include.
add_test(NAME layers
    COMMAND ${CMAKE_COMMAND} -DROOT=${PROJECT_SOURCE_DIR} -P ${PROJECT_SOURCE_DIR}/tools/check_layers.cmake)

# Headless ImGui checks do not require a display server or an OpenGL context.
add_executable(cw_ui_tests
    test_main.cpp
    test_authoring_ui.cpp
    test_ui_appearance.cpp
    test_ui_fonts.cpp)
target_link_libraries(cw_ui_tests PRIVATE cw_ui imgui doctest cw_warnings)
add_test(NAME cw_ui_tests COMMAND cw_ui_tests)

# Format corpus (decision D9 of docs/planning/refactor-plan.md): loads and saves synthetic fixtures
# through the application's public API. cw_format_tests compares the result with the golden files;
# tools/compare-with-main.sh runs cw_format_corpus from two builds and compares their outputs.
add_library(cw_format_corpus_lib STATIC
    format_corpus/corpus.cpp
    format_corpus/corpus_app_files.cpp
    format_corpus/corpus_bookmarks.cpp
    format_corpus/corpus_code_notes.cpp
    format_corpus/corpus_rich_presence.cpp
    format_corpus/corpus_save_state.cpp
    format_corpus/corpus_user_file.cpp)
target_include_directories(cw_format_corpus_lib PUBLIC format_corpus)
# The directory settings are written by ui_widgets, which needs an ImGui build to link.
target_link_libraries(cw_format_corpus_lib PUBLIC cw_devtools cw_frontend cw_memory cw_ui imgui
    PRIVATE cw_warnings)

add_executable(cw_format_corpus format_corpus/main.cpp)
target_link_libraries(cw_format_corpus PRIVATE cw_format_corpus_lib cw_warnings)

add_executable(cw_format_tests test_main.cpp test_format_corpus.cpp)
target_compile_definitions(cw_format_tests PRIVATE
    CW_CORPUS_DIR="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/corpus")
target_link_libraries(cw_format_tests PRIVATE cw_format_corpus_lib doctest cw_warnings)
add_test(NAME cw_format_tests COMMAND cw_format_tests)

# Window tests. They link cw_app and cw_ui, compile src/frontend/main.cpp into test_main_windows.cpp
# (with its main() renamed) and press the real widgets through ImGui's test engine hooks, so they
# link a dedicated ImGui build: the hooks must not reach the shipped application.
add_library(imgui_test_hooks STATIC
    ${IMGUI_DIR}/imgui.cpp
    ${IMGUI_DIR}/imgui_demo.cpp
    ${IMGUI_DIR}/imgui_draw.cpp
    ${IMGUI_DIR}/imgui_tables.cpp
    ${IMGUI_DIR}/imgui_widgets.cpp
    ${IMGUI_DIR}/backends/imgui_impl_sdl3.cpp
    ${IMGUI_DIR}/backends/imgui_impl_opengl3.cpp)
target_compile_definitions(imgui_test_hooks PUBLIC IMGUI_ENABLE_TEST_ENGINE)
target_link_libraries(imgui_test_hooks PUBLIC imgui_headers)

add_executable(cw_window_tests
    test_main.cpp
    test_main_windows.cpp
    test_window_achievement_editor.cpp
    test_window_assisted_search.cpp
    test_window_authoring.cpp
    test_window_shortcuts.cpp
    window_test_hooks.cpp)
target_compile_definitions(cw_window_tests PRIVATE
    CW_TEST_CORE_PATH="$<TARGET_FILE:cw_test_core>")
target_link_libraries(cw_window_tests PRIVATE
    cw_app cw_ui cw_devtools cw_frontend cw_memory imgui_test_hooks libretro_api doctest SDL3::SDL3 OpenGL::GL
    cw_warnings ${CMAKE_DL_LIBS})
add_dependencies(cw_window_tests cw_test_core)
add_test(NAME cw_window_tests COMMAND cw_window_tests)

# Run explicitly in a Release build; timings are informational, never a CI threshold.
add_executable(cw_assisted_search_benchmark EXCLUDE_FROM_ALL memory/benchmark_assisted_search.cpp)
target_link_libraries(cw_assisted_search_benchmark PRIVATE cw_memory cw_warnings)

# Size ratchet (tools/size_ratchet.json): files and functions over the limits may only shrink. The
# function check runs clang-tidy over every translation unit, so it is labeled slow; skip it in the
# edit-build-test loop with `ctest -LE slow`.
add_test(NAME size_ratchet_files
    COMMAND ${CMAKE_COMMAND} -DROOT=${PROJECT_SOURCE_DIR} -DMODE=files
            -P ${PROJECT_SOURCE_DIR}/tools/size_ratchet.cmake)
find_program(CW_CLANG_TIDY clang-tidy)
if(CW_CLANG_TIDY)
    add_test(NAME size_ratchet_functions
        COMMAND ${CMAKE_COMMAND} -DROOT=${PROJECT_SOURCE_DIR} -DMODE=functions
                -DBUILD_DIR=${PROJECT_BINARY_DIR} -DCLANG_TIDY=${CW_CLANG_TIDY}
                -P ${PROJECT_SOURCE_DIR}/tools/size_ratchet.cmake)
    set_tests_properties(size_ratchet_functions PROPERTIES LABELS slow)
else()
    message(STATUS "clang-tidy not found: the function size ratchet is not registered")
endif()
