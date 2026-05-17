#!/usr/bin/env python3
"""
1회성 변환기: pokered_assets/ 안의 .blk + .bst + .asm 을 PNG + JSON 으로 변환.
실행 후 원본 .blk/.bst/.asm 은 삭제됨.

이후 gen_maps.py는 PNG + JSON만 읽음.

PNG 인코딩: 1픽셀 = 1타일. 픽셀 RGB 색이 타일 char 를 인코딩.
복호 불변성을 위해 char 별 고정 색 매핑 사용.
"""
import os, re, sys, json
from PIL import Image

ROOT   = os.path.dirname(os.path.abspath(__file__))
ASSETS = os.path.normpath(os.path.join(ROOT, "..", "pokered_assets"))

# ─── char ↔ RGB 색상 매핑 (round-trip 가능, 시각적 직관 우선) ────
CHAR_TO_COLOR = {
    ' ': (120, 200,  80),   # 풀밭/빈땅
    '.': (210, 180, 110),   # 길
    ',': (160, 220, 110),   # 짧은풀
    ';': ( 40, 130,  40),   # 긴풀
    '#': (110, 110, 110),   # 벽/건물
    '~': ( 50, 110, 200),   # 물
    'D': (220,  80,  80),   # 문
    's': (240, 220, 100),   # 표지판
    'T': ( 25,  80,  30),   # 나무
    'N': (255,   0, 255),   # NPC 자리(미사용)
    'H': (180, 140,  80),   # 인테리어 바닥
    'L': (220,  60,  60),   # lab door
    'C': (220,  90,  90),   # center door
    'G': (220, 100, 100),   # gym door
    'M': (220, 110, 110),   # mart door
    'B': (140, 140, 140),   # block
    'P': (200, 200, 200),   # 테이블
}
COLOR_TO_CHAR = {v: k for k, v in CHAR_TO_COLOR.items()}

# ─── 타일 분류 (gen_maps.py 에서 흡수) ──────────────────────────
OVERWORLD_PASS = {0x00, 0x10, 0x1b, 0x20, 0x21, 0x23, 0x2c, 0x2d, 0x2e,
                  0x30, 0x31, 0x33, 0x39, 0x3c, 0x3e, 0x52, 0x54, 0x58, 0x5b}
REDS_HOUSE_PASS = {0x01, 0x02, 0x03, 0x11, 0x12, 0x13, 0x14, 0x1c, 0x1a}
LAB_PASS        = {0x0c, 0x26, 0x16, 0x1e, 0x34, 0x37}
GYM_PASS        = {0x11, 0x16, 0x19, 0x2b, 0x3c, 0x3d, 0x3f, 0x4a, 0x4c, 0x4d, 0x03}
HOUSE_PASS      = {0x01, 0x12, 0x14, 0x28, 0x32, 0x37, 0x44, 0x54, 0x5c}
CENTER_PASS     = {0x11, 0x1a, 0x1c, 0x3c, 0x5e}

OVERWORLD_TALL_GRASS = {0x52}
OVERWORLD_DOOR       = {0x1b, 0x58}
HOUSE_DOOR           = {0x1a, 0x1c}
LAB_DOOR             = {0x34}
GYM_DOOR             = {0x4a}
POKECENTER_DOOR      = {0x5e}
INTERIOR_TILESETS = {"reds_house", "reds_house_1", "reds_house_2", "house",
                     "lab", "gym", "pokecenter", "mart", "lobby", "mansion"}

def get_block_tiles(bst, idx):
    off = idx * 16
    if off + 16 > len(bst):
        return [0] * 16
    return list(bst[off:off+16])

def step_tiles(bst, block_idx, sx, sy):
    b = get_block_tiles(bst, block_idx)
    tx, ty = sx * 2, sy * 2
    return [b[ty*4+tx], b[ty*4+tx+1], b[(ty+1)*4+tx], b[(ty+1)*4+tx+1]]

def classify_overworld(tiles):
    s = set(tiles)
    if s & OVERWORLD_DOOR:       return 'D'
    if s & OVERWORLD_TALL_GRASS: return ';'
    pc = sum(1 for t in tiles if t in OVERWORLD_PASS)
    if pc >= 2:
        if 0x10 in s or any(t in {0x11, 0x12, 0x13} for t in s): return '.'
        if any(0x3c <= t <= 0x3f for t in s) and pc < 4: return '~'
        return ' '
    if all(0x14 <= t <= 0x27 for t in tiles): return '~'
    if all(0x28 <= t <= 0x2f or t in {0x3c,0x3d,0x3e,0x3f} for t in tiles): return '~'
    return '#'

def classify_interior_block(bi, bst, n_blocks, door_set, pass_set):
    tiles = get_block_tiles(bst, bi)
    s = set(tiles)
    if s & door_set: return 'D'
    if sum(1 for t in tiles if t in pass_set) >= 2: return ' '
    if len(s) == 1: return ' '
    return ' '

def classify_interior_blk(blk, bst, tileset, w_blk, h_blk):
    if tileset in ("reds_house", "reds_house_1", "reds_house_2"):
        door_set, pass_set = HOUSE_DOOR, REDS_HOUSE_PASS
    elif tileset == "house": door_set, pass_set = HOUSE_DOOR, HOUSE_PASS
    elif tileset == "lab":   door_set, pass_set = LAB_DOOR, LAB_PASS
    elif tileset == "gym":   door_set, pass_set = GYM_DOOR, GYM_PASS
    elif tileset in ("pokecenter", "mart"):
        door_set, pass_set = POKECENTER_DOOR, CENTER_PASS
    else:
        door_set, pass_set = set(), set()

    rows = []
    for by in range(h_blk):
        for sy in range(2):
            row = ""
            for bx in range(w_blk):
                bi = blk[by * w_blk + bx]
                for sx in range(2):
                    row += classify_interior_block(bi, bst, len(bst)//16, door_set, pass_set)
            rows.append(row)
    return rows

def blk_to_charmap(blk, bst, tileset, w_blk, h_blk):
    if tileset in INTERIOR_TILESETS:
        return classify_interior_blk(blk, bst, tileset, w_blk, h_blk)
    rows = []
    for by in range(h_blk):
        for sy in range(2):
            row = ""
            for bx in range(w_blk):
                bidx = blk[by * w_blk + bx]
                for sx in range(2):
                    t = step_tiles(bst, bidx, sx, sy)
                    row += classify_overworld(t)
            rows.append(row)
    return rows

# ─── ASM 파싱 ────────────────────────────────────────────────
def parse_warps(text):
    warps = []
    for m in re.finditer(r'warp_event\s+(\d+)\s*,\s*(\d+)\s*,\s*(\w+)\s*,\s*\d+', text):
        warps.append([int(m.group(1)), int(m.group(2)), m.group(3)])
    return warps

def parse_bg_events(text):
    out = []
    for m in re.finditer(r'bg_event\s+(\d+)\s*,\s*(\d+)', text):
        out.append([int(m.group(1)), int(m.group(2))])
    return out

def parse_object_events(text):
    out = []
    for m in re.finditer(r'object_event\s+(\d+)\s*,\s*(\d+)', text):
        out.append([int(m.group(1)), int(m.group(2))])
    return out

# ─── 맵 정의 (gen_maps.py 와 동일) ────────────────────────────
MAPS = [
    ("MAP_PALLET",       "PalletTown",     10,  9, "overworld", "PalletTown",
        "MAP_ROUTE1", -1, []),
    ("MAP_ROUTE1",       "Route1",         10, 18, "overworld", "Route1",
        "MAP_VIRIDIAN", "MAP_PALLET",
        [[19, 2, 4, 50, "꼬렛"], [16, 2, 4, 50, "구구"]]),
    ("MAP_VIRIDIAN",     "ViridianCity",   20, 18, "overworld", "ViridianCity",
        "MAP_ROUTE2", "MAP_ROUTE1", []),
    ("MAP_ROUTE2",       "Route2",         10, 18, "overworld", "Route2",
        "MAP_VIR_FOREST", "MAP_VIRIDIAN",
        [[19, 3, 5, 50, "꼬렛"], [16, 3, 5, 50, "구구"]]),
    ("MAP_VIR_FOREST",   "ViridianForest",  9, 14, "forest",    "ViridianForest",
        "MAP_PEWTER", "MAP_ROUTE2",
        [[10, 3, 5, 55, "캐터피"], [13, 3, 5, 35, "뿔충이"],
         [25, 3, 5,  5, "피카츄"], [11, 4, 6,  5, "메타포드"]]),
    ("MAP_PEWTER",       "PewterCity",     10,  9, "overworld", "PewterCity",
        -1, "MAP_VIR_FOREST", []),
    ("MAP_OAK_LAB",      "OaksLab",         5,  6, "lab",       "OaksLab",
        -1, "MAP_PALLET", []),
    ("MAP_PLAYER_HOUSE", "RedsHouse1F",     4,  4, "reds_house","RedsHouse1F",
        -1, "MAP_PALLET", []),
    ("MAP_PEWTER_GYM",   "PewterGym",       4,  7, "gym",       "PewterGym",
        -1, "MAP_PEWTER", []),
]

# ─── 메인 ─────────────────────────────────────────────────────
def main():
    print(f"베이크 대상: {ASSETS}\n")

    bst_cache = {}
    def load_bst(name):
        if name in bst_cache: return bst_cache[name]
        p = os.path.join(ASSETS, "gfx", "blocksets", f"{name}.bst")
        if not os.path.exists(p):
            print(f"  [없음] blockset {name}", file=sys.stderr)
            bst_cache[name] = b""
        else:
            with open(p, "rb") as f:
                bst_cache[name] = f.read()
        return bst_cache[name]

    # ── 1) 맵 → PNG + JSON ──
    print("[1/3] 맵 .blk/.asm → PNG + JSON")
    for cpp_id, blk_name, w_blk, h_blk, tileset, asm_name, north, south, encs in MAPS:
        blk_path = os.path.join(ASSETS, "maps", f"{blk_name}.blk")
        asm_path = os.path.join(ASSETS, "data", "maps", "objects", f"{asm_name}.asm")
        if not os.path.exists(blk_path):
            print(f"  [skip] {blk_name}.blk 없음")
            continue

        with open(blk_path, "rb") as f:
            blk = f.read()
        bst = load_bst(tileset)
        rows = blk_to_charmap(blk, bst, tileset, w_blk, h_blk)
        map_w, map_h = w_blk * 2, h_blk * 2

        # ASM 파싱
        warps, signs, objs = [], [], []
        if os.path.exists(asm_path):
            with open(asm_path, "r", encoding="utf-8", errors="ignore") as f:
                asm_text = f.read()
            warps = parse_warps(asm_text)
            signs = parse_bg_events(asm_text)
            objs  = parse_object_events(asm_text)

        # 인테리어: 워프 좌표에 'D'
        if tileset in INTERIOR_TILESETS:
            for wx, wy, _ in warps:
                if 0 <= wy < map_h and 0 <= wx < map_w:
                    r = list(rows[wy]); r[wx] = 'D'; rows[wy] = ''.join(r)
        # 표지판 좌표에 's'
        for sx, sy in signs:
            if 0 <= sy < map_h and 0 <= sx < map_w:
                if rows[sy][sx] != 'D':
                    r = list(rows[sy]); r[sx] = 's'; rows[sy] = ''.join(r)

        # PNG 저장 (1px = 1tile)
        img = Image.new("RGB", (map_w, map_h), (0, 0, 0))
        for y, row in enumerate(rows):
            for x, ch in enumerate(row):
                img.putpixel((x, y), CHAR_TO_COLOR.get(ch, (255, 0, 255)))
        png_out = os.path.join(ASSETS, "maps", f"{blk_name}.png")
        img.save(png_out)

        # JSON 저장 (메타데이터)
        meta = {
            "cpp_id":   cpp_id,
            "tileset":  tileset,
            "size":     {"w": map_w, "h": map_h},
            "north":    north if north != -1 else None,
            "south":    south if south != -1 else None,
            "warps":    warps,
            "signs":    signs,
            "objects":  objs,
            "encounters": encs,
        }
        json_out = os.path.join(ASSETS, "maps", f"{blk_name}.json")
        with open(json_out, "w", encoding="utf-8") as f:
            json.dump(meta, f, ensure_ascii=False, indent=2)
        print(f"  ✓ {blk_name}: {map_w}×{map_h} → {os.path.basename(png_out)} + {os.path.basename(json_out)}")

    # ── 2) palettes.asm → JSON ──
    print("\n[2/3] palettes.asm → pokemon_palettes.json")
    pal_asm = os.path.join(ASSETS, "data", "pokemon", "palettes.asm")
    if os.path.exists(pal_asm):
        with open(pal_asm, "r", encoding="utf-8", errors="ignore") as f:
            text = f.read()
        species_pal = {}
        for m in re.finditer(r'db\s+PAL_(\w+)\s*;\s*(\w+)', text):
            pal, species = m.group(1), m.group(2)
            species_pal[species.lower()] = pal
        out = os.path.join(ASSETS, "pokemon_palettes.json")
        with open(out, "w", encoding="utf-8") as f:
            json.dump(species_pal, f, ensure_ascii=False, indent=2)
        print(f"  ✓ {len(species_pal)} 종족 → {out}")

    # ── 3) 원본 어셈블리/바이너리 삭제 ──
    print("\n[3/3] 원본 .blk/.bst/.asm 삭제")
    deleted = 0
    for root, dirs, files in os.walk(ASSETS):
        for f in files:
            if f.endswith(('.blk', '.bst', '.asm')):
                os.remove(os.path.join(root, f))
                deleted += 1
    # 빈 폴더 제거
    for sub in ("data/maps/objects", "data/maps", "data/pokemon", "data", "gfx/blocksets"):
        p = os.path.join(ASSETS, sub)
        if os.path.exists(p) and not os.listdir(p):
            os.rmdir(p)
    print(f"  ✓ {deleted}개 파일 제거")

    print("\n베이크 완료. pokered_assets/ 는 이제 PNG + JSON 만 포함.")

if __name__ == "__main__":
    main()
