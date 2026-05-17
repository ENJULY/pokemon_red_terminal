#!/usr/bin/env python3
# coding: utf-8
"""TILE_WALL을 단순 벽돌 패턴으로 교체 (트리 모양 제거)"""
import re

PALETTE = (255, 250, 244, 232)
ESC = chr(0x1b)

def cell_solid(color):
    return ESC + "[48;5;" + str(color) + "m " + ESC + "[0m"

def cell_split(top, bot):
    if top == bot:
        return cell_solid(top)
    return ESC + "[38;5;" + str(top) + ";48;5;" + str(bot) + "m" + chr(0xe2) + chr(0x96) + chr(0x80) + ESC + "[0m"

def render_pixels(get_color):
    rows = []
    for hy in range(8):
        cells = []
        for x in range(16):
            top = get_color(hy*2, x)
            bot = get_color(hy*2+1, x)
            cells.append(cell_split(PALETTE[top], PALETTE[bot]))
        rows.append("".join(cells))
    return rows

def brick_wall(r, c):
    # 단순 벽돌: 가로 모르타르 + 엇갈린 세로 이음매
    if r in (0, 15): return 3
    if r in (7, 8): return 3
    brick_row = r // 8
    col_offset = 0 if brick_row == 0 else 4
    if (c + col_offset) % 8 == 0: return 3
    if r in (1, 9): return 1  # brick top highlight
    return 2

rows = render_pixels(brick_wall)

# C++ 소스 escape: 0x1b → "\x1b", 0xe2 → "\xe2" 등
def to_cpp(s):
    out = ""
    for ch in s:
        cp = ord(ch)
        if cp < 0x20 or cp >= 0x7f:
            out += "\\x" + format(cp, "02x")
        else:
            out += ch
    return out

cpp_rows = [to_cpp(r) for r in rows]
new_block = "static const TileArt TILE_WALL = {{\n"
for r in cpp_rows:
    new_block += '    "' + r + '",\n'
new_block += "}, '#'};"

TILES_H = r"C:\Users\han06\Downloads\pokemon\pokemon_red_terminal-main\src\data\tiles.h"
with open(TILES_H, "r", encoding="utf-8") as f:
    src = f.read()

pattern = re.compile(r"static const TileArt TILE_WALL = \{\{[^}]+\}, '#'\};", re.S)
m = pattern.search(src)
if not m:
    print("TILE_WALL pattern not found - check existing definition")
else:
    src_new = src[:m.start()] + new_block + src[m.end():]
    with open(TILES_H, "w", encoding="utf-8") as f:
        f.write(src_new)
    print("TILE_WALL replaced. old=%d new=%d" % (m.end()-m.start(), len(new_block)))
