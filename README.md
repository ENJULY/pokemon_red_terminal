# Pokemon Red — Terminal Edition

Windows 콘솔(CMD / Windows Terminal)에서 ANSI 이스케이프로 동작하는
포켓몬스터 레드 터미널 클론. C++17, MinGW(g++) 기반.

목표 범위: **팔레트시티 ~ 회색시티 1관장(브록) 클리어**까지.

---

## 빌드 & 실행

요구사항: Windows + MinGW-w64 (`C:\mingw64\bin\g++.exe` 기준).

```bat
build.bat
```

자동으로 컴파일 → `build\PokemonRed.exe` 생성 → 실행까지 한 번에.

수동 빌드:
```bat
g++ -std=c++17 src\main.cpp src\engine\*.cpp src\game\*.cpp ^
    -o build\PokemonRed.exe -lwinmm -I src ^
    -DUNICODE -D_UNICODE -static -static-libgcc -static-libstdc++
```

---

## 폴더 구조

```
pokemon_red_terminal/
├── README.md / DESIGN.md     ← 문서
├── build.bat / CMakeLists.txt ← 빌드 시스템
├── src/
│   ├── main.cpp              ← 진입점
│   ├── engine/               ← ANSI 더블버퍼 렌더러, 논블로킹 입력, MCI BGM
│   ├── game/                 ← 게임 로직 (Scene 머신, 배틀, 오버월드)
│   └── data/                 ← 자동 생성 헤더 (스프라이트/타일/맵)
├── tools/                    ← 데이터 변환 파이썬 도구
│   ├── fetch_assets.py       ← pokered 원본에서 필요한 파일만 다운로드 (1회)
│   ├── bake_assets.py        ← .blk/.bst/.asm → PNG/JSON 변환 (1회, 어셈블리 제거)
│   ├── gen_sprites.py        ← PNG → ANSI half-block 스프라이트 헤더
│   ├── gen_tiles.py          ← 타일셋 PNG → ANSI 타일 헤더
│   └── gen_maps.py           ← 맵 PNG + JSON → C++ 맵 데이터
├── pokered_assets/           ← 자산 (PNG + JSON 만; 어셈블리/바이너리 없음)
│   ├── gfx/
│   │   ├── pokemon/{front,back}/*.png   ← 22장 (앞/뒤 11종)
│   │   ├── trainers/*.png, player/red.png ← 인트로용
│   │   └── tilesets/*.png                ← 5종
│   ├── maps/
│   │   ├── *.png                         ← 9개 (1픽셀=1타일, 색=타일종류)
│   │   └── *.json                        ← 9개 (warps/signs/encounters)
│   └── pokemon_palettes.json             ← 종족→팔레트 매핑
└── build/                    ← 빌드 산출물 (.exe + 런타임 DLL)
```

---

## 데이터 변환 파이프라인

```
pokered (PNG/blk/bst/asm)              [한 번만 받음]
    ↓ tools/fetch_assets.py            (네트워크 다운로드)
임시 파일 (.blk/.bst/.asm 포함)
    ↓ tools/bake_assets.py             (1회 변환 — 어셈블리/바이너리 제거)
pokered_assets/ (PNG + JSON 만)        [최종 자산]
    ↓ tools/gen_sprites.py
    ↓ tools/gen_tiles.py
    ↓ tools/gen_maps.py
src/data/*.h                           [컴파일-인 ANSI 헤더]
    ↓ g++
PokemonRed.exe                         [완전 오프라인 동작]
```

**자산 형식**:
- 모든 시각 자산은 **PNG** (포켓몬/트레이너/타일셋/맵)
- 메타데이터(맵 워프/표지판/조우 풀, 종족→팔레트)는 **JSON**
- pokered의 어셈블리(.asm)/바이너리(.blk/.bst) 형식은 변환 후 제거됨

게임 자체는 빌드 후엔 `pokered_assets/`나 인터넷 없이 단독으로 실행됨.

---

## 사용한 외부 자료 (출처 명시)

본 프로젝트는 다음 자료를 변환·활용하였습니다:

- **[pret/pokered](https://github.com/pret/pokered)** — Pokémon Red의 공개 디스어셈블리 프로젝트.
  - 활용 범위: 포켓몬 앞/뒤 스프라이트 PNG, 트레이너 PNG, 타일셋 PNG, 맵 블록(.blk),
    블록셋(.bst), 맵 오브젝트(.asm), 종족별 팔레트 매핑(`data/pokemon/palettes.asm`).
  - 변환 방식: 본 저장소의 `tools/` 파이썬 스크립트들이 위 원본 자산을
    **ANSI 256색 half-block 터미널 형식 C++ 헤더**로 변환.
  - 원본 그래픽 자산의 저작권은 닌텐도/게임프리크/크리처스에 있음.

- **포켓몬 레드 도트(스프라이터스 리소스 시트 #8728)** — 오버월드 플레이어 8방향 프레임 추출용.

### 직접 작성한 부분
- 게임 엔진: ANSI 더블버퍼 렌더러(`src/engine/renderer.cpp`), 논블로킹 키 입력, MCI BGM
- 게임 로직: 씬 상태머신, 배틀 시스템(데미지/타입 상성/스탯 변화), 오버월드(이동/충돌/NPC/트레이너 시야), 인트로/엔딩
- 데이터 파이프라인: `tools/` 4종 파이썬 변환기 (PNG→ANSI half-block, 종족 컬러 팔레트, blk→문자맵 등)
- 한국어 텍스트 / 게임 콘텐츠 구성

---

## 라이선스

본 프로젝트의 직접 작성 코드는 학습 목적의 과제물입니다.
포켓몬 레드의 그래픽/맵/캐릭터 데이터는 닌텐도/게임프리크/크리처스의 저작물이며,
본 저장소에서는 비상업적 학습 목적의 변환물로 포함됩니다.
