# Layer test (decision D3 in docs/planning/refactor-plan.md): reads every #include in src/, maps
# the including file and the included file to their modules, and fails on an edge the table below
# does not allow, or on a third-party header the module may not use.
#
#   cmake -DROOT=<repo> -P tools/check_layers.cmake
#
# The table describes the code as it is today and grows with every module PR. Adding an allowed
# edge or removing a forbidden header needs the user's approval (no new exceptions without it).

cmake_minimum_required(VERSION 3.20)

if(NOT DEFINED ROOT)
    message(FATAL_ERROR "check_layers.cmake: -DROOT=... is required")
endif()

# Modules, matched in this order against the path relative to ROOT; the first match wins.
# host = today's cw_frontend library (src/frontend/CMakeLists.txt); the rest of src/frontend is
# the windows and the application until they are split into ui and app.
set(modules devtools host frontend)
set(devtools_path "^src/devtools/")
set(host_path "^src/frontend/(audio_sink|core_options|frame_pacer|game_ids|input_state|libretro_core|libretro_memory|paths|recent_files|save_state|save_states|sdl_audio_output|session_state|video_frame)\\.(cpp|hpp)$")
set(frontend_path "^src/frontend/")

# Modules each one may include besides itself.
set(devtools_uses "")
set(host_uses devtools)
set(frontend_uses devtools host)

# Third-party headers each module may not include (regular expressions on the include text).
set(gui "^imgui" "^backends/" "^GL/" "SDL_opengl")
set(devtools_forbidden ${gui} "^SDL3/" "^SDL\\.h$" "libretro\\.h$")
set(host_forbidden ${gui})
set(frontend_forbidden "")

# Where quoted includes are searched after the including file's own directory (the include paths
# the CMake targets export).
set(include_roots "src/devtools/include" "src/frontend")

function(module_of path out)
    foreach(module IN LISTS modules)
        if(path MATCHES "${${module}_path}")
            set(${out} ${module} PARENT_SCOPE)
            return()
        endif()
    endforeach()
    set(${out} "" PARENT_SCOPE)
endfunction()

file(GLOB_RECURSE sources RELATIVE "${ROOT}" "${ROOT}/src/*")
list(FILTER sources INCLUDE REGEX "\\.(c|h|cpp|hpp)$")

set(problems "")
set(edges 0)
foreach(path IN LISTS sources)
    module_of("${path}" from)
    if(from STREQUAL "")
        string(APPEND problems "  ${path}: not in any module; add it to tools/check_layers.cmake\n")
        continue()
    endif()
    get_filename_component(dir "${path}" DIRECTORY)
    file(STRINGS "${ROOT}/${path}" lines REGEX "^[ \t]*#[ \t]*include[ \t]*[<\"]")
    foreach(line IN LISTS lines)
        if(NOT line MATCHES "#[ \t]*include[ \t]*([<\"])([^>\"]+)[>\"]")
            continue()
        endif()
        set(quoted "${CMAKE_MATCH_1}")
        set(header "${CMAKE_MATCH_2}")
        if(quoted STREQUAL "<")
            set(spelled "<${header}>")
        else()
            set(spelled "\"${header}\"")
        endif()
        if(header MATCHES "^\\.\\./" OR header MATCHES "/\\.\\./")
            string(APPEND problems "  ${path}: #include ${spelled} climbs out with ../\n")
            continue()
        endif()

        # A project header: resolve it the way the compiler would.
        set(target "")
        if(quoted STREQUAL "\"")
            foreach(base IN ITEMS "${dir}" ${include_roots})
                if(EXISTS "${ROOT}/${base}/${header}")
                    file(RELATIVE_PATH target "${ROOT}" "${ROOT}/${base}/${header}")
                    break()
                endif()
            endforeach()
        endif()

        if(target STREQUAL "")
            foreach(pattern IN LISTS ${from}_forbidden)
                if(header MATCHES "${pattern}")
                    string(APPEND problems
                        "  ${path}: #include ${spelled} is not allowed in ${from}\n")
                    break()
                endif()
            endforeach()
            continue()
        endif()

        math(EXPR edges "${edges} + 1")
        module_of("${target}" to)
        if(to STREQUAL from OR to STREQUAL "")
            continue()
        endif()
        list(FIND ${from}_uses ${to} allowed)
        if(allowed EQUAL -1)
            string(APPEND problems
                "  ${path}: ${from} may not include ${to} (${spelled} -> ${target})\n")
        endif()
    endforeach()
endforeach()

list(LENGTH sources files)
if(problems)
    message(FATAL_ERROR "Layer test failed:\n${problems}")
endif()
message(STATUS "Layer test: ${files} files, ${edges} project includes, no forbidden edge")
