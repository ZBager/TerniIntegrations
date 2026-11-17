#!/usr/bin/env bash
# SPDX-License-Identifier: LGPL-2.1-only WITH AdditionRef-Terni-linking-exception-1.0
# Copyright (C) 2026 Rafał Zając
#
# Runs the example's self-test in a headless editor: the galaxy example on every Vulkan
# device, checked against the state hash Terni gives on its own.
#   FLAX_EDITOR=/path/to/FlaxEditor ./test.sh
set -euo pipefail
cd "$(dirname "$0")/Example"
: "${FLAX_EDITOR:?Set FLAX_EDITOR to the FlaxEditor program}"
RESULT="$(mktemp)"
SCENE=$(grep -m1 '"ID"' Content/Main.scene | sed -E 's/.*"([0-9a-f]{32})".*/\1/')
TERNI_EXAMPLE_TEST="$RESULT" timeout -s KILL "${TIMEOUT:-300}" "$FLAX_EDITOR" -std -headless -mute "-project=$PWD" -play "$SCENE" > /dev/null 2>&1 || true
cat "$RESULT"
grep -q '^PASSED$' "$RESULT"
