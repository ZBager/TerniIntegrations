#!/usr/bin/env bash
# SPDX-License-Identifier: LGPL-2.1-only WITH AdditionRef-Terni-linking-exception-1.0
# Copyright (C) 2026 Rafał Zając
#
# Runs the example's self-test: the galaxy example on every Vulkan device, checked
# against the state hash Terni gives on its own.
#   FLAX_EDITOR=/path/to/FlaxEditor ./test.sh            in a headless editor
#   FLAX_EDITOR=/path/to/FlaxEditor ./test.sh --cooked   cooks the example for Linux and
#                                                        runs the test in the cooked game
#                                                        (the editor needs the Linux
#                                                        platform files)
set -euo pipefail
cd "$(dirname "$0")/Example"
: "${FLAX_EDITOR:?Set FLAX_EDITOR to the FlaxEditor program}"
RESULT="$(mktemp)"
if [[ "${1:-}" == "--cooked" ]]; then
    rm -rf Output/Linux Galaxy/spirv   # the cook must compile the rules itself
    COOK=$(timeout -s KILL "${TIMEOUT:-1200}" "$FLAX_EDITOR" -std -headless -mute -null "-project=$PWD" -build Release.Linux 2>&1 || true)
    grep -E "\[Terni\]|Game building done" <<< "$COOK" || true
    grep -q "Game building done" <<< "$COOK" || { echo "cooking failed, see Example/Logs"; exit 1; }
    GAME=$(find Output/Linux -maxdepth 1 -type f -executable ! -name '*.so*' ! -name terni_compile_rules | head -1)
    # A rule changed after cooking, as a mod would: the cooked game must compile it itself.
    echo "// changed after cooking" >> Output/Linux/Galaxy/rules/planet_growth.glsl
    BEFORE=$(ls Output/Linux/Galaxy/spirv | wc -l)
    TERNI_EXAMPLE_TEST="$RESULT" timeout -s KILL 300 "$GAME" -headless -mute > /dev/null 2>&1 || true
    [[ $(ls Output/Linux/Galaxy/spirv | wc -l) -gt $BEFORE ]] || { echo "the cooked game did not compile the changed rule"; exit 1; }
else
    SCENE=$(grep -m1 '"ID"' Content/Main.scene | sed -E 's/.*"([0-9a-f]{32})".*/\1/')
    TERNI_EXAMPLE_TEST="$RESULT" timeout -s KILL "${TIMEOUT:-300}" "$FLAX_EDITOR" -std -headless -mute "-project=$PWD" -play "$SCENE" > /dev/null 2>&1 || true
fi
cat "$RESULT"
grep -q '^PASSED$' "$RESULT"
