#!/usr/bin/env sh
set -eu

prefix=${1:-"$(pwd)/third_party/fltk-musl"}
source_dir=${2:-"$(pwd)/third_party/fltk"}
build_dir="${source_dir}/build-musl-wayland"
toolchain=${MUSL_TOOLCHAIN:-/opt/cross/x86_64-linux-musl-cross/bin}
sysroot=${MUSL_SYSROOT:-}

if [ -z "$sysroot" ]; then
  printf '%s\n' 'MUSL_SYSROOT must point to a musl-built Wayland, xkbcommon, Cairo and Pango dependency sysroot.' >&2
  exit 1
fi

rm -rf "$build_dir"

PKG_CONFIG_SYSROOT_DIR="$sysroot" \
PKG_CONFIG_LIBDIR="$sysroot/lib/pkgconfig:$sysroot/share/pkgconfig" \
cmake -S "$source_dir" -B "$build_dir" \
  -DCMAKE_BUILD_TYPE=MinSizeRel \
  -DCMAKE_C_COMPILER="$toolchain/x86_64-linux-musl-gcc" \
  -DCMAKE_CXX_COMPILER="$toolchain/x86_64-linux-musl-g++" \
  -DCMAKE_SYSROOT="$sysroot" \
  -DCMAKE_INSTALL_PREFIX="$prefix" \
  -DBUILD_SHARED_LIBS=OFF \
  -DFLTK_BUILD_EXAMPLES=OFF \
  -DFLTK_BUILD_TEST=OFF \
  -DFLTK_BUILD_FLUID=OFF \
  -DFLTK_BACKEND_WAYLAND=ON \
  -DFLTK_BACKEND_X11=OFF \
  -DFLTK_BACKEND_COCOA=OFF \
  -DOPTION_USE_SYSTEM_LIBDECOR=OFF
cmake --build "$build_dir"
cmake --install "$build_dir"
