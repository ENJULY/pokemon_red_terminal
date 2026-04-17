#!/usr/bin/env python3
"""
pokered 실제 맵 데이터 → C++ map_data.h 생성기
pret/pokered .blk + .bst + objects/*.asm → 원본 맵 레이아웃 그대로 변환
"""
import urllib.request, re, sys

BASE = "https://raw.githubusercontent.com/pret/pokered/master"

def fetch(url):
    try:
        with urllib.request.urlopen(url, timeout=15) as r:
            return r.read()
    except Exception as e:
        print(f"  [ERROR] {url}: {e}", file=sys.stderr)
        return b""

def fetch_text(url):
    return fetch(url).decode('utf-8', errors='ignore')

# ─── 오버월드 타일 분류 ─────────────────────────────────────────
# collision_tile_ids.asm → Overworld_Coll (통행 가능 타일 ID)
OVERWORLD_PASS = {
    0x00, 0x10, 0x1b, 0x20, 0x21, 0x23, 0x2c, 0x2d, 0x2e,
    0x30, 0x31, 0x33, 0x39, 0x3c, 0x3e, 0x52, 0x54, 0x58, 0x5b
}
# RedsHouse_Coll: $01, $02, $03, $11, $12, $13, $14, $1c, $1a
REDS_HOUSE_PASS = {0x01, 0x02, 0x03, 0x11, 0x12, 0x13, 0x14, 0x1c, 0x1a}
# Lab_Coll: $0c, $26, $16, $1e, $34, $37
LAB_PASS        = {0x0c, 0x26, 0x16, 0x1e, 0x34, 0x37}
# Gym_Coll: $11, $16, $19, $2b, $3c, $3d, $3f, $4a, $4c, $4d, $03
GYM_PASS        = {0x11, 0x16, 0x19, 0x2b, 0x3c, 0x3d, 0x3f, 0x4a, 0x4c, 0x4d, 0x03}
# House_Coll: $01, $12, $14, $28, $32, $37, $44, $54, $5c
HOUSE_PASS      = {0x01, 0x12, 0x14, 0x28, 0x32, 0x37, 0x44, 0x54, 0x5c}
# Pokecenter/Mart_Coll: $11, $1a, $1c, $3c, $5e
CENTER_PASS     = {0x11, 0x1a, 0x1c, 0x3c, 0x5e}

OVERWORLD_TALL_GRASS = {0x52}
OVERWORLD_DOOR       = {0x1b, 0x58}
HOUSE_DOOR           = {0x1a, 0x1c}
LAB_DOOR             = {0x34}
GYM_DOOR             = {0x4a}
POKECENTER_DOOR      = {0x5e}
MART_DOOR            = {0x5e}

# ─── 블록셋 관리 ──────────────────────────────────────────────
_bst_cache = {}

def get_bst(tileset):
    if tileset not in _bst_cache:
        url = f"{BASE}/gfx/blocksets/{tileset}.bst"
        print(f"  블록셋 다운로드: {tileset}.bst")
        data = fetch(url)
        _bst_cache[tileset] = data
    return _bst_cache[tileset]

def get_block_tiles(bst, block_idx):
    off = block_idx * 16
    if off + 16 > len(bst):
        return [0] * 16
    return list(bst[off:off+16])

def step_tiles(bst, block_idx, sx, sy):
    """블록 내 step 위치(0~1)의 2×2 타일 ID 반환"""
    b = get_block_tiles(bst, block_idx)
    tx, ty = sx * 2, sy * 2
    return [b[ty*4+tx], b[ty*4+tx+1], b[(ty+1)*4+tx], b[(ty+1)*4+tx+1]]

# ─── 타일 분류 (틸셋별) ────────────────────────────────────────
def classify_overworld(tiles):
    s = set(tiles)
    if s & OVERWORLD_DOOR:       return 'D'
    if s & OVERWORLD_TALL_GRASS: return ';'
    passable_count = sum(1 for t in tiles if t in OVERWORLD_PASS)
    if passable_count >= 2:
        if 0x10 in s or any(t in {0x11, 0x12, 0x13} for t in s): return '.'
        if any(0x3c <= t <= 0x3f for t in s) and passable_count < 4: return '~'
        return ' '
    if all(0x14 <= t <= 0x27 for t in tiles): return '~'
    if all(0x28 <= t <= 0x2f or t in {0x3c,0x3d,0x3e,0x3f} for t in tiles): return '~'
    return '#'

def classify_interior_block(block_idx, bst, bst_len_blocks, door_set, pass_set):
    """
    인테리어 맵 블록 분류:
    - 범위 내 블록: 타일 분석
    - 범위 밖 블록: '#' (벽 - blockset에 없는 블록 = 구조물)
    """
    if block_idx * 16 + 16 > len(bst):
        return '#'  # 범위 밖 = 벽/구조물
    tiles = get_block_tiles(bst, block_idx)
    s = set(tiles)
    if s & door_set: return 'D'
    if sum(1 for t in tiles if t in pass_set) >= 2: return ' '
    # 모든 타일이 같은 값(단색 블록) → 바닥으로 처리
    if len(s) == 1: return ' '
    return ' '  # 인테리어: 기본 바닥으로 처리 (워프 데이터로 문 표시)

def classify_tile(tiles, tileset_name):
    if tileset_name == "overworld":
        return classify_overworld(tiles)
    elif tileset_name == "forest":
        return classify_overworld(tiles)
    else:
        return None  # 인테리어: 별도 처리

def classify_interior_blk(blk_bytes, bst, tileset_name, w_blk, h_blk):
    """인테리어 맵 전용 분류"""
    if tileset_name in ("reds_house", "reds_house_1", "reds_house_2"):
        door_set, pass_set = HOUSE_DOOR, REDS_HOUSE_PASS
    elif tileset_name == "house":
        door_set, pass_set = HOUSE_DOOR, HOUSE_PASS
    elif tileset_name == "lab":
        door_set, pass_set = LAB_DOOR, LAB_PASS
    elif tileset_name == "gym":
        door_set, pass_set = GYM_DOOR, GYM_PASS
    elif tileset_name in ("pokecenter", "mart"):
        door_set, pass_set = POKECENTER_DOOR, CENTER_PASS
    else:
        door_set, pass_set = set(), set()

    rows = []
    for by in range(h_blk):
        for sy in range(2):
            row = ""
            for bx in range(w_blk):
                bi = blk_bytes[by * w_blk + bx]
                for sx in range(2):
                    row += classify_interior_block(bi, bst, len(bst)//16, door_set, pass_set)
            rows.append(row)
    return rows

# ─── .blk → 문자 맵 변환 ──────────────────────────────────────
INTERIOR_TILESETS = {"reds_house", "reds_house_1", "reds_house_2", "house",
                     "lab", "gym", "pokecenter", "mart", "lobby", "mansion"}

def blk_to_charmap(blk_bytes, bst, tileset_name, w_blk, h_blk):
    """
    .blk 데이터 → (2*w_blk) × (2*h_blk) 문자 배열
    각 블록(32×32px) = 2×2 스텝(16×16px 단위)
    """
    if tileset_name in INTERIOR_TILESETS:
        return classify_interior_blk(blk_bytes, bst, tileset_name, w_blk, h_blk)

    rows = []
    for by in range(h_blk):
        for sy in range(2):
            row = ""
            for bx in range(w_blk):
                block_idx = blk_bytes[by * w_blk + bx]
                for sx in range(2):
                    t = step_tiles(bst, block_idx, sx, sy)
                    row += classify_tile(t, tileset_name)
            rows.append(row)
    return rows

# ─── 오브젝트 데이터 파싱 ─────────────────────────────────────
def parse_warps(asm_text):
    """warp_event X, Y, DEST_MAP, DEST_WARP → [(x, y, dest_name)]"""
    warps = []
    for m in re.finditer(r'warp_event\s+(\d+)\s*,\s*(\d+)\s*,\s*(\w+)\s*,\s*\d+', asm_text):
        warps.append((int(m.group(1)), int(m.group(2)), m.group(3)))
    return warps

def parse_objects(asm_text):
    """object_event X, Y, SPRITE_*, ... → [(x, y)]"""
    objs = []
    for m in re.finditer(r'object_event\s+(\d+)\s*,\s*(\d+)', asm_text):
        objs.append((int(m.group(1)), int(m.group(2))))
    return objs

def parse_bg_events(asm_text):
    """bg_event X, Y, TEXT_* → [(x, y)] (표지판 등)"""
    events = []
    for m in re.finditer(r'bg_event\s+(\d+)\s*,\s*(\d+)', asm_text):
        events.append((int(m.group(1)), int(m.group(2))))
    return events

# ─── 맵 이름 → C++ 맵 ID 매핑 ───────────────────────────────
MAP_NAME_TO_ID = {
    "PALLET_TOWN":       "MAP_PALLET",
    "ROUTE_1":           "MAP_ROUTE1",
    "VIRIDIAN_CITY":     "MAP_VIRIDIAN",
    "ROUTE_2":           "MAP_ROUTE2",
    "VIRIDIAN_FOREST":   "MAP_VIR_FOREST",
    "PEWTER_CITY":       "MAP_PEWTER",
    "PEWTER_GYM":        "MAP_PEWTER_GYM",
    "OAKS_LAB":          "MAP_OAK_LAB",
    "REDS_HOUSE_1F":     "MAP_PLAYER_HOUSE",
    "REDS_HOUSE_2F":     "MAP_PLAYER_HOUSE2",
    "BLUES_HOUSE":       "MAP_RIVAL_HOUSE",
    # LAST_MAP = 직전 맵으로 돌아가기 (집 출구)
    "LAST_MAP":          "MAP_PALLET",
}

def map_dest(dest_name):
    return MAP_NAME_TO_ID.get(dest_name, "-1  /*" + dest_name + "*/")

# ─── 맵 정의 ─────────────────────────────────────────────────
# (cpp_id, cpp_name, korean_name, blk_file, w_blk, h_blk, tileset, asm_file,
#  north_map, south_map, encounters)
MAPS = [
    (
        "MAP_PALLET", "Pallet Town", "팔레트시티",
        "PalletTown", 10, 9, "overworld", "PalletTown",
        "MAP_ROUTE1", -1,
        [],  # encounters
    ),
    (
        "MAP_ROUTE1", "Route 1", "1번도로",
        "Route1", 10, 18, "overworld", "Route1",
        "MAP_VIRIDIAN", "MAP_PALLET",
        [
            (19, 2, 4, 50, "꼬렛"),
            (16, 2, 4, 50, "구구"),
        ],
    ),
    (
        "MAP_VIRIDIAN", "Viridian City", "상록시티",
        "ViridianCity", 20, 18, "overworld", "ViridianCity",
        "MAP_ROUTE2", "MAP_ROUTE1",
        [],
    ),
    (
        "MAP_ROUTE2", "Route 2", "2번도로",
        "Route2", 10, 18, "overworld", "Route2",
        "MAP_VIR_FOREST", "MAP_VIRIDIAN",
        [
            (19, 3, 5, 50, "꼬렛"),
            (16, 3, 5, 50, "구구"),
        ],
    ),
    (
        "MAP_VIR_FOREST", "Viridian Forest", "상록숲",
        "ViridianForest", 9, 14, "forest", "ViridianForest",
        "MAP_PEWTER", "MAP_ROUTE2",
        [
            (10, 3, 5, 55, "캐터피"),
            (13, 3, 5, 35, "뿔충이"),
            (25, 3, 5,  5, "피카츄"),
            (11, 4, 6,  5, "메타포드"),
        ],
    ),
    (
        "MAP_PEWTER", "Pewter City", "회색시티",
        "PewterCity", 10, 9, "overworld", "PewterCity",
        -1, "MAP_VIR_FOREST",
        [],
    ),
    (
        "MAP_OAK_LAB", "Oak's Lab", "오박사 연구소",
        "OaksLab", 5, 6, "lab", "OaksLab",
        -1, "MAP_PALLET",
        [],
    ),
    (
        "MAP_PLAYER_HOUSE", "Player's House", "주인공의 집",
        "RedsHouse1F", 4, 4, "reds_house", "RedsHouse1F",
        -1, "MAP_PALLET",
        [],
    ),
    (
        "MAP_PEWTER_GYM", "Pewter Gym", "회색시티 체육관",
        "PewterGym", 4, 7, "gym", "PewterGym",
        -1, "MAP_PEWTER",
        [],
    ),
]

# ─── C++ 출력 헬퍼 ────────────────────────────────────────────
def escape_row(s):
    return s.replace('\\', '\\\\').replace('"', '\\"')

def map_const_name(cpp_id, idx):
    """MAP_PALLET → MAP_0 형태 인덱스로 변환"""
    return cpp_id

# ─── 메인 생성 ───────────────────────────────────────────────
def generate():
    print("pokered 맵 데이터 다운로드 중...\n")

    out_lines = []
    out_lines.append("// AUTO-GENERATED by tools/gen_maps.py -- DO NOT EDIT")
    out_lines.append("// pret/pokered 원본 맵 데이터 기반 (블록 좌표 → 스텝 좌표 변환)")
    out_lines.append("#pragma once")
    out_lines.append("")
    out_lines.append("// ─── 타일 종류 ─────────────────────────────────────────────────")
    out_lines.append("// ' '  빈 땅/잔디 (이동 가능)")
    out_lines.append("// '.'  길 (이동 가능)")
    out_lines.append("// ';'  긴 풀 (야생 인카운터)")
    out_lines.append("// '#'  벽/건물/나무 (통과 불가)")
    out_lines.append("// '~'  물 (통과 불가, 비주얼)")
    out_lines.append("// 'D'  문 (워프 타일)")
    out_lines.append("// 's'  표지판")
    out_lines.append("// 'N'  NPC 위치")
    out_lines.append("")
    out_lines.append("struct NpcDef {")
    out_lines.append("    int x, y;")
    out_lines.append("    const wchar_t* lines[4];")
    out_lines.append("};")
    out_lines.append("")
    out_lines.append("struct TrainerDef {")
    out_lines.append("    int x, y;")
    out_lines.append("    int dir;")
    out_lines.append("    int sightRange;")
    out_lines.append("    const wchar_t* name;")
    out_lines.append("    const wchar_t* preBattleText;")
    out_lines.append("    int partyIds[3];")
    out_lines.append("    int partyLevels[3];")
    out_lines.append("    bool defeated;")
    out_lines.append("};")
    out_lines.append("")
    out_lines.append("struct WarpDef {")
    out_lines.append("    int srcX, srcY;")
    out_lines.append("    int destMap;")
    out_lines.append("    int destX, destY;")
    out_lines.append("};")
    out_lines.append("")
    out_lines.append("struct EncounterEntry {")
    out_lines.append("    int speciesId;")
    out_lines.append("    int minLevel, maxLevel;")
    out_lines.append("    int weight;")
    out_lines.append("};")
    out_lines.append("")
    out_lines.append("struct MapDef {")
    out_lines.append("    int         id;")
    out_lines.append("    int         mapW, mapH;   // 실제 맵 크기 (스텝 단위)")
    out_lines.append("    const char* name;")
    out_lines.append("    const wchar_t* nameW;")
    out_lines.append("    const char* tiles[72];   // 최대 72행 지원")
    out_lines.append("    NpcDef      npcs[8];")
    out_lines.append("    int         numNpcs;")
    out_lines.append("    TrainerDef  trainers[4];")
    out_lines.append("    int         numTrainers;")
    out_lines.append("    WarpDef     warps[12];")
    out_lines.append("    int         numWarps;")
    out_lines.append("    EncounterEntry encounters[6];")
    out_lines.append("    int         numEncounters;")
    out_lines.append("    int         northMap;")
    out_lines.append("    int         southMap;")
    out_lines.append("    int         northEntryX, northEntryY;")
    out_lines.append("    int         southEntryX, southEntryY;")
    out_lines.append("    const wchar_t* bgmFile;")
    out_lines.append("};")
    out_lines.append("")

    # 맵 ID 상수
    id_map = {}
    for i, (cpp_id, *_) in enumerate(MAPS):
        id_map[cpp_id] = i
        out_lines.append(f"static const int {cpp_id} = {i};")
    # 추가 ID (게임에서 참조하지만 맵 배열에 없는 것들)
    out_lines.append(f"static const int MAP_PLAYER_HOUSE2 = {len(MAPS)};")
    out_lines.append(f"static const int MAP_RIVAL_HOUSE   = {len(MAPS)+1};")
    out_lines.append("")

    map_def_names = []

    for map_idx, (cpp_id, eng_name, kor_name, blk_file, w_blk, h_blk,
                  tileset_name, asm_file, north_map, south_map, encounters) in enumerate(MAPS):

        print(f"[{map_idx+1}/{len(MAPS)}] {eng_name} ({w_blk}×{h_blk} 블록 = {w_blk*2}×{h_blk*2} 스텝)")

        # ── 블록 데이터 다운로드 ──
        blk_bytes = fetch(f"{BASE}/maps/{blk_file}.blk")
        if not blk_bytes:
            print(f"  .blk 없음, 스킵")
            continue

        bst = get_bst(tileset_name)

        # ── 문자 맵 생성 ──
        rows = blk_to_charmap(blk_bytes, bst, tileset_name, w_blk, h_blk)
        map_w = w_blk * 2
        map_h = h_blk * 2

        # ── 오브젝트 데이터 파싱 ──
        asm_text = fetch_text(f"{BASE}/data/maps/objects/{asm_file}.asm")
        warps    = parse_warps(asm_text)
        bg_evs   = parse_bg_events(asm_text)

        # 인테리어: 워프 위치에 'D' 표시 (블록 분류로 감지 못 할 수 있으므로)
        if tileset_name in INTERIOR_TILESETS:
            for wx, wy, _ in warps:
                if 0 <= wy < map_h and 0 <= wx < map_w:
                    row = list(rows[wy])
                    row[wx] = 'D'
                    rows[wy] = ''.join(row)

        # 표지판 위치에 's' 표시 (단, 문 위치는 유지)
        for sx, sy in bg_evs:
            if 0 <= sy < map_h and 0 <= sx < map_w:
                if rows[sy][sx] not in ('D',):
                    row = list(rows[sy])
                    row[sx] = 's'
                    rows[sy] = ''.join(row)

        # ── C++ 출력 ──
        def_name = f"MAP_{map_idx}"
        map_def_names.append(def_name)

        out_lines.append(f"// ─── {eng_name} ({map_w}×{map_h}) ─────────────────────────────────────")
        out_lines.append(f"inline MapDef {def_name} = {{")
        out_lines.append(f"    {cpp_id}, {map_w}, {map_h},")
        out_lines.append(f'    "{eng_name}", L"{kor_name}",')
        out_lines.append("    {")
        for row in rows:
            # 길이 맞추기 (패딩)
            padded = (row + ' ' * map_w)[:map_w]
            out_lines.append(f'        "{escape_row(padded)}",')
        out_lines.append("        nullptr")
        out_lines.append("    },")

        # NPC (빈 배열, 런타임 텍스트는 별도 관리)
        out_lines.append("    {}, 0,  // npcs")
        out_lines.append("    {}, 0,  // trainers")

        # Warps
        warp_lines = []
        for wx, wy, dest_name in warps:
            dest_id = map_dest(dest_name)
            if dest_id == "-1":
                continue
            # 목적지 좌표는 게임 로직에서 각 맵의 default entry point 사용
            warp_lines.append(f"        {{{wx}, {wy}, {dest_id}, -1, -1}},")

        if warp_lines:
            out_lines.append("    {")
            for wl in warp_lines:
                out_lines.append(wl)
            out_lines.append(f"    }}, {len(warp_lines)},  // warps")
        else:
            out_lines.append("    {}, 0,  // warps")

        # Encounters
        if encounters:
            out_lines.append("    {")
            for spec, minl, maxl, wt, _ in encounters:
                out_lines.append(f"        {{{spec}, {minl}, {maxl}, {wt}}},")
            out_lines.append(f"    }}, {len(encounters)},  // encounters")
        else:
            out_lines.append("    {}, 0,  // no encounters")

        # northMap / southMap
        n_id = f"{north_map}" if isinstance(north_map, str) else str(north_map)
        s_id = f"{south_map}" if isinstance(south_map, str) else str(south_map)
        out_lines.append(f"    {n_id}, {s_id},  // north/south")
        # entry 좌표 (기본값: 맵 중앙)
        out_lines.append(f"    {map_w//2}, {map_h-1}, {map_w//2}, 0,  // entry points")
        out_lines.append("    nullptr  // bgm")
        out_lines.append("};")
        out_lines.append("")

        print(f"  → {map_w}×{map_h} 완료, 워프 {len(warp_lines)}개")

    # 전체 맵 배열
    n = len(map_def_names)
    out_lines.append("// ─── 전체 맵 배열 ────────────────────────────────────────────")
    out_lines.append("inline MapDef* ALL_MAPS[] = {")
    for name in map_def_names:
        out_lines.append(f"    &{name},")
    out_lines.append("};")
    out_lines.append(f"inline constexpr int NUM_MAPS = {n};")
    out_lines.append("")

    out_lines.append("inline MapDef* getMap(int id) {")
    out_lines.append("    if (id >= 0 && id < NUM_MAPS) return ALL_MAPS[id];")
    out_lines.append("    return nullptr;")
    out_lines.append("}")
    out_lines.append("")
    out_lines.append("inline bool tileWalkable(char t) {")
    out_lines.append("    return t == ' ' || t == '.' || t == ',' || t == ';';")
    out_lines.append("}")
    out_lines.append("inline bool tileIsEncounter(char t) { return t == ';'; }")
    out_lines.append("")
    out_lines.append("inline char getTile(const MapDef* m, int x, int y) {")
    out_lines.append("    if (!m || x < 0 || x >= m->mapW || y < 0 || y >= m->mapH) return '#';")
    out_lines.append("    if (!m->tiles[y]) return '#';")
    out_lines.append("    int len = 0; while (m->tiles[y][len]) len++;")
    out_lines.append("    if (x >= len) return ' ';")
    out_lines.append("    return m->tiles[y][x];")
    out_lines.append("}")

    content = "\n".join(out_lines) + "\n"
    out_path = "../src/data/map_data.h"
    with open(out_path, "w", encoding="utf-8") as f:
        f.write(content)
    print(f"\n✓ 생성 완료: {out_path}")
    print(f"  총 {n}개 맵 (팔레트시티~회색시티)")

if __name__ == "__main__":
    generate()
