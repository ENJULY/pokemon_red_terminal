#!/usr/bin/env python3
"""
HEAD's per-block char 인코딩에 맞게 tileWalkable / tileIsEncounter 자동 생성.
각 char 의 atomic 조합 → pokered collision_tile_ids 매칭 → 걷기 여부 결정.

map_data.h 끝에 inline bool 함수 두 개를 자동 갱신 (또는 추가).
"""
import os, re

POKERED = "/mnt/c/Users/김지훈/Desktop/pokered-master/pokered-master"
PROJECT = "/mnt/c/Users/김지훈/Desktop/git/pokemon_red_terminal"

# pokered collision_tile_ids.asm 정의 (걷기 가능한 atomic IDs)
OVERWORLD_PASS = {0x00, 0x10, 0x1B, 0x20, 0x21, 0x23, 0x2C, 0x2D, 0x2E,
                  0x30, 0x31, 0x33, 0x39, 0x3C, 0x3E, 0x52, 0x54, 0x58, 0x5B}
REDS_HOUSE_PASS = {0x01, 0x02, 0x03, 0x11, 0x12, 0x13, 0x14, 0x1C, 0x1A}
LAB_PASS        = {0x0C, 0x26, 0x16, 0x1E, 0x34, 0x37}
GYM_PASS        = {0x11, 0x16, 0x19, 0x2B, 0x3C, 0x3D, 0x3F, 0x4A, 0x4C, 0x4D, 0x03}
FOREST_PASS     = {0x20, 0x2E, 0x39, 0x52, 0x5A, 0x5C, 0x5E}

PASS_BY_TS = {
    'overworld':  OVERWORLD_PASS,
    'forest':     FOREST_PASS,
    'lab':        LAB_PASS,
    'reds_house': REDS_HOUSE_PASS,
    'gym':        GYM_PASS,
}

# 모든 워프(door) atomic 도 walkable (워프 트리거 자체가 step-on)
ALL_WARP = {0x1A, 0x1B, 0x1C, 0x34, 0x4A, 0x5E, 0x58, 0x54, 0x3A}
# 인카운터 atomic (긴 풀)
ENCOUNTER = {0x52}
# 도어 atomic (워프 트리거)
DOOR_TRIGGERS = {0x1A, 0x1B, 0x1C, 0x34, 0x4A, 0x5E, 0x58, 0x54, 0x3A}

MAPS = [
    ("PalletTown",     10,  9, "overworld"),
    ("Route1",         10, 18, "overworld"),
    ("ViridianCity",   20, 18, "overworld"),
    ("Route2",         10, 18, "overworld"),
    ("ViridianForest",  9, 14, "forest"),
    ("PewterCity",     10,  9, "overworld"),
    ("OaksLab",         5,  6, "lab"),
    ("RedsHouse1F",     4,  4, "reds_house"),
    ("PewterGym",       4,  7, "gym"),
]

# 각 tileset 의 .bst 로드
BST = {}
for name in PASS_BY_TS:
    with open(f"{POKERED}/gfx/blocksets/{name}.bst", "rb") as f:
        BST[name] = f.read()

def step_atomics(bst, block_idx, sx, sy):
    b = list(bst[block_idx*16:(block_idx+1)*16])
    tx, ty = sx*2, sy*2
    return (b[ty*4+tx], b[ty*4+tx+1], b[(ty+1)*4+tx], b[(ty+1)*4+tx+1])

# map_data.h 에서 각 char 의 (atomics, tileset) 파악 (첫 등장 기준)
with open(f"{PROJECT}/src/data/map_data.h", encoding='utf-8') as f:
    md = f.read()

char_info = {}  # ch -> (atomics, tileset)
for map_idx, (mname, w_blk, h_blk, ts) in enumerate(MAPS):
    bst = BST[ts]
    blk_path = f"{POKERED}/maps/{mname}.blk"
    if not os.path.exists(blk_path): continue
    with open(blk_path, "rb") as f:
        blk = f.read()
    pat = re.compile(rf'MAP_{map_idx} = \{{[^}}]*?\{{(.*?)nullptr', re.DOTALL)
    m = pat.search(md)
    if not m: continue
    rows = re.findall(r'"([^"]+)"', m.group(1))
    rows = [r for r in rows if len(r) == w_blk*2]
    for sy, row in enumerate(rows):
        for sx, ch in enumerate(row):
            if ch in char_info: continue
            bx, by = sx//2, sy//2
            if by*w_blk + bx >= len(blk): continue
            block_idx = blk[by*w_blk + bx]
            atomics = step_atomics(bst, block_idx, sx%2, sy%2)
            char_info[ch] = (atomics, ts)

# 각 char 분류
walkable = []
encounter = []
door = []
for ch, (atomics, ts) in char_info.items():
    pass_set = PASS_BY_TS[ts]
    s = set(atomics)
    is_walk = sum(1 for a in atomics if a in pass_set) >= 2 or bool(s & DOOR_TRIGGERS)
    is_enc  = bool(s & ENCOUNTER)
    is_door = bool(s & DOOR_TRIGGERS)
    if is_walk: walkable.append(ch)
    if is_enc:  encounter.append(ch)
    if is_door: door.append(ch)

walkable = sorted(set(walkable))
encounter = sorted(set(encounter))
door = sorted(set(door))

print(f"walkable chars ({len(walkable)}): {walkable}")
print(f"encounter chars ({len(encounter)}): {encounter}")
print(f"door chars ({len(door)}): {door}")

# === map_data.h 의 tileWalkable / tileIsEncounter 함수 교체 ===
def gen_func(name, chars, ret_type='bool'):
    if not chars:
        return f"inline {ret_type} {name}(char t) {{ return false; }}\n"
    # char 들을 single-quoted list 로
    quoted = []
    for ch in chars:
        if ch == "'": quoted.append("'\\''")
        elif ch == '\\': quoted.append("'\\\\'")
        else: quoted.append(f"'{ch}'")
    cond = ' || '.join(f't == {q}' for q in quoted)
    return f"inline {ret_type} {name}(char t) {{\n    return {cond};\n}}\n"

new_walkable = gen_func('tileWalkable', walkable)
new_encounter = gen_func('tileIsEncounter', encounter)
new_door = gen_func('tileIsDoor', door)

# map_data.h 의 기존 함수 교체
def replace_func(text, fname, new_body):
    pat = re.compile(rf'inline bool {fname}\(char t\)\s*\{{[^}}]*\}}\n?', re.DOTALL)
    if pat.search(text):
        return pat.sub(new_body, text, count=1)
    # 없으면 끝부분에 추가
    return text + '\n' + new_body

md_new = md
md_new = replace_func(md_new, 'tileWalkable', new_walkable)
md_new = replace_func(md_new, 'tileIsEncounter', new_encounter)
# tileIsDoor 도 추가 (overworld.cpp 가 이미 검사)
md_new = replace_func(md_new, 'tileIsDoor', new_door)

with open(f"{PROJECT}/src/data/map_data.h", 'w', encoding='utf-8') as f:
    f.write(md_new)
print("\nmap_data.h tileWalkable / tileIsEncounter 갱신 완료")
