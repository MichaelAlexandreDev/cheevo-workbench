# The front-end library is shared by the application and tests. Dev tools logic lives in
# cw_devtools and remains independent of SDL/OpenGL.
add_library(cw_frontend STATIC
    core_options.cpp
    frame_pacer.cpp
    libretro_core.cpp
    paths.cpp
    sdl_audio_output.cpp
    video_frame.cpp)
target_include_directories(cw_frontend PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(cw_frontend
    PUBLIC libretro_api
    PRIVATE SDL3::SDL3 ${CMAKE_DL_LIBS} cw_warnings)

# The application: SDL3 window, OpenGL rendering and Dear ImGui UI.
add_executable(cheevo-workbench
    game_view.cpp
    main.cpp)
target_link_libraries(cheevo-workbench PRIVATE
    cw_devtools cw_frontend imgui libretro_api SDL3::SDL3 OpenGL::GL cw_warnings)
target_compile_definitions(cheevo-workbench PRIVATE CW_VERSION="${PROJECT_VERSION}")

install(TARGETS cheevo-workbench RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
