#!/usr/bin/env python3
# coding: utf-8
"""
gen_overworld_tiles.py - 오버월드 타일 디코더

v2 와 차이점:
- 모든 hand-crafted TileArt 폐기 (TILE_WALL, TILE_DOOR, TILE_MART 등)
- 시맨틱 char도 pokered 원본 atom으로 렌더링
- ESSENTIAL_ATOMS 우선순위 보장 (빈도 무관)

처리:
  1. pokered .blk 디코딩
  2. ESSENTIAL_ATOMS → 시맨틱 char 강제 매핑
  3. 나머지 atom → 빈도순 char 할당
  4. 모든 char를 atom-rendered TileArt로 출력 (시맨틱 포함)
  5. map_data.h tiles[] + tiles.h 패치
"""
import os, re, sys
from PIL import Image

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

MAPS = [
    ("PalletTown",    10,  9, "MAP_0"),
    ("Route1",        10, 18, "MAP_1"),
    ("ViridianCity",  20, 18, "MAP_2"),
    ("Route2",        10, 36, "MAP_3"),
    ("PewterCity",    20, 18, "MAP_5"),
]

OVERWORLD_PASS  = {0x00, 0x10, 0x1b, 0x20, 0x21, 0x23, 0x2c, 0x2d, 0x2e,
                   0x30, 0x31, 0x33, 0x39, 0x3c, 0x3e, 0x52, 0x54, 0x58, 0x5b}
ATOM_TALL_GRASS = 0x52
ATOM_CUT_TREE   = 0x3D
ATOM_DOOR_SET   = {0x1b, 0x58}

# 필수 atom → 시맨틱 char 강제 매핑 (pokered .asm / .blk 직접 식별)
# 이 매핑은 빈도 무관, 절대 우선
# 'C'/'M'/'G' 는 사인 atom이 아닌 워프(도어 스텝) atom 으로 렌더링 — 게임로직과 시각 분리
ESSENTIAL_ATOMS = {
    (11, 12, 27, 28): 'D',   # 도어 스텝 (모든 빌딩 입구 공통 — 워프 자리)
    (10, 34, 23, 23): 'H',   # 집 도어 비주얼 (워프 위 셀)
    (10, 75, 75, 75): 'L',   # 연구소 도어 비주얼
    (10, 31, 26, 79): 'P',   # 체육관 도어 우측 (체육관 specific)
    (10, 10, 75, 75): 'N',   # 빌딩 지붕 상단 (사인 위 셀)
    (45, 46, 61, 62): 'T',   # Cut tree (풀베기 나무)
    (82, 82, 82, 82): ';',   # Tall grass (풀숲)
    # 사인 atoms - 빈도 매우 낮지만 시각 식별 핵심 (도어 우측 1칸)
    (66, 67, 74, 74): 'O',   # PC 사인 (포케볼)
    (68, 69, 74, 74): 'R',   # Mart 사인 (MART 글자)
    (40, 41, 34, 31): 'S',   # Gym 사인
}

# 시맨틱 char 시각 alias: 워프 게임로직용 char들은 'D' 와 같은 시각 (도어 스텝)
# 'C' (PC warp), 'M' (Mart warp), 'G' (Gym warp) → 도어 스텝 시각
VISUAL_ALIAS = {
    'C': 'D',
    'M': 'D',
    'G': 'D',
}

# 워프 좌표에서 override 사용할 char (사인이 도어 옆에 있을 때 도어가 시맨틱 받음)
# 각 워프 위치는 ESSENTIAL_ATOMS 에 의해 자동 'D' (door step) 매핑됨
# 다만 마트/포센/체육관 의 *워프*에서 게임 로직이 'M'/'C'/'G' 를 기대하므로 override 필요
WARP_SEMANTIC_OVERRIDE = {
    'MAP_2': [
        (23, 25, 'C'),   # PC warp → 'C' (게임로직 트리거)
        (29, 19, 'M'),   # Mart warp → 'M'
        (32,  7, 'G'),   # Gym warp → 'G'
    ],
    'MAP_5': [
        (13, 25, 'C'),
        (23, 17, 'M'),
        (16, 17, 'G'),
    ],
    # MAP_0 (PT): 일반 집/연구소 워프는 'D'/'L'이 자동 매핑되거나 ESSENTIAL_ATOMS 로 처리
}

# ASCII char 풀 (map-aware 라우팅 전제)
# getTileArt(c, mapId) 가 mapId 로 dispatch 하므로 overworld는 lowercase/대문자 자유
# 제외: 시맨틱/ESSENTIAL + escape risk + 워프 트리거 시맨틱 (C/M/G)
RESERVED_SEMANTIC = set("DLGMTHPCN;~# .,")  # 워프/시맨틱
RESERVED_OTHER = set("\"'\\")               # C++ escape risk
ALL_PRINTABLE = set(chr(c) for c in range(33, 127))
AVAILABLE_POOL = sorted(ALL_PRINTABLE - RESERVED_SEMANTIC - RESERVED_OTHER)
# = 0-9 + EFIJKOQRSUVWXYZ + !@$%^&*()/  (약 31개)
print(f'[i] 가용 풀 ({len(AVAILABLE_POOL)} char): {"".join(AVAILABLE_POOL)}', file=sys.stderr)

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
for name, w, h, mid in MAPS:
    p = os.path.join(POKERED, 'maps', f'{name}.blk')
    with open(p, 'rb') as f:
        data = f.read()
    BLKS[mid] = (data, w, h, name)

variant_count = {}
for mid, (blk, w, h, name) in BLKS.items():
    for sy in range(h*2):
        for sx in range(w*2):
            atoms = step_atomics(blk, w, sx, sy)
            variant_count[atoms] = variant_count.get(atoms, 0) + 1

print(f'[i] Unique step variants: {len(variant_count)}', file=sys.stderr)

# ─── char 할당 ───────────────────────────────────────────────
variant_to_char = {}

# 1) ESSENTIAL_ATOMS 우선
for atoms, ch in ESSENTIAL_ATOMS.items():
    if atoms in variant_count:
        variant_to_char[atoms] = ch
    else:
        print(f'[WARN] ESSENTIAL atom {atoms} not found in any map (assigned char {ch} unused)', file=sys.stderr)

# 2) 빈도순으로 가용 풀에서 할당 (essential 이미 매핑된 atom 제외)
remaining = sorted(
    [a for a in variant_count if a not in variant_to_char],
    key=lambda a: -variant_count[a])
pool_iter = iter(AVAILABLE_POOL)
fallback_count = 0
for a in remaining:
    try:
        ch = next(pool_iter)
        # ESSENTIAL char와 충돌 확인 (방어적)
        while ch in set(ESSENTIAL_ATOMS.values()):
            ch = next(pool_iter)
        variant_to_char[a] = ch
    except StopIteration:
        variant_to_char[a] = '#'
        fallback_count += 1

print(f'[i] # fallback: {fallback_count} atoms', file=sys.stderr)
total_steps = sum(variant_count.values())
fallback_steps = sum(variant_count[a] for a in variant_to_char if variant_to_char[a] == '#')
print(f'[i] # fallback이 차지하는 step: {100*fallback_steps/total_steps:.2f}%', file=sys.stderr)

# ─── 맵 레이아웃 + 워프 오버라이드 ────────────────────────────
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
    # 워프 시맨틱 override (게임 로직용 char)
    for sx, sy, ch in WARP_SEMANTIC_OVERRIDE.get(mid, []):
        if sy < len(layout) and sx < len(layout[sy]):
            layout[sy] = layout[sy][:sx] + ch + layout[sy][sx+1:]
    map_layouts[mid] = (step_w, step_h, layout)

# ─── TileArt 생성 (모든 unique char) ─────────────────────────
# 각 char에 대해 char_to_first_atoms 매핑 + atom render
char_to_atoms = {}
for atoms, ch in variant_to_char.items():
    if ch not in char_to_atoms:
        char_to_atoms[ch] = atoms

# 워프 override char도 추가 (override 시 같은 atom을 다른 char로 - 별도 art 필요)
# 게임 로직 char 'C'/'M'/'G' 는 ESSENTIAL_ATOMS에 의해 사인 atom과 매핑됨
# 하지만 워프 위치엔 door step atom이 있으므로 'C'/'M'/'G' 는 사인 atom으로 렌더링
# (위치는 워프지만 시각은 사인 — 약간의 시각적 trade-off, 이걸 변경하려면 멀티 TileArt 필요)
# ESSENTIAL_ATOMS 가 이미 사인 atom을 'C'/'M'/'G' 에 매핑했으므로 OK

art_blocks = []
case_lines = []
for ch in sorted(char_to_atoms):
    atoms = char_to_atoms[ch]
    name = "OW3_C{:03d}".format(ord(ch))
    rows = render_step(atoms)
    art = ["// OW3 '{}' atoms={} count={}".format(ch, list(atoms), variant_count.get(atoms, 0)),
           "static const TileArt TILE_{} = {{{{".format(name)]
    for r in rows:
        art.append('    "{}",'.format(r))
    art.append("}}, '{}'}};".format(ch if ch != '\\' and ch != "'" else "\\" + ch))
    art.append("")
    art_blocks.append("\n".join(art))
    esc = "\\\\" if ch == '\\' else ("\\'" if ch == "'" else ch)
    case_lines.append("    case '{}': return &TILE_{};".format(esc, name))

# VISUAL_ALIAS: 게임로직용 char (C/M/G)는 alias로 같은 TileArt 참조
for alias_ch, target_ch in VISUAL_ALIAS.items():
    if target_ch in char_to_atoms:
        target_name = "OW3_C{:03d}".format(ord(target_ch))
        case_lines.append("    case '{}': return &TILE_{};  // alias of '{}' (door step)".format(alias_ch, target_name, target_ch))

# ─── tiles.h 패치 ────────────────────────────────────────────
OW_BEGIN    = "// --- OW3_AUTO_BEGIN (gen_overworld_tiles_v3.py) ---"
OW_END      = "// --- OW3_AUTO_END ---"
OW_SW_BEGIN = "    // OW3_SWITCH_BEGIN (gen_overworld_tiles_v3.py)"
OW_SW_END   = "    // OW3_SWITCH_END"

art_section  = OW_BEGIN + "\n" + "\n".join(art_blocks) + OW_END + "\n"
case_section = OW_SW_BEGIN + "\n" + "\n".join(case_lines) + "\n" + OW_SW_END + "\n"

with open(TILES_H, 'r', encoding='utf-8') as f:
    src = f.read()

# OW2 (v2) 섹션 제거
src = re.sub(r"// --- OW_AUTO_BEGIN.*?--- OW_AUTO_END ---\n", "", src, flags=re.S)
src = re.sub(r"    // OW_SWITCH_BEGIN.*?    // OW_SWITCH_END\n", "", src, flags=re.S)

# 기존 OW3 섹션 있으면 제거
src = re.sub(r"// --- OW3_AUTO_BEGIN.*?--- OW3_AUTO_END ---\n", "", src, flags=re.S)
src = re.sub(r"    // OW3_SWITCH_BEGIN.*?    // OW3_SWITCH_END\n", "", src, flags=re.S)

# Hand-crafted TileArt 들 제거 (v3에서는 atom-rendered로 대체)
HANDCRAFTED_NAMES = [
    'TILE_GROUND', 'TILE_PATH', 'TILE_GRASS', 'TILE_TALLGRASS',
    'TILE_WALL', 'TILE_HOUSE', 'TILE_TREE', 'TILE_WATER',
    'TILE_DOOR', 'TILE_LABDOOR', 'TILE_CENTER', 'TILE_GYM', 'TILE_MART',
    'TILE_TABLE'
]
for name in HANDCRAFTED_NAMES:
    pat = re.compile(r"//[^\n]*\n?static const TileArt " + re.escape(name) + r"\s*=\s*\{\{[^}]+\},\s*'[^']'\};\n?", re.S)
    src = pat.sub("", src)

# 기존 getTileArt 전체 함수 제거 (정규식 - getTileArt 부터 다음 함수 또는 끝까지)
# 우선 시그니처부터 닫는 } 까지 매칭 (carefully: 가장 짧은 매치)
m = re.search(r'inline const TileArt\* getTileArt\b', src)
if m:
    # 시작 위치부터 함수 끝 찾기 (brace counter)
    start = m.start()
    i = src.find('{', start)
    depth = 1
    i += 1
    while i < len(src) and depth > 0:
        if src[i] == '{': depth += 1
        elif src[i] == '}': depth -= 1
        i += 1
    end = i
    # remove old function
    src = src[:start] + src[end:]
else:
    print('[WARN] old getTileArt not found', file=sys.stderr)

# 가장 흔한 atom의 char를 fallback으로
most_common_atom = max(variant_count, key=lambda a: variant_count[a])
fallback_char = variant_to_char[most_common_atom]
fallback_tile = "TILE_OW3_C{:03d}".format(ord(fallback_char))

# 새 getTileArt: map-aware
indoor_switch_lines = [
    "        // Shared semantic + atoms",
    "        case ' ': return &" + fallback_tile + ";",
    "        case 'D': return &TILE_OW3_C068;",
    "        case ';': return &TILE_OW3_C059;",
    "        // Reds house",
    "        case 'a': return &TILE_RH_A;",
    "        case 'b': return &TILE_RH_B;",
    "        case 'c': return &TILE_RH_C;",
    "        case 'd': return &TILE_RH_D;",
    "        case 'e': return &TILE_RH_E;",
    "        case 'f': return &TILE_RH_F;",
    "        case 'g': return &TILE_RH_G;",
    "        case 'h': return &TILE_RH_H;",
    "        case 'i': return &TILE_RH_I;",
    "        case 'j': return &TILE_RH_J;",
    "        case 'k': return &TILE_RH_K;",
    "        case 'l': return &TILE_RH_L;",
    "        case 'm': return &TILE_RH_M;",
    "        case 'n': return &TILE_RH_N;",
    "        case 'o': return &TILE_RH_O;",
    "        case 'p': return &TILE_RH_P;",
    "        case 'q': return &TILE_RH_Q;",
    "        case 'r': return &TILE_RH_R;",
    "        case 's': return &TILE_RH_S;",
    "        case 't': return &TILE_RH_T;",
    "        case 'u': return &TILE_RH_U;",
    "        case 'v': return &TILE_RH_V;",
    "        case 'w': return &TILE_RH_W;",
    "        case 'x': return &TILE_RH_X;",
    "        case 'y': return &TILE_RH_Y;",
    "        case 'z': return &TILE_RH_Z;",
    "        case 'A': return &TILE_RH_XA;",
    "        case 'B': return &TILE_RH_XB;",
    "        // OakLab",
    "        case '+': return &TILE_OL_FLOOR;",
    "        case '?': return &TILE_OL_C063;",
    "        case ']': return &TILE_OL_C093;",
    "        case '`': return &TILE_OL_C096;",
    "        case '[': return &TILE_OL_C091;",
    "        case '<': return &TILE_OL_C060;",
    "        case '>': return &TILE_OL_C062;",
    "        case '=': return &TILE_OL_C061;",
    "        case '_': return &TILE_OL_C095;",
    "        case '|': return &TILE_OL_C124;",
    "        case ':': return &TILE_OL_C058;",
    "        case '-': return &TILE_OL_C045;",
    "        case '{': return &TILE_OL_C123;",
    "        case '}': return &TILE_OL_C125;",
    "        default: return &" + fallback_tile + ";",
]

new_getTileArt = (
    "inline const TileArt* getTileArt(char c, int mapId = 0) {\n"
    "    if (mapId >= 0 && mapId <= 5) {\n"
    "        switch (c) {\n"
    + "\n".join("        " + l.strip() for l in case_lines) + "\n"
    "            default: return &" + fallback_tile + ";\n"
    "        }\n"
    "    }\n"
    "    switch (c) {\n"
    + "\n".join(indoor_switch_lines) + "\n"
    "    }\n"
    "}\n"
)

# Insert OW3 art + new getTileArt right before any remaining handcrafted reference
# Find a safe insertion point: end of file (before final '}') or after last hand-crafted TileArt
# Simpler: append art_section + new_getTileArt at end of src
src += "\n" + art_section + "\n" + new_getTileArt + "\n"

with open(TILES_H, 'w', encoding='utf-8') as f:
    f.write(src)
print(f'[OK] tiles.h 패치 완료: {len(art_blocks)}개 atom-rendered TileArt', file=sys.stderr)

# ─── map_data.h 패치 ─────────────────────────────────────────
with open(MAP_H, 'r', encoding='utf-8') as f:
    md = f.read()

for mid, (step_w, step_h, layout) in map_layouts.items():
    pattern = (r'(inline MapDef ' + mid + r'\s*=\s*\{[^{}]*\{\s*\n)'
               r'((?:[^{}]|\n)*?nullptr\s*\n\s*)'
               r'(\})')
    new_tiles = "        // gen_overworld_tiles_v3.py 자동 생성\n"
    for r in layout:
        new_tiles += '        "{}",\n'.format(r)
    new_tiles += "        nullptr\n    "
    md_new, n = re.subn(pattern, lambda m: m.group(1) + new_tiles + m.group(3),
                        md, count=1, flags=re.S)
    if n != 1:
        print(f'[WARN] {mid} tiles[] 치환 실패', file=sys.stderr)
    else:
        md = md_new
        print(f'[OK] {mid} ({step_w}x{step_h}) tiles[] 갱신', file=sys.stderr)

# ─── walkable / encounter ────────────────────────────────────
walkable_chars = set()
encounter_chars = set()
for atoms, ch in variant_to_char.items():
    bl = atoms[2]
    if bl in OVERWORLD_PASS:
        walkable_chars.add(ch)
    if ATOM_TALL_GRASS in atoms:
        walkable_chars.add(ch)
        encounter_chars.add(ch)
# 시맨틱 - door step은 walkable
walkable_chars.add('D')
walkable_chars.add('C')
walkable_chars.add('M')
walkable_chars.add('G')
walkable_chars.add('L')
walkable_chars.add(' ')
walkable_chars.add('+')   # oaklab floor
walkable_chars.add('i')   # interior
walkable_chars.add('u')   # interior
walkable_chars.discard('T')   # cut tree unwalkable
walkable_chars.discard('#')   # wall unwalkable
walkable_chars.discard('~')   # water unwalkable
walkable_chars.discard('H')   # house door visual unwalkable
walkable_chars.discard('N')   # roof unwalkable
walkable_chars.discard('P')   # gym door right unwalkable

walk_exprs = " ||\n           ".join(sorted("t == '{}'".format(c if c != "'" else "\\'") for c in walkable_chars))
enc_exprs = " || ".join(sorted("t == '{}'".format(c) for c in encounter_chars))

new_walkable = (
    'inline bool tileWalkable(char t) {\n'
    '    return ' + walk_exprs + ';\n'
    '}'
)
new_encounter = (
    'inline bool tileIsEncounter(char t) {\n'
    '    return ' + enc_exprs + ';\n'
    '}'
)

md = re.sub(r'inline bool tileWalkable\([^)]*\)\s*\{[^}]*\}', new_walkable, md, count=1)
md = re.sub(r'inline bool tileIsEncounter\([^)]*\)\s*\{[^}]*\}', new_encounter, md, count=1)

with open(MAP_H, 'w', encoding='utf-8') as f:
    f.write(md)
print('[OK] map_data.h walkable/encounter 갱신', file=sys.stderr)

# ─── 요약 ────────────────────────────────────────────────────
print('', file=sys.stderr)
print('═══════════════════════════════════════════════════', file=sys.stderr)
print('v3 완료. 모든 hand-crafted TileArt 제거됨.', file=sys.stderr)
print('═══════════════════════════════════════════════════', file=sys.stderr)
print(f'TileArt 개수: {len(art_blocks)}', file=sys.stderr)
print(f'# fallback: {fallback_count} atoms ({100*fallback_steps/total_steps:.2f}% of steps)', file=sys.stderr)
print(f'Walkable chars: {sorted(walkable_chars)}', file=sys.stderr)
print(f'Encounter chars: {sorted(encounter_chars)}', file=sys.stderr)
