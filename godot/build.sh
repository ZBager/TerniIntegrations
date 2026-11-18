#!/usr/bin/env bash
# SPDX-License-Identifier: LGPL-2.1-only WITH AdditionRef-Terni-linking-exception
# Copyright (C) 2026 Rafał Zając
#
# Builds Terni and the GDExtension into addons/terni/bin. Needs CMake, Ninja, Python 3,
# Git, a C++20 compiler, the Vulkan headers and shaderc.
#   ./build.sh                                   the Terni commit the plugins are pinned to
#   TERNI_DIR=/path/to/Terni ./build.sh          a local Terni checkout instead
# The software Vulkan driver that ../native/lavapipe/build.sh builds ships with the addon
# once it is built; TERNI_SOFTWARE_DRIVER names another (a libvulkan_lvp.so), or none.
set -euo pipefail
cd "$(dirname "$0")"
BUILD="$(realpath -m ../build)"
DRIVER="${TERNI_SOFTWARE_DRIVER:-$BUILD/lavapipe/libvulkan_lvp.so}"
if [[ "$DRIVER" == none || ! -f "$DRIVER" ]]; then DRIVER=""; fi
rm -f "$BUILD/terni/bin/libvulkan_lvp.so" addons/terni/bin/libvulkan_lvp.so   # none left from an earlier build
cmake -S ../native -B "$BUILD/native" -G Ninja -DTERNI_DIR="${TERNI_DIR:-}" -DTERNI_SOFTWARE_DRIVER="$DRIVER" > /dev/null
cmake --build "$BUILD/native"
cmake --install "$BUILD/native" --prefix "$BUILD/terni" > /dev/null
cmake -S . -B "$BUILD/godot" -G Ninja -DCMAKE_BUILD_TYPE=Release -DTERNI_PREFIX="$BUILD/terni" > /dev/null
cmake --build "$BUILD/godot"

# The example project gets a copy of the addon (Godot needs it inside the project).
for project in example example-csharp; do
    rm -rf "$project/addons/terni" && mkdir -p "$project/addons" && cp -r addons/terni "$project/addons/"
done
echo "built $(pwd)/addons/terni, copied into the example projects${DRIVER:+, with the software Vulkan driver $DRIVER}"
