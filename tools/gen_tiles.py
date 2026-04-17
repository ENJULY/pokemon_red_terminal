"""
pokered 타일셋 → 터미널 ANSI 2char 타일 변환기
포켓몬 레드 실제 스프라이트 타일 사용
출력: src/data/tiles.h

각 8×8픽셀 타일 → 터미널 2char×4행(half-block) 표현
"""
import urllib.request, io, sys
from PIL import Image

TILESET_BASE = "https://raw.githubusercontent.com/pret/pokered/master/gfx/tilesets/{}.png"

TILE_W = 8
TILE_H = 8
COLS_PER_ROW = 16   # 타일셋 가로 타일 수

# ─── 색상 팔레트 매핑 ────────────────────────────────────────────
# GB 4단계 → 실제 게임 색감에 맞는 ANSI 256색
# 각 지형별 팔레트: (밝은색, 중간밝은, 중간어둠, 어두운색) ANSI bg code

PALETTES = {
    # 풀숲/땅 (Ground): 연두 계열
    'grass':    ("48;5;106", "48;5;70",  "48;5;28",  "48;5;22"),
    # 키큰풀 (Tall grass): 더 짙은 초록
    'tallgrass':("48;5;70",  "48;5;28",  "48;5;22",  "48;5;0"),
    # 길 (Path): 갈색/베이지
    'path':     ("48;5;222", "48;5;179", "48;5;136", "48;5;94"),
    # 물 (Water): 파란색
    'water':    ("48;5;75",  "48;5;33",  "48;5;27",  "48;5;19"),
    # 나무 (Tree): 짙은 초록
    'tree':     ("48;5;28",  "48;5;22",  "48;5;0",   "48;5;0"),
    # 건물 벽 (Building): 회색/갈색
    'wall':     ("48;5;250", "48;5;240", "48;5;237", "48;5;232"),
    # 건물 지붕 (Roof): 빨간/갈색
    'roof':     ("48;5;167", "48;5;124", "48;5;88",  "48;5;52"),
    # 내부/바닥 (Floor): 밝은 회색
    'floor':    ("48;5;255", "48;5;252", "48;5;248", "48;5;244"),
    # 산/바위 (Rock): 회색
    'rock':     ("48;5;250", "48;5;244", "48;5;238", "48;5;236"),
}

def fetch_tileset(name):
    url = TILESET_BASE.format(name)
    try:
        with urllib.request.urlopen(url, timeout=10) as r:
            img = Image.open(io.BytesIO(r.read())).convert('L')  # grayscale
        print(f"  다운로드 완료: {name}.png ({img.size[0]}×{img.size[1]}px, {(img.size[0]//TILE_W)*(img.size[1]//TILE_H)}타일)")
        return img
    except Exception as e:
        print(f"  오류: {e}", file=sys.stderr)
        return None

def get_tile(img, idx):
    cols = img.size[0] // TILE_W
    row = idx // cols
    col = idx % cols
    if row * TILE_H >= img.size[1]:
        return None
    return img.crop((col*TILE_W, row*TILE_H, (col+1)*TILE_W, (row+1)*TILE_H))

def tile_to_ansi(tile, palette_key):
    """8×8 픽셀 타일 → 2char×2행 ANSI 문자열
    각 행 = 4픽셀 높이 (half-block으로 상하 2픽셀 표현)
    각 칸 = 4픽셀 너비 (공백 2개로 표현)
    출력: 2줄 (위 4px 행, 아래 4px 행), 각 줄 2칸 너비 = 4chars"""
    pal = PALETTES[palette_key]
    rows = []

    def classify(v):
        if v > 200: return 0
        if v > 130: return 1
        if v > 60:  return 2
        return 3

    for hy in range(2):       # 2행 (각 행 = 4픽셀 높이)
        py_top = hy * 4       # 상단 2픽셀 (py_top, py_top+1)
        py_bot = hy * 4 + 2   # 하단 2픽셀 (py_top+2, py_top+3)
        row = ""
        for hx in range(2):   # 2칸 (각 칸 = 4픽셀 너비)
            px_s = hx * 4
            # 상단 2×4 블록 평균
            top_vals = [tile.getpixel((px_s + dx, py_top + dy))
                       for dx in range(4) for dy in range(2)]
            top_avg = sum(top_vals) // len(top_vals)
            # 하단 2×4 블록 평균
            bot_vals = [tile.getpixel((px_s + dx, py_bot + dy))
                       for dx in range(4) for dy in range(2)]
            bot_avg = sum(bot_vals) // len(bot_vals)

            tc = classify(top_avg)
            bc = classify(bot_avg)

            if tc == bc:
                row += f"\x1b[{pal[tc]}m  \x1b[0m"
            else:
                bg_code = pal[tc]
                fg_code = pal[bc].replace("48;5;", "38;5;")
                row += f"\x1b[{bg_code};{fg_code}m\xe2\x96\x84\xe2\x96\x84\x1b[0m"
        rows.append(row)
    return rows

def make_flat_tile(palette_key, shade=0):
    """단색 타일 (스프라이트 없을 때 fallback)"""
    pal = PALETTES[palette_key]
    rows = []
    for _ in range(2):
        rows.append(f"\x1b[{pal[shade]}m    \x1b[0m")  # 4chars
    return rows

def escape_c(s):
    result = ""
    for ch in s.encode("utf-8"):
        if ch == ord('"'):
            result += '\\"'
        elif ch == ord('\\'):
            result += '\\\\'
        elif 32 <= ch < 127 and ch != ord('\x1b'):
            result += chr(ch)
        else:
            result += f"\\x{ch:02x}"
    return result

# ─── 타일 정의: (이름, 타일셋, 타일인덱스, 팔레트, 설명) ─────────
# pokered 오버월드 타일셋 기준
# 확인된 타일 인덱스:
#   $00(0)=일반땅, $52(82)=긴풀, $10(16)=길, $14(20)=물
#   나무: 약 $01(1) 또는 주변 어두운 타일
#   건물: $02(2), $03(3) 등
TILE_DEFS = [
    # (char, tileset, tile_idx, palette, fallback_palette, comment)
    (' ',  'overworld',  0,   'grass',    'grass',    '빈 땅 (기본 잔디)'),
    ('.',  'overworld',  16,  'path',     'path',     '길 ($10)'),
    (',',  'overworld',  3,   'grass',    'grass',    '짧은 풀 ($03)'),
    (';',  'overworld',  82,  'tallgrass','tallgrass', '긴 풀 ($52, 야생 인카운터)'),
    ('#',  'overworld',  2,   'wall',     'wall',     '건물 벽 ($02)'),
    ('H',  'overworld',  2,   'wall',     'wall',     '건물 벽 (내부용)'),
    ('T',  'overworld',  50,  'tree',     'tree',     '나무 ($32)'),
    ('~',  'overworld',  6,   'water',    'water',    '물 ($06)'),
    ('D',  'overworld',  20,  'floor',    'floor',    '문 ($14)'),
    ('L',  'overworld',  20,  'floor',    'floor',    '연구소 문'),
    ('C',  'overworld',  20,  'floor',    'floor',    '포켓몬센터 문'),
    ('G',  'overworld',  20,  'floor',    'floor',    '체육관 문'),
    ('M',  'overworld',  20,  'floor',    'floor',    '마트 문'),
    ('s',  'overworld',  60,  'floor',    'floor',    '표지판'),
    ('B',  'overworld',  2,   'wall',     'wall',     '브록/특수'),
    ('P',  'overworld',  0,   'floor',    'floor',    '포켓볼 테이블'),
    ('N',  'overworld',  0,   'grass',    'grass',    'NPC 위치 (런타임 처리)'),
]

print("포켓몬 레드 타일셋 다운로드 중...")

# 타일셋 다운로드 (overworld)
print("  overworld 타일셋...")
ow_img = fetch_tileset('overworld')

# ─── 헤더 생성 ───────────────────────────────────────────────────
lines = []
lines.append("// AUTO-GENERATED by tools/gen_tiles.py -- DO NOT EDIT")
lines.append("// 포켓몬 레드 실제 타일셋 스프라이트 기반")
lines.append("#pragma once")
lines.append("#include <cstring>")
lines.append("")
lines.append("// 각 타일: 2글자 너비 × 2행 (half-block ANSI, 각 행=4픽셀)")
lines.append("static const int TILE_COLS = 2;  // 터미널 칸 수 (가로, 각 칸=2chars)")
lines.append("static const int TILE_ROWS = 2;  // 터미널 행 수 (세로)")
lines.append("")
lines.append("struct TileArt {")
lines.append("    const char* rows[2];  // ANSI UTF-8 문자열 (각 4chars 너비)")
lines.append("    char mapChar;")
lines.append("};")
lines.append("")

tile_names = []

for char, tileset, tile_idx, pal_key, fallback_pal, comment in TILE_DEFS:
    img = ow_img
    rows = None

    if img:
        tile = get_tile(img, tile_idx)
        if tile:
            rows = tile_to_ansi(tile, pal_key)

    if rows is None:
        rows = make_flat_tile(fallback_pal)

    safe_name = {
        ' ': 'GROUND', '.': 'PATH', ',': 'GRASS', ';': 'TALLGRASS',
        '#': 'WALL', 'H': 'HOUSE', 'T': 'TREE', '~': 'WATER',
        'D': 'DOOR', 'L': 'LABDOOR', 'C': 'CENTER', 'G': 'GYM',
        'M': 'MART', 's': 'SIGN', 'B': 'BLOCK', 'P': 'TABLE', 'N': 'NPC',
    }.get(char, f"TILE_{ord(char)}")

    tile_names.append((char, safe_name))
    lines.append(f"// {comment} (맵 문자: '{char}')")
    lines.append(f"static const TileArt TILE_{safe_name} = {{{{")
    for row in rows:
        lines.append(f'    "{escape_c(row)}",')
    char_esc = char if char != "'" else "\\'"
    lines.append(f"}}, '{char_esc}'}};")
    lines.append("")

# getter 함수
lines.append("// 맵 문자 → 타일 아트 getter")
lines.append("inline const TileArt* getTileArt(char c) {")
lines.append("    switch(c) {")
for char, name in tile_names:
    if char == "'":
        continue
    esc_char = char if char not in ('"', '\\') else ('\\' + char)
    lines.append(f"    case '{esc_char}': return &TILE_{name};")
lines.append("    default: return &TILE_GROUND;")
lines.append("    }")
lines.append("}")

output = "\n".join(lines) + "\n"
out_path = "../src/data/tiles.h"
with open(out_path, "w", encoding="utf-8") as f:
    f.write(output)
print(f"\n생성 완료: {out_path}")
print(f"총 {len(TILE_DEFS)}개 타일 정의")
