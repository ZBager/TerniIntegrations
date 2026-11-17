# SPDX-License-Identifier: LGPL-2.1-only WITH AdditionRef-Terni-linking-exception
# Copyright (C) 2026 Rafał Zając
#
# Builds what the engine plugins link and ship: Terni's static libraries (position
# independent, so they can go into an engine module), terni_bridge and terni_compile_rules.
#
#   cmake -S native -B build/native -G Ninja
#   cmake --build build/native
#   cmake --install build/native --prefix flax/ThirdParty/Terni
#
# Terni comes from GitHub, pinned to one commit. -DTERNI_DIR=/path/to/Terni builds a
# local checkout instead.
cmake_minimum_required(VERSION 3.21)
project(TerniIntegrationsNative LANGUAGES CXX)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
endif()
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
set(TERNI_BUILD_TOOLS OFF CACHE BOOL "" FORCE)
set(TERNI_BUILD_TESTS OFF CACHE BOOL "" FORCE)

# terni_compile_rules ships with games, so that players can change rules, and must not
# need shaderc on their machines. With a static shaderc that has glslang and SPIRV-Tools
# inside (shaderc_combined, as in the Vulkan SDK) it is linked in. Some systems have a
# shaderc_combined without them; then the shared libraries are installed beside the
# program instead (see the install rules below).
find_package(Vulkan REQUIRED)
find_package(Threads REQUIRED)
get_filename_component(_vulkan_libdir "${Vulkan_LIBRARY}" DIRECTORY)
find_library(TERNI_SHADERC_COMBINED NAMES shaderc_combined HINTS ${_vulkan_libdir} "$ENV{VULKAN_SDK}/Lib")
find_path(TERNI_SHADERC_INCLUDE shaderc/shaderc.hpp HINTS ${Vulkan_INCLUDE_DIRS} "$ENV{VULKAN_SDK}/Include")
set(TERNI_SHADERC_STATIC OFF)
if(TERNI_SHADERC_COMBINED AND TERNI_SHADERC_INCLUDE)
  include(CheckCXXSourceCompiles)
  set(CMAKE_TRY_COMPILE_CONFIGURATION Release)  # MSVC: the SDK's library is built for the release runtime
  set(CMAKE_REQUIRED_INCLUDES ${TERNI_SHADERC_INCLUDE})
  set(CMAKE_REQUIRED_LIBRARIES ${TERNI_SHADERC_COMBINED} Threads::Threads)
  check_cxx_source_compiles("#include <shaderc/shaderc.hpp>
int main() { shaderc::Compiler c; return c.IsValid() ? 0 : 1; }" TERNI_SHADERC_COMBINED_LINKS)
  unset(CMAKE_REQUIRED_INCLUDES)
  unset(CMAKE_REQUIRED_LIBRARIES)
  if(TERNI_SHADERC_COMBINED_LINKS)
    set(TERNI_SHADERC_STATIC ON)
    set(SHADERC_LIB "${TERNI_SHADERC_COMBINED}" CACHE FILEPATH "" FORCE)  # read by Terni's own CMakeLists
  endif()
endif()
message(STATUS "terni_compile_rules: shaderc ${TERNI_SHADERC_STATIC}" " (ON: linked in, OFF: shared libraries installed beside it)")

set(TERNI_DIR "" CACHE PATH "Local Terni checkout to build instead of the pinned GitHub commit")
set(TERNI_COMMIT 10cf94d9e05dff9e02c29bd48bcc5cb18e095758)
set(FETCHCONTENT_SOURCE_DIR_TERNI "${TERNI_DIR}")  # empty: fetch the pinned commit
include(FetchContent)
FetchContent_Declare(terni GIT_REPOSITORY https://github.com/ZBager/Terni.git GIT_TAG ${TERNI_COMMIT})
FetchContent_MakeAvailable(terni)

# Terni behind a C++14 interface without exceptions, for engine modules (see the header).
add_library(terni_bridge STATIC bridge/terni_bridge.cpp bridge/renderer_gpu.cpp)
target_link_libraries(terni_bridge PRIVATE Terni::runtime)
target_compile_features(terni_bridge PRIVATE cxx_std_20)
if(WIN32)
  target_link_libraries(terni_bridge PUBLIC dxgi)  # the adapter LUID of a DirectX renderer
endif()

# Checks a game folder and fills its SPIR-V cache, without a GPU. Engine editors run it
# as a separate process: they link Terni::runtime, which has no rule compiler, because
# an engine's own shader compiler can clash with the one Terni uses.
add_executable(terni_compile_rules compile_rules.cpp)
target_link_libraries(terni_compile_rules PRIVATE Terni::core)
if(NOT MSVC)
  # Fewer things to find on a player's machine. An old-style RPATH, because unlike
  # RUNPATH it also serves the libraries that shaderc itself loads.
  target_link_options(terni_compile_rules PRIVATE -static-libstdc++ -static-libgcc)
  if(NOT WIN32)
    target_link_options(terni_compile_rules PRIVATE -Wl,--disable-new-dtags)
    set_target_properties(terni_compile_rules PROPERTIES INSTALL_RPATH "$ORIGIN")
  endif()
endif()

include(GNUInstallDirs)
if(TERNI_SHADERC_STATIC)
  install(TARGETS terni_compile_rules RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
else()
  # shaderc and what it loads, beside the program.
  install(TARGETS terni_compile_rules RUNTIME_DEPENDENCY_SET terni_compiler_libraries
          RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
  install(RUNTIME_DEPENDENCY_SET terni_compiler_libraries
          PRE_INCLUDE_REGEXES "shaderc" "glslang" "SPIRV" PRE_EXCLUDE_REGEXES ".*"
          DIRECTORIES ${_vulkan_libdir} "$ENV{VULKAN_SDK}/Bin"
          RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR} LIBRARY DESTINATION ${CMAKE_INSTALL_BINDIR})
endif()
install(TARGETS terni_bridge ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR})
# Terni's notice and the licences of shaderc, glslang, SPIRV-Tools and SPIRV-Headers,
# beside the programs: plugins ship everything in bin/ with the game.
install(FILES notices/Terni-NOTICES.txt DESTINATION ${CMAKE_INSTALL_BINDIR})
install(FILES bridge/terni_bridge.h DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
