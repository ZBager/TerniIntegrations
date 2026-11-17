#!/usr/bin/env bash
# SPDX-License-Identifier: LGPL-2.1-only WITH AdditionRef-Terni-linking-exception
# Copyright (C) 2026 Rafał Zając
#
# Runs the example's self-test: the galaxy example on every Vulkan device, checked
# against the state hash Terni gives on its own. Build first (build.sh).
#   ./test.sh              headless (the renderer's GPU is not checked: there is none)
#   ./test.sh --window     in a window, so the renderer's GPU is checked too; extra
#                          arguments go to Godot, e.g. ./test.sh --window --gpu-index 0
#   ./test.sh --export     exports the example for Linux, changes a rule in the exported
#                          game as a mod would, and runs the test there. Needs Godot's
#                          export templates: installed, or the folder holding
#                          linux_release.x86_64 in GODOT_TEMPLATES
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
if [[ "${1:-}" == "--export" ]]; then
    OUT="$(realpath -m ../../build/godot-export)"
    rm -rf "$OUT" Galaxy/spirv && mkdir -p "$OUT"   # the export must compile the rules itself
    TEMPLATE=""
    if [[ -n "${GODOT_TEMPLATES:-}" ]]; then TEMPLATE="$(realpath "$GODOT_TEMPLATES")/linux_release.x86_64"; fi
    cat > export_presets.cfg <<PRESET
[preset.0]

name="Linux"
platform="Linux"
runnable=true
export_filter="all_resources"
include_filter=""
exclude_filter=""
export_path=""

[preset.0.options]

custom_template/debug="$TEMPLATE"
custom_template/release="$TEMPLATE"
binary_format/embed_pck=false
binary_format/architecture="x86_64"
PRESET
    EXPORT=$("$GODOT" --headless --path . --export-release Linux "$OUT/TerniExample.x86_64" 2>&1 || true)
    grep -E "\[Terni\]|ERROR" <<< "$EXPORT" || true
    [[ -x "$OUT/TerniExample.x86_64" && -f "$OUT/Galaxy/schema/entities.txt" ]] || { echo "the export failed"; exit 1; }
    [[ -x "$OUT/terni_compile_rules" && -f "$OUT/Terni-NOTICES.txt" ]] || { echo "the rule compiler or the notices are not in the export"; exit 1; }
    # A rule changed after exporting, as a mod would: the exported game must compile it itself.
    echo "// changed after exporting" >> "$OUT/Galaxy/rules/planet_growth.glsl"
    BEFORE=$(ls "$OUT/Galaxy/spirv" | wc -l)
    TERNI_EXAMPLE_TEST="$RESULT" timeout -s KILL 300 "$OUT/TerniExample.x86_64" --headless > /dev/null 2>&1 || true
    [[ $(ls "$OUT/Galaxy/spirv" | wc -l) -gt $BEFORE ]] || { echo "the exported game did not compile the changed rule"; exit 1; }
else
    MODE=(--headless)
    if [[ "${1:-}" == "--window" ]]; then MODE=("${@:2}"); fi
    TERNI_EXAMPLE_TEST="$RESULT" timeout -s KILL "${TIMEOUT:-300}" "$GODOT" --path . "${MODE[@]}" > /dev/null 2>&1 || true
fi
cat "$RESULT"
grep -q '^PASSED$' "$RESULT"
