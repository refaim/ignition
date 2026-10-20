# Ignition - reconstructed engine
#
# Native macOS build: Cocoa and OpenGL 3.2 core. The engine core is plain
# C99; only src/platform_macos.m is host specific.

TARGET := ignition
SRCDIR := src
BUILD  := build

CORE_SRC := $(SRCDIR)/app.c $(SRCDIR)/game.c $(SRCDIR)/stubs.c \
            $(SRCDIR)/lisa/lisa.c $(SRCDIR)/lisa/log.c $(SRCDIR)/lisa/mem.c $(SRCDIR)/lisa/file.c \
            $(SRCDIR)/lisa/exit.c $(SRCDIR)/lisa/gfx.c $(SRCDIR)/lisa/texture.c $(SRCDIR)/lisa/sprite.c \
            $(SRCDIR)/lisa/font.c $(SRCDIR)/startup.c $(SRCDIR)/input.c \
            $(SRCDIR)/menu_engine.c $(SRCDIR)/menu_data.c $(SRCDIR)/settings.c \
            $(SRCDIR)/video.c $(SRCDIR)/gfx_gl.c $(SRCDIR)/vfs.c $(SRCDIR)/main.c \
            $(SRCDIR)/cdimage.c
MAC_SRC  := $(SRCDIR)/platform_macos.m

CORE_OBJ := $(patsubst $(SRCDIR)/%.c,$(BUILD)/%.o,$(CORE_SRC))
HOST_OBJ := $(patsubst $(SRCDIR)/%.m,$(BUILD)/%.o,$(MAC_SRC))

CC        := clang
CFLAGS    := -std=c99 -D_POSIX_C_SOURCE=200809L -D_DARWIN_C_SOURCE -Wall -Wextra -Wno-unused-parameter -g -O1 -I$(SRCDIR)
OBJCFLAGS := -fobjc-arc -DGL_SILENCE_DEPRECATION -Wno-deprecated-declarations -Wall -Wextra -Wno-unused-parameter -g -O1 -I$(SRCDIR)
LDFLAGS   := -framework Cocoa -framework OpenGL

.PHONY: all clean extract test run

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

test: test_lisa test_menu test_settings
	./test_lisa $(BUILD)
	./test_menu
	./test_settings

# Extracts each disc image in data/ into data/<image name>/disc/.
extract: cd_extract
	@for cue in data/*/*.cue; do \
		./cd_extract "$$cue" "$$(dirname "$$cue")/disc" || exit 1; \
	done

run: $(TARGET)
	./$(TARGET)

clean:
	rm -rf $(BUILD) $(TARGET) cd_extract test_lisa test_menu test_settings *.dSYM
