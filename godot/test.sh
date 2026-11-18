#!/usr/bin/env bash
# SPDX-License-Identifier: LGPL-2.1-only WITH AdditionRef-Terni-linking-exception
# Copyright (C) 2026 Rafał Zając
#
# Runs the example's self-test: the galaxy example on every Vulkan device, checked
# against the state hash Terni gives on its own. Build first (build.sh).
#   ./test.sh              headless (the renderer's GPU is not checked: there is none)
#   ./test.sh --window     in a window, so the renderer's GPU is checked too; extra
#                          arguments go to Godot, e.g. ./test.sh --window --gpu-index 0
#   ./test.sh --csharp     the C# example, headless, with godot-mono (GODOT_MONO)
#   ./test.sh --export     exports the example for Linux, changes a rule in the exported
#                          game as a mod would, puts the example mod beside it, and runs
#                          the test there. Needs Godot's
#                          export templates: installed, or the folder holding
#                          linux_release.x86_64 in GODOT_TEMPLATES
#   ./test.sh --export-csharp  the same with the C# example and godot-mono; .NET export
#                          templates installed, or their folder in GODOT_MONO_TEMPLATES
#   ./test.sh --no-gpu [mode]  any of the above as on a machine whose Vulkan drivers give
#                          no device: the system's drivers are hidden, and the software
#                          driver shipped with the addon must be added and used. Build with
#                          TERNI_SOFTWARE_DRIVER for that.
set -euo pipefail
if [[ "${1:-}" == "--no-gpu" ]]; then
    shift
    export VK_DRIVER_FILES=/nonexistent/terni.json VK_ICD_FILENAMES=/nonexistent/terni.json TERNI_EXPECT_SOFTWARE_DRIVER=1
fi
cd "$(dirname "$0")/example"
GODOT="${GODOT:-godot}"
RESULT="$(mktemp)"
if [[ ! -d .godot ]]; then
    # The first import of a project registers the extension; Godot 4.7 may crash as it
    # quits after that, with the import done. A second run checks it.
    "$GODOT" --headless --path . --import > /dev/null 2>&1 || true
    "$GODOT" --headless --path . --import > /dev/null 2>&1
fi
if [[ "${1:-}" == "--csharp" ]]; then
    cd ../example-csharp
    MONO="${GODOT_MONO:-godot-mono}"
    if [[ ! -d .godot ]]; then
        "$MONO" --headless --path . --import > /dev/null 2>&1 || true
    fi
    BUILD_LOG="$(mktemp)"
    rm -f .godot/mono/temp/bin/Debug/TerniExampleCs.dll   # so that a failed build is not hidden by an old one
    "$MONO" --headless --path . --build-solutions --quit > "$BUILD_LOG" 2>&1 || true
    [[ -f .godot/mono/temp/bin/Debug/TerniExampleCs.dll ]] || { cat "$BUILD_LOG"; echo "the C# project did not build"; exit 1; }
    "$MONO" --headless --path . --import > /dev/null 2>&1
    TERNI_EXAMPLE_TEST="$RESULT" timeout -s KILL "${TIMEOUT:-300}" "$MONO" --headless --path . > /dev/null 2>&1 || true
elif [[ "${1:-}" == "--export" || "${1:-}" == "--export-csharp" ]]; then
    if [[ "$1" == "--export-csharp" ]]; then
        cd ../example-csharp
        GODOT="${GODOT_MONO:-godot-mono}"
        TEMPLATES="${GODOT_MONO_TEMPLATES:-}"
        NAME=TerniExampleCs
        [[ -d .godot ]] || "$GODOT" --headless --path . --import > /dev/null 2>&1 || true
        "$GODOT" --headless --path . --build-solutions --quit > /dev/null 2>&1
    else
        TEMPLATES="${GODOT_TEMPLATES:-}"
        NAME=TerniExample
    fi
    OUT="$(realpath -m ../../build/godot-export/$NAME)"
    rm -rf "$OUT" Galaxy/spirv && mkdir -p "$OUT"   # the export must compile the rules itself
    TEMPLATE=""
    if [[ -n "$TEMPLATES" ]]; then TEMPLATE="$(realpath "$TEMPLATES")/linux_release.x86_64"; fi
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
    EXPORT=$("$GODOT" --headless --path . --export-release Linux "$OUT/$NAME.x86_64" 2>&1 || true)
    grep -E "\[Terni\]|ERROR" <<< "$EXPORT" || true
    [[ -x "$OUT/$NAME.x86_64" && -f "$OUT/Galaxy/schema/entities.txt" ]] || { echo "the export failed"; exit 1; }
    [[ -x "$OUT/terni_compile_rules" && -f "$OUT/Terni-NOTICES.txt" ]] || { echo "the rule compiler or the notices are not in the export"; exit 1; }
    # A rule changed after exporting, as a mod would: the exported game must compile it itself.
    echo "// changed after exporting" >> "$OUT/Galaxy/rules/planet_growth.glsl"
    # A mod installed beside the game, as players install them; res://Mods finds it there.
    cp -r Mods "$OUT/"
    BEFORE=$(ls "$OUT/Galaxy/spirv" | wc -l)
    TERNI_EXAMPLE_TEST="$RESULT" timeout -s KILL 300 "$OUT/$NAME.x86_64" --headless > /dev/null 2>&1 || true
    [[ $(ls "$OUT/Galaxy/spirv" | wc -l) -gt $BEFORE ]] || { echo "the exported game did not compile the changed rule"; exit 1; }
else
    MODE=(--headless)
    if [[ "${1:-}" == "--window" ]]; then MODE=("${@:2}"); fi
    TERNI_EXAMPLE_TEST="$RESULT" timeout -s KILL "${TIMEOUT:-300}" "$GODOT" --path . "${MODE[@]}" > /dev/null 2>&1 || true
fi
cat "$RESULT"
grep -q '^PASSED$' "$RESULT"
