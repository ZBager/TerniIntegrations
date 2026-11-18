#!/usr/bin/env bash
# SPDX-License-Identifier: LGPL-2.1-only WITH AdditionRef-Terni-linking-exception
# Copyright (C) 2026 Rafał Zając
#
# Runs the example's self-test: the galaxy example on every Vulkan device, checked
# against the state hash Terni gives on its own.
#   FLAX_EDITOR=/path/to/FlaxEditor ./test.sh            in a headless editor
#   FLAX_EDITOR=/path/to/FlaxEditor ./test.sh --cooked   cooks the example for Linux, puts
#                                                        the example mod beside it and
#                                                        runs the test in the cooked game
#                                                        (the editor needs the Linux
#                                                        platform files)
#   FLAX_EDITOR=/path/to/FlaxEditor ./test.sh --reload   starts a world in the editor and
#                                                        makes it rebuild and reload the
#                                                        scripts: the world must be freed
#                                                        and a new one must run
#   FLAX_EDITOR=/path/to/FlaxEditor ./test.sh --visual   checks, by the editor's own rules,
#                                                        that visual scripts can use what
#                                                        they need of the plugin
set -euo pipefail
cd "$(dirname "$0")/Example"
: "${FLAX_EDITOR:?Set FLAX_EDITOR to the FlaxEditor program}"
RESULT="$(mktemp)"
if [[ "${1:-}" == "--visual" ]]; then
    TERNI_VISUAL_CHECK="$RESULT" timeout -s KILL "${TIMEOUT:-300}" "$FLAX_EDITOR" -std -headless -mute "-project=$PWD" > /dev/null 2>&1 || true
    sed -i '/^usable/d' "$RESULT"   # the full list is long; keep what failed and the checks
elif [[ "${1:-}" == "--reload" ]]; then
    rm -f "$RESULT.reloading"
    TERNI_RELOAD_TEST="$RESULT" timeout -s KILL "${TIMEOUT:-300}" "$FLAX_EDITOR" -std -headless -mute "-project=$PWD" > /dev/null 2>&1 || true
    LOG="Logs/$(ls -t Logs | head -1)"
    (iconv -f utf-16 -t utf-8 "$LOG" 2>/dev/null || cat "$LOG") | grep -a -q "scripts reload: unloaded 1 world" \
        || { echo "the editor did not free the world when the scripts reloaded"; exit 1; }
elif [[ "${1:-}" == "--cooked" ]]; then
    rm -rf Output/Linux Galaxy/spirv   # the cook must compile the rules itself
    COOK=$(timeout -s KILL "${TIMEOUT:-1200}" "$FLAX_EDITOR" -std -headless -mute -null "-project=$PWD" -build Release.Linux 2>&1 || true)
    grep -E "\[Terni\]|Game building done" <<< "$COOK" || true
    grep -q "Game building done" <<< "$COOK" || { echo "cooking failed, see Example/Logs"; exit 1; }
    [[ -f Output/Linux/Terni-NOTICES.txt ]] || { echo "Terni-NOTICES.txt is not in the cooked game"; exit 1; }
    GAME=$(find Output/Linux -maxdepth 1 -type f -executable ! -name '*.so*' ! -name terni_compile_rules | head -1)
    # A rule changed after cooking, as a mod would: the cooked game must compile it itself.
    echo "// changed after cooking" >> Output/Linux/Galaxy/rules/planet_growth.glsl
    # A mod installed beside the game, as players install them.
    cp -r Mods Output/Linux/
    BEFORE=$(ls Output/Linux/Galaxy/spirv | wc -l)
    TERNI_EXAMPLE_TEST="$RESULT" timeout -s KILL 300 "$GAME" -headless -mute > /dev/null 2>&1 || true
    [[ $(ls Output/Linux/Galaxy/spirv | wc -l) -gt $BEFORE ]] || { echo "the cooked game did not compile the changed rule"; exit 1; }
else
    SCENE=$(grep -m1 '"ID"' Content/Main.scene | sed -E 's/.*"([0-9a-f]{32})".*/\1/')
    TERNI_EXAMPLE_TEST="$RESULT" timeout -s KILL "${TIMEOUT:-300}" "$FLAX_EDITOR" -std -headless -mute "-project=$PWD" -play "$SCENE" > /dev/null 2>&1 || true
fi
cat "$RESULT"
grep -q '^PASSED$' "$RESULT"
