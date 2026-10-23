#!/bin/sh
# Packs build-linux/ignition into an AppImage for any desktop Linux and
# the Steam Deck. It needs only what every desktop has: libc, X11,
# libGL and ALSA (which PulseAudio and PipeWire also serve).
#
#   platforms/linux/appimage.sh [version] [arch]
#       -> build-linux/ignition-linux-<arch>-<version>.AppImage
#
# arch is x86_64 (default) or aarch64. Downloads appimagetool.
set -eu

HERE=$(cd "$(dirname "$0")" && pwd)
ROOT=$(cd "$HERE/../.." && pwd)
OUT=$ROOT/build-linux
VERSION=${1:-v$(cat "$ROOT/VERSION")}
ARCH=${2:-x86_64}
TOOL_URL=https://github.com/AppImage/appimagetool/releases/download/continuous/appimagetool-$ARCH.AppImage

[ -x "$OUT/ignition" ] || { echo "Build first: make linux" >&2; exit 1; }

APPDIR=$OUT/AppDir
rm -rf "$APPDIR"
mkdir -p "$APPDIR/usr/bin" "$APPDIR/usr/share/applications" \
    "$APPDIR/usr/share/icons/hicolor/256x256/apps"
cp "$OUT/ignition" "$OUT/cd_extract" "$APPDIR/usr/bin/"
strip "$APPDIR/usr/bin/ignition" "$APPDIR/usr/bin/cd_extract"
cp "$HERE/ignition.desktop" "$APPDIR/usr/share/applications/"
cp "$HERE/ignition.desktop" "$APPDIR/"
cp "$HERE/ignition.png" "$APPDIR/usr/share/icons/hicolor/256x256/apps/"
cp "$HERE/ignition.png" "$APPDIR/"

cat > "$APPDIR/AppRun" << 'APPRUN'
#!/bin/sh
HERE=$(dirname "$(readlink -f "$0")")
exec "$HERE/usr/bin/ignition" "$@"
APPRUN
chmod +x "$APPDIR/AppRun"

TOOL=$OUT/appimagetool-$ARCH.AppImage
if [ ! -x "$TOOL" ]; then
    curl -sSfL -o "$TOOL" "$TOOL_URL"
    chmod +x "$TOOL"
fi
ARCH=$ARCH "$TOOL" --appimage-extract-and-run "$APPDIR" \
    "$OUT/ignition-linux-$ARCH-$VERSION.AppImage"
echo "Built $OUT/ignition-linux-$ARCH-$VERSION.AppImage"
