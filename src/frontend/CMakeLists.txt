# The application: SDL3 window, OpenGL rendering, Dear ImGui UI and (from step 1.2) the
# libretro core host. Dev tools logic lives in cw_devtools.
add_executable(cheevo-workbench
    core_options.cpp
    libretro_core.cpp
    main.cpp
    paths.cpp)
target_link_libraries(cheevo-workbench PRIVATE
    cw_devtools imgui libretro_api SDL3::SDL3 OpenGL::GL ${CMAKE_DL_LIBS} cw_warnings)
target_compile_definitions(cheevo-workbench PRIVATE CW_VERSION="${PROJECT_VERSION}")

install(TARGETS cheevo-workbench RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
