"""
오박사/블루(및 임의 NPC)의 OW walk 프레임만 추출 → sprites.h 삽입용 C++ 문자열 생성.
gen_sprites.py의 OW 변환 로직(ow_classify/ow_cell/convert_ow_player_frame)을 그대로 복제.
원본: C:\\Users\\김지훈\\Desktop\\pokered-master\\pokered-master\\gfx\\sprites\\*.png (16×96 = 6 frame)
frame layout: 0=down idle,1=up idle,2=side idle,3=down walk,4=up walk,5=side walk
사용: python3 tools/gen_npc_walk.py [preview|emit]
"""
import sys, os
from PIL import Image

POKERED = "/mnt/c/Users/김지훈/Desktop/pokered-master/pokered-master/gfx/sprites"

def ow_classify(v):
    if v > 240: return 0
    if v > 127: return 1
    if v > 42:  return 2
    return 3

def ow_color(cls):
    if cls == 1: return "38;5;250"
    if cls == 2: return "38;5;244"
    return "38;5;232"

def ow_bg(cls):
    if cls == 1: return "48;5;250"
    if cls == 2: return "48;5;244"
    return "48;5;232"

def ow_cell(top, bot):
    if top == 0 and bot == 0:    return "\x1f"
    if top == 0:                 return f"\033[{ow_color(bot)}m▄\033[0m"
    if bot == 0:                 return f"\033[{ow_color(top)}m▀\033[0m"
    if top == bot:               return f"\033[{ow_bg(top)}m \033[0m"
    return f"\033[{ow_color(top)};{ow_bg(bot)}m▀\033[0m"

def convert_frame(img, frame_idx):
    base_y = frame_idx * 16
    rows = []
    for hy in range(8):
        pt = base_y + hy * 2
        pb = pt + 1
        row = ""
        for x in range(16):
            t = img.getpixel((x, pt))
            b = img.getpixel((x, pb))
            row += ow_cell(ow_classify(t), ow_classify(b))
        rows.append(row)
    return rows

def escape_c(s):
    out = []
    for ch in s:
        o = ord(ch)
        if ch == '"':   out.append('\\"')
        elif ch == '\\':out.append('\\\\')
        elif o < 32 or o == 127: out.append(f"\\x{o:02x}")
        elif o < 128:   out.append(ch)
        else:
            for byte in ch.encode("utf-8"):
                out.append(f"\\x{byte:02x}")
    return "".join(out)

# (png 파일명, C++ varname)
TARGETS = [("oak", "OAK"), ("blue", "BLUE")]
# (frame_idx, suffix)
WALK = [(3, "_WALK"), (4, "_UP_WALK")]

mode = sys.argv[1] if len(sys.argv) > 1 else "preview"

for name, var in TARGETS:
    img = Image.open(os.path.join(POKERED, f"{name}.png")).convert("L")
    if mode == "preview":
        print(f"\n===== {name}.png {img.size} =====")
        for fi in range(img.size[1] // 16):
            print(f"-- frame {fi} --")
            for r in convert_frame(img, fi):
                print(r + "\033[0m")
    else:  # emit
        for fi, suf in WALK:
            rows = convert_frame(img, fi)
            print(f"// {name} walk (frame {fi})")
            print(f"static const OwPlayerFrame SPR_{var}{suf} = {{{{")
            for r in rows:
                print(f'    "{escape_c(r)}",')
            print("}};")
