#!/usr/bin/env bash
# SPDX-License-Identifier: LGPL-2.1-only WITH AdditionRef-Terni-linking-exception
# Copyright (C) 2026 Rafał Zając
#
# Builds Mesa's lavapipe, a Vulkan driver that runs on the CPU, with LLVM inside it, for
# the plugins to ship with games: on a machine whose GPUs cannot run the simulation, it
# is the device the simulation runs on (with the same results). Built for compute only:
# no window-system libraries, no shader cache on disk, the C++ runtime linked in, so the
# driver needs nothing on the player's machine but the C library and the Vulkan loader.
#
#   native/lavapipe/build.sh        ->  build/lavapipe/libvulkan_lvp.so and its licences
#
# The plugins' build scripts ship it from there. The first run takes long (LLVM: half an
# hour or more); later runs reuse what is built. Needs curl, CMake, Ninja, a C++
# compiler, Python 3 with meson, mako, PyYAML and packaging, bison, flex and
# glslangValidator. JOBS sets the number of parallel jobs.
set -euo pipefail

LLVM_VERSION=23.1.5
LLVM_SHA256=fb50b1eac93ddeb3aa8dab4cfe324dff74f4a9c2bdee6160c9e36188f8d06a6e
MESA_VERSION=26.2.7
MESA_SHA256=04294f75d22e9f61cd1e050206bc48b3d39366e788f1803a3368e3d5a872ce18

ROOT="$(realpath -m "$(dirname "$0")/../../build/lavapipe")"
JOBS="${JOBS:-$(nproc)}"
mkdir -p "$ROOT/downloads" "$ROOT/src"

# Downloads a file once and checks it against its pinned SHA-256.
fetch() {
    local url="$1" sha="$2" file="$ROOT/downloads/$(basename "$1")"
    if [[ ! -f "$file" ]]; then
        echo "downloading $url"
        curl -fL --retry 3 -o "$file.part" "$url"
        mv "$file.part" "$file"
    fi
    echo "$sha  $file" | sha256sum -c --quiet || { echo "$file does not match its checksum"; exit 1; }
}

# ---- LLVM: static libraries for the x86 CPU only, no tools but llvm-config ----
LLVM_SRC="$ROOT/src/llvm-project-$LLVM_VERSION.src"
LLVM_BUILD="$ROOT/llvm-build"
if [[ ! -d "$LLVM_SRC/libc" ]]; then
    fetch "https://github.com/llvm/llvm-project/releases/download/llvmorg-$LLVM_VERSION/llvm-project-$LLVM_VERSION.src.tar.xz" "$LLVM_SHA256"
    # The parts an LLVM build reads (it takes a few headers from libc).
    tar xf "$ROOT/downloads/llvm-project-$LLVM_VERSION.src.tar.xz" -C "$ROOT/src" \
        "llvm-project-$LLVM_VERSION.src/llvm" "llvm-project-$LLVM_VERSION.src/cmake" \
        "llvm-project-$LLVM_VERSION.src/third-party" "llvm-project-$LLVM_VERSION.src/libc"
fi
cmake -S "$LLVM_SRC/llvm" -B "$LLVM_BUILD" -G Ninja -DCMAKE_BUILD_TYPE=Release \
    -DLLVM_TARGETS_TO_BUILD=X86 -DLLVM_ENABLE_PROJECTS= -DLLVM_ENABLE_RUNTIMES= \
    -DLLVM_BUILD_TOOLS=OFF -DLLVM_BUILD_UTILS=OFF -DLLVM_INCLUDE_TESTS=OFF -DLLVM_INCLUDE_EXAMPLES=OFF \
    -DLLVM_INCLUDE_BENCHMARKS=OFF -DLLVM_INCLUDE_DOCS=OFF -DLLVM_ENABLE_BINDINGS=OFF \
    -DLLVM_ENABLE_ZLIB=OFF -DLLVM_ENABLE_ZSTD=OFF -DLLVM_ENABLE_LIBXML2=OFF -DLLVM_ENABLE_LIBEDIT=OFF \
    -DLLVM_ENABLE_LIBPFM=OFF -DLLVM_ENABLE_CURL=OFF -DLLVM_ENABLE_HTTPLIB=OFF -DLLVM_ENABLE_FFI=OFF \
    -DLLVM_ENABLE_Z3_SOLVER=OFF -DLLVM_ENABLE_PIC=ON -DLLVM_ENABLE_RTTI=OFF -DLLVM_BUILD_LLVM_DYLIB=OFF > /dev/null
cmake --build "$LLVM_BUILD" --target llvm-libraries llvm-config -j "$JOBS"

# ---- Mesa: lavapipe alone ----
MESA_SRC="$ROOT/src/mesa-$MESA_VERSION"
MESA_BUILD="$ROOT/mesa-build"
if [[ ! -d "$MESA_SRC" ]]; then
    fetch "https://archive.mesa3d.org/mesa-$MESA_VERSION.tar.xz" "$MESA_SHA256"
    tar xf "$ROOT/downloads/mesa-$MESA_VERSION.tar.xz" -C "$ROOT/src"
fi
cat > "$ROOT/llvm.ini" <<EOF
[binaries]
llvm-config = '$LLVM_BUILD/bin/llvm-config'
EOF
# The driver exports only what the Vulkan loader calls. Without this, every symbol of the
# static LLVM and C++ runtime inside it would be exported too: tens of thousands, kept
# whether used or not, and free to clash with the host's own.
cat > "$ROOT/lavapipe.map" <<EOF
{
  global: vk_icdGetInstanceProcAddr; vk_icdGetPhysicalDeviceProcAddr; vk_icdNegotiateLoaderICDInterfaceVersion;
  local: *;
};
EOF
if [[ ! -f "$MESA_BUILD/build.ninja" ]]; then
    # pkg-config finds nothing: Mesa would otherwise link the system's libudev and
    # libdrm. libdrm, which Mesa requires on Linux, is built from Mesa's own wrap (a
    # pinned, checksummed download) into the driver instead. LLVM comes from llvm-config.
    PKG_CONFIG_LIBDIR="$ROOT/no-pkg-config" PKG_CONFIG_PATH= \
    meson setup "$MESA_BUILD" "$MESA_SRC" --native-file "$ROOT/llvm.ini" \
        --buildtype=release -Db_ndebug=true -Dstrip=true \
        -Dplatforms= -Dvulkan-drivers=swrast -Dgallium-drivers=llvmpipe \
        -Dopengl=false -Dgles1=disabled -Dgles2=disabled -Dglx=disabled -Degl=disabled -Dgbm=disabled \
        -Dllvm=enabled -Dshared-llvm=disabled -Dcpp_rtti=false \
        --force-fallback-for=libdrm -Dallow-fallback-for=libdrm -Dlibdrm:default_library=static \
        -Dshader-cache=disabled -Dxmlconfig=disabled -Dexpat=disabled -Dzlib=disabled -Dzstd=disabled \
        -Ddisplay-info=disabled -Dspirv-tools=disabled -Dvalgrind=disabled -Dlibunwind=disabled -Dlmsensors=disabled \
        -Dbuild-tests=false -Dtools=[] -Dvulkan-layers=[] -Dvideo-codecs=[] -Dperfetto=false \
        -Dteflon=false -Dgallium-va=disabled -Dgallium-rusticl=false -Dhtml-docs=disabled \
        "-Dcpp_link_args=['-static-libstdc++', '-static-libgcc', '-Wl,--version-script=$ROOT/lavapipe.map']" \
        "-Dc_link_args=['-static-libgcc', '-Wl,--version-script=$ROOT/lavapipe.map']"
fi
ninja -C "$MESA_BUILD" -j "$JOBS" src/gallium/targets/lavapipe/libvulkan_lvp.so

# ---- the driver and the licences of what is inside it ----
strip --strip-unneeded -o "$ROOT/libvulkan_lvp.so" "$MESA_BUILD/src/gallium/targets/lavapipe/libvulkan_lvp.so"
mkdir -p "$ROOT/licences"
cp "$MESA_SRC/docs/license.rst" "$ROOT/licences/Mesa-license.rst"
cp "$LLVM_SRC/llvm/LICENSE.TXT" "$ROOT/licences/LLVM-LICENSE.TXT"
echo "built $ROOT/libvulkan_lvp.so ($(du -h "$ROOT/libvulkan_lvp.so" | cut -f1)), Mesa $MESA_VERSION with LLVM $LLVM_VERSION"
# What it needs from the system: only the C library and the dynamic linker.
NEEDED=$(readelf -d "$ROOT/libvulkan_lvp.so" | sed -n 's/.*Shared library: \[\(.*\)\]/\1/p' | tr '\n' ' ')
echo "it needs: $NEEDED"
for lib in $NEEDED; do
    case "$lib" in libc.so.*|libm.so.*|libdl.so.*|libpthread.so.*|ld-linux*) ;;
    *) echo "the driver needs $lib, which a player's machine may not have"; exit 1 ;;
    esac
done
