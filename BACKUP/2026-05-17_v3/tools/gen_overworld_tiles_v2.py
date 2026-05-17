#!/usr/bin/env python3
# coding: utf-8
"""
gen_overworld_tiles_v2.py - 오버월드 타일 디코더 v2 (재설계)

v1 대비 변경점:
- PT preservation 폐기 (PT/Vir/Pewter 등 모두 fresh decode)
- 시맨틱 char (D/L/G/M/T/H/C)는 워프 좌표에서만 사용
- 빈도순 char 할당 (top ~37개 atom variant 처리, 나머지는 '#' fallback)
- TileArt는 실제 atom에서 자동 생성 (hardcode된 TILE_MART 등 사용 안 함)

처리 맵:
  PalletTown   step 20×18
  Route1       step 20×36
  ViridianCity step 40×36
  Route2       step 20×72
  PewterCity   step 40×36

입력:
  pokered/maps/<MapName>.blk
  pokered/gfx/blocksets/overworld.bst
  pokered/gfx/tilesets/overworld.png

출력 (덮어씀):
  src/data/tiles.h         (OW_AUTO_BEGIN ... OW_AUTO_END 블록)
  src/data/map_data.h      (MAP_0 ... MAP_5 의 tiles[] + walkable/encounter)

가이드: BACKUP/2026-05-16_redesign_v2/REDESIGN_GUIDE.md 참고
"""
import os, re, sys
from PIL import Image

# ─── 경로 ─────────────────────────────────────────────────────
ROOT    = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
ASSETS  = os.path.join(ROOT, 'pokered_assets')
POKERED = os.path.abspath(os.path.join(ROOT, '..', 'pokered'))

def find_tileset_png():
    for base in (ASSETS, POKERED):
        p = os.path.join(base, 'gfx', 'tilesets', 'overworld.png')
        if os.path.exists(p): return p
    raise FileNotFoundError("overworld.png 못 찾음")

PNG_PATH = find_tileset_png()
BST_PATH = os.path.join(POKERED, 'gfx', 'blocksets', 'overworld.bst')
TILES_H  = os.path.join(ROOT, 'src', 'data', 'tiles.h')
MAP_H    = os.path.join(ROOT, 'src', 'data', 'map_data.h')

PNG = Image.open(PNG_PATH).convert('L')
with open(BST_PATH, 'rb') as f:
    BST = f.read()

# ─── 맵 목록 ─────────────────────────────────────────────────
# (이름, blk_w, blk_h, MapDef 매크로 라벨)
MAPS = [
    ("PalletTown",    10,  9, "MAP_0"),
    ("Route1",        10, 18, "MAP_1"),
    ("ViridianCity",  20, 18, "MAP_2"),
    ("Route2",        10, 36, "MAP_3"),
    ("PewterCity",    20, 18, "MAP_5"),
]

# ─── pokered 충돌 데이터 (data/tilesets/collision_tile_ids.asm) ─────
OVERWORLD_PASS  = {0x00, 0x10, 0x1b, 0x20, 0x21, 0x23, 0x2c, 0x2d, 0x2e,
                   0x30, 0x31, 0x33, 0x39, 0x3c, 0x3e, 0x52, 0x54, 0x58, 0x5b}
ATOM_TALL_GRASS = 0x52
ATOM_CUT_TREE   = 0x3D
ATOM_DOOR_SET   = {0x1b, 0x58}

# ─── 워프 좌표 (pokered .asm 직접 참조, hardcode) ─────────────
# step 좌표 (sx, sy), char, 설명
# 워프 좌표는 map_data.h 의 warp 정의를 따라 PT가 만든 step 단위 좌표
WARP_OVERRIDES = {
    # 'MAP_0' (PalletTown): Player House / Rival House / Oak's Lab
    'MAP_0': [
        (5, 5,  'D', 'Player House door'),
        (13, 5, 'D', 'Rival House door'),
        (12, 11,'L', "Oak's Lab door"),
    ],
    # 'MAP_1' (Route1): no warps
    'MAP_1': [],
    # 'MAP_2' (ViridianCity)
    'MAP_2': [
        (23, 25, 'C', 'Pokemon Center'),
        (29, 19, 'M', 'Mart'),
        (21, 15, 'D', 'School House'),
        (21,  9, 'D', 'Nickname House'),
        (32,  7, 'G', 'Gym'),
    ],
    # 'MAP_3' (Route2): cut trees treated separately
    'MAP_3': [],
    # 'MAP_5' (PewterCity)
    'MAP_5': [
        (14,  7, 'D', 'Museum entrance'),
        (19,  5, 'D', 'Museum NE'),
        (16, 17, 'G', 'Gym'),
        (29, 13, 'D', 'Nidoran House'),
        (23, 17, 'M', 'Mart'),
        (7,  29, 'D', 'Speech House'),
        (13, 25, 'C', 'Pokemon Center'),
    ],
}

# ─── 시맨틱 char ─────────────────────────────────────────────
# 풀숲, cut tree 는 atom 단위 자동 매핑
SEMANTIC_CHARS = {
    'D': 'TILE_DOOR',       # door warp
    'L': 'TILE_LABDOOR',    # lab door
    'G': 'TILE_GYM',        # gym sign
    'M': 'TILE_MART',       # mart sign
    'C': 'TILE_CENTER',     # pokecenter sign
    'T': 'TILE_TREE',       # cut tree
    ';': 'TILE_TALLGRASS',  # tall grass (encounter)
}
# 위 char 들은 absolute 예약 — atom→char 풀에서 절대 안 씀

# ─── char 풀 ────────────────────────────────────────────────
# reds_house: a-z, A, B, C 예약
# oaklab: + ] [ < > = _ | : - { } ? ` 예약
# C++ escape: " ' \ 회피
# 기본 시맨틱: ' ' . , ; # ~ 예약
# 시맨틱 워프: D L G M T H P C N (위 SEMANTIC_CHARS) 예약
RESERVED = set("abcdefghijklmnopqrstuvwxyzABC+][<>=_|:-{}?`\"'\\ .,;#~DLGMTHPCN")

# 빈도순 할당용 풀 (walkable 우선 vs wall 우선 카테고리별)
# walkable atoms (BL in OVERWORLD_PASS) 에 줄 char: 숫자 0-9 (' '도 walkable)
WALK_POOL = list("0123456789")    # 10 chars (excluding ' ' which is default)
# wall atoms 에 줄 char: 대문자 (예약/시맨틱 제외) + 특수
WALL_POOL = list("EFIJKOQRSUVWXYZ!@$%^&*()/")  # 25 chars

# default fallback chars
DEFAULT_GROUND = ' '
DEFAULT_WALL   = '#'
DEFAULT_WATER  = '~'

# ─── ANSI 변환 ───────────────────────────────────────────────
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

# ─── atom 수집 + 카테고리 ───────────────────────────────────
BLKS = {}
for name, w, h, mid in MAPS:
    p = os.path.join(POKERED, 'maps', f'{name}.blk')
    with open(p, 'rb') as f:
        data = f.read()
    if len(data) != w * h:
        print(f"[FATAL] {name}.blk = {len(data)}B, expected {w}*{h}={w*h}B", file=sys.stderr)
        sys.exit(1)
    BLKS[mid] = (data, w, h, name)

variant_count = {}
variant_first_pos = {}
for mid, (blk, w, h, name) in BLKS.items():
    for sy in range(h*2):
        for sx in range(w*2):
            atoms = step_atomics(blk, w, sx, sy)
            variant_count[atoms] = variant_count.get(atoms, 0) + 1
            if atoms not in variant_first_pos:
                variant_first_pos[atoms] = (mid, sx, sy)

def categorize(atoms):
    """카테고리: 'tallgrass','cuttree','walkable','wall'"""
    if ATOM_TALL_GRASS in atoms: return 'tallgrass'
    if ATOM_CUT_TREE   in atoms: return 'cuttree'
    bl = atoms[2]
    if bl in OVERWORLD_PASS:     return 'walkable'
    return 'wall'

variant_cat = {a: categorize(a) for a in variant_count}
print(f'[i] Unique step variants: {len(variant_count)}', file=sys.stderr)
by_cat = {}
for a, c in variant_cat.items():
    by_cat.setdefault(c, []).append(a)
for c in ('walkable', 'wall', 'tallgrass', 'cuttree'):
    print(f'    {c}: {len(by_cat.get(c, []))}', file=sys.stderr)

# ─── char 할당 ───────────────────────────────────────────────
# 우선순위:
# 1) 'tallgrass' → ';'  (모두)
# 2) 'cuttree'  → 'T'   (모두)
# 3) 'walkable' → ' ' 다음에 빈도순으로 0..9
# 4) 'wall'     → 빈도순으로 WALL_POOL
# 5) 부족분 → '#' (wall) 또는 ' ' (walkable) fallback
variant_to_char = {}

for a, cat in variant_cat.items():
    if cat == 'tallgrass':
        variant_to_char[a] = ';'
    elif cat == 'cuttree':
        variant_to_char[a] = 'T'

# walkable: 빈도순. 첫 번째 (가장 흔한) → ' ', 나머지 → 0,1,2,...
walkable_sorted = sorted(
    [a for a, c in variant_cat.items() if c == 'walkable'],
    key=lambda a: -variant_count[a])
walk_pool_iter = iter(WALK_POOL)
for i, a in enumerate(walkable_sorted):
    if i == 0:
        variant_to_char[a] = DEFAULT_GROUND  # ' '
    else:
        try:
            variant_to_char[a] = next(walk_pool_iter)
        except StopIteration:
            variant_to_char[a] = DEFAULT_GROUND  # ' ' fallback

# wall: 빈도순. WALL_POOL 소진시 '#'
wall_sorted = sorted(
    [a for a, c in variant_cat.items() if c == 'wall'],
    key=lambda a: -variant_count[a])
wall_pool_iter = iter(WALL_POOL)
fallback_walls = 0
for a in wall_sorted:
    try:
        variant_to_char[a] = next(wall_pool_iter)
    except StopIteration:
        variant_to_char[a] = DEFAULT_WALL
        fallback_walls += 1

print(f'[i] Char 할당 결과:', file=sys.stderr)
print(f'    walkable: {len(walkable_sorted)}개 atom → " " + {len(WALK_POOL)} chars', file=sys.stderr)
print(f'    wall: {len(wall_sorted)}개 atom, fallback "#" = {fallback_walls}개', file=sys.stderr)

# 빈도 통계
total_steps = sum(variant_count.values())
fallback_count = sum(variant_count[a] for a in wall_sorted[len(WALL_POOL):])
print(f'    "#" fallback이 차지하는 step 비율: {100*fallback_count/total_steps:.2f}%', file=sys.stderr)

# ─── 맵 레이아웃 생성 ─────────────────────────────────────────
map_layouts = {}
for mid, (blk, w, h, name) in BLKS.items():
    step_w, step_h = w*2, h*2
    layout = []
    for sy in range(step_h):
        row = ""
        for sx in range(step_w):
            atoms = step_atomics(blk, w, sx, sy)
            row += variant_to_char[atoms]
        layout.append(row)
    # 워프 오버라이드 적용
    for sx, sy, ch, desc in WARP_OVERRIDES.get(mid, []):
        if sy < len(layout) and sx < len(layout[sy]):
            layout[sy] = layout[sy][:sx] + ch + layout[sy][sx+1:]
    map_layouts[mid] = (step_w, step_h, layout)

# ─── TileArt 출력 (tiles.h 의 OW_AUTO 섹션) ──────────────────
# 시맨틱 char (D L G M T C ;)는 기존 hardcode된 TileArt 사용 → 새 art 안 만듦
# 다른 모든 atom→char 매핑에 대해 actual atom rendering 생성

art_blocks = []
case_lines = []
emitted_chars = set()

for atoms in sorted(variant_count, key=lambda a: -variant_count[a]):
    ch = variant_to_char[atoms]
    if ch in emitted_chars: continue
    emitted_chars.add(ch)
    if ch in SEMANTIC_CHARS or ch == ' ' or ch == '#' or ch == '~':
        # 시맨틱은 hardcode된 TileArt 사용 (D→TILE_DOOR, T→TILE_TREE, ; → TILE_TALLGRASS 등)
        # ' ' → TILE_GROUND, '#' → TILE_WALL, '~' → TILE_WATER (기존)
        continue
    # 자동 생성된 atom-based TileArt
    name = "OW2_C{:03d}".format(ord(ch))
    rows = render_step(atoms)
    art = ["// Overworld v2 '{}' atoms={} count={}".format(ch, list(atoms), variant_count[atoms]),
           "static const TileArt TILE_{} = {{{{".format(name)]
    for r in rows:
        art.append('    "{}",'.format(r))
    art.append("}}, '{}'}};".format(ch))
    art.append("")
    art_blocks.append("\n".join(art))
    esc = "\\\\" if ch == '\\' else ("\\'" if ch == "'" else ch)
    case_lines.append("    case '{}': return &TILE_{};".format(esc, name))

# ─── tiles.h 패치 ────────────────────────────────────────────
OW_BEGIN    = "// --- OW_AUTO_BEGIN (gen_overworld_tiles_v2.py) ---"
OW_END      = "// --- OW_AUTO_END ---"
OW_SW_BEGIN = "    // OW_SWITCH_BEGIN (gen_overworld_tiles_v2.py)"
OW_SW_END   = "    // OW_SWITCH_END"

art_section  = OW_BEGIN + "\n" + "\n".join(art_blocks) + OW_END + "\n"
case_section = OW_SW_BEGIN + "\n" + "\n".join(case_lines) + "\n" + OW_SW_END + "\n"

with open(TILES_H, 'r', encoding='utf-8') as f:
    src = f.read()

# OW_AUTO 섹션: 기존(v1)의 OW_AUTO_BEGIN/END 도 매칭하도록
src = re.sub(r"// --- OW_AUTO_BEGIN.*?--- OW_AUTO_END ---\n",
             lambda m: art_section, src, count=1, flags=re.S)

# OW_SWITCH 섹션
src = re.sub(r"    // OW_SWITCH_BEGIN.*?    // OW_SWITCH_END\n",
             lambda m: case_section, src, count=1, flags=re.S)

# 시맨틱 char가 switch에서 hardcode 되어있다면 그대로 유지 (D/L/G/M/C/T/H/P/;/.,
# 이미 switch case에 있음. 변경 안 함)

with open(TILES_H, 'w', encoding='utf-8') as f:
    f.write(src)
print(f'[OK] tiles.h 패치 완료: {len(art_blocks)}개 새 TileArt', file=sys.stderr)

# ─── map_data.h 패치: MAP_n 의 tiles[] 교체 ──────────────────
with open(MAP_H, 'r', encoding='utf-8') as f:
    md = f.read()

for mid, (step_w, step_h, layout) in map_layouts.items():
    # 각 MAP_n inline MapDef 블록 내부의 tiles[] 영역만 치환
    # 구조:  inline MapDef MAP_N = {
    #            ID, W, H,
    #            "name", L"namew",
    #            {                ← tiles[] opening (두번째 '{')
    #              "row0",
    #              ...
    #              nullptr
    #            },               ← tiles[] closing
    # pattern: MapDef opening + ... + tiles opening | content | tiles closing
    pattern = (r'(inline MapDef ' + mid + r'\s*=\s*\{[^{}]*\{\s*\n)'
               r'((?:[^{}]|\n)*?nullptr\s*\n\s*)'
               r'(\})')
    new_tiles = "        // gen_overworld_tiles_v2.py 자동 생성\n"
    for r in layout:
        new_tiles += '        "{}",\n'.format(r)
    new_tiles += "        nullptr\n    "
    md_new, n = re.subn(pattern, lambda m: m.group(1) + new_tiles + m.group(3),
                        md, count=1, flags=re.S)
    if n != 1:
        print(f'[WARN] {mid} tiles[] 치환 실패 (regex 안 맞음)', file=sys.stderr)
    else:
        md = md_new
        print(f'[OK] {mid} ({step_w}x{step_h}) tiles[] 갱신', file=sys.stderr)

# ─── tileWalkable / tileIsEncounter 자동 생성 ────────────────
walkable_chars = set()
encounter_chars = set()
for atoms, ch in variant_to_char.items():
    cat = variant_cat[atoms]
    if cat == 'walkable':
        walkable_chars.add(ch)
    if cat == 'tallgrass':
        walkable_chars.add(ch)
        encounter_chars.add(ch)
# 인테리어/특수
walkable_chars.update([' ', '+', 'i', 'u'])  # oaklab '+' floor, etc.

walkable_chars.discard('#')
walkable_chars.discard('~')
walkable_chars.discard('T')   # cut tree 는 walkable 아님

walk_expr = ' ||\n           '.join(sorted("t == '{}'".format(c if c != "'" else "\\'") for c in walkable_chars))
enc_expr  = ' || '.join(sorted("t == '{}'".format(c) for c in encounter_chars))

new_walkable = (
    'inline bool tileWalkable(char t) {\n'
    '    return ' + walk_expr + ';\n'
    '}'
)
new_encounter = (
    'inline bool tileIsEncounter(char t) {\n'
    '    return ' + enc_expr + ';\n'
    '}'
)

md = re.sub(r'inline bool tileWalkable\([^)]*\)\s*\{[^}]*\}', new_walkable, md, count=1)
md = re.sub(r'inline bool tileIsEncounter\([^)]*\)\s*\{[^}]*\}', new_encounter, md, count=1)

with open(MAP_H, 'w', encoding='utf-8') as f:
    f.write(md)
print(f'[OK] map_data.h tiles[]/walkable/encounter 갱신', file=sys.stderr)

# ─── 요약 출력 ────────────────────────────────────────────────
print('', file=sys.stderr)
print('═══════════════════════════════════════════════════════', file=sys.stderr)
print('재설계 v2 완료', file=sys.stderr)
print('═══════════════════════════════════════════════════════', file=sys.stderr)
print('맵별 차원:', file=sys.stderr)
for mid, (w, h, _) in map_layouts.items():
    print(f'  {mid}: {w}x{h}', file=sys.stderr)
print('', file=sys.stderr)
print('Char 매핑 통계:', file=sys.stderr)
print(f'  Walkable: {len([a for a in variant_cat if variant_cat[a]=="walkable"])}개 atom', file=sys.stderr)
print(f'  Wall:     {len([a for a in variant_cat if variant_cat[a]=="wall"])}개 atom', file=sys.stderr)
print(f'  Tall:     {len([a for a in variant_cat if variant_cat[a]=="tallgrass"])}', file=sys.stderr)
print(f'  Cuttree:  {len([a for a in variant_cat if variant_cat[a]=="cuttree"])}', file=sys.stderr)
print(f'  "#" fallback: 전체 step 의 {100*fallback_count/total_steps:.2f}%', file=sys.stderr)
print('', file=sys.stderr)
print(f'Walkable chars: {sorted(walkable_chars)}', file=sys.stderr)
print(f'Encounter chars: {sorted(encounter_chars)}', file=sys.stderr)
print('', file=sys.stderr)
print('다음 단계: build.bat 실행 → 시각 확인', file=sys.stderr)
