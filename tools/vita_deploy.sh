#!/bin/sh
# Copies the Vita build to the console, as it is installed there:
#
#   ux0:app/IGNT00001/           eboot.bin, sce_sys/, shaders/ (the package)
#   ux0:data/ignition/ignition.vpk   the package, for VitaShell
#   ux0:data/ignition/disc/      the extracted disc, if it is not there
#
#   tools/vita_deploy.sh pull    shaders the Vita compiled ->
#                                platforms/vita/shaders/
#   tools/vita_deploy.sh push    build-vita/ignition.vpk -> the Vita
#
# The Vita is either a mounted ux0 (its memory card in a reader, or
# VitaShell's USB mode): VITA_UX0, or the first volume in /Volumes with
# app/, data/ and id.dat; or VitaShell's FTP server (Select in VitaShell):
# VITA_IP, port VITA_PORT (1337). DATA is the extracted disc (default the
# one `make` runs). VITA_MUSIC=1 also copies the .cue and .bin files for
# the CD music.
#
# A new install has no LiveArea bubble until VitaShell installs
# ux0:data/ignition/ignition.vpk once (or "Refresh LiveArea"); after
# that a push replaces the files in place.

set -e

TITLE=IGNT00001
VPK=build-vita/ignition.vpk
SHADERS=platforms/vita/shaders
: "${DATA:=$(ls -d data/*/disc 2>/dev/null | head -n 1)}"
: "${VITA_PORT:=1337}"

find_ux0() {
    if [ -n "$VITA_UX0" ]; then
        echo "$VITA_UX0"
        return
    fi
    for v in /Volumes/*; do
        if [ -d "$v/app" ] && [ -d "$v/data" ] && [ -f "$v/id.dat" ]; then
            echo "$v"
            return
        fi
    done
}

UX0=$(find_ux0)
if [ -z "$UX0" ] && [ -z "$VITA_IP" ]; then
    echo "vita_deploy: no Vita: mount its ux0 (memory card, or VitaShell's USB mode) or" >&2
    echo "  start VitaShell's FTP server and set VITA_IP (and VITA_PORT, default 1337)" >&2
    exit 1
fi
FTP="ftp://$VITA_IP:$VITA_PORT"

pull() {
    mkdir -p "$SHADERS"
    if [ -n "$UX0" ]; then
        for g in "$UX0"/data/ignition/shaders/*.gxp; do
            [ -f "$g" ] || continue
            n=$(basename "$g")
            if [ ! -f "$SHADERS/$n" ]; then
                cp "$g" "$SHADERS/$n"
                echo "vita_deploy: shader $n"
            fi
        done
    else
        for n in $(curl -s --list-only "$FTP/ux0:/data/ignition/shaders/" | grep '\.gxp$' || true); do
            if [ ! -f "$SHADERS/$n" ]; then
                curl -s -o "$SHADERS/$n" "$FTP/ux0:/data/ignition/shaders/$n"
                echo "vita_deploy: shader $n"
            fi
        done
    fi
}

# put FILE PATH: FILE to ux0:PATH.
put() {
    if [ -n "$UX0" ]; then
        mkdir -p "$(dirname "$UX0/$2")"
        cp "$1" "$UX0/$2"
    else
        curl -s --ftp-create-dirs -T "$1" "$FTP/ux0:/$2"
    fi
}

push() {
    tmp=$(mktemp -d)
    trap 'rm -rf "$tmp"' EXIT
    unzip -q "$VPK" -d "$tmp"
    (cd "$tmp" && find . -type f | sed 's|^\./||') | while read -r f; do
        put "$tmp/$f" "app/$TITLE/$f"
    done
    put "$VPK" data/ignition/ignition.vpk
    echo "vita_deploy: $VPK -> ux0:app/$TITLE${UX0:+ on $UX0}${VITA_IP:+ at $VITA_IP}"

    if [ -n "$UX0" ] && [ -d "$DATA" ] && [ ! -d "$UX0/data/ignition/disc" ]; then
        echo "vita_deploy: copying the disc to ux0:data/ignition/disc"
        mkdir -p "$UX0/data/ignition"
        cp -R "$DATA" "$UX0/data/ignition/disc"
    elif [ -z "$UX0" ]; then
        echo "vita_deploy: over FTP the disc is not copied; copy $DATA to ux0:data/ignition/disc once"
    fi
    if [ "$VITA_MUSIC" = 1 ]; then
        for f in "$(dirname "$DATA")"/*.cue "$(dirname "$DATA")"/*.bin; do
            [ -f "$f" ] && put "$f" "data/ignition/$(basename "$f")"
        done
    fi
    if [ -n "$UX0" ]; then
        sync
    fi
}

case "$1" in
pull) pull ;;
push) push ;;
*) echo "usage: $0 pull|push" >&2; exit 2 ;;
esac
