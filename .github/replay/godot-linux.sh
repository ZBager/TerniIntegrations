#!/usr/bin/env bash
# SPDX-License-Identifier: LGPL-2.1-only WITH AdditionRef-Terni-linking-exception
# Copyright (C) 2026 Rafał Zając
#
# Replays the CI jobs lavapipe-linux and godot-linux in a clean ubuntu:24.04 container,
# to find what a runner lacks before pushing. From the repository:
#   docker run --rm -v $PWD:/src:ro -v $PWD/.github/replay:/ci:ro -v $PWD/build/ci-replay:/out \
#     ubuntu:24.04 bash /ci/godot-linux.sh
# /src: the repository (read-only); /out: the results, and the software driver's sources
# and build (so a second run is quick).
set -euxo pipefail
# The results belong to whoever owns the repository, not to the container's root.
trap 'chown -R "$(stat -c %u:%g /src)" /out' EXIT
export DEBIAN_FRONTEND=noninteractive
apt-get update
# What the GitHub runner image has already (git, curl, cmake, gcc, python3, unzip, sudo)
apt-get install -y --no-install-recommends ca-certificates git curl cmake g++ python3 python3-venv unzip xz-utils sudo file
# lavapipe-linux: Install packages
apt-get install -y --no-install-recommends ninja-build bison flex glslang-tools python3-venv
python3 -m venv /tmp/venv && /tmp/venv/bin/pip install -q meson mako pyyaml packaging
export PATH=/tmp/venv/bin:$PATH

git config --global --add safe.directory /src   # /src belongs to another user
rm -rf /work && mkdir /work && cd /src && git ls-files -co --exclude-standard -z | xargs -0 cp --parents -t /work
test -f /work/native/lavapipe/build.sh
cd /work
mkdir -p build
# LLVM and Mesa sources and builds kept in /out, so a second run is quick.
mkdir -p /out/lavapipe && ln -s /out/lavapipe build/lavapipe
native/lavapipe/build.sh > /out/lavapipe-build.txt 2>&1
tail -3 /out/lavapipe-build.txt

# godot-linux: Install packages
apt-get install -y --no-install-recommends ninja-build libvulkan-dev glslc libshaderc-dev mesa-vulkan-drivers vulkan-tools dotnet-sdk-8.0
mkdir -p /tmp/godot && cd /tmp/godot
base=https://github.com/godotengine/godot/releases/download/4.7.2-stable
[[ -f /out/godot.zip ]] || curl -fsSL -o /out/godot.zip "$base/Godot_v4.7.2-stable_linux.x86_64.zip"
[[ -f /out/mono.zip ]] || curl -fsSL -o /out/mono.zip "$base/Godot_v4.7.2-stable_mono_linux_x86_64.zip"
unzip -q /out/godot.zip && unzip -q /out/mono.zip
export GODOT=/tmp/godot/Godot_v4.7.2-stable_linux.x86_64
export GODOT_MONO=/tmp/godot/Godot_v4.7.2-stable_mono_linux_x86_64/Godot_v4.7.2-stable_mono_linux.x86_64
vulkaninfo --summary | grep -E "deviceName|driverName" || true
cd /work
godot/build.sh > /out/godot-build.txt 2>&1
tail -1 /out/godot-build.txt
for mode in "" "--csharp" "--no-gpu" "--no-gpu --csharp"; do
    name=godot$(echo $mode | tr -d ' -')
    echo "=== godot/test.sh $mode"
    godot/test.sh $mode > /out/$name.txt 2>&1 && echo PASSED || { echo FAILED; tail -20 /out/$name.txt; }
done
