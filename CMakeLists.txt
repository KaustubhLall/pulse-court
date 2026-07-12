cmake_minimum_required(VERSION 3.24)

project(PulseCourt VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(PULSE_BUILD_VIEWER "Build the native Windows visualizer" ON)

add_library(pulse_core
    src/core/fixed_math.cpp
    src/core/pulse_sim.cpp
    src/core/pulse_replay.cpp
)

target_include_directories(pulse_core PUBLIC src/core)

if(MSVC)
    target_compile_options(pulse_core PRIVATE /W4 /permissive- /EHsc /Zc:__cplusplus)
else()
    target_compile_options(pulse_core PRIVATE -Wall -Wextra -Wpedantic -Wconversion)
endif()

add_executable(pulse_headless src/apps/headless_main.cpp)
target_link_libraries(pulse_headless PRIVATE pulse_core)

if(WIN32 AND PULSE_BUILD_VIEWER)
    add_executable(pulse_viewer WIN32 src/apps/viewer_win32.cpp)
    target_link_libraries(pulse_viewer PRIVATE pulse_core user32 gdi32)
    if(MSVC)
        target_compile_options(pulse_viewer PRIVATE /W4 /permissive- /EHsc /Zc:__cplusplus)
    endif()
endif()

include(CTest)
if(BUILD_TESTING)
    add_executable(pulse_tests tests/sim_tests.cpp)
    target_link_libraries(pulse_tests PRIVATE pulse_core)
    if(MSVC)
        target_compile_options(pulse_tests PRIVATE /W4 /permissive- /EHsc /Zc:__cplusplus)
    endif()
    add_test(NAME pulse_tests COMMAND pulse_tests)
endif()
