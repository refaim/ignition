#!/usr/bin/env python3
"""Writes src/race/data.c: the race's initialised tables, read from the
image of IGN_WIN.EXE (.ida-mcp/image.bin, from VA 0x479000).

Each table is (name, address, C type, count[, const]); s16, s32 and
double tables are read as such, charN as N byte strings.
"""

import pathlib
import struct

BASE = 0x479000
TABLES = [
    # The curves 0x411dc0 builds, four points each, three a car: for
    # player cars by car, and one set for the computer's.
    ("g_car_curves", 0x492568, "s32", 11 * 24, True),
    ("g_ai_curves", 0x492988, "s32", 24, True),
    ("g_car_4923b8", 0x4923B8, "s32", 11, True),
    # The computer's cars by difficulty, ten a row.
    ("g_ai_car_order", 0x4997E0, "s32", 40, True),
    ("g_car_499a90", 0x499A90, "s32", 11, True),
    ("g_car_499880", 0x499880, "s32", 44, True),
    ("g_car_codes", 0x497200, "char4", 11, True),
    # The computer drivers' manner, nine doubles a car (0x414e40): skill,
    # four weights, how far it looks for cars (written at load, 0x41988f),
    # two more, and a whole number.
    ("g_ai_params", 0x493960, "double", 11 * 9, False),
    # How many segments ahead the driver looks.
    ("g_ai_steps", 0x4937C8, "s16", 6, True),
    # Forks, four a track: the segment, the chance of the second branch
    # in percent, and the lane it keeps.
    ("g_ai_forks", 0x4937D8, "s16", 7 * 4 * 3, True),
    # Speed limits, eight a track: the segment (even) and km/h; 500
    # means full throttle, -999 an unused entry.
    ("g_ai_limits", 0x493880, "s16", 7 * 8 * 2, True),
    # The controls (0x442030..0x444e20), by car; the twelfth entry is
    # the computer's.
    ("g_boost_force", 0x4923E8, "s32", 12, True),
    ("g_steer_speed", 0x492418, "double", 12, True),
    ("g_steer_range", 0x492478, "double", 12, True),
    ("g_steer_rate", 0x4924D8, "s32", 12, True),
    ("g_boost_refill", 0x492508, "double", 12, True),
    # The gears' speed scale, by difficulty.
    ("g_gear_scale", 0x4929E8, "double", 4, True),
    # The grid cells 0x448c30 gathers: a disc as its first row and
    # column, then a count of cells and a step to the next row, until
    # -5000; and where the disc sits ahead of the camera, a dx, dz in
    # cells by the camera's pitch (36 rows) and yaw (36).
    ("g_cells_disc", 0x499FA0, "s32", 43, True),
    # The cameras over the finish, three a track: x, y, z, then pitch
    # and yaw in radians.
    ("g_finish_cams", 0x498538, "float", 7 * 3 * 5, True),
    # The sky's colour by track.
    ("g_sky_colour", 0x497ED8, "s32", 7, True),
    # The pause menu's words, seven a language: CONTINUE, RESTART, QUIT,
    # CD TRACK, an unused one, DEFAULT, RANDOM.
    ("g_pause_text", 0x494D58, "char30", 6 * 7, True),
    # The Y/N questions, by language.
    ("g_ask_restart", 0x495300, "char30", 6, True),
    ("g_ask_quit", 0x495248, "char30", 6, True),
    # The network's messages, by language.
    ("g_net_player_quit", 0x495BE8, "char150", 6, True),
    ("g_net_host_quit", 0x495C1A, "char150", 6, True),
    ("g_net_host_restart", 0x495C4C, "char150", 6, True),
    # The CD track each track plays by default.
    ("g_track_cd", 0x497EB8, "s32", 7, True),
    # Championship points by finishing place.
    ("g_place_points", 0x492A24, "s32", 12, True),
    ("g_cells_ahead", 0x49A058, "s32", 36 * 36 * 2, True),
    # Ground contact (0x424570): grip by surface type 0..29; a car's
    # traction by car, the twelfth the computer's; the grip's scale by
    # difficulty; the computer's grip, eleven a difficulty, indexed
    # through g_car_499a90.
    ("g_surface_grip", 0x4972D0, "double", 30, True),
    ("g_car_traction", 0x492358, "double", 12, True),
    ("g_grip_scale", 0x492A08, "double", 4, True),
    ("g_ai_grip", 0x499930, "double", 44, True),
    # A smoke puff's frames on its page: u0, v0, u1, v1 for nine.
    ("g_smoke_frames", 0x497FF8, "s32", 36, True),
    # The exhaust's puffs, the same way (0x444450).
    ("g_exhaust_frames", 0x498118, "s32", 36, True),
    # The race's sounds: each car's folder under CARS\, and the tyre
    # sound's base pitch and loudness by surface (0x444fa0, 0..8).
    ("g_car_dirs", 0x494A70, "char9", 11, True),
    ("g_tyre_pitch", 0x4974D0, "s32", 9, True),
    # The HUD by layout (small, large, high resolution): where its
    # sprites are cut from the panel pictures (x, y, w, h, 36 a layout),
    # the text rows and columns (8 a layout) and the sprites' places
    # (28 a layout), from the view's corners; the hint sign shown for a
    # piece's hint by difficulty (mirrored tracks swap left and right).
    ("g_hud_cuts", 0x4974F8, "s32", 3 * 144, True),
    ("g_hud_text", 0x497BC8, "s32", 3 * 8, True),
    ("g_hud_places", 0x497C28, "s32", 3 * 28, True),
    ("g_hud_signs", 0x497D74, "s32", 80, True),
    # The results screens' texts (0x438a60), one a language. Some sit
    # inside a longer record, so the stride is the record's.
    ("g_res_track_results", 0x495470, "char30", 6, True),
    ("g_res_track_score", 0x495528, "char30", 6, True),
    ("g_res_total_score", 0x4955E0, "char30", 6, True),
    ("g_res_play_again", 0x495698, "char45", 6, True),
    ("g_res_waiting", 0x4957A8, "char30", 6, True),
    ("g_res_press_return", 0x495860, "char50", 6, True),
    ("g_res_level_done", 0x495F70, "char100", 6, True),
    ("g_res_well_done", 0x4961C8, "char100", 6, True),
    ("g_res_sorry", 0x496420, "char100", 6, True),
    ("g_res_or_better", 0x496452, "char100", 6, True),
    ("g_res_quit", 0x494D94, "char210", 6, True),
    ("g_res_retry", 0x494DD0, "char210", 6, True),
    ("g_res_congrats", 0x4969A8, "char245", 6, True),
    ("g_res_completed", 0x4969CB, "char245", 6, True),
    ("g_res_championship", 0x4969EE, "char245", 6, True),
    ("g_res_first", 0x496A11, "char245", 6, True),
    ("g_res_second", 0x496A34, "char245", 6, True),
    ("g_res_third", 0x496A57, "char245", 6, True),
    ("g_res_score_of", 0x496A7A, "char245", 6, True),
    # The leagues, four a language (NOVICE .. ), and one more: the next
    # league is read one entry on.
    ("g_res_leagues", 0x496678, "char30", 25, True),
    ("g_res_pts", 0x496948, "char15", 6, True),
    ("g_res_must_achieve", 0x497058, "char70", 6, True),
    ("g_res_gold_statue", 0x49707B, "char70", 6, True),
    ("g_res_now_try", 0x496F68, "char40", 6, True),
    # The effects (0x434190): their frames, u0, v0, u1, v1 in texels, as
    # one run of tables (0x497ef8 flashes, 0x497f48 splash, 0x497ff8
    # smoke, 0x498118, 0x4981a8 fireball, 0x4982a8 fire, 0x4983a8,
    # 0x498428 halo), which an animation may run on from one into the
    # next.
    ("g_fx_frames", 0x497EF8, "s32", 340, True),
    # Which effect a full pool gives way to, by type.
    ("g_fx_priority", 0x4986E0, "s32", 12, True),
    # The lightning bolt's mesh (0x430300).
    ("g_lightning_mesh", 0x493CF0, "s32", 163, True),
    # An ease from 0 to 1 in 200 steps, times 200 (0x4358e0).
    ("g_ease", 0x4944E0, "s32", 201, True),
    # Ticks between the animated textures' frames, by track (0x434190).
    ("g_anim_ticks", 0x498480, "s32", 7, True),
    # The surfaces a landing raises dust from, by type 0..39 (0x4338d0).
    ("g_dusty", 0x497438, "u8", 40, True),
    # The skid marks' colour by surface 0..39, -1 none, and the skid
    # sound's base rate and volume by material 0..9 (0x42bc80).
    ("g_skid_colour", 0x4973C0, "s32", 40, True),
    ("g_skid_rate", 0x497458, "s32", 10, True),
    ("g_skid_volume", 0x497480, "s32", 10, True),
]


def c_string(raw):
    """The bytes as a C string body: the game's own code page, so bytes
    outside printable ASCII are written as octal escapes."""
    out = []
    for b in raw:
        if b in (0x22, 0x5C):
            out.append("\\" + chr(b))
        elif 0x20 <= b < 0x7F and b != 0x3F:
            out.append(chr(b))
        else:
            # '?' too, so no trigraph can form.
            out.append(f"\\{b:03o}")
    return "".join(out)


def main():
    root = pathlib.Path(__file__).resolve().parent.parent
    img = (root / ".ida-mcp" / "image.bin").read_bytes()
    out = ["/* Generated by tools/gen_race_data.py from IGN_WIN.EXE. */", "",
           '#include "race/data.h"', ""]
    hdr = ["/* Generated by tools/gen_race_data.py. */", "", "#ifndef IGN_RACE_DATA_H",
           "#define IGN_RACE_DATA_H", "", '#include "types.h"', ""]
    for name, addr, typ, count, const in TABLES:
        off = addr - BASE
        assert 0 <= off < len(img), name
        q = "const " if const else ""
        if typ == "s32":
            vals = struct.unpack_from(f"<{count}i", img, off)
            body = ", ".join(str(v) for v in vals)
            decl = f"{q}s32 {name}[{count}]"
        elif typ == "s16":
            vals = struct.unpack_from(f"<{count}h", img, off)
            body = ", ".join(str(v) for v in vals)
            decl = f"{q}s16 {name}[{count}]"
        elif typ == "double":
            vals = struct.unpack_from(f"<{count}d", img, off)
            body = ", ".join(repr(v) for v in vals)
            decl = f"{q}double {name}[{count}]"
        elif typ == "u8":
            vals = img[off:off + count]
            body = ", ".join(str(v) for v in vals)
            decl = f"{q}u8 {name}[{count}]"
        elif typ == "float":
            vals = struct.unpack_from(f"<{count}f", img, off)
            body = ", ".join(repr(v) + "f" for v in vals)
            decl = f"{q}float {name}[{count}]"
        else:
            # charN: strings N bytes apart.
            n = int(typ[4:])
            vals = [img[off + n * i:off + n * i + n].split(b"\0")[0] for i in range(count)]
            body = ", ".join(f'"{c_string(v)}"' for v in vals)
            decl = f"{q}char {name}[{count}][{n}]"
        out.append(f"/* {addr:#x} */")
        out.append(f"{decl} = {{ {body} }};")
        out.append("")
        hdr.append(f"extern {decl};  /* {addr:#x} */")
    hdr += ["", "#endif /* IGN_RACE_DATA_H */", ""]
    (root / "src" / "race" / "data.c").write_text("\n".join(out))
    (root / "src" / "race" / "data.h").write_text("\n".join(hdr))


if __name__ == "__main__":
    main()
