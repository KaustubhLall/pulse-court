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

add_library(pulse_ai
    src/ai/pulse_ai.cpp
)
target_include_directories(pulse_ai PUBLIC src/ai)
target_link_libraries(pulse_ai PRIVATE pulse_core)

add_library(pulse_policy
    src/ai/pulse_policy.cpp
    src/ai/pulse_policy_adapter.cpp
)
target_include_directories(pulse_policy PUBLIC src/ai)
target_link_libraries(pulse_policy PRIVATE pulse_ai pulse_core)

# TEST/DEV ONLY -- NON-CERTIFIED synthetic bundle generator, kept as its own
# library so it is not linked into production/certified consumers of
# pulse_policy by accident.
add_library(pulse_policy_devgen
    src/ai/pulse_policy_devgen.cpp
)
target_include_directories(pulse_policy_devgen PUBLIC src/ai)

add_library(pulse_challenge
    src/ai/challenge_session.cpp
    src/ai/challenge_cli.cpp
    src/ai/scripted_policy.cpp
    src/ai/challenge_smoke.cpp
    src/ai/challenge_protocol.cpp
)

target_include_directories(pulse_challenge PUBLIC src/ai)
# T4: challenge_smoke.cpp uses pulse::Policy / pulse::ai::PolicyController
# (non-certified preview mode) in addition to the scripted-bot path.
target_link_libraries(pulse_challenge PUBLIC pulse_core pulse_policy pulse_ai)

if(MSVC)
    target_compile_options(pulse_core PRIVATE /W4 /permissive- /EHsc /Zc:__cplusplus)
    target_compile_options(pulse_ai PRIVATE /W4 /permissive- /EHsc /Zc:__cplusplus)
    target_compile_options(pulse_policy PRIVATE /W4 /permissive- /EHsc /Zc:__cplusplus)
    target_compile_options(pulse_policy_devgen PRIVATE /W4 /permissive- /EHsc /Zc:__cplusplus)
    target_compile_options(pulse_challenge PRIVATE /W4 /permissive- /EHsc /Zc:__cplusplus)
else()
    target_compile_options(pulse_core PRIVATE -Wall -Wextra -Wpedantic -Wconversion)
endif()

add_executable(pulse_headless src/apps/headless_main.cpp)
target_link_libraries(pulse_headless PRIVATE pulse_core pulse_challenge pulse_policy pulse_ai)

add_executable(pulse_policy_verify src/apps/policy_verify_main.cpp)
target_link_libraries(pulse_policy_verify PRIVATE pulse_policy pulse_ai pulse_core)
if(MSVC)
    target_compile_options(pulse_policy_verify PRIVATE /W4 /permissive- /EHsc /Zc:__cplusplus)
endif()

# TEST/DEV ONLY -- NON-CERTIFIED synthetic bundle/golden generator CLI.
add_executable(pulse_dev_bundle src/apps/dev_bundle_main.cpp)
target_link_libraries(pulse_dev_bundle PRIVATE pulse_policy pulse_policy_devgen pulse_ai pulse_core)
if(MSVC)
    target_compile_options(pulse_dev_bundle PRIVATE /W4 /permissive- /EHsc /Zc:__cplusplus)
endif()

if(WIN32 AND PULSE_BUILD_VIEWER)
    add_executable(pulse_viewer WIN32
        src/apps/viewer_win32.cpp
        src/apps/viewer_assets.cpp
        src/apps/viewer_monitor.cpp
        src/apps/viewer_d2d.cpp
    )
    target_link_libraries(pulse_viewer PRIVATE pulse_core pulse_challenge user32 gdi32 gdiplus d2d1 dwrite windowscodecs ole32)
    if(MSVC)
        target_compile_options(pulse_viewer PRIVATE /W4 /permissive- /EHsc /Zc:__cplusplus)
    endif()

    add_custom_command(TARGET pulse_viewer POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            ${CMAKE_SOURCE_DIR}/assets
            $<TARGET_FILE_DIR:pulse_viewer>/assets
        COMMENT "Copying assets next to viewer executable"
        VERBATIM
    )
endif()

include(CTest)
if(BUILD_TESTING)
    add_executable(pulse_tests tests/sim_tests.cpp)
    target_link_libraries(pulse_tests PRIVATE pulse_core)
    if(MSVC)
        target_compile_options(pulse_tests PRIVATE /W4 /permissive- /EHsc /Zc:__cplusplus)
    endif()
    add_test(NAME pulse_tests COMMAND pulse_tests)

    add_executable(pulse_challenge_tests tests/challenge_tests.cpp)
    target_link_libraries(pulse_challenge_tests PRIVATE pulse_core pulse_challenge pulse_policy pulse_policy_devgen pulse_ai)
    if(MSVC)
        target_compile_options(pulse_challenge_tests PRIVATE /W4 /permissive- /EHsc /Zc:__cplusplus)
    endif()
    add_test(NAME pulse_challenge_tests COMMAND pulse_challenge_tests)

    add_executable(pulse_policy_tests tests/policy_tests.cpp)
    target_link_libraries(pulse_policy_tests PRIVATE pulse_policy pulse_policy_devgen pulse_ai pulse_core)
    if(MSVC)
        target_compile_options(pulse_policy_tests PRIVATE /W4 /permissive- /EHsc /Zc:__cplusplus)
    endif()
    add_test(NAME pulse_policy_tests COMMAND pulse_policy_tests)

    add_executable(pulse_challenge_protocol_tests tests/challenge_protocol_tests.cpp)
    target_link_libraries(pulse_challenge_protocol_tests PRIVATE pulse_core pulse_challenge)
    if(MSVC)
        target_compile_options(pulse_challenge_protocol_tests PRIVATE /W4 /permissive- /EHsc /Zc:__cplusplus)
    endif()
    add_test(NAME pulse_challenge_protocol_tests COMMAND pulse_challenge_protocol_tests)
endif()
