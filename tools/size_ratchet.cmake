# Size ratchet (decision D10 in docs/planning/refactor-plan.md): no file over max_file_lines and no
# function over max_function_lines, except the entries listed in tools/size_ratchet.json. Listed
# counts may only go down, and the JSON must follow them down, so the limit tightens over time.
#
#   cmake -DROOT=<repo> -DMODE=files -P tools/size_ratchet.cmake
#   cmake -DROOT=<repo> -DMODE=functions -DBUILD_DIR=<build> [-DCLANG_TIDY=<path>]
#         -P tools/size_ratchet.cmake
#
# Files are counted in physical lines (as wc -l). Functions are measured by
# tools/function_sizes.sh (clang-tidy readability-function-size) and keyed "<path>::<name>"; a
# second oversized function with the same name in the same file gets "#2", and so on.

cmake_minimum_required(VERSION 3.20)

foreach(var ROOT MODE)
    if(NOT DEFINED ${var})
        message(FATAL_ERROR "size_ratchet.cmake: -D${var}=... is required")
    endif()
endforeach()
if(NOT DEFINED RATCHET)
    set(RATCHET "${ROOT}/tools/size_ratchet.json")
endif()
file(READ "${RATCHET}" json)

set(measured_keys "")
set(measured_counts "")
if(MODE STREQUAL "files")
    string(JSON limit GET "${json}" max_file_lines)
    set(section files)
    file(GLOB_RECURSE sources RELATIVE "${ROOT}" "${ROOT}/src/*" "${ROOT}/tests/*")
    list(FILTER sources INCLUDE REGEX "\\.(c|h|cpp|hpp)$")
    foreach(path IN LISTS sources)
        file(READ "${ROOT}/${path}" text)
        string(REGEX MATCHALL "\n" newlines "${text}")
        list(LENGTH newlines lines)
        if(lines GREATER limit)
            list(APPEND measured_keys "${path}")
            list(APPEND measured_counts ${lines})
        endif()
    endforeach()
elseif(MODE STREQUAL "functions")
    if(NOT DEFINED BUILD_DIR)
        message(FATAL_ERROR "size_ratchet.cmake: MODE=functions needs -DBUILD_DIR=...")
    endif()
    if(NOT DEFINED CLANG_TIDY)
        set(CLANG_TIDY clang-tidy)
    endif()
    string(JSON limit GET "${json}" max_function_lines)
    set(section functions)
    execute_process(
        COMMAND bash "${ROOT}/tools/function_sizes.sh" "${BUILD_DIR}" "${limit}" "${CLANG_TIDY}"
        OUTPUT_VARIABLE report
        RESULT_VARIABLE result)
    if(NOT result EQUAL 0)
        message(FATAL_ERROR "tools/function_sizes.sh failed (${result})")
    endif()
    string(REPLACE "\n" ";" rows "${report}")
    foreach(row IN LISTS rows)
        if(row STREQUAL "")
            continue()
        endif()
        string(REPLACE "\t" ";" fields "${row}")
        list(GET fields 0 path)
        list(GET fields 1 name)
        list(GET fields 2 lines)
        set(key "${path}::${name}")
        set(base "${key}")
        set(n 1)
        list(FIND measured_keys "${key}" taken)
        while(taken GREATER -1)
            math(EXPR n "${n} + 1")
            set(key "${base}#${n}")
            list(FIND measured_keys "${key}" taken)
        endwhile()
        list(APPEND measured_keys "${key}")
        list(APPEND measured_counts ${lines})
    endforeach()
else()
    message(FATAL_ERROR "size_ratchet.cmake: MODE must be files or functions, not '${MODE}'")
endif()

set(problems "")
foreach(key lines IN ZIP_LISTS measured_keys measured_counts)
    string(JSON allowed ERROR_VARIABLE missing GET "${json}" ${section} "${key}")
    if(missing)
        string(APPEND problems "  new: ${key} has ${lines} lines (limit ${limit}); split it\n")
    elseif(lines GREATER allowed)
        string(APPEND problems "  grew: ${key} has ${lines} lines, allowed ${allowed}\n")
    elseif(lines LESS allowed)
        string(APPEND problems
            "  shrank: ${key} has ${lines} lines; lower its entry from ${allowed} to ${lines}\n")
    endif()
endforeach()

string(JSON entries LENGTH "${json}" ${section})
if(entries GREATER 0)
    math(EXPR last "${entries} - 1")
    foreach(i RANGE ${last})
        string(JSON key MEMBER "${json}" ${section} ${i})
        list(FIND measured_keys "${key}" found)
        if(found EQUAL -1)
            string(APPEND problems
                "  gone: ${key} is no longer over ${limit} lines; remove its entry\n")
        endif()
    endforeach()
endif()

list(LENGTH measured_keys over)
if(problems)
    message(FATAL_ERROR
        "Size ratchet (${section}) failed; update tools/size_ratchet.json only to lower or "
        "remove entries:\n${problems}")
endif()
message(STATUS "Size ratchet (${section}): ${over} known excesses, none grew")
