#!/usr/bin/env python3
"""
overworld 타일셋 step-level 디코더.

원본 pokered 의 .blk + overworld.bst + overworld.png 를 정확히 디코딩 →
각 unique 16×16px step variant 에 char 할당 + ANSI tile art 생성.

이 스크립트는 BGM / NPC / 워프 / 인카운터 같은 .asm 데이터는 건드리지 않음.
오직 타일 격자 + tile art 만 출력. NPC 등은 별도로 .asm 직역.

처리 맵 (pokered/constants/map_constants.asm 기준 정확한 차원):
  PalletTown   10× 9 → step  20×18
  Route1       10×18 → step  20×36
  Route2       10×36 → step  20×72
  ViridianCity 20×18 → step  40×36
  PewterCity   20×18 → step  40×36

입력:
  pokered/maps/<MapName>.blk
  pokered/gfx/blocksets/overworld.bst
  pokered_assets/gfx/tilesets/overworld.png  (또는 pokered/gfx/tilesets/overworld.png)

출력:
  src/data/tiles.h
    - OW_AUTO_BEGIN/END 사이에 모든 step variant TileArt 정의
    - OW_SWITCH_BEGIN/END 사이에 case 라인
  stdout:
    - 각 맵의 정확한 char 격자 (map_data.h tiles[] 에 그대로 복사)
    - walkable/encounter/door char 추천 분류 (수동으로 tileWalkable 등에 반영)
    - 새 맵 차원 (map_data.h MapDef.mapW/mapH 갱신용)
"""
import os, re, sys
from PIL import Image

# --- 경로 -------------------------------------------------------
ROOT    = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
ASSETS  = os.path.join(ROOT, 'pokered_assets')
POKERED = os.path.abspath(os.path.join(ROOT, '..', 'pokered'))

# 타일셋 PNG 는 pokered_assets 우선, 없으면 pokered 원본
def find_tileset_png():
    for base in (ASSETS, POKERED):
        p = os.path.join(base, 'gfx', 'tilesets', 'overworld.png')
        if os.path.exists(p):
            return p
    raise FileNotFoundError("overworld.png 못 찾음")

PNG_PATH = find_tileset_png()
BST_PATH = os.path.join(POKERED, 'gfx', 'blocksets', 'overworld.bst')

PNG = Image.open(PNG_PATH).convert('L')
with open(BST_PATH, 'rb') as f:
    BST = f.read()

# --- 맵 목록: (이름, blk_w, blk_h, cpp_id) ---------------------
MAPS = [
    ("PalletTown",    10,  9, "MAP_0"),
    ("Route1",        10, 18, "MAP_1"),
    ("ViridianCity",  20, 18, "MAP_2"),
    ("Route2",        10, 36, "MAP_3"),
    ("PewterCity",    20, 18, "MAP_5"),
]

# 각 맵 .blk 로드 + 크기 검증
BLKS = {}
for name, w, h, cid in MAPS:
    p = os.path.join(POKERED, 'maps', f'{name}.blk')
    with open(p, 'rb') as f:
        data = f.read()
    if len(data) != w * h:
        print(f"[FATAL] {name}.blk = {len(data)}B, expected {w}×{h}={w*h}B")
        sys.exit(1)
    BLKS[name] = (data, w, h)

# --- pokered 원본 atomic 분류 (bake_assets.py 와 동일) ----------
OVERWORLD_PASS       = {0x00, 0x10, 0x1b, 0x20, 0x21, 0x23, 0x2c, 0x2d, 0x2e,
                        0x30, 0x31, 0x33, 0x39, 0x3c, 0x3e, 0x52, 0x54, 0x58, 0x5b}
OVERWORLD_TALL_GRASS = {0x52}
OVERWORLD_DOOR       = {0x1b, 0x58}

def category(atoms):
    """step 의 atomic 4-tuple → 카테고리 ('door','tallgrass','ground','path','water','wall')"""
    s = set(atoms)
    if s & OVERWORLD_DOOR:       return 'door'
    if s & OVERWORLD_TALL_GRASS: return 'tallgrass'
    pc = sum(1 for t in atoms if t in OVERWORLD_PASS)
    if pc >= 2:
        if 0x10 in s or any(t in {0x11, 0x12, 0x13} for t in s):
            return 'path'
        if any(0x3c <= t <= 0x3f for t in s) and pc < 4:
            return 'water'
        return 'ground'
    if all(0x14 <= t <= 0x27 for t in atoms): return 'water'
    if all(0x28 <= t <= 0x2f or t in {0x3c,0x3d,0x3e,0x3f} for t in atoms): return 'water'
    return 'wall'

# --- ANSI 변환 --------------------------------------------------
def classify_px(v):
    if v > 200: return 0
    if v > 130: return 1
    if v > 60:  return 2
    return 3

PALETTE = (255, 250, 244, 232)
def fg(c): return f"38;5;{PALETTE[c]}"
def bg(c): return f"48;5;{PALETTE[c]}"
def cell(top, bot):
    if top == bot:
        return f"\\x1b[{bg(top)}m \\x1b[0m"
    return f"\\x1b[{fg(top)};{bg(bot)}m\\xe2\\x96\\x80\\x1b[0m"

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
            row += cell(t, b)
        rows.append(row)
    return rows

# --- 모든 맵에서 unique step variant 수집 ----------------------
variant_count = {}   # atom 4-tuple → 등장 횟수
variant_cat   = {}   # atom 4-tuple → 카테고리
for name, w, h, cid in MAPS:
    blk, blk_w, blk_h = BLKS[name]
    for sy in range(blk_h * 2):
        for sx in range(blk_w * 2):
            atoms = step_atomics(blk, blk_w, sx, sy)
            variant_count[atoms] = variant_count.get(atoms, 0) + 1
            variant_cat[atoms]   = category(atoms)

print(f"[수집] 총 {len(variant_count)}종 unique step variant", file=sys.stderr)
by_cat = {}
for atoms, cat in variant_cat.items():
    by_cat.setdefault(cat, []).append(atoms)
for cat, lst in sorted(by_cat.items()):
    print(f"  {cat}: {len(lst)}종", file=sys.stderr)

# --- char 할당 --------------------------------------------------
# 다른 타일셋과 충돌 방지:
#   reds_house: a-z, A, B, C  (절대 사용 금지)
#   oaklab:     + ] ` [ < > = _ | : - { } ?
#   semantic:   ' '(grass) . , ; # ~ T s H D L G M N P B
#
# 사용 가능한 char (overworld 전용 풀):
#   숫자 0-9 (10)
#   대문자 중 reds_house/semantic 제외: E F I J K O Q R S U V W X Y Z (15)
#   특수문자: ! @ $ % ^ & *  (  ) " ' / \  (몇 개)
#
# 시맨틱 의미 유지:
#   'D'  = door (워프 트리거 - overworld.cpp tryMove 에서 'D' 인식)
#   ';'  = tall grass (인카운터)
#   '~'  = water (현재 walkable=false, 시각)
#
# 'ground', 'path', 'water_inner', 'wall' 카테고리의 unique variants 는 풀에서 할당.

# --- PT 보존 모드: 현 map_data.h 의 MAP_0 (PalletTown) char 매핑 학습 ---
# 사용자 주장: PalletTown 은 이미 원본 게임과 같음 → 그 char 매핑을 정답으로 받음.
# 학습된 (atom 4-tuple → char) 를 variant_to_char 에 pin → 다른 맵에서 같은 variant 시 재사용.
MAP_DATA_H = os.path.join(ROOT, 'src', 'data', 'map_data.h')

def load_pt_char_mapping():
    """현 map_data.h 의 MAP_0 tiles[] 읽고, (sx,sy) → char 매핑 추출.
    그 다음 PalletTown.blk + overworld.bst 로 (sx,sy) → atom 4-tuple 매핑 계산.
    결합 → (atom 4-tuple → char) 매핑 반환."""
    if not os.path.exists(MAP_DATA_H):
        return {}
    with open(MAP_DATA_H, 'r', encoding='utf-8') as f:
        md_src = f.read()
    # MAP_0 의 tiles[] 추출. "{ 로 시작하고 nullptr 로 끝나는 첫 번째 블록을 MAP_0 직후로 찾기.
    m = re.search(r'MAP_0\s*=\s*\{(?:[^{]|\{[^{]*?\})*?\{\s*((?:"[^"]*",\s*)+)\s*nullptr',
                  md_src, re.DOTALL)
    if not m:
        return {}
    body = m.group(1)
    rows = re.findall(r'"([^"]*)"', body)
    pt_blk, pt_w, pt_h = BLKS['PalletTown']
    if len(rows) != pt_h * 2:
        print(f"[WARN] PT row 수 {len(rows)} != expected {pt_h*2}, PT 보존 skip", file=sys.stderr)
        return {}
    mapping = {}
    for sy, row in enumerate(rows):
        if len(row) != pt_w * 2:
            continue
        for sx, ch in enumerate(row):
            atoms = step_atomics(pt_blk, pt_w, sx, sy)
            if atoms in mapping and mapping[atoms] != ch:
                # 모순: 같은 atom 4-tuple 인데 char 가 다름 → 첫 매핑 유지
                continue
            mapping[atoms] = ch
    print(f"[PT 보존] MAP_0 에서 {len(mapping)}종 atom-char 매핑 학습", file=sys.stderr)
    return mapping

PT_PINNED = load_pt_char_mapping()

# 시맨틱 pinned: door 모두 'D', tallgrass 모두 ';'
PINNED_CATS = {'door': 'D', 'tallgrass': ';'}

# 카테고리 default char (해당 카테고리에서 char pool 부족 시 fallback)
DEFAULT_CAT = {
    'ground': ' ',
    'path':   '.',
    'water':  '~',
    'wall':   '#',
}

# 카테고리별 unique variant 할당용 풀.
# reds_house(a-z, A-C), oaklab(+ ] ` [ < > = _ | : - { } ?), semantic(D L G M H T s N B P) 제외.
POOLS = {
    'ground': list("0123456789EF"),                # 12
    'path':   list("IJKOQR"),                      # 6
    'water':  list("UVWXYZ"),                      # 6
    'wall':   list("!@$%^&*()/"),                  # 10  (oaklab/escape 충돌 없는 chars)
}

variant_to_char = {}

# 0) PT 보존: PalletTown 에서 학습한 매핑 우선 적용 (PT 의 char 가 그대로 다른 맵에도 재사용됨)
pt_reused = 0
for atoms in variant_count:
    if atoms in PT_PINNED:
        variant_to_char[atoms] = PT_PINNED[atoms]
        pt_reused += 1
print(f"[PT 보존] {pt_reused}/{len(variant_count)} variant 가 PT char 재사용", file=sys.stderr)

# PT 가 쓴 chars 는 풀에서 제거 (중복 방지)
pt_used_chars = set(PT_PINNED.values())

# 1) pinned: door / tallgrass (PT 가 안 쓴 경우만 — 보통 PT 도 'D' ';' 사용)
for atoms, cat in variant_cat.items():
    if atoms in variant_to_char:
        continue
    if cat in PINNED_CATS:
        variant_to_char[atoms] = PINNED_CATS[cat]

# 2) 카테고리별: 빈도 높은 순으로 풀에서 할당 → 풀 초과시 default char fallback
remaining = [(atoms, variant_count[atoms], variant_cat[atoms])
             for atoms in variant_count if atoms not in variant_to_char]
remaining.sort(key=lambda x: -x[1])

# 각 풀에서 PT 가 이미 쓴 chars 제거 (중복 방지)
POOLS_FILTERED = {cat: [c for c in pool if c not in pt_used_chars]
                  for cat, pool in POOLS.items()}

pool_idx = {k: 0 for k in POOLS_FILTERED}
fallbacks_per_cat = {}
for atoms, cnt, cat in remaining:
    pool = POOLS_FILTERED.get(cat, POOLS_FILTERED['wall'])
    if pool_idx.get(cat, 0) >= len(pool):
        # 풀 초과 → 카테고리 default char 로 fallback (시각 손실, 기능 보존)
        default_ch = DEFAULT_CAT.get(cat, '#')
        variant_to_char[atoms] = default_ch
        fallbacks_per_cat.setdefault(cat, 0)
        fallbacks_per_cat[cat] += 1
        continue
    variant_to_char[atoms] = pool[pool_idx[cat]]
    pool_idx[cat] += 1

# 추가 안전장치: PT 보존으로 사용한 char 이 이미 tiles.h switch 에 있으면 OW 가 art 재정의 안 함
# (아래 art 생성 루프에서 ch in pt_used_chars 면 skip)

# 카테고리별 default char 가 unique variant 중 하나로 픽되도록 - 첫 fallback 위치를
# 가장 평균적인 atom (가장 흔한 variant) 으로 강제 - 이미 빈도순 sort 였으므로 그대로 OK.

if fallbacks_per_cat:
    print(f"\n[INFO] char pool 초과 - 카테고리 default char fallback 적용:", file=sys.stderr)
    for cat, n in fallbacks_per_cat.items():
        print(f"  {cat}: {n}종 variant → '{DEFAULT_CAT[cat]}' (시각 손실, 기능 OK)", file=sys.stderr)

# 같은 char 의 atoms 중복 가능 (pinned 'D' 가 여러 atom 에 매핑) - 첫 출현 atom 으로 art 생성
char_to_first_atoms = {}
for atoms, ch in variant_to_char.items():
    if ch not in char_to_first_atoms:
        char_to_first_atoms[ch] = atoms

# --- tile art 생성 ----------------------------------------------
NAME_MAP = {
    'D': 'OW_DOOR',      # 다른 'D' 와 충돌 - gen_tiles.py 의 TILE_DOOR 가 이미 있음
    ';': 'OW_TALLGRASS', # 동일 - TILE_TALLGRASS 이미 있음
}
# 위 두 char 의 art 는 새로 안 만들고 기존 사용 → switch case 도 안 추가

art_blocks = []
case_lines = []
for ch in sorted(char_to_first_atoms.keys()):
    atoms = char_to_first_atoms[ch]
    cat   = variant_cat[atoms]
    # pinned 'D' / ';' 는 기존 TILE_DOOR/TILE_TALLGRASS 재사용 - 새 art 안 만듦
    if ch in PINNED_CATS.values():
        continue
    # PT 보존: PT 가 쓰는 chars 는 기존 TILE_PT_* 가 이미 있음 - 새 art 안 만듦
    if ch in pt_used_chars:
        continue
    name = f"OW_C{ord(ch):03d}"
    rows = render_step(atoms)
    art = [f"// Overworld '{ch}' ({cat}) atoms={list(atoms)}",
           f"static const TileArt TILE_{name} = {{{{"]
    for r in rows:
        art.append(f'    "{r}",')
    art.append(f"}}, '{ch}'}};")
    art.append("")
    art_blocks.append("\n".join(art))
    esc = f"\\{ch}" if ch in ("'", "\\") else ch
    case_lines.append(f"    case '{esc}': return &TILE_{name};")

# --- tiles.h 패치 -----------------------------------------------
OW_BEGIN    = "// --- OW_AUTO_BEGIN (gen_overworld_tiles.py) ---"
OW_END      = "// --- OW_AUTO_END ---"
OW_SW_BEGIN = "    // OW_SWITCH_BEGIN (gen_overworld_tiles.py)"
OW_SW_END   = "    // OW_SWITCH_END"

art_section  = OW_BEGIN + "\n" + "\n".join(art_blocks) + OW_END + "\n"
case_section = OW_SW_BEGIN + "\n" + "\n".join(case_lines) + "\n" + OW_SW_END + "\n"

TILES_H = os.path.join(ROOT, 'src', 'data', 'tiles.h')
with open(TILES_H, 'r', encoding='utf-8') as f:
    src = f.read()

# art 섹션 패치: 기존 마커 있으면 교체, 없으면 OAKLAB_AUTO_BEGIN 앞에 삽입
if OW_BEGIN in src:
    src = re.sub(re.escape(OW_BEGIN) + r".*?" + re.escape(OW_END) + r"\n",
                 lambda m: art_section, src, count=1, flags=re.S)
else:
    target = "// --- OAKLAB_AUTO_BEGIN"
    if target in src:
        src = src.replace(target, art_section + target)
    else:
        src = src.replace("inline const TileArt* getTileArt(char c) {",
                          art_section + "inline const TileArt* getTileArt(char c) {")

# switch 섹션 패치: 기존 마커 있으면 교체, 없으면 OAKLAB_SWITCH_BEGIN 앞에 삽입
if OW_SW_BEGIN in src:
    src = re.sub(re.escape(OW_SW_BEGIN) + r".*?" + re.escape(OW_SW_END) + r"\n",
                 lambda m: case_section, src, count=1, flags=re.S)
else:
    sw_target = "    // OAKLAB_SWITCH_BEGIN"
    if sw_target in src:
        src = src.replace(sw_target, case_section + sw_target)
    else:
        # 마지막 case 앞에 삽입
        src = re.sub(r"(\s*default:\s*return\s*&TILE_GROUND;)",
                     "\n" + case_section + r"\1", src, count=1)

with open(TILES_H, 'w', encoding='utf-8') as f:
    f.write(src)

print(f"\n✓ tiles.h 패치 완료: {len(art_blocks)}종 새 OW step variant tile art", file=sys.stderr)

# --- 각 맵 layout 출력 ------------------------------------------
print("\n" + "="*70)
print("=== map_data.h 에 복사할 데이터 ===")
print("="*70)

for name, w, h, cid in MAPS:
    blk, blk_w, blk_h = BLKS[name]
    step_w, step_h = blk_w * 2, blk_h * 2
    layout = []
    for sy in range(step_h):
        row = ""
        for sx in range(step_w):
            atoms = step_atomics(blk, blk_w, sx, sy)
            row += variant_to_char[atoms]
        layout.append(row)

    print(f"\n--- {name} ({cid}) - mapW={step_w}, mapH={step_h} ---")
    print(f"// MapDef.mapW = {step_w}, mapH = {step_h} 확인 필요")
    for r in layout:
        print(f'        "{r}",')

# --- walkability 분류 추천 --------------------------------------
print("\n" + "="*70)
print("=== walkability 분류 (map_data.h tileWalkable 갱신용) ===")
print("="*70)

walkable_chars   = set()
encounter_chars  = set()
door_chars       = set()

for atoms, ch in variant_to_char.items():
    cat = variant_cat[atoms]
    # walkable: atom 4개 중 2개 이상이 OVERWORLD_PASS 면 walkable
    pc = sum(1 for t in atoms if t in OVERWORLD_PASS)
    if pc >= 2:
        walkable_chars.add(ch)
    if cat == 'tallgrass':
        walkable_chars.add(ch)
        encounter_chars.add(ch)
    if cat == 'door':
        # door는 unwalkable (워프 trigger 전제) - walkable 에는 추가 안 함
        door_chars.add(ch)

# 'D' 는 unwalkable (워프 트리거), ';' 는 walkable+encounter
walkable_chars.discard('D')
walkable_chars.add(';')
encounter_chars.add(';')
door_chars.add('D')

print(f"\nwalkable chars (overworld): {' '.join(repr(c) for c in sorted(walkable_chars))}")
print(f"encounter chars (overworld): {' '.join(repr(c) for c in sorted(encounter_chars))}")
print(f"door/warp chars (overworld): {' '.join(repr(c) for c in sorted(door_chars))}")

print("\n# tileWalkable 함수에 추가할 조건:")
expr = " || ".join(f"t == '{c}'" for c in sorted(walkable_chars))
print(f"#   {expr}")
print("\n# tileIsEncounter:")
print(f"#   t == '" + "' || t == '".join(sorted(encounter_chars)) + "'")
print("\n# overworld.cpp tryMove 의 도어 char 목록은 'D' 이미 포함됨 (변경 불필요)")

print("\n" + "="*70)
print("=== 완료 ===")
print("="*70)
print("\n다음 단계:")
print("  1) 위 layout 을 src/data/map_data.h 의 MAP_0..MAP_5 tiles[] 에 복사")
print("  2) mapW/mapH 변경 (특히 Route2, PewterCity)")
print("  3) tileWalkable / tileIsEncounter 갱신")
print("  4) build.bat 빌드 후 시각 확인")
