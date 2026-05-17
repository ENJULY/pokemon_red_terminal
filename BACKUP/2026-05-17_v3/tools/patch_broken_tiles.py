#!/usr/bin/env python3
"""
HEAD's tiles.h 의 깨진 6개 char (D/L/C/G/M/T) 를 진짜 atomic 합성으로 패치.
로컬 pokered repo (Desktop/pokered-master/pokered-master) 사용.

각 char 가 어떤 16x16 walkable step 을 나타내는지 map_data.h 와 .blk + .bst 분석으로 역추적.
ANSI escape 인코딩은 raw 4글자 시퀀스 ('\\','x','1','b') 로 박아야 C 컴파일러가 \\x1b 로 해석.
"""
import os, re
from PIL import Image

POKERED = "/mnt/c/Users/김지훈/Desktop/pokered-master/pokered-master"
PROJECT = "/mnt/c/Users/김지훈/Desktop/git/pokemon_red_terminal"

# 9 맵 (이름, 너비_block, 높이_block, tileset)
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

# tilesets: 픽셀 + bst 로드
TS_IMG = {}
TS_BST = {}
for name in ["overworld", "forest", "lab", "reds_house", "gym"]:
    TS_IMG[name] = Image.open(f"{POKERED}/gfx/tilesets/{name}.png").convert('L')
    with open(f"{POKERED}/gfx/blocksets/{name}.bst", "rb") as f:
        TS_BST[name] = f.read()

def step_atomics(bst, block_idx, sx, sy):
    b = list(bst[block_idx*16:(block_idx+1)*16])
    tx, ty = sx*2, sy*2
    return (b[ty*4+tx], b[ty*4+tx+1], b[(ty+1)*4+tx], b[(ty+1)*4+tx+1])

def get_atomic(ts_name, idx):
    img = TS_IMG[ts_name]
    cols = img.size[0] // 8
    r, c = idx // cols, idx % cols
    return img.crop((c*8, r*8, (c+1)*8, (r+1)*8))

def compose_step(atomic_ids, ts_name):
    out = Image.new('L', (16, 16), 255)
    out.paste(get_atomic(ts_name, atomic_ids[0]), (0, 0))
    out.paste(get_atomic(ts_name, atomic_ids[1]), (8, 0))
    out.paste(get_atomic(ts_name, atomic_ids[2]), (0, 8))
    out.paste(get_atomic(ts_name, atomic_ids[3]), (8, 8))
    return out

# ─── HEAD's map_data.h 에서 각 char 첫 등장 좌표 + atomic 조합 추출 ─────
char_atomics = {}
with open(f"{PROJECT}/src/data/map_data.h", encoding='utf-8') as f:
    md = f.read()

for map_idx, (mname, w_blk, h_blk, ts) in enumerate(MAPS):
    bst = TS_BST[ts]
    blk_path = f"{POKERED}/maps/{mname}.blk"
    if not os.path.exists(blk_path): continue
    with open(blk_path, "rb") as f:
        blk = f.read()
    # 해당 맵의 tile rows 추출
    pat = re.compile(rf'MAP_{map_idx} = \{{[^}}]*?\{{(.*?)nullptr', re.DOTALL)
    m = pat.search(md)
    if not m: continue
    body = m.group(1)
    rows = re.findall(r'"([^"]+)"', body)
    rows = [r for r in rows if len(r) == w_blk*2]
    for sy, row in enumerate(rows):
        for sx, ch in enumerate(row):
            if ch in char_atomics: continue
            bx, by = sx//2, sy//2
            if by*w_blk + bx >= len(blk): continue
            block_idx = blk[by*w_blk + bx]
            atomics = step_atomics(bst, block_idx, sx%2, sy%2)
            char_atomics[ch] = (atomics, ts)

# ─── ANSI 변환: raw 4-char escape 시퀀스로 출력 ─────
# pal: (밝음, 중간밝음, 중간어둠, 어두움) ANSI 256
# HEAD 원본 tiles.h 와 동일한 색 사용 (255/250/244/232 4단계, 240 사용 안 함)
PALETTES = {
    'overworld':  (255, 250, 244, 232),
    'forest':     (255, 250, 244, 232),
    'lab':        (255, 250, 244, 232),
    'reds_house': (255, 250, 244, 232),
    'gym':        (255, 250, 244, 232),
}

ESC = "\\x1b"     # 4글자 라이터럴
RESET = ESC + "[0m"
HALF_TOP = "\\xe2\\x96\\x80"    # ▀ 3바이트 UTF-8을 12글자 escape로
HALF_BOT = "\\xe2\\x96\\x84"    # ▄

def classify(v):
    if v > 200: return 0
    if v > 130: return 1
    if v > 60:  return 2
    return 3

def step_to_ansi(step_img, pal):
    rows = []
    for hy in range(8):
        py_top, py_bot = hy*2, hy*2 + 1
        row = ""
        for x in range(16):
            tc = classify(step_img.getpixel((x, py_top)))
            bc = classify(step_img.getpixel((x, py_bot)))
            if tc == bc:
                # 단색 cell: 배경색 + 공백
                row += f"{ESC}[48;5;{pal[tc]}m {RESET}"
            else:
                # 위/아래 다름: ▄ 사용. fg=bottom, bg=top
                bg = pal[tc]
                fg = pal[bc]
                row += f"{ESC}[48;5;{bg};38;5;{fg}m{HALF_BOT}{RESET}"
        rows.append(row)
    return rows

# ─── 주인공 집 (MAP_PLAYER_HOUSE = idx 7) 의 char 는 HEAD 원본 보존 ─────
import subprocess
head_md = subprocess.check_output(['git', '-C', PROJECT, 'show', 'HEAD:src/data/map_data.h']).decode('utf-8')
m = re.search(r'MAP_7 = \{[^}]*?\{(.*?)nullptr', head_md, re.DOTALL)
house_chars = set()
if m:
    for r in re.findall(r'"([^"]+)"', m.group(1)):
        if len(r) == 8:
            house_chars |= set(r)
print(f"주인공 집 보존 char: {sorted(house_chars)}")

# ─── tiles.h 일괄 재생성 (집 char 제외) ─────
with open(f"{PROJECT}/src/data/tiles.h", encoding='utf-8') as f:
    content = f.read()

print(f"\n=== {len(char_atomics)}개 char 중 (집 chars 제외) 재생성 ===")
patched, skipped = 0, 0
for ch, (atomics, ts) in char_atomics.items():
    if ch in house_chars:
        skipped += 1
        continue  # 주인공 집에 쓰인 char 는 HEAD 원본 그대로 유지
    end_marker = f"}}, '{ch}'}};"
    end_idx = content.find(end_marker)
    if end_idx < 0:
        continue
    pal = PALETTES.get(ts, PALETTES['overworld'])
    step_img = compose_step(atomics, ts)
    new_rows = step_to_ansi(step_img, pal)

    open_idx = content.rfind('{{', 0, end_idx)
    body_start = open_idx + 2
    new_body = '\n' + '\n'.join(f'    "{r}",' for r in new_rows) + '\n'
    content = content[:body_start] + new_body + content[end_idx:]
    patched += 1

print(f"  {patched}개 패치, {skipped}개 보존")
with open(f"{PROJECT}/src/data/tiles.h", 'w', encoding='utf-8') as f:
    f.write(content)
print("\n저장 완료")
