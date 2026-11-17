# SPDX-License-Identifier: LGPL-2.1-only WITH AdditionRef-Terni-linking-exception
# Copyright (C) 2026 Rafał Zając
#
# The Terni GDExtension: builds libterni into addons/terni/bin, with what it ships
# beside it (terni_compile_rules, the shaderc libraries it may need, Terni-NOTICES.txt).
# Run build.sh (or build.bat): it builds Terni first, through ../native.
cmake_minimum_required(VERSION 3.21)

set(TERNI_PREFIX "" CACHE PATH "Where ../native was installed (build.sh sets it)")
if(NOT EXISTS "${TERNI_PREFIX}/include/terni_bridge.h")
  message(FATAL_ERROR "Terni is not built: run build.sh (or build.bat) instead of CMake directly.")
endif()

# godot-cpp, pinned. An extension built for 4.5 also loads in later 4.x versions.
set(GODOTCPP_TARGET template_release CACHE STRING "")
include(FetchContent)
FetchContent_Declare(godot-cpp GIT_REPOSITORY https://github.com/godotengine/godot-cpp.git
                     GIT_TAG godot-4.5-stable GIT_SHALLOW TRUE)
FetchContent_MakeAvailable(godot-cpp)
get_target_property(GODOTCPP_SUFFIX godot::cpp GODOTCPP_SUFFIX)

project(TerniGodot LANGUAGES CXX)

find_package(Terni REQUIRED PATHS "${TERNI_PREFIX}" NO_DEFAULT_PATH)

add_library(terni SHARED src/register_types.cpp src/terni_world.cpp src/terni_simulation.cpp)
target_include_directories(terni PRIVATE "${TERNI_PREFIX}/include")
if(MSVC)
  set(_bridge "${TERNI_PREFIX}/lib/terni_bridge.lib")
else()
  set(_bridge "${TERNI_PREFIX}/lib/libterni_bridge.a")
endif()
target_link_libraries(terni PRIVATE godot::cpp "${_bridge}" Terni::runtime)
if(WIN32)
  target_link_libraries(terni PRIVATE dxgi)
endif()
set_target_properties(terni PROPERTIES CXX_STANDARD 17 PREFIX "lib" OUTPUT_NAME "terni${GODOTCPP_SUFFIX}"
                      CXX_VISIBILITY_PRESET hidden VISIBILITY_INLINES_HIDDEN ON)
if(NOT WIN32)
  # Export only the entry point: the static libraries inside must not clash with others.
  target_link_options(terni PRIVATE -Wl,--no-undefined -Wl,--exclude-libs,ALL -static-libstdc++ -static-libgcc)
endif()

# The library and what ships beside it, into the addon.
set(ADDON_BIN "${CMAKE_CURRENT_SOURCE_DIR}/addons/terni/bin")
add_custom_command(TARGET terni POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E make_directory "${ADDON_BIN}"
  COMMAND ${CMAKE_COMMAND} -E copy "$<TARGET_FILE:terni>" "${ADDON_BIN}/"
  COMMAND ${CMAKE_COMMAND} -E copy_directory "${TERNI_PREFIX}/bin" "${ADDON_BIN}")
