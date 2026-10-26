cmake_minimum_required(VERSION 3.20)
project(cheevo-workbench VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Debug CACHE STRING "Build type" FORCE)
endif()

option(CW_BUILD_TESTS "Build the unit tests" ON)

include(GNUInstallDirs)

# Warnings for our own targets only; third-party targets do not link this.
add_library(cw_warnings INTERFACE)
target_compile_options(cw_warnings INTERFACE -Wall -Wextra -Wpedantic -Wshadow)

include(cmake/ThirdParty.cmake)

add_subdirectory(src/devtools)
add_subdirectory(src/frontend)

if(CW_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
