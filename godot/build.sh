#!/usr/bin/env bash
# SPDX-License-Identifier: LGPL-2.1-only WITH AdditionRef-Terni-linking-exception
# Copyright (C) 2026 Rafał Zając
#
# Builds Terni and the GDExtension into addons/terni/bin. Needs CMake, Ninja, Python 3,
# Git, a C++20 compiler, the Vulkan headers and shaderc.
#   ./build.sh                                   the Terni commit the plugins are pinned to
#   TERNI_DIR=/path/to/Terni ./build.sh          a local Terni checkout instead
set -euo pipefail
cd "$(dirname "$0")"
BUILD="$(realpath -m ../build)"
cmake -S ../native -B "$BUILD/native" -G Ninja -DTERNI_DIR="${TERNI_DIR:-}" > /dev/null
cmake --build "$BUILD/native"
cmake --install "$BUILD/native" --prefix "$BUILD/terni" > /dev/null
cmake -S . -B "$BUILD/godot" -G Ninja -DCMAKE_BUILD_TYPE=Release -DTERNI_PREFIX="$BUILD/terni" > /dev/null
cmake --build "$BUILD/godot"

# The example project gets a copy of the addon (Godot needs it inside the project).
rm -rf example/addons/terni && mkdir -p example/addons && cp -r addons/terni example/addons/
echo "built $(pwd)/addons/terni, copied into example/addons"
