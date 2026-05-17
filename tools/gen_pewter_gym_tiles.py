#!/usr/bin/env python3
# coding: utf-8
"""
gen_pewter_gym_tiles.py — Pewter Gym 디코더 (5x7 blocks → 10x14 steps)
pokered: maps/PewterGym.blk + gfx/blocksets/gym.bst + gfx/tilesets/gym.png
Output: src/data/tiles.h (TILE_PG_*) + src/data/map_data.h (MAP_8 tiles[])
"""
import os, re, sys
from PIL import Image

ROOT    = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
POKERED = os.path.abspath(os.path.join(ROOT, '..', 'pokered'))
TILES_H = os.path.join(ROOT, 'src', 'data', 'tiles.h')
MAP_H   = os.path.join(ROOT, 'src', 'data', 'map_data.h')

GYM_PASS = {0x11, 0x16, 0x19, 0x2b, 0x3c, 0x3d, 0x3f, 0x4a, 0x4c, 0x4d, 0x03}
WARP_OVERRIDE = [
    (4, 13, '?'),  # 도어 좌
    (5, 13, '?'),  # 도어 우
]

# ─── ANSI rendering ─────────────────────────────────────────
def classify_px(v):
    if v > 200: return 0
    if v > 130: return 1
    if v > 60:  return 2
    return 3
PALETTE = (255, 250, 244, 232)
def cell_ansi(top, bot):
    if top == bot:
        return '\\x1b[48;5;{}m \\x1b[0m'.format(PALETTE[top])
    return '\\x1b[38;5;{};48;5;{}m\\xe2\\x96\\x80\\x1b[0m'.format(PALETTE[top], PALETTE[bot])

def get_atomic(png, idx):
    cols = png.size[0] // 8
    r, c = idx // cols, idx % cols
    return png.crop((c*8, r*8, (c+1)*8, (r+1)*8))

def step_atomics(blk, bst, blk_w, sx, sy):
    bx, by = sx // 2, sy // 2
    qx, qy = sx % 2, sy % 2
    block_id = blk[by * blk_w + bx]
    a = bst[block_id*16:(block_id+1)*16]
    base_y = qy * 2; base_x = qx * 2
    return (a[base_y*4 + base_x],     a[base_y*4 + base_x + 1],
            a[(base_y+1)*4 + base_x], a[(base_y+1)*4 + base_x + 1])

def render_step(png, bst, atoms):
    img = Image.new('L', (16, 16), 255)
    img.paste(get_atomic(png, atoms[0]), (0, 0))
    img.paste(get_atomic(png, atoms[1]), (8, 0))
    img.paste(get_atomic(png, atoms[2]), (0, 8))
    img.paste(get_atomic(png, atoms[3]), (8, 8))
    rows = []
    for hy in range(8):
        row = ""
        for x in range(16):
            t = classify_px(img.getpixel((x, hy*2)))
            b = classify_px(img.getpixel((x, hy*2 + 1)))
            row += cell_ansi(t, b)
        rows.append(row)
    return rows

# ─── 처리 ─────────────────────────────────────────────────────
PNG = Image.open(os.path.join(POKERED, 'gfx', 'tilesets', 'gym.png')).convert('L')
with open(os.path.join(POKERED, 'gfx', 'blocksets', 'gym.bst'), 'rb') as f:
    BST = f.read()
with open(os.path.join(POKERED, 'maps', 'PewterGym.blk'), 'rb') as f:
    blk = f.read()

W, H = 5, 7  # blocks
assert len(blk) == W*H, f'PewterGym.blk size mismatch: {len(blk)} != {W*H}'

variant_count = {}
for sy in range(H*2):
    for sx in range(W*2):
        atoms = step_atomics(blk, BST, W, sx, sy)
        variant_count[atoms] = variant_count.get(atoms, 0) + 1
print(f'[i] PewterGym unique step variants: {len(variant_count)}', file=sys.stderr)

# Char 풀: 33-126 minus reserved
ESCAPE   = set("\"'\\")
RESERVED = set("#~ ")  # 폴백/공백
POOL = sorted(set(chr(c) for c in range(33, 127)) - ESCAPE - RESERVED)
# '?' reserved for door warp
POOL = [c for c in POOL if c != '?']

sorted_atoms = sorted(variant_count.items(), key=lambda x: -x[1])
variant_to_char = {}
pool_iter = iter(POOL)
for atoms, _ in sorted_atoms:
    try:
        variant_to_char[atoms] = next(pool_iter)
    except StopIteration:
        variant_to_char[atoms] = '#'

# 레이아웃 생성
layout = []
for sy in range(H*2):
    row = ""
    for sx in range(W*2):
        atoms = step_atomics(blk, BST, W, sx, sy)
        row += variant_to_char[atoms]
    layout.append(row)
# 워프 override
for x, y, ch in WARP_OVERRIDE:
    if 0 <= y < len(layout) and 0 <= x < len(layout[y]):
        layout[y] = layout[y][:x] + ch + layout[y][x+1:]

# walkable
walkable = set()
for atoms, ch in variant_to_char.items():
    if atoms[2] in GYM_PASS:
        walkable.add(ch)
walkable.add('?')  # 도어
print(f'[i] PewterGym walkable chars: {sorted(walkable)}', file=sys.stderr)

# TileArt
char_to_atoms = {}
for a, c in variant_to_char.items():
    if c not in char_to_atoms:
        char_to_atoms[c] = a

PREFIX = 'PG'
art_blocks = []
case_lines = []
for ch in sorted(char_to_atoms):
    atoms = char_to_atoms[ch]
    tname = f"{PREFIX}_C{ord(ch):03d}"
    rows = render_step(PNG, BST, atoms)
    art = [f"// {PREFIX} '{ch}' atoms={list(atoms)} count={variant_count[atoms]}",
           f"static const TileArt TILE_{tname} = {{{{"]
    for r in rows:
        art.append('    "{}",'.format(r))
    art.append("}, '" + (ch if ch not in ('\\', "'") else "\\" + ch) + "'};")
    art.append("")
    art_blocks.append("\n".join(art))
    esc = "\\\\" if ch == '\\' else ("\\'" if ch == "'" else ch)
    case_lines.append(f"        case '{esc}': return &TILE_{tname};")

# '?' fallback (도어) — Oak Lab '?' (TILE_OL_C063) 재사용 가능. 별도 추가 안 함.
# 대신 layout에서 '?'는 indoor fallback case로 라우팅됨.

fb_atoms = max(variant_count, key=lambda a: variant_count[a])
fb_ch = variant_to_char[fb_atoms]
fb_tile = f"TILE_{PREFIX}_C{ord(fb_ch):03d}"

# ─── tiles.h 패치 ────────────────────────────────────────────
with open(TILES_H, 'r', encoding='utf-8') as f:
    src = f.read()

# 기존 PG 섹션 제거 (재실행 안전)
src = re.sub(r"// --- PG_AUTO_BEGIN.*?--- PG_AUTO_END ---\n", "", src, flags=re.S)
src = re.sub(r"    // PG_DISPATCH_BEGIN.*?    // PG_DISPATCH_END\n", "", src, flags=re.S)

# Art 섹션 — getTileArt 앞에 삽입
art_section = (
    "// --- PG_AUTO_BEGIN (MAP_8 Pewter Gym) ---\n"
    + "\n".join(art_blocks)
    + "// --- PG_AUTO_END ---\n"
)
m = re.search(r'inline const TileArt\* getTileArt\(', src)
if m:
    src = src[:m.start()] + art_section + "\n" + src[m.start():]

# Dispatch 추가 — overworld 분기 BEFORE Forest/Gate
pg_dispatch = (
    "    // PG_DISPATCH_BEGIN (MAP_8 Pewter Gym - gym tileset)\n"
    "    if (mapId == 8) {\n"
    "        switch (c) {\n"
    + "\n".join(case_lines) + "\n"
    f"            default: return &{fb_tile};\n"
    "        }\n"
    "    }\n"
    "    // PG_DISPATCH_END\n"
)
# MAP_4 분기 BEFORE에 삽입
src = src.replace(
    "    if (mapId == 4) {",
    pg_dispatch + "    if (mapId == 4) {",
    1
)

with open(TILES_H, 'w', encoding='utf-8') as f:
    f.write(src)
print(f'[OK] tiles.h: PewterGym TileArt {len(art_blocks)}개 추가', file=sys.stderr)

# ─── map_data.h 패치 ─────────────────────────────────────────
with open(MAP_H, 'r', encoding='utf-8') as f:
    md = f.read()

# MAP_8 tiles[] 교체 (MAP_PEWTER_GYM, 10, 14)
pattern = (r'(inline MapDef MAP_8\s*=\s*\{[^{}]*\{\s*\n)'
           r'((?:[^{}]|\n)*?nullptr\s*\n\s*)'
           r'(\})')
new_tiles = "        // gen_pewter_gym_tiles.py — pokered PewterGym.blk\n"
for row in layout:
    new_tiles += '        "{}",\n'.format(row)
new_tiles += "        nullptr\n    "
md_new, n = re.subn(pattern, lambda m: m.group(1) + new_tiles + m.group(3),
                   md, count=1, flags=re.S)
if n == 1:
    md = md_new
    # mapW/mapH 확인
    md = re.sub(r'(MAP_PEWTER_GYM,)\s*\d+,\s*\d+,', r'\1 10, 14,', md)
    print(f'[OK] MAP_8 (Pewter Gym 10x14) tiles 갱신', file=sys.stderr)
else:
    print(f'[WARN] MAP_8 tiles 패턴 매칭 실패', file=sys.stderr)

# Tile walkable: MAP_8 specific walkable chars
walkable_chars = sorted(walkable)
walkable_expr = ' || '.join([f"t == '{c}'" if c not in "\\'" else f"t == '\\{c}'"
                              for c in walkable_chars])
print(f'  Walkable: {walkable_expr}', file=sys.stderr)

with open(MAP_H, 'w', encoding='utf-8') as f:
    f.write(md)

# 출력 끝
print(f'  Layout:', file=sys.stderr)
for row in layout:
    print(f'    "{row}"', file=sys.stderr)
