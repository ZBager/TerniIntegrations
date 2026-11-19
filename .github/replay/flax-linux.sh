#!/usr/bin/env bash
# SPDX-License-Identifier: LGPL-2.1-only WITH AdditionRef-Terni-linking-exception
# Copyright (C) 2026 Rafał Zając
#
# Replays the CI job flax-linux in a clean ubuntu:24.04 container, with the software
# driver that godot-linux.sh built in /out/lavapipe (run that first). From the repository:
#   docker run --rm -v $PWD:/src:ro -v $PWD/.github/replay:/ci:ro -v $PWD/build/ci-replay:/out \
#     ubuntu:24.04 bash /ci/flax-linux.sh
# /src: the repository (read-only); /out: the results.
set -euxo pipefail
# The results belong to whoever owns the repository, not to the container's root.
trap 'chown -R "$(stat -c %u:%g /src)" /out' EXIT
export DEBIAN_FRONTEND=noninteractive
apt-get update
# What the GitHub runner image has already
apt-get install -y --no-install-recommends ca-certificates git curl cmake g++ python3 unzip xz-utils sudo file libicu74
# flax-linux: Install packages
apt-get install -y --no-install-recommends ninja-build clang libvulkan-dev glslc libshaderc-dev mesa-vulkan-drivers vulkan-tools libxrandr2 libxi6 zlib1g-dev libx11-dev libxcursor-dev libxinerama-dev libxfixes-dev libwayland-dev libglib2.0-dev xvfb xauth
# The runner's own .NET 11, then actions/setup-dotnet with 10.0.x in a folder of its own
curl -fsSL https://dot.net/v1/dotnet-install.sh -o /tmp/dotnet-install.sh
bash /tmp/dotnet-install.sh --channel 11.0 --install-dir /usr/share/dotnet > /dev/null
ln -sf /usr/share/dotnet/dotnet /usr/bin/dotnet
bash /tmp/dotnet-install.sh --channel 10.0 --install-dir /opt/dotnet10 > /dev/null
export DOTNET_ROOT=/opt/dotnet10 PATH=/opt/dotnet10:$PATH

git config --global --add safe.directory /src   # /src belongs to another user
rm -rf /work && mkdir /work && cd /src && git ls-files -co --exclude-standard -z | xargs -0 cp --parents -t /work
test -f /work/native/lavapipe/build.sh
cd /work
mkdir -p build/lavapipe && cp /out/lavapipe/libvulkan_lvp.so build/lavapipe/

# The Flax editor and its Linux platform tools
base=https://vps3.flaxengine.com/store/builds/Package_1_13_07014
[[ -f /out/editor.zip ]] || curl -fsSL -o /out/editor.zip "$base/FlaxEditorLinux.zip"
[[ -f /out/linux.zip ]] || curl -fsSL -o /out/linux.zip "$base/Linux.zip"
sha256sum -c <<EOF
accc1057101870398d776faf653cd5f0541f4a716e2c502f937401d7e1345dac  /out/editor.zip
f6a4743fc81b3f7b82493bf02deb174944a4fb6b363db891ea34837c6df08875  /out/linux.zip
EOF
mkdir -p build/FlaxEditor/Source/Platforms/Linux
unzip -q /out/editor.zip -d build/FlaxEditor
unzip -q /out/linux.zip -d build/FlaxEditor/Source/Platforms/Linux
export FLAX_EDITOR=/work/build/FlaxEditor/Binaries/Editor/Linux/Development/FlaxEditor

vulkaninfo --summary | grep -E "deviceName|driverName" || true
flax/build-native.sh > /out/flax-native.txt 2>&1
for mode in "" "--visual" "--reload" "--cooked" "--no-gpu"; do
    name=flax$(echo $mode | tr -d ' -')
    echo "=== flax/test.sh $mode"
    wrap=(); [[ "$mode" == "" || "$mode" == "--no-gpu" ]] && wrap=(xvfb-run -a)
    "${wrap[@]}" flax/test.sh $mode > /out/$name.txt 2>&1 && echo PASSED || { echo FAILED; tail -20 /out/$name.txt; }
done
mkdir -p /out/flax-logs && cp -r flax/Example/Logs/. /out/flax-logs/ 2>/dev/null || true
