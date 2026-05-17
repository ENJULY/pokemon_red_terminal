#!/usr/bin/env python3
# coding: utf-8
"""
gen_gate_forest_tiles.py — Route 2 Gate + Viridian Forest 디코더

Route2Gate (10x8 step) — gate.bst tileset → MAP_15
ViridianForest (34x48 step) — forest.bst tileset → MAP_4 (재생성, 기존 손편집 대체)
"""
import os, re, sys
from PIL import Image

ROOT    = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
POKERED = os.path.abspath(os.path.join(ROOT, '..', 'pokered'))
TILES_H = os.path.join(ROOT, 'src', 'data', 'tiles.h')
MAP_H   = os.path.join(ROOT, 'src', 'data', 'map_data.h')

# 처리 맵: (이름, blk_w, blk_h, MapDef 라벨, map ID 상수, png_name, bst_name, pass_set, prefix, kname)
GATE_PASS = {0x01, 0x12, 0x14, 0x1a, 0x1c, 0x37, 0x38, 0x3b, 0x3c, 0x5e}
FOREST_PASS = {0x1e, 0x20, 0x2e, 0x30, 0x34, 0x37, 0x39, 0x3a, 0x40, 0x51, 0x52, 0x5a, 0x5c, 0x5e, 0x5f}
FOREST_TALL_GRASS = {0x20}  # forest tileset: grass tile = 0x20

MAPS = [
    ("Route2Gate",     5, 4, "MAP_15", "MAP_ROUTE2_GATE",      "gate",   "gate",   GATE_PASS,   "GT", "2번도로 게이트", set()),
    ("ViridianForest", 17,24,"MAP_4",  "MAP_VIR_FOREST",       "forest", "forest", FOREST_PASS, "FR", "상록숲",         FOREST_TALL_GRASS),
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

# 각 맵 처리 — tileset별 독립
results = {}
for name, w, h, mid, cid, png_name, bst_name, pass_set, prefix, kname, tall_grass in MAPS:
    PNG = Image.open(os.path.join(POKERED, 'gfx', 'tilesets', f'{png_name}.png')).convert('L')
    with open(os.path.join(POKERED, 'gfx', 'blocksets', f'{bst_name}.bst'), 'rb') as f:
        BST = f.read()
    with open(os.path.join(POKERED, 'maps', f'{name}.blk'), 'rb') as f:
        blk = f.read()
    if len(blk) != w * h:
        print(f"[FATAL] {name}.blk = {len(blk)}B, expected {w}*{h}={w*h}B", file=sys.stderr)
        sys.exit(1)

    variant_count = {}
    for sy in range(h*2):
        for sx in range(w*2):
            atoms = step_atomics(blk, BST, w, sx, sy)
            variant_count[atoms] = variant_count.get(atoms, 0) + 1

    print(f'[i] {name}: {len(variant_count)} unique variants', file=sys.stderr)

    # char 할당 — 각 tileset별 독립 풀
    ESCAPE = set("\"'\\")
    RESERVED = set("#~ ")
    POOL = sorted(set(chr(c) for c in range(33, 127)) - ESCAPE - RESERVED)
    sorted_atoms = sorted(variant_count.items(), key=lambda x: -x[1])
    variant_to_char = {}
    # 풀숲은 ';' 강제
    for atoms in variant_count:
        if any(a in tall_grass for a in atoms):
            variant_to_char[atoms] = ';'
    remaining = [a for a in sorted_atoms if a[0] not in variant_to_char]
    pool_iter = iter([c for c in POOL if c != ';'])
    for atoms, _ in remaining:
        try:
            variant_to_char[atoms] = next(pool_iter)
        except StopIteration:
            variant_to_char[atoms] = '#'

    # 레이아웃 생성
    layout = []
    for sy in range(h*2):
        row = ""
        for sx in range(w*2):
            atoms = step_atomics(blk, BST, w, sx, sy)
            row += variant_to_char[atoms]
        layout.append(row)

    # TileArt
    char_to_atoms = {}
    for a, c in variant_to_char.items():
        if c not in char_to_atoms:
            char_to_atoms[c] = a
    art_blocks = []
    case_lines = []
    for ch in sorted(char_to_atoms):
        atoms = char_to_atoms[ch]
        tname = f"{prefix}_C{ord(ch):03d}"
        rows = render_step(PNG, BST, atoms)
        art = [f"// {prefix} '{ch}' atoms={list(atoms)} count={variant_count[atoms]}",
               f"static const TileArt TILE_{tname} = {{{{"]
        for r in rows:
            art.append('    "{}",'.format(r))
        art.append("}, '" + (ch if ch not in ('\\', "'") else "\\" + ch) + "'};")
        art.append("")
        art_blocks.append("\n".join(art))
        esc = "\\\\" if ch == '\\' else ("\\'" if ch == "'" else ch)
        case_lines.append(f"        case '{esc}': return &TILE_{tname};")

    # walkable
    walkable = set()
    for atoms, ch in variant_to_char.items():
        if atoms[2] in pass_set:
            walkable.add(ch)
    if tall_grass:
        walkable.add(';')

    results[mid] = {
        'name': name, 'w': 2*w, 'h': 2*h, 'cid': cid, 'kname': kname,
        'prefix': prefix, 'layout': layout,
        'art_blocks': art_blocks, 'case_lines': case_lines,
        'walkable': walkable, 'fallback_tile': f"TILE_{prefix}_C{ord(max(variant_count, key=lambda a: variant_count[a]) and (variant_to_char[max(variant_count, key=lambda a: variant_count[a])])):03d}"
    }
    # fallback_tile 더 단순하게 - 첫 char 사용
    fb_atoms = max(variant_count, key=lambda a: variant_count[a])
    fb_ch = variant_to_char[fb_atoms]
    results[mid]['fallback_tile'] = f"TILE_{prefix}_C{ord(fb_ch):03d}"

# ─── tiles.h 패치 ─────────────────────────────────────────────
with open(TILES_H, 'r', encoding='utf-8') as f:
    src = f.read()

# 기존 Gate/Forest 섹션 제거
for prefix in ['GT', 'FR']:
    src = re.sub(rf"// --- {prefix}_AUTO_BEGIN.*?--- {prefix}_AUTO_END ---\n", "", src, flags=re.S)

# Art 섹션 + dispatch 추가
for mid in ['MAP_15', 'MAP_4']:
    r = results[mid]
    prefix = r['prefix']
    begin = f"// --- {prefix}_AUTO_BEGIN ({mid} {r['name']}) ---"
    end = f"// --- {prefix}_AUTO_END ---"
    art_section = begin + "\n" + "\n".join(r['art_blocks']) + end + "\n"
    # getTileArt 앞에 삽입
    m = re.search(r'inline const TileArt\* getTileArt\(', src)
    if m:
        src = src[:m.start()] + art_section + "\n" + src[m.start():]

# getTileArt dispatch 확장 — MAP_15 (gate), MAP_4 (forest)
# overworld 분기(>=0 <=5) 직후, indoor switch 전에 삽입
forest_dispatch = (
    "    // Forest (MAP_4 Viridian Forest)\n"
    "    if (mapId == 4) {\n"
    "        switch (c) {\n"
    + "\n".join(results['MAP_4']['case_lines']) + "\n"
    f"            default: return &{results['MAP_4']['fallback_tile']};\n"
    "        }\n"
    "    }\n"
)
gate_dispatch = (
    "    // Gate (MAP_15 Route 2 Gate)\n"
    "    if (mapId == 15) {\n"
    "        switch (c) {\n"
    + "\n".join(results['MAP_15']['case_lines']) + "\n"
    f"            default: return &{results['MAP_15']['fallback_tile']};\n"
    "        }\n"
    "    }\n"
)

# 기존 Forest/Gate dispatch 제거 (재실행 안전)
src = re.sub(r"    // Forest \(MAP_4.*?if \(mapId == 4.*?\}\s*\}\s*\n", "", src, flags=re.S)
src = re.sub(r"    // Gate \(MAP_15.*?if \(mapId == 15.*?\}\s*\}\s*\n", "", src, flags=re.S)

# overworld 분기(mapId 0-5) 다음에 추가 - MAP_4가 overworld 범위인데 forest dispatch로 가야하므로 overworld 검사 BEFORE 에 forest 분기 추가
# 즉, MAP_4를 overworld에서 빼고 forest로
# 더 간단: overworld 조건을 (0-3, 5)로 변경
src = src.replace(
    "if (mapId >= 0 && mapId <= 5) {",
    "if (mapId == 4) {\n"
    "        switch (c) {\n"
    + "\n".join(results['MAP_4']['case_lines']) + "\n"
    f"            default: return &{results['MAP_4']['fallback_tile']};\n"
    "        }\n"
    "    }\n"
    "    if (mapId == 15) {\n"
    "        switch (c) {\n"
    + "\n".join(results['MAP_15']['case_lines']) + "\n"
    f"            default: return &{results['MAP_15']['fallback_tile']};\n"
    "        }\n"
    "    }\n"
    "    if ((mapId >= 0 && mapId <= 5 && mapId != 4)) {",
    1
)

with open(TILES_H, 'w', encoding='utf-8') as f:
    f.write(src)
print(f'[OK] tiles.h: Gate({len(results["MAP_15"]["art_blocks"])}) + Forest({len(results["MAP_4"]["art_blocks"])}) TileArt', file=sys.stderr)

# ─── map_data.h 패치 ─────────────────────────────────────────
with open(MAP_H, 'r', encoding='utf-8') as f:
    md = f.read()

# MAP_ROUTE2_GATE 상수 추가
if 'MAP_ROUTE2_GATE' not in md:
    md = md.replace(
        'static const int MAP_PEWTER_MART   = 14;',
        'static const int MAP_PEWTER_MART   = 14;\n'
        'static const int MAP_ROUTE2_GATE   = 15;'
    )

# isIndoorMap 확장 - MAP_15 추가
md = re.sub(
    r'inline bool isIndoorMap\(int id\)\s*\{[^}]+\}',
    'inline bool isIndoorMap(int id) {\n'
    '    return id == MAP_OAK_LAB || id == MAP_PLAYER_HOUSE ||\n'
    '           id == MAP_PLAYER_HOUSE2 || id == MAP_PEWTER_GYM ||\n'
    '           id == MAP_RIVAL_HOUSE ||\n'
    '           id == MAP_VIRIDIAN_PC || id == MAP_PEWTER_PC ||\n'
    '           id == MAP_VIRIDIAN_MART || id == MAP_PEWTER_MART ||\n'
    '           id == MAP_ROUTE2_GATE;\n'
    '}',
    md
)

# NUM_MAPS + ALL_MAPS 갱신
md = re.sub(r'inline constexpr int NUM_MAPS = \d+;',
            'inline constexpr int NUM_MAPS = 16;', md)
if '&MAP_15' not in md:
    md = md.replace('&MAP_14,', '&MAP_14,\n    &MAP_15,')

# MAP_4 (Viridian Forest) tiles 교체
r = results['MAP_4']
pattern = (r'(inline MapDef MAP_4\s*=\s*\{[^{}]*\{\s*\n)'
           r'((?:[^{}]|\n)*?nullptr\s*\n\s*)'
           r'(\})')
new_tiles = "        // gen_gate_forest_tiles.py — pokered ViridianForest.blk 원본\n"
for row in r['layout']:
    new_tiles += '        "{}",\n'.format(row)
new_tiles += "        nullptr\n    "
md_new, n = re.subn(pattern, lambda m: m.group(1) + new_tiles + m.group(3),
                   md, count=1, flags=re.S)
if n == 1:
    md = md_new
    # mapW/mapH 갱신
    md = re.sub(r'(MAP_VIR_FOREST,)\s*\d+,\s*\d+,', r'\1 34, 48,', md)
    print(f'[OK] MAP_4 (Viridian Forest 34x48) tiles 갱신', file=sys.stderr)

# MAP_15 신규 추가 (ALL_MAPS 앞에)
if 'inline MapDef MAP_15 =' not in md:
    r = results['MAP_15']
    new_def = (
        f'\n// ─── Route2Gate (10×8) — pokered 원본 ─────\n'
        f'inline MapDef MAP_15 = {{\n'
        f'    {r["cid"]}, {r["w"]}, {r["h"]},\n'
        f'    "Route 2 Gate", L"{r["kname"]}",\n'
        f'    {{\n'
        f'        // gen_gate_forest_tiles.py\n'
    )
    for row in r['layout']:
        new_def += '        "{}",\n'.format(row)
    new_def += (
        f'        nullptr\n'
        f'    }},\n'
        f'    {{\n'
        f'        // 오박사 보조원 (1,4) facing LEFT — pokered Route2Gate_Object.asm\n'
        f'        {{1, 4, {{\n'
        f'            L"보조원: 어서오세요, 2번도로 게이트입니다!",\n'
        f'            L"오박사님 심부름 잘 마쳤나요?",\n'
        f'            nullptr\n'
        f'        }}, NPC_SPR_OAK, 0}},\n'
        f'    }}, 1,  // npcs\n'
        f'    {{}}, 0,  // trainers\n'
        f'    {{\n'
        f'        // 북쪽 출구 (4,0)(5,0) → Viridian Forest 남쪽 입구\n'
        f'        {{4, 0, MAP_VIR_FOREST, 16, 47}},\n'
        f'        {{5, 0, MAP_VIR_FOREST, 16, 47}},\n'
        f'        // 남쪽 출구 (4,7)(5,7) → Route 2 (게이트 아래)\n'
        f'        {{4, 7, MAP_ROUTE2, 15, 36}},\n'
        f'        {{5, 7, MAP_ROUTE2, 16, 36}},\n'
        f'    }}, 4,  // warps\n'
        f'    {{}}, 0,  // no encounters\n'
        f'    -1, -1,  // north/south\n'
        f'    4, 7, 4, 7,  // entry\n'
        f'    nullptr  // bgm\n'
        f'}};\n'
    )
    md = md.replace('// ─── 전체 맵 배열', new_def + '\n// ─── 전체 맵 배열')
    print('[OK] MAP_15 (Route2Gate) 신규 추가', file=sys.stderr)

# Route 2 warps: 게이트로 연결 (기존 -1 → MAP_15)
md = md.replace(
    "{16, 35, -1  /*ROUTE_2_GATE*/, -1, -1}",
    "{16, 35, MAP_ROUTE2_GATE, 4, 7}"
)
md = md.replace(
    "{15, 39, -1  /*ROUTE_2_GATE*/, -1, -1}",
    "{15, 39, MAP_ROUTE2_GATE, 4, 7}"
)

# Viridian Forest 워프도 추가 - 남쪽 출구 (Route 2 Gate 로)
# MAP_4 (ViridianForest) 의 워프 부분 갱신
# 기존 warps 가 잘못된 좌표 (mapH=28 일 때 (15,47) 등) - mapH=48 으로 바뀌었으니 (15,47)~(18,47) 이 맞음
# MAP_4 NPC 영역은 일단 유지 (수동 갱신 - 트레이너/아이템은 별도 작업)

with open(MAP_H, 'w', encoding='utf-8') as f:
    f.write(md)
print('[OK] map_data.h 갱신 — Forest + Gate + 워프 연결', file=sys.stderr)

# 요약
print('', file=sys.stderr)
for mid, r in results.items():
    print(f'  {mid} ({r["name"]}): {r["w"]}x{r["h"]}, walkable={sorted(r["walkable"])}', file=sys.stderr)
