#!/bin/sh
# Builds vitaGL for the Vita port into PREFIX (default build-vita/deps):
# PREFIX/lib/libvitaGL.a and PREFIX/include/vitaGL.h.
#
#   tools/vita_deps.sh [PREFIX]
#
# The renderer copies the framebuffer between draws that test depth
# (gl3d.c, the faces through a colour table), and each copy starts a new
# GXM scene; vitaGL keeps the depth buffer across scenes only when built
# with STORE_DEPTH_STENCIL=1, which the vdpm package is not. Everything
# else comes from VitaSDK's packages:
#
#   vdpm install vitaShaRK libmathneon taihen SceShaccCgExt
#
# VITASDK defaults to ~/vitasdk. Extra include directories for those
# packages, if they are not in the SDK, go in VITA_DEPS_INCLUDE
# (colon separated).

set -e

: "${VITASDK:=$HOME/vitasdk}"
export VITASDK
PREFIX=${1:-build-vita/deps}
# The commit VitaSDK's vitaGL package is built from.
COMMIT=2bdbe895ddc6a7e410a9ea55a8f53d8173c61ad8
SRC="$PREFIX/src/vitaGL"

mkdir -p "$PREFIX/lib" "$PREFIX/include" "$PREFIX/src"
PREFIX=$(cd "$PREFIX" && pwd)
SRC="$PREFIX/src/vitaGL"
if [ ! -d "$SRC/.git" ]; then
    git init -q "$SRC"
    git -C "$SRC" remote add origin https://github.com/Rinnegatamante/vitaGL.git
fi
if [ "$(git -C "$SRC" rev-parse -q --verify HEAD 2>/dev/null)" != "$COMMIT" ]; then
    git -C "$SRC" fetch -q --depth 1 origin "$COMMIT"
    git -C "$SRC" checkout -q FETCH_HEAD
fi

inc="$PREFIX/include:$VITASDK/arm-vita-eabi/include"
[ -n "$VITA_DEPS_INCLUDE" ] && inc="$VITA_DEPS_INCLUDE:$inc"
jobs=$(sysctl -n hw.ncpu 2>/dev/null || nproc 2>/dev/null || echo 4)
log="$PREFIX/vitaGL-build.log"
if ! PATH="$VITASDK/bin:$PATH" CPATH="$inc" \
    make -C "$SRC" NO_DEBUG=1 STORE_DEPTH_STENCIL=1 -j"$jobs" >"$log" 2>&1; then
    tail -n 30 "$log"
    echo "vita_deps: vitaGL failed to build; see $log" >&2
    exit 1
fi
cp "$SRC/libvitaGL.a" "$PREFIX/lib/"
cp "$SRC/source/vitaGL.h" "$PREFIX/include/"
echo "vita_deps: $PREFIX/lib/libvitaGL.a (STORE_DEPTH_STENCIL=1)"
