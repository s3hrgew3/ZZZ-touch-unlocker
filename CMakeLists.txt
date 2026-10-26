# =============================================================================
# zzz-touch-unlocker — runtime-only native touch + mobile UI restore
#
# Native (MSVC / MinGW-w64) and Wine (MinGW-w64 cross build) targets share the
# same sources. Wine support only changes how the hook reaches the game:
#   native  zzz-touch-unlocker.exe  create suspended -> inject -> resume
#   wine    wine/zzz-touch-unlocker game first       -> zzz-touch-inject.exe
# =============================================================================

cmake_minimum_required(VERSION 3.20)
project(zzz_touch_unlocker CXX C)

if(MSVC)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
endif()

if(MSVC)
    add_compile_options(/W4 /permissive- /Zc:__cplusplus /utf-8)
    add_compile_definitions(_CRT_SECURE_NO_WARNINGS NOMINMAX WIN32_LEAN_AND_MEAN)
else()
    add_compile_options(-Wall -Wextra -Wno-unused-parameter -Wno-unused-variable
                        -Wno-unused-but-set-variable -Wno-cast-function-type
                        -fno-strict-aliasing -Wno-pedantic)
    add_compile_definitions(_CRT_SECURE_NO_WARNINGS NOMINMAX WIN32_LEAN_AND_MEAN
                            UNICODE _UNICODE)
endif()

# MinGW: link the C++ runtime statically so the DLL and the tools can be copied
# into a Wine prefix without shipping libstdc++-6.dll / libgcc_s_seh-1.dll.
if(MINGW)
    add_link_options(-static -static-libgcc -static-libstdc++)
endif()

# -----------------------------------------------------------------------------
# Hook DLL
# -----------------------------------------------------------------------------
add_library(zzz_touch_hook SHARED
    hook/dllmain.cpp
    hook/touch_hook.cpp
    hook/legacy_input_switch.cpp
    hook/layout_platform_override.cpp
    hook/logger.cpp
    hook/patterns.cpp
)
set_target_properties(zzz_touch_hook PROPERTIES
    PREFIX ""
    OUTPUT_NAME "zzz-touch-hook"
)
target_include_directories(zzz_touch_hook PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/hook)
target_link_libraries(zzz_touch_hook PRIVATE kernel32)

# -----------------------------------------------------------------------------
# Injection primitives shared by the launcher and the attach-mode injector
# -----------------------------------------------------------------------------
add_library(zzz_inject STATIC launcher/inject.cpp)
target_include_directories(zzz_inject PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/launcher)
target_link_libraries(zzz_inject PUBLIC kernel32)

# -----------------------------------------------------------------------------
# Windows launcher (native only, refuses to run under Wine)
# -----------------------------------------------------------------------------
add_executable(zzz_touch_unlocker launcher/main.cpp)
set_target_properties(zzz_touch_unlocker PROPERTIES OUTPUT_NAME "zzz-touch-unlocker")
target_link_libraries(zzz_touch_unlocker PRIVATE zzz_inject)
if(NOT MSVC)
    target_link_options(zzz_touch_unlocker PRIVATE -municode)
endif()

# -----------------------------------------------------------------------------
# Attach-mode injector (Wine path, also useful on Windows for a running game)
# -----------------------------------------------------------------------------
add_executable(zzz_touch_inject launcher/inject_main.cpp)
set_target_properties(zzz_touch_inject PROPERTIES OUTPUT_NAME "zzz-touch-inject")
target_link_libraries(zzz_touch_inject PRIVATE zzz_inject)
if(NOT MSVC)
    target_link_options(zzz_touch_inject PRIVATE -municode)
endif()

# -----------------------------------------------------------------------------
# Regression tests (native build only; the argument parser is platform neutral)
# -----------------------------------------------------------------------------
option(ZZZ_BUILD_TESTS "Build standalone regression tests" ON)
if(ZZZ_BUILD_TESTS AND NOT CMAKE_CROSSCOMPILING)
    enable_testing()
    add_executable(launcher_args_test tests/launcher_args_test.cpp)
    target_link_libraries(launcher_args_test PRIVATE zzz_inject)
    if(NOT MSVC)
        target_link_options(launcher_args_test PRIVATE -municode)
    endif()
    add_test(NAME launcher_args COMMAND launcher_args_test)
endif()
