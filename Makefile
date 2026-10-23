# Ignition - reconstructed engine
#
# The engine core is plain C99; each host is one native file behind
# src/platform.h, and every host but the Vita's draws with OpenGL
# through src/rnd_gl.c:
#
#   make                 the native build: macOS (src/platform_macos.m,
#                        Cocoa) or Linux (src/platform_linux.c, X11 + GLX)
#   make linux           Linux, also the Steam Deck build: build-linux/
#   make windows         Windows x86_64 (src/platform_windows.c, Win32 + WGL),
#                        cross-built with MinGW-w64: build-windows/
#   make web             WebAssembly and WebGL 2 with Emscripten
#                        (src/platform_web.c): build-web/dist/
#   make vita            PlayStation Vita (src/platform_vita.c,
#                        src/rnd_gxm.c, docs/vita.md)
#
# No third-party libraries: only each system's own APIs, and Khronos'
# OpenGL headers in src/third_party/. VERSION holds this port's
# version; `make VERSION=v1.2.3` overrides it (CI passes the release
# tag). docs/platforms.md covers each platform.

.DEFAULT_GOAL := all

TARGET := ignition
SRCDIR := src
BUILD  := build
CD_EXTRACT ?= cd_extract

VERSION ?= $(shell cat VERSION)
IGN_VERSION := $(patsubst v%,%,$(VERSION))

UNAME := $(shell uname -s)
ifeq ($(UNAME),Darwin)
HOST ?= macos
else
HOST ?= linux
endif

CORE_SRC := $(SRCDIR)/app.c $(SRCDIR)/game.c $(SRCDIR)/stubs.c $(SRCDIR)/net.c $(SRCDIR)/canvas.c $(SRCDIR)/scene.c $(SRCDIR)/gl3d.c $(SRCDIR)/menucar.c $(SRCDIR)/menubg.c $(SRCDIR)/race_load.c $(SRCDIR)/race_track.c $(SRCDIR)/race_world.c $(SRCDIR)/race_cars.c $(SRCDIR)/phys.c $(SRCDIR)/race_phys.c $(SRCDIR)/anim_tex.c $(SRCDIR)/race_ai.c $(SRCDIR)/srf.c $(SRCDIR)/race_contact.c $(SRCDIR)/race_fx.c $(SRCDIR)/race_respawn.c $(SRCDIR)/race_camera.c $(SRCDIR)/race_view.c $(SRCDIR)/race_misc.c $(SRCDIR)/race_hud.c $(SRCDIR)/race_results.c $(SRCDIR)/race_sound.c $(SRCDIR)/race_control.c $(SRCDIR)/race_init.c $(SRCDIR)/race.c $(SRCDIR)/race_frame_stubs.c $(SRCDIR)/race_input.c $(SRCDIR)/race_data.c \
            $(SRCDIR)/lisa/lisa.c $(SRCDIR)/lisa/log.c $(SRCDIR)/lisa/mem.c $(SRCDIR)/lisa/file.c \
            $(SRCDIR)/lisa/exit.c $(SRCDIR)/lisa/gfx.c $(SRCDIR)/lisa/texture.c $(SRCDIR)/lisa/sprite.c \
            $(SRCDIR)/lisa/font.c $(SRCDIR)/startup.c $(SRCDIR)/input.c \
            $(SRCDIR)/menu_engine.c $(SRCDIR)/menu_data.c $(SRCDIR)/settings.c $(SRCDIR)/frontend.c $(SRCDIR)/frontend_state.c \
            $(SRCDIR)/menu_panels.c $(SRCDIR)/menu_pages.c $(SRCDIR)/audio.c $(SRCDIR)/cd.c $(SRCDIR)/sound.c $(SRCDIR)/cdp.c \
            $(SRCDIR)/video.c $(SRCDIR)/gfx.c $(SRCDIR)/rnd_gl.c $(SRCDIR)/vfs.c $(SRCDIR)/main.c \
            $(SRCDIR)/cdimage.c

WARN := -Wall -Wextra -Wno-unused-parameter

ifeq ($(HOST),macos)
# MACOS_ARCH="-arch arm64 -arch x86_64" makes a universal binary (CI).
MAC_SRC  := $(SRCDIR)/platform_macos.m
HOST_C   := $(SRCDIR)/platform_common.c

CC        := clang
CFLAGS    := -std=c99 -D_POSIX_C_SOURCE=200809L -D_DARWIN_C_SOURCE $(WARN) -g -O1 -I$(SRCDIR) $(MACOS_ARCH)
OBJCFLAGS := -fobjc-arc -DGL_SILENCE_DEPRECATION -Wno-deprecated-declarations $(WARN) -g -O1 -I$(SRCDIR) $(MACOS_ARCH)
LDFLAGS   := -framework Cocoa -framework OpenGL -framework AudioUnit -framework AudioToolbox $(MACOS_ARCH)
TOOL_CFLAGS := $(CFLAGS)
else
# Linux, Windows and the web. -fsigned-char: the game is x86 code, where
# char is signed (arm64 Linux). GCC also warns where snprintf may
# truncate, which the game means.
MAC_SRC  :=
OPT      ?= -O2
TOOL_CFLAGS := -std=c99 -D_POSIX_C_SOURCE=200809L -D_DEFAULT_SOURCE -fsigned-char $(WARN) $(OPT) -I$(SRCDIR)
ifneq ($(shell $(CC) --version 2>/dev/null | grep -c "Free Software Foundation"),0)
TOOL_CFLAGS += -Wno-format-truncation -Wno-format-overflow
endif
CFLAGS   := $(TOOL_CFLAGS) -g -I$(SRCDIR)/third_party

ifeq ($(HOST),linux)
HOST_C   := $(SRCDIR)/platform_linux.c $(SRCDIR)/platform_common.c
LDFLAGS  := -lX11 -lGL -lasound -lpthread -lm
endif
ifeq ($(HOST),windows)
HOST_C   := $(SRCDIR)/platform_windows.c $(SRCDIR)/platform_common.c
LDFLAGS  := -mwindows -static -lopengl32 -lgdi32 -lwinmm -luser32
endif
ifeq ($(HOST),web)
HOST_C   := $(SRCDIR)/platform_web.c $(SRCDIR)/platform_common.c $(SRCDIR)/platform_touch.c
CFLAGS   += -DIGN_GLES
LDFLAGS  := $(WEB_LDFLAGS)
endif
endif

HOST_OBJ := $(patsubst $(SRCDIR)/%.m,$(BUILD)/%.o,$(MAC_SRC)) $(patsubst $(SRCDIR)/%.c,$(BUILD)/%.o,$(HOST_C))
CORE_OBJ := $(patsubst $(SRCDIR)/%.c,$(BUILD)/%.o,$(CORE_SRC))

# The version only reaches the files that print it.
$(BUILD)/main.o $(BUILD)/startup.o: CFLAGS += -DIGN_VERSION='"$(IGN_VERSION)"'
$(BUILD)/main.o $(BUILD)/startup.o: VERSION

.PHONY: all clean extract test run vita vita-deploy vita-clean linux linux-clean windows \
        windows-clean web web-serve web-clean

all: $(TARGET) $(CD_EXTRACT)

$(TARGET): $(CORE_OBJ) $(HOST_OBJ)
	$(CC) $^ $(LDFLAGS) -o $@

$(BUILD)/%.o: $(SRCDIR)/%.c $(wildcard $(SRCDIR)/*.h $(SRCDIR)/*/*.h)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/%.o: $(SRCDIR)/%.m $(wildcard $(SRCDIR)/*.h)
	@mkdir -p $(dir $@)
	$(CC) $(OBJCFLAGS) -c $< -o $@

$(CD_EXTRACT): tools/cd_extract.c $(SRCDIR)/cdimage.c $(SRCDIR)/cdimage.h
	$(CC) $(TOOL_CFLAGS) tools/cd_extract.c $(SRCDIR)/cdimage.c -o $@

# The engine without main.c, for tests.
TEST_OBJ := $(filter-out $(BUILD)/main.o,$(CORE_OBJ)) $(HOST_OBJ)

test_lisa: tests/test_lisa.c $(TEST_OBJ)
	$(CC) $(CFLAGS) $^ $(LDFLAGS) -o $@

test_menu: tests/test_menu.c $(TEST_OBJ)
	$(CC) $(CFLAGS) $^ $(LDFLAGS) -o $@

test_settings: tests/test_settings.c $(TEST_OBJ)
	$(CC) $(CFLAGS) -Itests $^ $(LDFLAGS) -o $@

test_sound: tests/test_sound.c $(TEST_OBJ)
	$(CC) $(CFLAGS) $^ $(LDFLAGS) -o $@

test: test_lisa test_menu test_settings test_sound
	./test_lisa $(BUILD)
	./test_menu
	./test_settings
	./test_sound

# Extracts each disc image in data/ into data/<image name>/disc/.
extract: cd_extract
	@for cue in data/*/*.cue; do \
		./cd_extract "$$cue" "$$(dirname "$$cue")/disc" || exit 1; \
	done

run: $(TARGET)
	./$(TARGET)

# ---------------------------------------------------------------- other hosts
#
# Linux, Windows and the web run this Makefile again with their HOST and
# build folder. docs/platforms.md has the details.

NPROC ?= $(shell sysctl -n hw.ncpu 2>/dev/null || nproc 2>/dev/null || echo 4)

# Linux, and the Steam Deck: build-linux/ignition. Needs the X11, OpenGL
# and ALSA development files (libx11-dev libgl-dev libasound2-dev).
linux:
	$(MAKE) -j$(NPROC) HOST=linux BUILD=build-linux/obj TARGET=build-linux/ignition \
		CD_EXTRACT=build-linux/cd_extract all

linux-clean:
	rm -rf build-linux

# Windows x86_64: build-windows/ignition.exe, cross-compiled with
# MinGW-w64 (x86_64-w64-mingw32-gcc). MINGW= uses a native gcc (MSYS2).
MINGW ?= x86_64-w64-mingw32-

windows:
	$(MAKE) -j$(NPROC) HOST=windows CC=$(MINGW)gcc BUILD=build-windows/obj \
		TARGET=build-windows/ignition.exe CD_EXTRACT=build-windows/cd_extract.exe all

windows-clean:
	rm -rf build-windows

# The web: build-web/dist/, a static page (index.html, ignition.js,
# ignition.wasm). Needs Emscripten (emcc on PATH). WebGL 2 and Web Audio
# through Emscripten's own APIs; Asyncify lets the game's loops wait for
# the browser.
WEB_DIST := build-web/dist
WEB_LDFLAGS := -sMIN_WEBGL_VERSION=2 -sMAX_WEBGL_VERSION=2 -sASYNCIFY \
               -sASYNCIFY_STACK_SIZE=262144 -sSTACK_SIZE=2MB -sINITIAL_MEMORY=256MB \
               -sALLOW_MEMORY_GROWTH=1 -sINVOKE_RUN=0 -sEXIT_RUNTIME=0 -sENVIRONMENT=web \
               -sEXPORTED_RUNTIME_METHODS=callMain,FS -sEXPORTED_FUNCTIONS=_main,_web_audio_fill \
               -lidbfs.js

web:
	@command -v emcc >/dev/null || { echo "Emscripten not found: install emsdk and source emsdk_env.sh."; exit 1; }
	@mkdir -p $(WEB_DIST)
	$(MAKE) -j$(NPROC) HOST=web CC=emcc BUILD=build-web/obj TARGET=$(WEB_DIST)/ignition.js \
		WEB_LDFLAGS="$(WEB_LDFLAGS)" $(WEB_DIST)/ignition.js
	cp platforms/web/index.html $(WEB_DIST)/index.html

web-serve: web
	python3 -m http.server -d $(WEB_DIST) 8080

web-clean:
	rm -rf build-web

# PlayStation Vita: build-vita/ignition.vpk. Needs VitaSDK ($VITASDK, or
# ~/vitasdk), nothing else. The renderer is src/rnd_gxm.c.
VITASDK ?= $(HOME)/vitasdk
VBUILD  := build-vita
JOBS    ?= $(shell sysctl -n hw.ncpu 2>/dev/null || echo 4)

vita:
	@test -f $(VBUILD)/CMakeCache.txt || VITASDK=$(VITASDK) cmake -S platforms/vita -B $(VBUILD) >/dev/null
	@VITASDK=$(VITASDK) cmake --build $(VBUILD) -j $(JOBS)
	@echo "ignition: $(VBUILD)/ignition.vpk"

# To the console (tools/vita_deploy.sh): the shaders it compiled come
# back into platforms/vita/shaders/ first, so the package carries them.
vita-deploy:
	@sh tools/vita_deploy.sh pull
	@$(MAKE) --no-print-directory vita
	@sh tools/vita_deploy.sh push

vita-clean:
	rm -rf $(VBUILD)

clean:
	rm -rf $(BUILD) $(TARGET) cd_extract test_lisa test_menu test_settings test_sound *.dSYM
