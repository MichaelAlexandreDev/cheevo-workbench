# The front-end library is shared by the application and tests. Dev tools logic lives in
# cw_devtools and remains independent of SDL/OpenGL.
add_library(cw_frontend STATIC
    core_options.cpp
    frame_pacer.cpp
    game_ids.cpp
    input_state.cpp
    libretro_core.cpp
    libretro_memory.cpp
    paths.cpp
    recent_files.cpp
    save_state.cpp
    save_states.cpp
    sdl_audio_output.cpp
    session_state.cpp
    video_frame.cpp)
target_include_directories(cw_frontend PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(cw_frontend
    PUBLIC cw_devtools libretro_api SDL3::SDL3
    PRIVATE cw_io ${CMAKE_DL_LIBS} cw_warnings)

# The ImGui windows and widgets (module `ui` in docs/planning/refactor-plan.md). Compiled against
# the ImGui headers only: the application links `imgui`, the window tests `imgui_test_hooks`.
add_library(cw_ui STATIC
    achievement_editor_window.cpp
    assisted_search_window.cpp
    authoring_window.cpp
    code_notes_window.cpp
    logic_tester_window.cpp
    memory_search_window.cpp
    memory_viewer_window.cpp
    ui_appearance.cpp
    ui_fonts.cpp
    ui_settings.cpp
    ui_widgets.cpp
    watch_list_window.cpp)
target_link_libraries(cw_ui
    PUBLIC cw_devtools cw_frontend imgui_headers SDL3::SDL3
    PRIVATE ${CMAKE_DL_LIBS} cw_warnings)

# The application around the windows (module `app`): menus, dialogs, status, game view. main.cpp
# stays in the executable.
add_library(cw_app STATIC
    app_state.cpp
    content_dialogs.cpp
    game_view.cpp
    gamepad_input.cpp
    main_menu.cpp
    status_window.cpp)
target_link_libraries(cw_app
    PUBLIC cw_ui cw_frontend imgui_headers SDL3::SDL3 OpenGL::GL
    PRIVATE cw_hash cw_warnings)
target_compile_definitions(cw_app PRIVATE CW_VERSION="${PROJECT_VERSION}")

# The application: SDL3 window, OpenGL rendering and Dear ImGui UI.
add_executable(cheevo-workbench main.cpp)
target_link_libraries(cheevo-workbench PRIVATE
    cw_app cw_ui cw_devtools cw_frontend imgui libretro_api SDL3::SDL3 OpenGL::GL cw_warnings
    ${CMAKE_DL_LIBS})

install(TARGETS cheevo-workbench RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
