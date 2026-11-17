# SPDX-License-Identifier: LGPL-2.1-only WITH AdditionRef-Terni-linking-exception-1.0
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

set(TERNI_DIR "" CACHE PATH "Local Terni checkout to build instead of the pinned GitHub commit")
set(TERNI_COMMIT bc1144b16d6aa3a1dd179ddf1d6a3d43dd10fa84)
set(FETCHCONTENT_SOURCE_DIR_TERNI "${TERNI_DIR}")  # empty: fetch the pinned commit
include(FetchContent)
FetchContent_Declare(terni GIT_REPOSITORY https://github.com/ZBager/Terni.git GIT_TAG ${TERNI_COMMIT})
FetchContent_MakeAvailable(terni)

# Terni behind a C++14 interface without exceptions, for engine modules (see the header).
add_library(terni_bridge STATIC bridge/terni_bridge.cpp)
target_link_libraries(terni_bridge PRIVATE Terni::runtime)
target_compile_features(terni_bridge PRIVATE cxx_std_20)

# Checks a game folder and fills its SPIR-V cache, without a GPU. Engine editors run it
# as a separate process: they link Terni::runtime, which has no rule compiler, because
# an engine's own shader compiler can clash with the one Terni uses.
add_executable(terni_compile_rules compile_rules.cpp)
target_link_libraries(terni_compile_rules PRIVATE Terni::core)

include(GNUInstallDirs)
install(TARGETS terni_compile_rules RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
install(TARGETS terni_bridge ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR})
install(FILES bridge/terni_bridge.h DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
