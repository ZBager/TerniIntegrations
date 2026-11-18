#!/usr/bin/env bash
# SPDX-License-Identifier: LGPL-2.1-only WITH AdditionRef-Terni-linking-exception
# Copyright (C) 2026 Rafał Zając
#
# Builds Terni and puts it into ThirdParty/Terni, where the plugin's build script looks
# for it. Run once before opening a project that uses the plugin, and again after
# updating the plugin. Needs CMake, Ninja, a C++20 compiler, the Vulkan headers and shaderc.
#   ./build-native.sh                    the Terni commit the plugin is pinned to
#   TERNI_DIR=/path/to/Terni ./build-native.sh    a local checkout instead
# The software Vulkan driver that ../native/lavapipe/build.sh builds ships with the plugin
# once it is built; TERNI_SOFTWARE_DRIVER names another (a libvulkan_lvp.so), or none.
set -euo pipefail
cd "$(dirname "$0")"
DRIVER="${TERNI_SOFTWARE_DRIVER:-$(realpath -m ../build/lavapipe/libvulkan_lvp.so)}"
if [[ "$DRIVER" == none || ! -f "$DRIVER" ]]; then DRIVER=""; fi
rm -f ThirdParty/Terni/bin/libvulkan_lvp.so   # none left from an earlier build
cmake -S ../native -B ../build/native -G Ninja -DTERNI_DIR="${TERNI_DIR:-}" -DTERNI_SOFTWARE_DRIVER="$DRIVER"
cmake --build ../build/native
cmake --install ../build/native --prefix ThirdParty/Terni
