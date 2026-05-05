#!/usr/bin/env python3
"""
OakLab.blk + gym.bst → 모든 step의 unique quadrant variant 자동 식별.
각 variant에 char 할당 + tile art 자동 합성 + layout 자동 생성.

가구 정밀 매핑:
- block의 4 quadrant마다 다른 atomic 4-tuple → 별도 variant
- 카운터 모서리/책장 윗부분 등 디테일 정확
- 14종 variant (typical OakLab) → tiles.h 자동 패치

출력:
1. tiles.h에 자동 패치 (마커 사이 교체)
2. stdout에 map_data.h MAP_6 layout char grid (10×12) — 수동 복사
"""
import os, re
from PIL import Image

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
ASSETS = os.path.join(ROOT, 'pokered_assets')

# ─── 자산 로드 ────────────────────────────────────────────────────
PNG = Image.open(os.path.join(ASSETS, 'gfx/tilesets/gym.png')).convert('L')
with open(os.path.join(ASSETS, 'gfx/blocksets/gym.bst'), 'rb') as f:
    BST = f.read()
with open(os.path.join(ASSETS, 'maps/OaksLab.blk'), 'rb') as f:
    BLK = f.read()

BLK_W, BLK_H = 5, 6
STEP_W, STEP_H = BLK_W * 2, BLK_H * 2  # 10×12

# ─── ANSI half-block 변환 ─────────────────────────────────────────
def classify(v):
    """GB 4단계 (0/85/170/255) 정확 분리"""
    if v > 240: return 0  # 흰
    if v > 127: return 1  # 옅은회
    if v > 42:  return 2  # 진회
    return 3              # 검정

PALETTE = (255, 250, 244, 232)  # 그레이스케일

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

def step_atomics(sx, sy):
    """step (sx, sy)의 4 atomic 인덱스 (TL, TR, BL, BR) — 16×16 픽셀 구성"""
    bx, by = sx // 2, sy // 2
    qx, qy = sx % 2, sy % 2
    block_id = BLK[by * BLK_W + bx]
    a = BST[block_id*16:(block_id+1)*16]
    # block 4×4 atomic의 quadrant (qx,qy) = atomics index (qy*2 .. qy*2+1) × (qx*2 .. qx*2+1)
    base_y = qy * 2
    base_x = qx * 2
    return (a[base_y*4 + base_x],     a[base_y*4 + base_x + 1],
            a[(base_y+1)*4 + base_x], a[(base_y+1)*4 + base_x + 1])

def render_step(atoms):
    """16×16 픽셀 (4 atomics) → 16chars × 8 rows ANSI"""
    img = Image.new('L', (16, 16), 255)
    img.paste(get_atomic(atoms[0]), (0, 0))
    img.paste(get_atomic(atoms[1]), (8, 0))
    img.paste(get_atomic(atoms[2]), (0, 8))
    img.paste(get_atomic(atoms[3]), (8, 8))
    rows = []
    for hy in range(8):
        row = ""
        for x in range(16):
            t = classify(img.getpixel((x, hy*2)))
            b = classify(img.getpixel((x, hy*2 + 1)))
            row += cell(t, b)
        rows.append(row)
    return rows

# ─── Step 분석: unique variant 식별 ────────────────────────────────
# 모든 step의 atomic 4-tuple → unique 그룹화 (door 04는 따로 'D'로)

DOOR_VARIANT = (0x06, 0x06, 0x16, 0x16)  # block 0x04의 BL/BR
FLOOR_VARIANT = (0x11, 0x11, 0x11, 0x11)

# char pool — 다른 맵에서 안 쓰는 ASCII만.
# 'D'는 다른 맵(Pewter Gym, Rival House)의 도어와 충돌하므로 OakLab은 '?' 별도 char 사용.
CHAR_POOL = list("+]`[<>=_|:-{}/")  # 11종 (D, ? 제외)

variant_to_char = {FLOOR_VARIANT: '+', DOOR_VARIANT: '?'}
char_index = 0
def reserve_char():
    global char_index
    while char_index < len(CHAR_POOL):
        c = CHAR_POOL[char_index]; char_index += 1
        if c == '+': continue  # 이미 floor
        if c in variant_to_char.values(): continue
        return c
    raise RuntimeError("char pool 부족")

# 모든 step을 한 번 순회하여 unique variant 등록
layout = []
for sy in range(STEP_H):
    row = ""
    for sx in range(STEP_W):
        atoms = step_atomics(sx, sy)
        if atoms not in variant_to_char:
            variant_to_char[atoms] = reserve_char()
        row += variant_to_char[atoms]
    layout.append(row)

# variant → char 역방향
char_to_variant = {v: k for k, v in variant_to_char.items()}

# ─── tile art 생성 ──────────────────────────────────────────────
TILE_NAMES = {
    '+': 'OL_FLOOR', 'D': 'OL_DOOR',  # 'D'는 기존 TILE_DOOR가 있어 충돌 — 도어는 기존 사용
}
def name_for(ch):
    if ch in TILE_NAMES: return TILE_NAMES[ch]
    # 안전한 식별자 만들기 (ASCII 코드)
    return f"OL_C{ord(ch):03d}"

art_blocks = []
case_lines = []
for ch, atoms in char_to_variant.items():
    rows = render_step(atoms)
    name = name_for(ch)
    art = [f"// OakLab variant '{ch}' (atoms {atoms})",
           f"static const TileArt TILE_{name} = {{{{"]
    for r in rows:
        art.append(f'    "{r}",')
    art.append(f"}}, '{ch}'}};")
    art.append("")
    art_blocks.append("\n".join(art))
    if ch == '+':
        case_lines.append(f"    case '+': return &TILE_OL_FLOOR;")
    else:
        # ANSI escape 안전한 case
        if ch in "\\'":
            case_lines.append(f"    case '\\{ch}': return &TILE_{name};")
        else:
            case_lines.append(f"    case '{ch}': return &TILE_{name};")

# ─── tiles.h 패치 ─────────────────────────────────────────────────
OAKLAB_BEGIN = "// ─── OAKLAB_AUTO_BEGIN (gen_oaklab_tiles.py) ───"
OAKLAB_END   = "// ─── OAKLAB_AUTO_END ───"
SWITCH_BEGIN = "    // OAKLAB_SWITCH_BEGIN (gen_oaklab_tiles.py)"
SWITCH_END   = "    // OAKLAB_SWITCH_END"

art_section  = OAKLAB_BEGIN + "\n" + "\n".join(art_blocks) + OAKLAB_END + "\n"
case_section = SWITCH_BEGIN + "\n" + "\n".join(case_lines) + "\n" + SWITCH_END + "\n"

TILES_H = os.path.join(ROOT, 'src/data/tiles.h')
with open(TILES_H, 'r', encoding='utf-8') as f:
    src = f.read()

if OAKLAB_BEGIN in src:
    src = re.sub(re.escape(OAKLAB_BEGIN) + r".*?" + re.escape(OAKLAB_END) + r"\n",
                 lambda m: art_section, src, count=1, flags=re.S)
else:
    src = src.replace("inline const TileArt* getTileArt(char c) {",
                      art_section + "inline const TileArt* getTileArt(char c) {")

if SWITCH_BEGIN in src:
    src = re.sub(re.escape(SWITCH_BEGIN) + r".*?" + re.escape(SWITCH_END) + r"\n",
                 lambda m: case_section, src, count=1, flags=re.S)
else:
    src = re.sub(r"(\s*)(default:\s*return\s*&TILE_GROUND;)",
                 "\n" + case_section + r"\1\2", src, count=1)

with open(TILES_H, 'w', encoding='utf-8') as f:
    f.write(src)

print(f"✓ tiles.h 패치 완료: {len(art_blocks)}종 step variant")
print(f"  walkable: '+' (floor), 'D' (door)")
print(f"  unwalkable: " + " ".join(repr(c) for c in char_to_variant if c not in '+D'))
print()
print("─── MAP_6 layout (10×12) — map_data.h에 복사 ───")
for r in layout:
    print(f'        "{r}",')
print()
print(f"variant → atomics mapping:")
for ch, atoms in char_to_variant.items():
    if ch in '+D': continue
    print(f"  '{ch}' = {atoms}")
