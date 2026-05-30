"""
인트로(타이틀+오박사 스피치) 전용 에셋 베이크 — sprites.h 수동 삽입용 C++ 생성.
gen_sprites.py의 front 변환 로직(classify/half_block/convert_sprite, 40×20 GRAY)을 그대로 복제.
전체 sprites.h 재생성(OW/NPC 에셋 의존)을 피하려고 gen_npc_walk.py와 동일한 "전용 도구" 방식.

베이크 대상:
  1) 정면 픽처(배틀 포맷 40×20, GRAY): charizard(#6), nidorino(#33)  → SpriteData
  2) 타이틀 포켓몬 로고 그래픽(128×56 → 128×28 half-block, 노랑)        → TitleGfx
  3) RED VERSION 그래픽(80×8 → 80×4, 빨강)                            → TitleGfx

원본: C:\\Users\\김지훈\\Desktop\\pokered-master\\pokered-master
사용: python3 tools/gen_intro_assets.py [preview|emit]
"""
import sys, os
from PIL import Image

POKERED = "/mnt/c/Users/김지훈/Desktop/pokered-master/pokered-master"

# ─── gen_sprites.py 정면 변환 그대로 ─────────────────────────────
FRONT_W = 40; FRONT_H = 40            # 40 chars × 20 rows
GRAY = (244, 250, 240, 232)           # cls0 배경회 → cls1 옅은회 → cls2 진회 → cls3 검정

def brightness(r, g, b): return 0.299*r + 0.587*g + 0.114*b

def classify(px):
    r, g, b, a = px
    if a < 64: return -1
    br = brightness(r, g, b)
    if br > 180: return 0
    if br > 110: return 1
    if br > 50:  return 2
    return 3

def half_block(top, bot, pal):
    def fg(c): return f"38;5;{pal[0] if c < 0 else pal[c]}"
    def bg(c): return f"48;5;{pal[0] if c < 0 else pal[c]}"
    tt, bt = top < 0, bot < 0
    if tt and bt:      return "\x1f"
    if tt:             return f"\033[{fg(bot)}m▄\033[0m"
    if bt:             return f"\033[{fg(top)}m▀\033[0m"
    if top == bot:     return f"\033[{bg(top)}m \033[0m"
    return f"\033[{fg(top)};{bg(bot)}m▀\033[0m"

def convert_sprite(img, pal, ow, oh):
    img = img.convert("RGBA").resize((ow, oh), Image.NEAREST)
    rows = []
    for hy in range(oh // 2):
        row = ""
        for x in range(ow):
            t = classify(img.getpixel((x, hy*2)))
            b = classify(img.getpixel((x, hy*2+1)))
            row += half_block(t, b, pal)
        rows.append(row + "\033[0m")
    return rows

# ─── 타이틀 그래픽(단색 로고) 변환 ───────────────────────────────
# 흰 배경(밝은 픽셀)=투명, 그 외=단색 채움. 로고는 노랑, 버전은 빨강.
def gfx_cell(top, bot, fg_col):
    # top/bot: True=채움, False=투명
    if not top and not bot: return "\x1f"
    if not top:             return f"\033[38;5;{fg_col}m▄\033[0m"
    if not bot:             return f"\033[38;5;{fg_col}m▀\033[0m"
    return f"\033[48;5;{fg_col}m \033[0m"

def convert_gfx(img, fg_col, thresh=128):
    img = img.convert("L")
    w, h = img.size
    rows = []
    for hy in range(h // 2):
        row = ""
        for x in range(w):
            t = img.getpixel((x, hy*2))   < thresh   # 어두운 픽셀 = 채움
            b = img.getpixel((x, hy*2+1)) < thresh
            row += gfx_cell(t, b, fg_col)
        rows.append(row + "\033[0m")
    return rows

# ─── 인트로 전용 오버레이 그래픽 (gfx/intro/*) ──────────────────
# 원작 인트로는 흰 배경 위 어두운 실루엣. 흰색(cls0)=투명(\x1f)으로 굽어 겹쳐 그림.
# 명암 3단계: cls1→250(옅은회), cls2→240(진회), cls3→232(검정 외곽).
_OVL = {1: 250, 2: 240, 3: 232}
def _ovl_col(c): return _OVL.get(c, 232)

def overlay_cell(top, bot):
    tt = top <= 0   # -1(투명) 또는 0(흰색) → 투명
    bt = bot <= 0
    if tt and bt: return "\x1f"
    if tt:        return f"\033[38;5;{_ovl_col(bot)}m▄\033[0m"
    if bt:        return f"\033[38;5;{_ovl_col(top)}m▀\033[0m"
    if top == bot:return f"\033[48;5;{_ovl_col(top)}m \033[0m"
    return f"\033[38;5;{_ovl_col(top)};48;5;{_ovl_col(bot)}m▀\033[0m"

def convert_overlay(img, ow, oh):
    img = img.convert("RGBA").resize((ow, oh), Image.NEAREST)
    rows = []
    for hy in range(oh // 2):
        row = ""
        for x in range(ow):
            t = classify(img.getpixel((x, hy*2)))
            b = classify(img.getpixel((x, hy*2+1)))
            row += overlay_cell(t, b)
        rows.append(row + "\033[0m")
    return rows

def escape_c(s):
    out = []
    for ch in s.encode("utf-8"):
        if ch == ord('"'):    out.append('\\"')
        elif ch == ord('\\'): out.append('\\\\')
        elif 32 <= ch < 127:  out.append(chr(ch))
        else:                 out.append(f"\\x{ch:02x}")
    return "".join(out)

# ─── 대상 ────────────────────────────────────────────────────────
FRONT_TARGETS = [   # (png상대경로, varname, kor, species_id)
    ("gfx/pokemon/front/charizard.png", "CHARIZARD", "리자몽", 6),
    ("gfx/pokemon/front/nidorino.png",  "NIDORINO",  "니도리노", 33),
    ("gfx/pokemon/front/gengar.png",    "GENGAR",    "팬텀", 94),
]
GFX_TARGETS = [     # (png, varname, kor, fg_color)
    ("gfx/title/pokemon_logo.png", "TITLE_LOGO",    "포켓몬 로고", 226),  # 밝은 노랑
    ("gfx/title/red_version.png",  "TITLE_VERSION", "RED VERSION", 196),  # 빨강
]

# 인트로 무비 오버레이: 니도리노 3프레임(48×24) + 팬텀 3포즈(56×28)
NIDO_FRAMES = ["gfx/intro/red_nidorino_1.png", "gfx/intro/red_nidorino_2.png", "gfx/intro/red_nidorino_3.png"]
NIDO_W, NIDO_H = 48, 24          # 48 cols × 24 half-block rows
GENGAR_SHEET = "gfx/intro/gengar.png"   # 168×56 = 3포즈(56)
GEN_W, GEN_H = 56, 28

def emit_overlay_array(varname, frames_rows, w, h):
    print(f"static const int {varname}_W = {w};")
    print(f"static const int {varname}_H = {h};")
    print(f"static const int {varname}_FRAMES = {len(frames_rows)};")
    print(f"static const char* {varname}[{len(frames_rows)}][{h}] = {{")
    for fr in frames_rows:
        print("  {")
        for r in fr:
            print(f'    "{escape_c(r)}",')
        print("  },")
    print("};")

mode = sys.argv[1] if len(sys.argv) > 1 else "preview"

if mode == "emit_title":
    # 흑백 로고/버전(원작 GB는 모노크롬) + 레드 타이틀 캐릭터(gfx/title/player.png)
    print("// ===== emit_title — sprites.h 수동 삽입 (흑백 로고/버전 + 레드) =====")
    for rel, var, kor in [("gfx/title/pokemon_logo.png", "TITLE_LOGO", "포켓몬 로고"),
                          ("gfx/title/red_version.png",  "TITLE_VERSION", "RED VERSION")]:
        img = Image.open(os.path.join(POKERED, rel)); w, h = img.size
        rows = convert_gfx(img, 232)            # 흑백: 어두운 글자(232)
        print(f"// {kor} {img.size} → {w}×{h//2} (B&W)")
        print(f"static const int {var}_W = {w};")
        print(f"static const int {var}_H = {h//2};")
        print(f"static const char* {var}[{h//2}] = {{")
        for r in rows: print(f'    "{escape_c(r)}",')
        print("};")
    img = Image.open(os.path.join(POKERED, "gfx/title/player.png"))  # 40×56
    rows = convert_sprite(img, GRAY, 28, 40)    # 비율유지 축소(40:56≈28:40) → 28×20, 회색배경
    print("// 레드 타이틀 캐릭터 (gfx/title/player.png 40×56 → 28×20, GRAY)")
    print("static const int TITLE_RED_W = 28;")
    print("static const int TITLE_RED_H = 20;")
    print("static const char* TITLE_RED[20] = {")
    for r in rows: print(f'    "{escape_c(r)}",')
    print("};")
    sys.exit(0)

if mode == "emit_movie":
    # 인트로 무비 오버레이만 (니도리노 3프레임 + 팬텀 3포즈)
    print("// ===== gen_intro_assets.py emit_movie — sprites.h 수동 삽입 =====")
    nido = [convert_overlay(Image.open(os.path.join(POKERED, f)), NIDO_W, NIDO_H*2) for f in NIDO_FRAMES]
    print("// 인트로 니도리노 (gfx/intro/red_nidorino_1~3, 48×24, 투명배경)")
    emit_overlay_array("INTRO_NIDORINO", nido, NIDO_W, NIDO_H)
    sheet = Image.open(os.path.join(POKERED, GENGAR_SHEET)).convert("L")
    from PIL import ImageDraw
    gens = []
    for p in range(3):
        crop = sheet.crop((p*GEN_W, 0, p*GEN_W+GEN_W, GEN_W)).copy()
        # 좌상단 잔재 타일(아틀라스 패딩) 제거 → 흰색(=투명)으로 마스킹
        ImageDraw.Draw(crop).rectangle([0, 0, 7, 17], fill=255)
        gens.append(convert_overlay(crop, GEN_W, GEN_H*2))
    print("// 인트로 팬텀 3포즈 (gfx/intro/gengar.png, 56×28, 투명배경)")
    emit_overlay_array("INTRO_GENGAR", gens, GEN_W, GEN_H)
    sys.exit(0)

if mode == "preview":
    for rel, var, kor, sid in FRONT_TARGETS:
        img = Image.open(os.path.join(POKERED, rel))
        print(f"\n===== {kor} (#{sid}) {img.size} =====")
        for r in convert_sprite(img, GRAY, FRONT_W, FRONT_H):
            print(r + "\033[0m")
    for rel, var, kor, col in GFX_TARGETS:
        img = Image.open(os.path.join(POKERED, rel))
        print(f"\n===== {kor} {img.size} =====")
        for r in convert_gfx(img, col):
            print(r + "\033[0m")
else:  # emit
    print("// ===== gen_intro_assets.py 생성 — sprites.h에 수동 삽입 =====")
    for rel, var, kor, sid in FRONT_TARGETS:
        img = Image.open(os.path.join(POKERED, rel))
        rows = convert_sprite(img, GRAY, FRONT_W, FRONT_H)
        print(f"// {kor} (#{sid}) — GRAY (인트로/배틀 정면)")
        print(f"static const SpriteData SPR_{var} = {{{{")
        for r in rows:
            print(f'    "{escape_c(r)}",')
        print(f"}}, {sid}, {FRONT_W}, {FRONT_H // 2}}};")
    for rel, var, kor, col in GFX_TARGETS:
        img = Image.open(os.path.join(POKERED, rel))
        rows = convert_gfx(img, col)
        w, h = img.size
        print(f"// {kor} {img.size} → {w}×{h//2} half-block")
        print(f"static const int {var}_W = {w};")
        print(f"static const int {var}_H = {h//2};")
        print(f"static const char* {var}[{h//2}] = {{")
        for r in rows:
            print(f'    "{escape_c(r)}",')
        print("};")
