#!/bin/bash
# Build the Asahi AGX compiler and run its gtests on Linux (natively -- the
# project's default build cross-compiles for Windows-ARM64, whose binaries
# cannot be executed on the dev host, and Mesa's headers do not compile under
# the macOS SDK). This is how the AGX3 backend's end-to-end tests
# (mesa:asahi / agx_tests, incl. AGX3PackBinary) are actually run.
#
# Usage: run from anywhere with Docker available. ~15 min cold (builds
# SPIRV-Tools + mesa_clc); re-runs are fast.
set -euo pipefail
IMG=agx-native-test
SRC_TAR=$(mktemp /tmp/mesa-src.XXXX.tar)
ROOT=$(git -C "$(dirname "$0")" rev-parse --show-toplevel)
git -C "$ROOT" archive --format=tar HEAD -o "$SRC_TAR"

ctx=$(mktemp -d); cp "$SRC_TAR" "$ctx/mesa-src.tar"
cat > "$ctx/Dockerfile" <<'DOCKER'
FROM ubuntu:24.04
ENV DEBIAN_FRONTEND=noninteractive
RUN apt-get update && apt-get install -y --no-install-recommends \
      ninja-build pkg-config python3-mako python3-packaging python3-pip \
      bison flex libdrm-dev libexpat1-dev zlib1g-dev clang llvm-18-dev \
      libclang-18-dev clang-18 libclc-18-dev libllvmspirvlib-18-dev cmake \
      git ca-certificates libgtest-dev g++ && rm -rf /var/lib/apt/lists/*
RUN pip3 install --break-system-packages "meson>=1.4,<1.7"
# SPIRV-Tools dev libs are not packaged in Ubuntu 24.04; build from source.
RUN cd /opt && git clone --depth 1 https://github.com/KhronosGroup/SPIRV-Headers.git \
 && git clone --depth 1 https://github.com/KhronosGroup/SPIRV-Tools.git \
 && cd SPIRV-Tools && cp -r ../SPIRV-Headers external/spirv-headers \
 && cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release \
      -DCMAKE_INSTALL_PREFIX=/usr/local -DSPIRV_SKIP_TESTS=ON \
      -DSPIRV_TOOLS_BUILD_STATIC=ON && ninja -C build install
WORKDIR /src
COPY mesa-src.tar /tmp/mesa-src.tar
RUN tar xf /tmp/mesa-src.tar && rm /tmp/mesa-src.tar
ENV PATH=/usr/local/bin:$PATH PKG_CONFIG_PATH=/usr/local/lib/pkgconfig
RUN meson setup build -Dtools=asahi -Dbuild-tests=true -Dgallium-drivers= \
      -Dvulkan-drivers= "-Dplatforms=" -Dglx=disabled -Degl=disabled \
      -Dgbm=disabled "-Dvideo-codecs=" -Dzstd=disabled -Dllvm=enabled \
      -Dmesa-clc=enabled -Dprecomp-compiler=enabled
RUN ninja -C build src/asahi/compiler/agx_tests
DOCKER

docker build -t "$IMG" "$ctx"
docker run --rm "$IMG" ./build/src/asahi/compiler/agx_tests "$@"
rm -rf "$ctx" "$SRC_TAR"
