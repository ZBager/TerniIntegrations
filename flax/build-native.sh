#!/usr/bin/env bash
# SPDX-License-Identifier: LGPL-2.1-only WITH AdditionRef-Terni-linking-exception
# Copyright (C) 2026 Rafał Zając
#
# Builds Terni and puts it into ThirdParty/Terni, where the plugin's build script looks
# for it. Run once before opening a project that uses the plugin, and again after
# updating the plugin. Needs CMake, Ninja, a C++20 compiler, the Vulkan headers and shaderc.
#   ./build-native.sh                    the Terni commit the plugin is pinned to
#   TERNI_DIR=/path/to/Terni ./build-native.sh    a local checkout instead
set -euo pipefail
cd "$(dirname "$0")"
cmake -S ../native -B ../build/native -G Ninja -DTERNI_DIR="${TERNI_DIR:-}"
cmake --build ../build/native
cmake --install ../build/native --prefix ThirdParty/Terni
