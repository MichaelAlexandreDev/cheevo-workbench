# Third-party dependencies. Everything except SDL3 and OpenGL is vendored as a git submodule
# under third_party/ and built here with its own (relaxed) warning settings.

set(CW_THIRD_PARTY ${PROJECT_SOURCE_DIR}/third_party)

foreach(dep imgui rcheevos doctest libretro-common json)
    if(NOT EXISTS ${CW_THIRD_PARTY}/${dep}/.git)
        message(FATAL_ERROR
            "third_party/${dep} is missing. Run: git submodule update --init --recursive")
    endif()
endforeach()

find_package(SDL3 REQUIRED CONFIG)
find_package(OpenGL REQUIRED)

# --- Dear ImGui (docking branch) with the SDL3 + OpenGL 3 backends --------------------------
set(IMGUI_DIR ${CW_THIRD_PARTY}/imgui)

# The headers alone. cw_ui and cw_app compile against these and never link an ImGui build, so the
# application links `imgui` and the window tests link `imgui_test_hooks` (tests/CMakeLists.txt)
# with the same objects. IMGUI_ENABLE_TEST_ENGINE only adds enum bits and hook declarations
# (imgui_internal.h), so both builds agree on every type (refactor plan, decision D4).
add_library(imgui_headers INTERFACE)
target_include_directories(imgui_headers SYSTEM INTERFACE ${IMGUI_DIR} ${IMGUI_DIR}/backends)
target_link_libraries(imgui_headers INTERFACE SDL3::SDL3)

add_library(imgui STATIC
    ${IMGUI_DIR}/imgui.cpp
    ${IMGUI_DIR}/imgui_demo.cpp
    ${IMGUI_DIR}/imgui_draw.cpp
    ${IMGUI_DIR}/imgui_tables.cpp
    ${IMGUI_DIR}/imgui_widgets.cpp
    ${IMGUI_DIR}/backends/imgui_impl_sdl3.cpp
    ${IMGUI_DIR}/backends/imgui_impl_opengl3.cpp)
target_link_libraries(imgui PUBLIC imgui_headers)

# --- libretro API header (libretro.h) ---------------------------------------------------------
add_library(libretro_api INTERFACE)
target_include_directories(libretro_api SYSTEM INTERFACE ${CW_THIRD_PARTY}/libretro-common/include)

# --- rcheevos: logic evaluation, hashing, Rich Presence, libretro memory mapping -------------
# rapi/ and rc_client* are left out on purpose: they exist to talk to the RA server, which this
# project must not do without RA team approval (AGENTS.md hard rule 1).
set(RC_DIR ${CW_THIRD_PARTY}/rcheevos)
file(GLOB RC_RUNTIME_SOURCES CONFIGURE_DEPENDS ${RC_DIR}/src/rcheevos/*.c)
file(GLOB RC_HASH_SOURCES CONFIGURE_DEPENDS ${RC_DIR}/src/rhash/*.c)

# v12.5.0 promotes bytes to signed int before shifting the high byte of RAP values. Backport
# unsigned decoding into a generated copy, keeping the pinned submodule untouched. This also
# handles legitimate sentinel/negative values under UBSan instead of disabling its checks.
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
    ${RC_DIR}/src/rcheevos/runtime_progress.c)
file(READ ${RC_DIR}/src/rcheevos/runtime_progress.c CW_RC_PROGRESS_SOURCE)
set(CW_RC_SIGNED_READ [=[uint32_t value = progress->buffer[progress->offset + 0] |
      (progress->buffer[progress->offset + 1] << 8) |
      (progress->buffer[progress->offset + 2] << 16) |
      (progress->buffer[progress->offset + 3] << 24);]=])
set(CW_RC_UNSIGNED_READ [=[uint32_t value = (uint32_t)progress->buffer[progress->offset + 0] |
      ((uint32_t)progress->buffer[progress->offset + 1] << 8) |
      ((uint32_t)progress->buffer[progress->offset + 2] << 16) |
      ((uint32_t)progress->buffer[progress->offset + 3] << 24);]=])
string(FIND "${CW_RC_PROGRESS_SOURCE}" "${CW_RC_SIGNED_READ}" CW_RC_PROGRESS_READ_OFFSET)
if(CW_RC_PROGRESS_READ_OFFSET EQUAL -1)
    message(FATAL_ERROR "Review the rcheevos RAP unsigned-read patch after updating the dependency")
endif()
string(REPLACE "${CW_RC_SIGNED_READ}" "${CW_RC_UNSIGNED_READ}"
    CW_RC_PROGRESS_SOURCE "${CW_RC_PROGRESS_SOURCE}")
file(MAKE_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/rcheevos)
file(CONFIGURE OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/rcheevos/runtime_progress.c
    CONTENT "${CW_RC_PROGRESS_SOURCE}" @ONLY)
list(REMOVE_ITEM RC_RUNTIME_SOURCES ${RC_DIR}/src/rcheevos/runtime_progress.c)
list(APPEND RC_RUNTIME_SOURCES ${CMAKE_CURRENT_BINARY_DIR}/rcheevos/runtime_progress.c)

add_library(rcheevos STATIC
    ${RC_RUNTIME_SOURCES}
    ${RC_HASH_SOURCES}
    ${RC_DIR}/src/rc_compat.c
    ${RC_DIR}/src/rc_libretro.c
    ${RC_DIR}/src/rc_util.c
    ${RC_DIR}/src/rc_version.c)
# src/ is public too because rc_libretro.h lives there.
target_include_directories(rcheevos SYSTEM PUBLIC ${RC_DIR}/include ${RC_DIR}/src)
target_include_directories(rcheevos SYSTEM PRIVATE ${RC_DIR}/src/rcheevos)
target_link_libraries(rcheevos PUBLIC libretro_api) # rc_libretro.h includes libretro.h

# --- nlohmann/json (header-only): our JSON files (code notes, bookmarks) and RAIntegration's ----
add_library(nlohmann_json INTERFACE)
target_include_directories(nlohmann_json SYSTEM INTERFACE ${CW_THIRD_PARTY}/json/include)

# --- doctest (unit tests) ---------------------------------------------------------------------
add_library(doctest INTERFACE)
target_include_directories(doctest SYSTEM INTERFACE ${CW_THIRD_PARTY}/doctest)
