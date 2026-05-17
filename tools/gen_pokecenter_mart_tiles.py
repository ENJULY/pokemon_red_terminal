#!/usr/bin/env python3
# coding: utf-8
"""
gen_pokecenter_mart_tiles.py — PC + Mart 통합 디코더

PC 와 Mart 는 pokered 에서 같은 blockset/tileset 공유 (pokecenter.bst, pokecenter.png).
→ 같은 char→atom 매핑으로 처리 가능.

처리:
  ViridianPokecenter.blk (7×4 = 14×8 step) → MAP_11
  PewterPokecenter.blk   (7×4 = 14×8 step) → MAP_12
  ViridianMart.blk       (4×4 =  8×8 step) → MAP_13
  PewterMart.blk         (4×4 =  8×8 step) → MAP_14

출력:
  src/data/tiles.h: PC_AUTO_BEGIN..END (기존 교체) + getTileArt PC/Mart dispatch
  src/data/map_data.h: MAP_11..MAP_14 정의
"""
import os, re, sys
from PIL import Image

ROOT    = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
POKERED = os.path.abspath(os.path.join(ROOT, '..', 'pokered'))

PNG_PATH = os.path.join(POKERED, 'gfx', 'tilesets', 'pokecenter.png')
BST_PATH = os.path.join(POKERED, 'gfx', 'blocksets', 'pokecenter.bst')
TILES_H  = os.path.join(ROOT, 'src', 'data', 'tiles.h')
MAP_H    = os.path.join(ROOT, 'src', 'data', 'map_data.h')

PNG = Image.open(PNG_PATH).convert('L')
with open(BST_PATH, 'rb') as f:
    BST = f.read()

# Mart_Coll / Pokecenter_Coll: 둘 다 동일
PC_MART_PASS = {0x11, 0x1a, 0x1c, 0x3c, 0x5e}

# 처리 맵: (이름, blk_w, blk_h, MapDef 라벨, map ID 상수, dest_overworld_map, dest_x, dest_y, 한국어이름)
MAPS = [
    ("ViridianPokecenter", 7, 4, "MAP_11", "MAP_VIRIDIAN_PC",   "MAP_VIRIDIAN", 23, 26, "포켓몬센터"),
    ("PewterPokecenter",   7, 4, "MAP_12", "MAP_PEWTER_PC",     "MAP_PEWTER",   13, 26, "포켓몬센터"),
    ("ViridianMart",       4, 4, "MAP_13", "MAP_VIRIDIAN_MART", "MAP_VIRIDIAN", 29, 20, "프렌들리숍"),
    ("PewterMart",         4, 4, "MAP_14", "MAP_PEWTER_MART",   "MAP_PEWTER",   23, 18, "프렌들리숍"),
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

def get_atomic(idx):
    cols = PNG.size[0] // 8
    r, c = idx // cols, idx % cols
    return PNG.crop((c*8, r*8, (c+1)*8, (r+1)*8))

def step_atomics(blk, blk_w, sx, sy):
    bx, by = sx // 2, sy // 2
    qx, qy = sx % 2, sy % 2
    block_id = blk[by * blk_w + bx]
    a = BST[block_id*16:(block_id+1)*16]
    base_y = qy * 2; base_x = qx * 2
    return (a[base_y*4 + base_x],     a[base_y*4 + base_x + 1],
            a[(base_y+1)*4 + base_x], a[(base_y+1)*4 + base_x + 1])

def render_step(atoms):
    img = Image.new('L', (16, 16), 255)
    img.paste(get_atomic(atoms[0]), (0, 0))
    img.paste(get_atomic(atoms[1]), (8, 0))
    img.paste(get_atomic(atoms[2]), (0, 8))
    img.paste(get_atomic(atoms[3]), (8, 8))
    rows = []
    for hy in range(8):
        row = ""
        for x in range(16):
            t = classify_px(img.getpixel((x, hy*2)))
            b = classify_px(img.getpixel((x, hy*2 + 1)))
            row += cell_ansi(t, b)
        rows.append(row)
    return rows

# ─── atom 수집 ───────────────────────────────────────────────
BLKS = {}
for name, w, h, mid, cid, dest_map, dx, dy, kname in MAPS:
    p = os.path.join(POKERED, 'maps', f'{name}.blk')
    with open(p, 'rb') as f:
        data = f.read()
    if len(data) != w * h:
        print(f"[FATAL] {name}.blk = {len(data)}B, expected {w}*{h}={w*h}B", file=sys.stderr)
        sys.exit(1)
    BLKS[mid] = (data, w, h, name, cid, dest_map, dx, dy, kname)

variant_count = {}
for mid, (blk, w, h, *_) in BLKS.items():
    for sy in range(h*2):
        for sx in range(w*2):
            atoms = step_atomics(blk, w, sx, sy)
            variant_count[atoms] = variant_count.get(atoms, 0) + 1

print(f'[i] PC+Mart unique step variants: {len(variant_count)}', file=sys.stderr)

# ─── char 할당 ──────────────────────────────────────────────
ESCAPE = set("\"'\\")
RESERVED = set("#~ ")
ALL_PRINTABLE = set(chr(c) for c in range(33, 127))
POOL = sorted(ALL_PRINTABLE - ESCAPE - RESERVED)

sorted_atoms = sorted(variant_count.items(), key=lambda x: -x[1])
variant_to_char = {}
for i, (atoms, cnt) in enumerate(sorted_atoms):
    if i < len(POOL):
        variant_to_char[atoms] = POOL[i]
    else:
        variant_to_char[atoms] = '#'

print(f'[i] 할당: {len(sorted_atoms)} atoms', file=sys.stderr)

# ─── 맵 레이아웃 ────────────────────────────────────────────
map_layouts = {}
for mid, (blk, w, h, name, cid, dest_map, dx, dy, kname) in BLKS.items():
    step_w, step_h = w*2, h*2
    layout = []
    for sy in range(step_h):
        row = ""
        for sx in range(step_w):
            atoms = step_atomics(blk, w, sx, sy)
            row += variant_to_char[atoms]
        layout.append(row)
    map_layouts[mid] = (step_w, step_h, layout, cid, name, dest_map, dx, dy, kname)

# ─── TileArt 생성 ───────────────────────────────────────────
char_to_atoms = {}
for atoms, ch in variant_to_char.items():
    if ch not in char_to_atoms:
        char_to_atoms[ch] = atoms

art_blocks = []
case_lines = []
for ch in sorted(char_to_atoms):
    atoms = char_to_atoms[ch]
    name = "PC_C{:03d}".format(ord(ch))
    rows = render_step(atoms)
    art = ["// PC/Mart '{}' atoms={} count={}".format(ch, list(atoms), variant_count.get(atoms, 0)),
           "static const TileArt TILE_{} = {{{{".format(name)]
    for r in rows:
        art.append('    "{}",'.format(r))
    art.append("}}, '{}'}};".format(ch if ch != '\\' and ch != "'" else "\\" + ch))
    art.append("")
    art_blocks.append("\n".join(art))
    esc = "\\\\" if ch == '\\' else ("\\'" if ch == "'" else ch)
    case_lines.append("    case '{}': return &TILE_{};".format(esc, name))

# walkable chars
walkable_chars = set()
for atoms, ch in variant_to_char.items():
    if atoms[2] in PC_MART_PASS:
        walkable_chars.add(ch)

# ─── tiles.h 패치 ────────────────────────────────────────────
PC_BEGIN = "// --- PC_AUTO_BEGIN (gen_pokecenter_mart_tiles.py) ---"
PC_END   = "// --- PC_AUTO_END ---"

art_section = PC_BEGIN + "\n" + "\n".join(art_blocks) + PC_END + "\n"

with open(TILES_H, 'r', encoding='utf-8') as f:
    src = f.read()

# 기존 PC 섹션 제거
src = re.sub(r"// --- PC_AUTO_BEGIN.*?--- PC_AUTO_END ---\n", "", src, flags=re.S)

# getTileArt 안의 PC dispatch 블록 제거 (있으면)
src = re.sub(r"    // Pokemon Center.*?if \(mapId == 11.*?\}\s*\}\s*\n", "", src, flags=re.S)

# PC art 섹션을 getTileArt 앞에 삽입
m = re.search(r'inline const TileArt\* getTileArt\(', src)
if m:
    src = src[:m.start()] + art_section + "\n" + src[m.start():]

# dispatch 추가: MAP_11..MAP_14
fallback_char = variant_to_char[max(variant_count, key=lambda a: variant_count[a])]
fallback_tile = "TILE_PC_C{:03d}".format(ord(fallback_char))

pc_dispatch = (
    "    // Pokemon Center + Mart (MAP_11..MAP_14)\n"
    "    if (mapId == 11 || mapId == 12 || mapId == 13 || mapId == 14) {\n"
    "        switch (c) {\n"
    + "\n".join("        " + l.strip() for l in case_lines) + "\n"
    "            default: return &" + fallback_tile + ";\n"
    "        }\n"
    "    }\n"
)

# overworld 분기 다음에 삽입
src = re.sub(
    r'(if \(mapId >= 0 && mapId <= 5\) \{[^}]*\{[^}]*\}[^}]*\})',
    lambda m: m.group(1) + "\n" + pc_dispatch,
    src, count=1, flags=re.S
)

with open(TILES_H, 'w', encoding='utf-8') as f:
    f.write(src)
print(f'[OK] tiles.h: {len(art_blocks)} PC+Mart TileArt + dispatch', file=sys.stderr)

# ─── map_data.h 패치 ─────────────────────────────────────────
with open(MAP_H, 'r', encoding='utf-8') as f:
    md = f.read()

# MAP 상수 추가
if 'MAP_VIRIDIAN_MART' not in md:
    md = md.replace(
        'static const int MAP_PEWTER_PC     = 12;',
        'static const int MAP_PEWTER_PC     = 12;\n'
        'static const int MAP_VIRIDIAN_MART = 13;\n'
        'static const int MAP_PEWTER_MART   = 14;'
    )

# isIndoorMap 확장
md = re.sub(
    r'inline bool isIndoorMap\(int id\)\s*\{[^}]+\}',
    'inline bool isIndoorMap(int id) {\n'
    '    return id == MAP_OAK_LAB || id == MAP_PLAYER_HOUSE ||\n'
    '           id == MAP_PLAYER_HOUSE2 || id == MAP_PEWTER_GYM ||\n'
    '           id == MAP_RIVAL_HOUSE ||\n'
    '           id == MAP_VIRIDIAN_PC || id == MAP_PEWTER_PC ||\n'
    '           id == MAP_VIRIDIAN_MART || id == MAP_PEWTER_MART;\n'
    '}',
    md
)

# NUM_MAPS + ALL_MAPS 갱신
md = re.sub(r'inline constexpr int NUM_MAPS = \d+;',
            'inline constexpr int NUM_MAPS = 15;', md)
# ALL_MAPS 배열 - MAP_12 다음에 MAP_13, MAP_14 추가
if '&MAP_13' not in md:
    md = md.replace('&MAP_12,', '&MAP_12,\n    &MAP_13,\n    &MAP_14,')

# 각 맵 MapDef 추가/갱신
# 마트는 점원 위치: 보통 (1, 2) 카운터 뒤
# 마트 출구는 보통 (3, 7) — pokecenter 와 같은 구조
NURSE_OR_CLERK = {
    'MAP_11': ('NPC_SPR_NURSE', 2, '간호사 조이',
               ["간호사 조이: 어서오세요!",
                "포켓몬을 치료해드리겠습니다... 잠시만요.",
                "완료! 포켓몬들이 모두 회복됐습니다!"]),
    'MAP_12': ('NPC_SPR_NURSE', 2, '간호사 조이',
               ["간호사 조이: 어서오세요!",
                "포켓몬을 치료해드리겠습니다... 잠시만요.",
                "완료! 포켓몬들이 모두 회복됐습니다!"]),
    'MAP_13': ('NPC_SPR_CLERK', 0, '점원',
               ["점원: 어서오세요!",
                "프렌들리숍에 오신 것을 환영합니다.",
                "(상점 기능은 아직 구현 중...)"]),
    'MAP_14': ('NPC_SPR_CLERK', 0, '점원',
               ["점원: 어서오세요!",
                "프렌들리숍에 오신 것을 환영합니다.",
                "(상점 기능은 아직 구현 중...)"]),
}

for mid, (sw, sh, layout, cid, name, dest_map, dx, dy, kname) in map_layouts.items():
    npc_spr, trigger, npc_name, lines = NURSE_OR_CLERK[mid]
    line_str = ',\n            '.join(f'L"{l}"' for l in lines)

    # 마트는 4x4 = 8x8 스텝 — 입구가 (3,7) 또는 (2,7)
    # PC는 14x8 — 입구 (3,7), (4,7)
    if 'Mart' in name:
        # Mart 8x8: 입구 (3,7) 정도
        warp_section = (
            f'        {{3, 7, {dest_map}, {dx}, {dy}}},\n'
            f'        {{4, 7, {dest_map}, {dx}, {dy}}},\n'
        )
        nwarp = 2
        entry_x = 3
    else:
        # PC 14x8: 입구 (3,7), (4,7)
        warp_section = (
            f'        {{3, 7, {dest_map}, {dx}, {dy}}},\n'
            f'        {{4, 7, {dest_map}, {dx}, {dy}}},\n'
        )
        nwarp = 2
        entry_x = 3

    if f'inline MapDef {mid} =' in md:
        # 기존 있으면 tiles 만 교체
        pattern = (r'(inline MapDef ' + mid + r'\s*=\s*\{[^{}]*\{\s*\n)'
                   r'((?:[^{}]|\n)*?nullptr\s*\n\s*)'
                   r'(\})')
        new_tiles = "        // gen_pokecenter_mart_tiles.py\n"
        for r in layout:
            new_tiles += '        "{}",\n'.format(r)
        new_tiles += "        nullptr\n    "
        md = re.sub(pattern, lambda m: m.group(1) + new_tiles + m.group(3),
                    md, count=1, flags=re.S)
        print(f'[OK] {mid} ({name}) tiles 갱신', file=sys.stderr)
    else:
        # 신규 추가
        new_def = (
            f'\n// ─── {name} ({sw}×{sh}) — pokered 원본 ─────\n'
            f'inline MapDef {mid} = {{\n'
            f'    {cid}, {sw}, {sh},\n'
            f'    "{name}", L"{kname}",\n'
            f'    {{\n'
            f'        // gen_pokecenter_mart_tiles.py\n'
        )
        for r in layout:
            new_def += '        "{}",\n'.format(r)
        new_def += (
            f'        nullptr\n'
            f'    }},\n'
            f'    {{\n'
            f'        // {npc_name} — 카운터 뒤 (1,1) - 카운터 너머 대화 가능\n'
            f'        {{1, 1, {{\n'
            f'            {line_str},\n'
            f'            nullptr\n'
            f'        }}, {npc_spr}, {trigger}}},\n'
            f'    }}, 1,  // npcs\n'
            f'    {{}}, 0,  // trainers\n'
            f'    {{\n{warp_section}'
            f'    }}, {nwarp},  // warps\n'
            f'    {{}}, 0,  // no encounters\n'
            f'    -1, -1,  // north/south\n'
            f'    {entry_x}, 7, {entry_x}, 7,  // entry\n'
            f'    nullptr  // bgm\n'
            f'}};\n'
        )
        md = md.replace('// ─── 전체 맵 배열', new_def + '\n// ─── 전체 맵 배열')
        print(f'[OK] {mid} ({name}) 신규 추가', file=sys.stderr)

# Mart 워프 destination 갱신 (Viridian 29,19 + Pewter 23,17)
md = md.replace(
    "{29, 19, -1  /*VIRIDIAN_MART*/, -1, -1}",
    "{29, 19, MAP_VIRIDIAN_MART, 3, 7}"
)
md = md.replace(
    "{23, 17, -1  /*PEWTER_MART*/, -1, -1}",
    "{23, 17, MAP_PEWTER_MART, 3, 7}"
)

# tileWalkable 갱신 (PC+Mart walkable 추가)
pc_walkable_str = ' || '.join(
    "t == '{}'".format(c if c != "'" else "\\'") for c in sorted(walkable_chars))
# 기존 walkable에 PC+Mart 추가 — 기존 함수의 'return' 다음 줄에 OR 추가
# 단, 중복 방지 위해 PC chars 만 확실히 추가
md = re.sub(
    r'(inline bool tileWalkable\(char t\)\s*\{\s*\n\s*return\s+)',
    r'\g<1>' + pc_walkable_str + ' ||\n           ',
    md, count=1
)

with open(MAP_H, 'w', encoding='utf-8') as f:
    f.write(md)
print('[OK] map_data.h 갱신', file=sys.stderr)

print('', file=sys.stderr)
print('═══════════════════════════════════════', file=sys.stderr)
print('PC + Mart 디코더 완료', file=sys.stderr)
print('═══════════════════════════════════════', file=sys.stderr)
for mid, (sw, sh, layout, cid, name, *_) in map_layouts.items():
    print(f'  {mid} ({name}): {sw}x{sh}', file=sys.stderr)
print(f'TileArt: {len(art_blocks)}', file=sys.stderr)
