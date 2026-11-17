#!/usr/bin/env bash
# SPDX-License-Identifier: LGPL-2.1-only WITH AdditionRef-Terni-linking-exception
# Copyright (C) 2026 Rafał Zając
#
# Runs the example's self-test: the galaxy example on every Vulkan device, checked
# against the state hash Terni gives on its own. Build first (build.sh).
#   ./test.sh              headless (the renderer's GPU is not checked: there is none)
#   ./test.sh --window     in a window, so the renderer's GPU is checked too; extra
#                          arguments go to Godot, e.g. ./test.sh --window --gpu-index 0
set -euo pipefail
cd "$(dirname "$0")/example"
GODOT="${GODOT:-godot}"
RESULT="$(mktemp)"
if [[ ! -d .godot ]]; then
    # The first import of a project registers the extension; Godot 4.7 may crash as it
    # quits after that, with the import done. A second run checks it.
    "$GODOT" --headless --path . --import > /dev/null 2>&1 || true
    "$GODOT" --headless --path . --import > /dev/null 2>&1
fi
MODE=(--headless)
if [[ "${1:-}" == "--window" ]]; then MODE=("${@:2}"); fi
TERNI_EXAMPLE_TEST="$RESULT" timeout -s KILL "${TIMEOUT:-300}" "$GODOT" --path . "${MODE[@]}" > /dev/null 2>&1 || true
cat "$RESULT"
grep -q '^PASSED$' "$RESULT"
