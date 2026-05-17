#!/usr/bin/env python3
# coding: utf-8
"""
gen_pokecenter_tiles.py — Pokemon Center 타일셋 디코더

처리:
  ViridianPokecenter.blk (7×4 블록 = 14×8 스텝) → MAP_11
  PewterPokecenter.blk   (7×4 블록 = 14×8 스텝) → MAP_12

타일셋:
  pokecenter.bst (37 블록), pokecenter.png

출력:
  src/data/tiles.h: PC_AUTO_BEGIN..END 섹션에 TileArt + getTileArt 확장
  src/data/map_data.h: MAP_VIRIDIAN_PC / MAP_PEWTER_PC 정의

전제: 이미 map-aware getTileArt 가 있고 indoor switch dispatch 가능
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

# Pokecenter_Coll: pokered/data/tilesets/collision_tile_ids.asm
PC_PASS = {0x11, 0x1a, 0x1c, 0x3c, 0x5e}

# 처리 맵: (이름, blk_w, blk_h, MapDef 라벨, 새 맵 ID 상수)
MAPS = [
    ("ViridianPokecenter", 7, 4, "MAP_11", "MAP_VIRIDIAN_PC"),
    ("PewterPokecenter",   7, 4, "MAP_12", "MAP_PEWTER_PC"),
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
for name, w, h, mid, cid in MAPS:
    p = os.path.join(POKERED, 'maps', f'{name}.blk')
    with open(p, 'rb') as f:
        data = f.read()
    if len(data) != w * h:
        print(f"[FATAL] {name}.blk = {len(data)}B, expected {w}*{h}={w*h}B", file=sys.stderr)
        sys.exit(1)
    BLKS[mid] = (data, w, h, name, cid)

variant_count = {}
for mid, (blk, w, h, name, cid) in BLKS.items():
    for sy in range(h*2):
        for sx in range(w*2):
            atoms = step_atomics(blk, w, sx, sy)
            variant_count[atoms] = variant_count.get(atoms, 0) + 1

print(f'[i] Pokecenter unique step variants: {len(variant_count)}', file=sys.stderr)

# ─── char 할당 ──────────────────────────────────────────────
# PC tileset은 별도 char 풀 (다른 indoor tileset과 분리)
# map-aware getTileArt 가 MAP_VIRIDIAN_PC/MAP_PEWTER_PC 에 dispatch 하므로 자유로움
# 그래도 escape risk 만 피해서 풀 구성
ESCAPE = set("\"'\\")
RESERVED = set("#~ ")  # # ~ space 는 fallback/공유
ALL_PRINTABLE = set(chr(c) for c in range(33, 127))
POOL = sorted(ALL_PRINTABLE - ESCAPE - RESERVED)

# 빈도순 할당
sorted_atoms = sorted(variant_count.items(), key=lambda x: -x[1])
variant_to_char = {}
for i, (atoms, cnt) in enumerate(sorted_atoms):
    if i < len(POOL):
        variant_to_char[atoms] = POOL[i]
    else:
        variant_to_char[atoms] = '#'

print(f'[i] PC char 할당: {len(sorted_atoms)} atoms, 풀 사용 {min(len(sorted_atoms), len(POOL))}', file=sys.stderr)

# ─── 맵 레이아웃 ────────────────────────────────────────────
map_layouts = {}
for mid, (blk, w, h, name, cid) in BLKS.items():
    step_w, step_h = w*2, h*2
    layout = []
    for sy in range(step_h):
        row = ""
        for sx in range(step_w):
            atoms = step_atomics(blk, w, sx, sy)
            row += variant_to_char[atoms]
        layout.append(row)
    map_layouts[mid] = (step_w, step_h, layout, cid, name)

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
    art = ["// PC '{}' atoms={} count={}".format(ch, list(atoms), variant_count.get(atoms, 0)),
           "static const TileArt TILE_{} = {{{{".format(name)]
    for r in rows:
        art.append('    "{}",'.format(r))
    art.append("}}, '{}'}};".format(ch if ch != '\\' and ch != "'" else "\\" + ch))
    art.append("")
    art_blocks.append("\n".join(art))
    esc = "\\\\" if ch == '\\' else ("\\'" if ch == "'" else ch)
    case_lines.append("    case '{}': return &TILE_{};".format(esc, name))

# ─── walkability (pokered Pokecenter_Coll) ─────────────────
walkable_chars = set()
for atoms, ch in variant_to_char.items():
    bl = atoms[2]
    if bl in PC_PASS:
        walkable_chars.add(ch)

# ─── tiles.h 패치: PC 섹션 추가 ─────────────────────────────
PC_BEGIN    = "// --- PC_AUTO_BEGIN (gen_pokecenter_tiles.py) ---"
PC_END      = "// --- PC_AUTO_END ---"

art_section = PC_BEGIN + "\n" + "\n".join(art_blocks) + PC_END + "\n"

with open(TILES_H, 'r', encoding='utf-8') as f:
    src = f.read()

# 기존 PC 섹션 있으면 제거
src = re.sub(r"// --- PC_AUTO_BEGIN.*?--- PC_AUTO_END ---\n", "", src, flags=re.S)

# PC art 섹션을 getTileArt 앞에 삽입
m = re.search(r'inline const TileArt\* getTileArt\(', src)
if m:
    src = src[:m.start()] + art_section + "\n" + src[m.start():]
else:
    print('[WARN] getTileArt not found', file=sys.stderr)

# getTileArt 안에 PC 분기 추가
# 패턴: "    if (mapId >= 0 && mapId <= 5) {" 다음 } 블록 뒤에
# 더 안전: 첫 indoor switch 시작 전에 PC 분기 추가
fallback_char = variant_to_char[max(variant_count, key=lambda a: variant_count[a])]
fallback_tile = "TILE_PC_C{:03d}".format(ord(fallback_char))

pc_dispatch = (
    "    // Pokemon Center (MAP_11 ViridianPC, MAP_12 PewterPC)\n"
    "    if (mapId == 11 || mapId == 12) {\n"
    "        switch (c) {\n"
    + "\n".join("        " + l.strip() for l in case_lines) + "\n"
    "            default: return &" + fallback_tile + ";\n"
    "        }\n"
    "    }\n"
)

# overworld 분기 } 다음, indoor switch 전에 삽입
src = re.sub(
    r'(if \(mapId >= 0 && mapId <= 5\) \{[^}]*\{[^}]*\}[^}]*\})',
    lambda m: m.group(1) + "\n" + pc_dispatch,
    src, count=1, flags=re.S
)
# 위 regex가 안 맞으면 fallback
if "MAP_11" not in src and "mapId == 11" not in src:
    # Insert before "switch (c) {" of indoor section
    src = re.sub(
        r'(\n    switch \(c\) \{)',
        "\n" + pc_dispatch + r"\1",
        src, count=1
    )

with open(TILES_H, 'w', encoding='utf-8') as f:
    f.write(src)
print(f'[OK] tiles.h: {len(art_blocks)} PC TileArt + dispatch 추가', file=sys.stderr)

# ─── map_data.h 패치: MAP_11, MAP_12 정의 추가 ──────────────
with open(MAP_H, 'r', encoding='utf-8') as f:
    md = f.read()

# 1) MAP constant 추가
if 'MAP_VIRIDIAN_PC' not in md:
    md = md.replace(
        'static const int MAP_RIVAL_HOUSE   = 10;',
        'static const int MAP_RIVAL_HOUSE   = 10;\n'
        'static const int MAP_VIRIDIAN_PC   = 11;\n'
        'static const int MAP_PEWTER_PC     = 12;'
    )

# 2) isIndoorMap 확장 — MAP_11, MAP_12 추가
md = re.sub(
    r'inline bool isIndoorMap\(int id\)\s*\{[^}]+\}',
    'inline bool isIndoorMap(int id) {\n'
    '    return id == MAP_OAK_LAB || id == MAP_PLAYER_HOUSE ||\n'
    '           id == MAP_PLAYER_HOUSE2 || id == MAP_PEWTER_GYM ||\n'
    '           id == MAP_RIVAL_HOUSE ||\n'
    '           id == MAP_VIRIDIAN_PC || id == MAP_PEWTER_PC;\n'
    '}',
    md
)

# 3) ALL_MAPS 배열 + NUM_MAPS 갱신
md = re.sub(r'inline constexpr int NUM_MAPS = \d+;',
            'inline constexpr int NUM_MAPS = 13;', md)
md = re.sub(
    r'(inline MapDef\* ALL_MAPS\[\] = \{[^}]+)\};',
    lambda m: m.group(1) + '    &MAP_11,\n    &MAP_12,\n};',
    md, count=1, flags=re.S
)

# 4) MAP_11, MAP_12 MapDef 추가 (기존 추가 안 된 경우만)
for mid, (sw, sh, layout, cid, name) in map_layouts.items():
    if f'inline MapDef {mid} =' in md:
        # 기존 있으면 tiles 만 교체
        pattern = (r'(inline MapDef ' + mid + r'\s*=\s*\{[^{}]*\{\s*\n)'
                   r'((?:[^{}]|\n)*?nullptr\s*\n\s*)'
                   r'(\})')
        new_tiles = "        // gen_pokecenter_tiles.py\n"
        for r in layout:
            new_tiles += '        "{}",\n'.format(r)
        new_tiles += "        nullptr\n    "
        md = re.sub(pattern, lambda m: m.group(1) + new_tiles + m.group(3),
                    md, count=1, flags=re.S)
        print(f'[OK] {mid} ({name}) tiles 갱신', file=sys.stderr)
    else:
        # 신규 추가: ALL_MAPS 앞에 삽입
        # 14x8 PC: 도어 워프 (entry) 는 일반적으로 (3, 7) 또는 (4, 7) - 아래 가운데
        # 일단 임시로 (3, 7), 워프 destination 은 사용자가 수동 연결
        dest_overworld = "MAP_VIRIDIAN" if "Viridian" in name else "MAP_PEWTER"
        dest_x = "23" if "Viridian" in name else "13"  # PC warp x in overworld
        dest_y = "26" if "Viridian" in name else "26"  # one below PC warp (player exits down)

        new_def = (
            f'\n// ─── {name} ({sw}×{sh}) — pokered 원본 디코딩 ─────\n'
            f'inline MapDef {mid} = {{\n'
            f'    {cid}, {sw}, {sh},\n'
            f'    "Pokemon Center", L"포켓몬센터",\n'
            f'    {{\n'
            f'        // gen_pokecenter_tiles.py\n'
        )
        for r in layout:
            new_def += '        "{}",\n'.format(r)
        new_def += (
            f'        nullptr\n'
            f'    }},\n'
            f'    {{}}, 0,  // npcs (TODO: Nurse Joy 등)\n'
            f'    {{}}, 0,  // trainers\n'
            f'    {{\n'
            f'        // 출구 워프: 입구 도어 위치 (대략 가운데 하단)\n'
            f'        {{3, 7, {dest_overworld}, {dest_x}, {dest_y}}},\n'
            f'        {{4, 7, {dest_overworld}, {dest_x}, {dest_y}}},\n'
            f'    }}, 2,  // warps\n'
            f'    {{}}, 0,  // no encounters\n'
            f'    -1, -1,  // north/south\n'
            f'    3, 7, 3, 7,  // entry: 도착시 (3,7)\n'
            f'    nullptr  // bgm\n'
            f'}};\n'
        )
        # ALL_MAPS 배열 앞에 삽입
        md = md.replace('// ─── 전체 맵 배열', new_def + '\n// ─── 전체 맵 배열')
        print(f'[OK] {mid} ({name}) 신규 추가', file=sys.stderr)

# 5) Viridian/Pewter 외부 맵의 PC 워프 destination 갱신
# MAP_2 (Viridian): {23, 25, -1 /*VIRIDIAN_POKECENTER*/, -1, -1}
md = md.replace(
    "{23, 25, -1  /*VIRIDIAN_POKECENTER*/, -1, -1}",
    "{23, 25, MAP_VIRIDIAN_PC, 3, 7}"
)
# MAP_5 (Pewter): {13, 25, -1 /*PEWTER_POKECENTER*/, -1, -1}
md = md.replace(
    "{13, 25, -1  /*PEWTER_POKECENTER*/, -1, -1}",
    "{13, 25, MAP_PEWTER_PC, 3, 7}"
)

# 6) tileWalkable 갱신 — PC walkable chars 추가
# 기존 함수에 PC chars 합치기
pc_walkable_str = ' || '.join("t == '{}'".format(c if c != "'" else "\\'") for c in sorted(walkable_chars))
# 기존 tileWalkable 함수 안에 PC chars 추가 (간단히 OR 연결)
md = re.sub(
    r'(inline bool tileWalkable\(char t\)\s*\{\s*\n\s*return\s+)',
    r'\g<1>' + pc_walkable_str + ' ||\n           ',
    md, count=1
)

with open(MAP_H, 'w', encoding='utf-8') as f:
    f.write(md)
print('[OK] map_data.h 갱신', file=sys.stderr)

# ─── 요약 ───────────────────────────────────────────────────
print('', file=sys.stderr)
print('═══════════════════════════════════════', file=sys.stderr)
print('PC 디코더 완료', file=sys.stderr)
print('═══════════════════════════════════════', file=sys.stderr)
for mid, (sw, sh, layout, cid, name) in map_layouts.items():
    print(f'  {mid} ({name}): {sw}x{sh}', file=sys.stderr)
print(f'TileArt: {len(art_blocks)}', file=sys.stderr)
print(f'Walkable chars: {sorted(walkable_chars)}', file=sys.stderr)
