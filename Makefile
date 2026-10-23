# Ignition - reconstructed engine
#
# Native macOS build: Cocoa and OpenGL 3.2 core. The engine core is plain
# C99; only src/platform_macos.m is host specific. `make vita` builds the
# PlayStation Vita package (src/platform_vita.c, docs/vita.md).

TARGET := ignition
SRCDIR := src
BUILD  := build

CORE_SRC := $(SRCDIR)/app.c $(SRCDIR)/game.c $(SRCDIR)/stubs.c $(SRCDIR)/net.c $(SRCDIR)/canvas.c $(SRCDIR)/scene.c $(SRCDIR)/gl3d.c $(SRCDIR)/menucar.c $(SRCDIR)/menubg.c $(SRCDIR)/race_load.c $(SRCDIR)/race_track.c $(SRCDIR)/race_world.c $(SRCDIR)/race_cars.c $(SRCDIR)/phys.c $(SRCDIR)/race_phys.c $(SRCDIR)/anim_tex.c $(SRCDIR)/race_ai.c $(SRCDIR)/srf.c $(SRCDIR)/race_contact.c $(SRCDIR)/race_fx.c $(SRCDIR)/race_respawn.c $(SRCDIR)/race_camera.c $(SRCDIR)/race_view.c $(SRCDIR)/race_misc.c $(SRCDIR)/race_hud.c $(SRCDIR)/race_results.c $(SRCDIR)/race_sound.c $(SRCDIR)/race_control.c $(SRCDIR)/race_init.c $(SRCDIR)/race.c $(SRCDIR)/race_frame_stubs.c $(SRCDIR)/race_input.c $(SRCDIR)/race_data.c \
            $(SRCDIR)/lisa/lisa.c $(SRCDIR)/lisa/log.c $(SRCDIR)/lisa/mem.c $(SRCDIR)/lisa/file.c \
            $(SRCDIR)/lisa/exit.c $(SRCDIR)/lisa/gfx.c $(SRCDIR)/lisa/texture.c $(SRCDIR)/lisa/sprite.c \
            $(SRCDIR)/lisa/font.c $(SRCDIR)/startup.c $(SRCDIR)/input.c \
            $(SRCDIR)/menu_engine.c $(SRCDIR)/menu_data.c $(SRCDIR)/settings.c $(SRCDIR)/frontend.c $(SRCDIR)/frontend_state.c \
            $(SRCDIR)/menu_panels.c $(SRCDIR)/menu_pages.c $(SRCDIR)/audio.c $(SRCDIR)/cd.c $(SRCDIR)/sound.c $(SRCDIR)/cdp.c \
            $(SRCDIR)/video.c $(SRCDIR)/gfx_gl.c $(SRCDIR)/vfs.c $(SRCDIR)/main.c \
            $(SRCDIR)/cdimage.c
MAC_SRC  := $(SRCDIR)/platform_macos.m

CORE_OBJ := $(patsubst $(SRCDIR)/%.c,$(BUILD)/%.o,$(CORE_SRC))
HOST_OBJ := $(patsubst $(SRCDIR)/%.m,$(BUILD)/%.o,$(MAC_SRC))

CC        := clang
CFLAGS    := -std=c99 -D_POSIX_C_SOURCE=200809L -D_DARWIN_C_SOURCE -Wall -Wextra -Wno-unused-parameter -g -O1 -I$(SRCDIR)
OBJCFLAGS := -fobjc-arc -DGL_SILENCE_DEPRECATION -Wno-deprecated-declarations -Wall -Wextra -Wno-unused-parameter -g -O1 -I$(SRCDIR)
LDFLAGS   := -framework Cocoa -framework OpenGL -framework AudioUnit -framework AudioToolbox

.PHONY: all clean extract test run vita vita-clean

all: $(TARGET) cd_extract

$(TARGET): $(CORE_OBJ) $(HOST_OBJ)
	$(CC) $^ $(LDFLAGS) -o $@

$(BUILD)/%.o: $(SRCDIR)/%.c $(wildcard $(SRCDIR)/*.h $(SRCDIR)/*/*.h)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/%.o: $(SRCDIR)/%.m $(wildcard $(SRCDIR)/*.h)
	@mkdir -p $(dir $@)
	$(CC) $(OBJCFLAGS) -c $< -o $@

cd_extract: tools/cd_extract.c $(SRCDIR)/cdimage.c $(SRCDIR)/cdimage.h
	$(CC) $(CFLAGS) tools/cd_extract.c $(SRCDIR)/cdimage.c -o $@

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

# PlayStation Vita: build-vita/ignition.vpk. Needs VitaSDK ($VITASDK, or
# ~/vitasdk) with vitaShaRK, libmathneon, taihen and SceShaccCgExt from
# vdpm; vitaGL is built into build-vita/deps by tools/vita_deps.sh.
VITASDK ?= $(HOME)/vitasdk
VBUILD  := build-vita
JOBS    ?= $(shell sysctl -n hw.ncpu 2>/dev/null || echo 4)

vita:
	@test -f $(VBUILD)/deps/lib/libvitaGL.a || VITASDK=$(VITASDK) sh tools/vita_deps.sh $(VBUILD)/deps
	@test -f $(VBUILD)/CMakeCache.txt || VITASDK=$(VITASDK) cmake -S platforms/vita -B $(VBUILD) \
		-DVITA_DEPS=$(abspath $(VBUILD)/deps) >/dev/null
	@VITASDK=$(VITASDK) cmake --build $(VBUILD) -j $(JOBS)
	@echo "ignition: $(VBUILD)/ignition.vpk"

vita-clean:
	rm -rf $(VBUILD)

clean:
	rm -rf $(BUILD) $(TARGET) cd_extract test_lisa test_menu test_settings test_sound *.dSYM
