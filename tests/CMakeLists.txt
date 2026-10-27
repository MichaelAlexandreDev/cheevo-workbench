add_library(cw_test_core MODULE test_core/test_core.c)
target_link_libraries(cw_test_core PRIVATE libretro_api cw_warnings)

add_executable(cw_tests
    test_main.cpp
    test_rcheevos.cpp
    test_libretro_core.cpp
    test_input_state.cpp
    test_frame_pacer.cpp
    test_save_state.cpp
    test_save_states.cpp
    test_recent_files.cpp
    test_session_state.cpp
    test_video_frame.cpp)
target_link_libraries(cw_tests PRIVATE cw_devtools cw_frontend doctest cw_warnings)
target_compile_definitions(cw_tests PRIVATE CW_TEST_CORE_PATH="$<TARGET_FILE:cw_test_core>")
add_dependencies(cw_tests cw_test_core)

add_test(NAME cw_tests COMMAND cw_tests)
