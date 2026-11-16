# thebigs2 - ReXGlue Recompiled Project
#
# The SDK package provides rexglue_project and the rexglue_setup_* functions.

cmake_minimum_required(VERSION 3.25)
project(thebigs2 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(REXSDK_DIR)
    add_subdirectory("${REXSDK_DIR}" rexglue-sdk)
else()
    find_package(rexglue 0.11.0 REQUIRED CONFIG)
endif()

# Cross builds (Windows from Linux, packaging/windows/) can't run the target's
# rexglue.exe for codegen; use the host SDK's. The generated code is the same.
if(CMAKE_CROSSCOMPILING)
    find_program(REXGLUE_HOST_TOOL rexglue
        HINTS "$ENV{HOME}/rexsdk-install-next/bin" "$ENV{HOME}/rexsdk-install/bin"
        NO_CMAKE_FIND_ROOT_PATH)
    if(NOT REXGLUE_HOST_TOOL)
        message(FATAL_ERROR "Cross build: set REXGLUE_HOST_TOOL to a host rexglue binary")
    endif()
    set_target_properties(rex::rexglue PROPERTIES IMPORTED_LOCATION_RELEASE "${REXGLUE_HOST_TOOL}")
endif()

rexglue_project(thebigs2 OUTPUT_DIR generated/default)

# Game sources, an OBJECT library (never STATIC: an archive drops the hooks,
# which replace weak symbols of the generated code). src/main.cpp stays on the
# executable.
set(THEBIGS2_SOURCES
    src/app_services.cpp
    src/game_settings.cpp
    src/hooks.cpp
    src/renderdoc_capture.cpp
    src/settings.cpp
    src/main_menu_entry.cpp
    src/online.cpp
    src/online_menu.cpp
    src/settings_menu.cpp
    src/ui_theme.cpp
    src/imgui_gamepad.cpp
    src/frame_stats.cpp
    src/first_launch.cpp
    src/render_tweaks.cpp
)

add_library(thebigs2_common OBJECT ${THEBIGS2_SOURCES})
rexglue_setup_host_library(thebigs2_common)

if(WIN32)
    add_executable(thebigs2 WIN32 src/main.cpp packaging/windows/thebigs2.rc)
    target_link_libraries(thebigs2_common PUBLIC ws2_32)  # online.cpp: addresses, own-IP check
else()
    add_executable(thebigs2 src/main.cpp)
endif()

# The game calls SDL directly (window mode, display changes); the SDK package
# brings SDL3, which the UI driver module uses too.
if(TARGET SDL3::SDL3)
    target_link_libraries(thebigs2_common PUBLIC SDL3::SDL3)
elseif(DEFINED REXSDK_DIR AND TARGET SDL3-shared)
    target_link_libraries(thebigs2_common PUBLIC SDL3-shared)
else()
    message(FATAL_ERROR "SDL3 not found (it ships with the ReXGlue SDK)")
endif()

# A user's disc image is accepted only if its default.xex is the one this
# build was recompiled from.
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/game/default.xex")
    file(MD5 "${CMAKE_CURRENT_SOURCE_DIR}/game/default.xex" TB2_XEX_MD5)
    target_compile_definitions(thebigs2_common PRIVATE TB2_XEX_MD5="${TB2_XEX_MD5}")
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
                 "${CMAKE_CURRENT_SOURCE_DIR}/game/default.xex")
endif()

# Render call tracing (debug build option).
# Wraps every function in the XDK D3D library and the engine's renderlib with a
# call counter; F8 records a frame. Off by default (adds ~2,000 wrappers).
option(TB2_RENDER_TRACE "Build with render call tracing (F8)" OFF)
if(TB2_RENDER_TRACE)
    find_package(Python3 REQUIRED COMPONENTS Interpreter)
    set(_trace_cpp "${CMAKE_CURRENT_BINARY_DIR}/render_trace_hooks.cpp")
    set(_codegen_stamp "${CMAKE_CURRENT_SOURCE_DIR}/generated/default/codegen.build.stamp")
    add_custom_command(
        OUTPUT "${_trace_cpp}"
        COMMAND Python3::Interpreter "${CMAKE_CURRENT_SOURCE_DIR}/tools/gen_render_trace.py"
                "${CMAKE_CURRENT_SOURCE_DIR}/generated/default" "${_trace_cpp}"
                d3d:823A6400-823DE000 renderlib:82460000-8249B800
        DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/tools/gen_render_trace.py" "${_codegen_stamp}"
        COMMENT "Generating render trace wrappers"
        VERBATIM)
    target_sources(thebigs2_common PRIVATE src/render_trace.cpp "${_trace_cpp}")
    target_compile_definitions(thebigs2_common PUBLIC TB2_RENDER_TRACE=1)
endif()

rexglue_setup_target(thebigs2)
target_link_libraries(thebigs2 PRIVATE thebigs2_common)

# Online play is lockstep: both machines must compute bit-identical floats, so
# never fuse a*b+c into one FMA (clang does by default where the target has
# FMA, e.g. arm64); explicit fma calls stay as they are.
foreach(_t thebigs2 thebigs2_common thebigs2_recompiled)
    if(TARGET ${_t})
        target_compile_options(${_t} PRIVATE $<$<CXX_COMPILER_ID:Clang,AppleClang,GNU>:-ffp-contract=off>)
    endif()
endforeach()

# Keep the executable loadable on glibc older than the build machine's, e.g.
# SteamOS (see src/glibc_compat.h), including the recompiled sources.
if(CMAKE_SYSTEM_NAME STREQUAL "Linux" AND CMAKE_SYSTEM_PROCESSOR MATCHES "x86_64|AMD64")
    foreach(_t thebigs2 thebigs2_common thebigs2_recompiled)
        if(TARGET ${_t})
            target_compile_options(${_t} PRIVATE
                "SHELL:-include ${CMAKE_CURRENT_SOURCE_DIR}/src/glibc_compat.h")
        endif()
    endforeach()
endif()
