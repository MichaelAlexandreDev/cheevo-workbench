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
    PRIVATE ${CMAKE_DL_LIBS} cw_warnings)

# The application: SDL3 window, OpenGL rendering and Dear ImGui UI.
add_executable(cheevo-workbench
    achievement_editor_window.cpp
    game_view.cpp
    main.cpp)
target_link_libraries(cheevo-workbench PRIVATE
    cw_devtools cw_frontend imgui libretro_api SDL3::SDL3 OpenGL::GL cw_warnings)
target_compile_definitions(cheevo-workbench PRIVATE CW_VERSION="${PROJECT_VERSION}")

install(TARGETS cheevo-workbench RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
