#!/usr/bin/env python3
"""
pokered 원본 리포에서 필요한 파일만 받아 pokered_assets/ 안에 저장한다.
이후 gen_*.py는 GitHub fetch 대신 이 폴더를 읽는다.

사용법:
    python3 tools/fetch_assets.py
"""
import os
import sys
import urllib.request

BASE = "https://raw.githubusercontent.com/pret/pokered/master"
DEST = os.path.join(os.path.dirname(__file__), "..", "pokered_assets")

MONS = [
    "bulbasaur", "charmander", "squirtle", "caterpie", "metapod",
    "weedle", "pidgey", "rattata", "pikachu", "geodude", "onix",
]

MAPS = [
    "PalletTown", "Route1", "ViridianCity", "Route2", "ViridianForest",
    "PewterCity", "OaksLab", "RedsHouse1F", "PewterGym",
]

TILESETS = ["overworld", "forest", "lab", "reds_house", "gym"]

# 오버월드 NPC 스프라이트 (gfx/sprites/*.png)
# 모두 16×{16,48,96} L-mode PNG. gen_sprites.py가 top 16×16 (down idle)만 추출.
NPCS = [
    "mom", "oak", "blue", "gramps", "nurse", "clerk",
    "girl", "brunette_girl", "little_boy", "little_girl",
    "fisher", "gentleman", "poke_ball",
]

ASSETS = []
for m in MONS:
    ASSETS.append(f"gfx/pokemon/front/{m}.png")
    ASSETS.append(f"gfx/pokemon/back/{m}b.png")
for t in ["trainers/prof.oak", "trainers/rival1", "player/red"]:
    ASSETS.append(f"gfx/{t}.png")
for n in NPCS:
    ASSETS.append(f"gfx/sprites/{n}.png")
for ts in TILESETS:
    ASSETS.append(f"gfx/tilesets/{ts}.png")
    ASSETS.append(f"gfx/blocksets/{ts}.bst")
for mp in MAPS:
    ASSETS.append(f"maps/{mp}.blk")
    ASSETS.append(f"data/maps/objects/{mp}.asm")
# 종족 팔레트 매핑 (어셈) — 컬러 스프라이트 결정용
ASSETS.append("data/pokemon/palettes.asm")


def fetch_one(rel_path):
    url = f"{BASE}/{rel_path}"
    out_path = os.path.join(DEST, rel_path)
    os.makedirs(os.path.dirname(out_path), exist_ok=True)
    if os.path.exists(out_path):
        return "skip"
    try:
        with urllib.request.urlopen(url, timeout=20) as r:
            data = r.read()
        with open(out_path, "wb") as f:
            f.write(data)
        return f"ok ({len(data)}B)"
    except Exception as e:
        return f"FAIL: {e}"


def main():
    print(f"대상 폴더: {os.path.abspath(DEST)}")
    print(f"파일 개수: {len(ASSETS)}\n")
    failures = []
    for i, rel in enumerate(ASSETS, 1):
        status = fetch_one(rel)
        marker = "✗" if status.startswith("FAIL") else ("·" if status == "skip" else "✓")
        print(f"  [{i:3d}/{len(ASSETS)}] {marker} {rel}  {status}")
        if status.startswith("FAIL"):
            failures.append((rel, status))

    print()
    if failures:
        print(f"실패 {len(failures)}건:")
        for rel, status in failures:
            print(f"  - {rel}: {status}")
        sys.exit(1)
    print("완료.")


if __name__ == "__main__":
    main()
