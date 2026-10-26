cmake_minimum_required(VERSION 3.20)
project(GBEngine VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(GB_BUILD_SANDBOX "Build the Sandbox application" ON)
option(GB_BUILD_TESTS "Build the tests" ON)
option(GB_WARNINGS_AS_ERRORS "Treat warnings as errors" OFF)

include(FetchContent)
FetchContent_Declare(spdlog
    GIT_REPOSITORY https://github.com/gabime/spdlog.git
    GIT_TAG v1.14.1
    GIT_SHALLOW TRUE)
FetchContent_MakeAvailable(spdlog)

add_subdirectory(Engine)

if(GB_BUILD_SANDBOX)
    add_subdirectory(Sandbox)
endif()

if(GB_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
