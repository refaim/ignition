"""Generate src/menu_data.c from IGN_WIN.EXE's front end tables.

The tables are the original's data (.ida-mcp/image.bin from
tools/ida_image.py) with the writes of the static initialiser at
0x404a60 applied (tools/x86_stores.py, saved to
.ida-mcp/menu_writes.json). See NOTES.md, "Front end", for the records.

    python3 -I tools/x86_stores.py .ida-mcp/asm/404a60_*.asm 404a60 409434 \\
        > .ida-mcp/menu_writes.json
    python3 -I tools/gen_menu_data.py

Each record is named by its original address. Pointers into the
settings block become offsets into g_settings; pointers to code become
the C functions named in FUNCS.
"""
import json
import os
import struct
import sys

ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..")
IMAGE = os.path.join(ROOT, ".ida-mcp", "image.bin")
WRITES = os.path.join(ROOT, ".ida-mcp", "menu_writes.json")
OUT = os.path.join(ROOT, "src", "menu_data.c")
BASE = 0x479000

LANG_LISTS = [0x481880, 0x4847e8, 0x487750, 0x48a6b8, 0x48d620, 0x490588]
LIST_SLOTS = 119             # codes 0..118 in the tree
# Each language's 25 pages (0x490788, 6 x 25 pointers). 0x404a60 copies
# them to 0x4be8c8; choosing a language copies its row to 0x4be768.
PAGE_TABLES = 0x490788
# Language 0's row, as 0x404a60 also stores it at 0x4be768.
PAGE_TABLE = [0x481850, 0x481838, 0x481808, 0x4817d8, 0x4817c0, 0x4817a8,
              0x481790, 0x4817f0, 0x481778, 0x481718, 0x481730, 0x481748,
              0x481760, 0x4816d0, 0x4816e8, 0x481700, 0x481820, 0x481670,
              0x481688, 0x4816a0, 0x4816b8, 0x481868, 0x481628, 0x481640,
              0x481658]
TREE, TREE_ENTRIES = 0x4909e0, 25
CALLBACKS, CALLBACK_COUNT = 0x47df70, 5
SETTINGS, SETTINGS_END = 0x4bf580, 0x4bfb18
PAGE_STYLE = 0x47dd70        # the key map every page uses
DEFAULT_KEYMAP = 0x4931c8    # for a page without one
TEXT_CHARS = 0x4932c8        # scan code to character, codes 0..0x53
BINDABLE = 0x47de70          # 1 for keys a binding may take
# Runs of string pointers the front end indexes by address arithmetic.
TEXT_RUNS = [("g_ui_text", 0x47dfe8, 0x47e4dc), ("g_ui_names", 0x47e560, 0x47e58c),
             ("g_key_names", 0x493320, 0x493720)]
KEYMAP_ORIG = 0x47dc70       # a copy of the key map, to undo player keys
TRACK_PIC_OFFSETS = 0x47dfc8  # 8 offsets into trk_spr.pic, by selection
IDLE_SEQ, IDLE_SEQ_END = 0x47e4a0, 0x47e570  # idle panels, 4 s16 each; steps -8..17
SETTINGS_BLOCK = 0x47e590    # 0x348 bytes copied to settings offset 0x1bf
SETTINGS_BLOCK_SIZE = 0x348
FUNCS = {0x409fb0: "menu_page_handler_a", 0x409610: "menu_page_handler_b",
         0x4045b0: "menu_cb_4045b0", 0x4045e0: "menu_cb_4045e0",
         0x404610: "menu_cb_404610", 0x404740: "menu_cb_404740",
         0x404790: "menu_cb_404790"}

img = bytearray(open(IMAGE, "rb").read())
for k, v in json.load(open(WRITES))["writes"].items():
    a = int(k, 16)
    if BASE <= a < BASE + len(img):
        img[a - BASE] = v


def check(a, n):
    if not BASE <= a and a + n <= BASE + len(img):
        sys.exit("0x%x is outside the image" % a)
    return a - BASE


def u32(a):
    return struct.unpack_from("<I", img, check(a, 4))[0]


def s32(a):
    return struct.unpack_from("<i", img, check(a, 4))[0]


def s16(a):
    return struct.unpack_from("<h", img, check(a, 2))[0]


def cstring(a):
    raw = bytes(img[a - BASE:a - BASE + 256]).split(b"\0")[0]
    out = ""
    for c in raw:
        ch = chr(c)
        if ch in "\\\"":
            out += "\\" + ch
        elif 32 <= c < 127:
            out += ch
        else:
            out += "\\%03o" % c
    return '"%s"' % out


def settings_ref(a, ctype):
    if not a:
        return "NULL"
    if not SETTINGS <= a < SETTINGS_END:
        sys.exit("pointer 0x%x outside the settings" % a)
    return "(%s *)(g_settings + 0x%x)" % (ctype, a - SETTINGS)


ROOT_PAGE = 0x481868     # the language choice, in language 0 only


def page_copy(page, lang, offsets):
    """Language `lang`'s copy of language 0's `page`."""
    return page if page == ROOT_PAGE else page + offsets[lang]


def main():
    pages, item_arrays, visual_blocks, choices = {}, {}, {}, {}
    for lst in LANG_LISTS:
        for i in range(LIST_SLOTS):
            p = u32(lst + 4 * i)
            if p:
                pages[p] = True
    table = [[u32(PAGE_TABLES + 4 * (25 * lang + k)) for k in range(25)]
             for lang in range(len(LANG_LISTS))]
    if table[0] != PAGE_TABLE:
        sys.exit("0x%x does not start with 0x4be768's pages" % PAGE_TABLES)
    offsets = [u32(lst + 4) - PAGE_TABLE[0] for lst in LANG_LISTS]
    for lang in range(len(LANG_LISTS)):
        for k, p in enumerate(PAGE_TABLE):
            if table[lang][k] != page_copy(p, lang, offsets):
                print("note: language %d page %d is 0x%x, not 0x%x" % (
                    lang, k, table[lang][k], page_copy(p, lang, offsets)))
        for q in table[lang]:
            if u32(q) not in FUNCS or u32(q + 4) not in FUNCS:
                sys.exit("no page at 0x%x" % q)
            pages[q] = True
    for p in pages:
        n, items = u32(p + 0xc), u32(p + 0x10)
        item_arrays[items] = max(item_arrays.get(items, 0), n)
        for k in range(n):
            it = items + 0x1c * k
            if u32(it + 0xc):
                visual_blocks[u32(it + 0xc)] = it
            if u32(it + 0x16):
                choices[u32(it + 0x16)] = True
    for c in choices:
        if u32(c):
            visual_blocks[u32(c)] = c
    starts = sorted(set(pages) | set(item_arrays) | set(visual_blocks)
                    | set(choices) | set(LANG_LISTS) | {TREE, PAGE_STYLE})
    sizes = {}
    for v in visual_blocks:
        nxt = min(s for s in starts if s > v)
        sizes[v] = min((nxt - v) // 0x18, 64)
    for v, owner in visual_blocks.items():
        if owner in choices:
            continue
        lo, hi = s32(owner + 4), s32(owner + 8)
        need = 2 + 2 * (hi - lo + 1) if u32(owner) and lo <= hi else 2
        if need > sizes[v]:
            print("warning: visuals 0x%x: %d needed, %d room" % (v, need, sizes[v]))

    o = []
    w = o.append
    w("/*\n * menu_data.c - the front end's tables, from IGN_WIN.EXE.\n *\n")
    w(" * Generated by tools/gen_menu_data.py; do not edit. Each record is\n")
    w(" * named by its original address. See NOTES.md, \"Front end\".\n */\n\n")
    w('#include <stddef.h>\n\n#include "menu.h"\n\n')
    for v in sorted(visual_blocks):
        w("static struct menu_visual s_visual_%x[%d];\n" % (v, sizes[v]))
    for c in sorted(choices):
        w("static struct menu_choice s_choice_%x;\n" % c)
    for a in sorted(item_arrays):
        w("static struct menu_item s_items_%x[%d];\n" % (a, item_arrays[a]))
    w("\n")
    for v in sorted(visual_blocks):
        w("static struct menu_visual s_visual_%x[%d] = {\n" % (v, sizes[v]))
        for j in range(sizes[v]):
            e = v + 0x18 * j
            text = u32(e + 0xe)
            w("    { NULL, %d, %d, %d, %d, %d, %s, %d, %d, %d },\n" % (
                s16(e + 4), s16(e + 6), s16(e + 8), s16(e + 0xa), s16(e + 0xc),
                cstring(text) if text else "NULL",
                s16(e + 0x12), s16(e + 0x14), s16(e + 0x16)))
        w("};\n\n")
    for c in sorted(choices):
        vis = u32(c)
        w("static struct menu_choice s_choice_%x = {\n" % c)
        w("    %s, %s,\n" % ("s_visual_%x" % vis if vis else "NULL",
                            settings_ref(u32(c + 4), "u8")))
        w("    { %s }\n};\n\n" % ", ".join(str(s16(c + 8 + 2 * k)) for k in range(8)))
    for a in sorted(item_arrays):
        w("static struct menu_item s_items_%x[%d] = {\n" % (a, item_arrays[a]))
        for k in range(item_arrays[a]):
            it = a + 0x1c * k
            vis, ch = u32(it + 0xc), u32(it + 0x16)
            w("    { %s, %d, %d, %s, %d, %d, %d, %s, %d },\n" % (
                settings_ref(u32(it), "s32"), s32(it + 4), s32(it + 8),
                "s_visual_%x" % vis if vis else "NULL", s16(it + 0x10), s16(it + 0x12),
                s16(it + 0x14), "&s_choice_%x" % ch if ch else "NULL", s16(it + 0x1a)))
        w("};\n\n")
    for name, a in (("g_menu_keymap", PAGE_STYLE), ("g_menu_keymap_default", DEFAULT_KEYMAP)):
        vals = [struct.unpack_from("<b", img, a + k - BASE)[0] for k in range(256)]
        w("%s %s[256] = {\n" % ("s8" if a == PAGE_STYLE else "const s8", name))
        for k in range(0, 256, 16):
            w("    %s,\n" % ", ".join("%d" % v for v in vals[k:k + 16]))
        w("};\n\n")
    w("const u8 g_menu_text_chars[0x54] = {\n")
    for k in range(0, 0x54, 12):
        w("    %s,\n" % ", ".join("0x%02x" % img[TEXT_CHARS + j - BASE] for j in range(k, min(k + 12, 0x54))))
    w("};\n\n")
    for name, lo, hi in TEXT_RUNS:
        w("const char *const %s[%d] = {    /* 0x%x */\n" % (name, (hi - lo) // 4, lo))
        for a in range(lo, hi, 4):
            p = u32(a)
            w("    %s,\n" % (cstring(p) if p else "NULL"))
        w("};\n\n")
    vals = [struct.unpack_from("<b", img, check(KEYMAP_ORIG + k, 1))[0] for k in range(256)]
    w("const s8 g_menu_keymap_orig[256] = {\n")
    for k in range(0, 256, 16):
        w("    %s,\n" % ", ".join("%d" % v for v in vals[k:k + 16]))
    w("};\n\n")
    w("const s32 g_track_pic_offsets[8] = {\n    %s\n};\n\n" % ", ".join(
        "0x%x" % u32(TRACK_PIC_OFFSETS + 4 * k) for k in range(8)))
    n = (IDLE_SEQ_END - IDLE_SEQ) // 2
    w("const s16 g_idle_seq_raw[%d] = {    /* 0x%x */\n" % (n, IDLE_SEQ))
    for k in range(0, n, 16):
        w("    %s,\n" % ", ".join("%d" % s16(IDLE_SEQ + 2 * j) for j in range(k, min(k + 16, n))))
    w("};\n\n")
    w("const u8 g_settings_block_1bf[0x%x] = {\n" % SETTINGS_BLOCK_SIZE)
    for k in range(0, SETTINGS_BLOCK_SIZE, 16):
        w("    %s,\n" % ", ".join("0x%02x" % img[SETTINGS_BLOCK + j - BASE]
                                   for j in range(k, min(k + 16, SETTINGS_BLOCK_SIZE))))
    w("};\n\n")
    w("const u8 g_menu_bindable[256] = {\n")
    for k in range(0, 256, 16):
        w("    %s,\n" % ", ".join("%d" % img[BINDABLE + j - BASE] for j in range(k, k + 16)))
    w("};\n\n")
    for p in sorted(pages):
        fa, fb, style = u32(p), u32(p + 4), u32(p + 8)
        w("static struct menu_page s_page_%x = {\n" % p)
        w("    %s, %s, %s, %d, s_items_%x, %d\n};\n\n" % (
            FUNCS[fa], FUNCS[fb], "g_menu_keymap" if style == PAGE_STYLE else "NULL",
            u32(p + 0xc), u32(p + 0x10), s32(p + 0x14)))
    w("struct menu_page *const g_menu_pages[MENU_LANGUAGES][MENU_PAGE_SLOTS] = {\n")
    for lst in LANG_LISTS:
        refs = ["&s_page_%x" % u32(lst + 4 * i) if u32(lst + 4 * i) else "NULL"
                for i in range(LIST_SLOTS)]
        w("    {\n")
        for i in range(0, LIST_SLOTS, 4):
            w("        %s,\n" % ", ".join(refs[i:i + 4]))
        w("    },\n")
    w("};\n\n")
    w("struct menu_page *const g_menu_page_table[MENU_LANGUAGES][MENU_PAGE_TABLE] = {\n")
    for lang in range(len(LANG_LISTS)):
        refs = ["&s_page_%x" % q for q in table[lang]]
        w("    {\n")
        for i in range(0, len(refs), 4):
            w("        %s,\n" % ", ".join(refs[i:i + 4]))
        w("    },\n")
    w("};\n\n")
    w("const s16 g_menu_tree[MENU_TREE_ENTRIES][32] = {\n")
    for i in range(TREE_ENTRIES):
        e = TREE + 0x40 * i
        w("    { %s },\n" % ", ".join(str(s16(e + 2 * k)) for k in range(32)))
    w("};\n\n")
    w("menu_callback const g_menu_callbacks[MENU_CALLBACKS] = {\n    %s\n};\n" % ", ".join(
        FUNCS[u32(CALLBACKS + 4 * k)] for k in range(CALLBACK_COUNT)))
    open(OUT, "w").write("".join(o))
    print("%d pages, %d item arrays, %d visual blocks, %d choices" % (
        len(pages), len(item_arrays), len(visual_blocks), len(choices)))


if __name__ == "__main__":
    main()
