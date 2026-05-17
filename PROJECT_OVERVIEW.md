# Pokemon Red Terminal - 프로젝트 구조


## 전체 개요

**Pokemon Red 게임을 Windows 터미널에서 ANSI 컬러 + UTF-8 반블록 문자(▀▄)로 재현한 C++17 프로젝트.** 원본 pokered 디스어셈블리(`../pokered/`)에서 맵/스프라이트 데이터를 Python 스크립트로 디코드 후 C++ 헤더로 변환, 게임 엔진이 런타임에 ANSI를 직접 stdout에 출력하는 구조.

```
~9,500 lines C++ (engine + game + data)
~4,500 lines Python (asset generators)
```


## 디렉토리 구조

```
pokemon_red_terminal-main/
- src/
  - main.cpp              진입점 (5 lines)
  - engine/               플랫폼/렌더 레이어
    - renderer.{h,cpp}    더블버퍼 ANSI 출력
    - input.{h,cpp}       Win32 콘솔 키 입력
    - audio.{h,cpp}       winmm 사운드 (옵션)
  - game/                 씬/룰 레이어
    - game.{h,cpp}        Scene 머신 + 인트로/대화/엔딩
    - overworld.{h,cpp}   맵 이동 + 워프 + 인카운터
    - battle.{h,cpp}      턴제 배틀 엔진
    - player.h            플레이어 상태 구조체
  - data/                 자동생성된 데이터 헤더
    - tiles.h             모든 타일의 ANSI 아트 (3353 lines)
    - sprites.h           NPC/플레이어/인트로 스프라이트
    - map_data.h          맵 18개 레이아웃 + NPC/워프/인카운터
    - pokemon_data.h      종족값/기술/레벨업
    - type_chart.h        타입 상성
- tools/                  Python 디코더/생성기
- pokered_assets/         캐시된 원본 그래픽 PNG
- BACKUP/                 작업 체크포인트
- build.bat               MinGW g++ 빌드 스크립트
```


## 어떻게 화면을 그리나? (렌더링 파이프라인)

### Step 1: pokered 원본 → C++ 헤더 (오프라인, Python)

원본 `pokered/gfx/tilesets/overworld.png` (256x256 픽셀)을 8x8 픽셀 atom 단위로 자르고, `overworld.bst` 블록 정의를 읽어서 각 16x16 step 타일에 대응하는 4개 atom을 모음.

```python
# gen_overworld_tiles_v3.py
def step_atomics(blk, blk_w, sx, sy):
    # blk = ViridianCity.blk (1바이트=block_id)
    # bst = overworld.bst (block_id → 16개 atom)
    block_id = blk[by * blk_w + bx]
    a = BST[block_id*16:(block_id+1)*16]
    return (a[base_y*4 + base_x], ...)  # 2x2 atoms = 1 step tile

# pokered 픽셀 4단계 (255/170/85/0) → ANSI 5-color (255/250/244/232)
# 위/아래 픽셀이 다르면 반블록 `▀` 사용
"\x1b[38;5;232;48;5;244m▀\x1b[0m"  # foreground=black, bg=darkgrey
```

각 unique atom 4-tuple에 ASCII 문자 1개를 할당 (`!`, `%`, `(`, ... 빈도순). 그러면 한 줄에 40 chars = 40개 step 타일을 표현할 수 있음:

```cpp
// MAP_2 (Viridian) 한 행 예시
"*****1$$$$$$$$$$/&%%$$$$$$$$$$$$$$$$$$$$",
```

### Step 2: 런타임 (C++ Renderer)

`Renderer`는 200x60 char `Cell` 더블 버퍼를 유지:

```cpp
// engine/renderer.cpp
struct Cell { std::string ch; std::string color; };
std::vector<Cell> curr_, prev_;  // diff-based flush

// clear() → curr_을 공백으로
// printRaw() → ANSI 이스케이프가 박힌 row 문자열 파싱해서 cell에 저장
// flush() → prev_와 다른 셀만 stdout으로 출력 (커서 이동 + ANSI)
```

매 프레임 `overworld.cpp::renderKorean()`이 실행됨:

```cpp
int tilesX = W / 16;  // 12 tiles wide
int tilesY = viewH / 8;
int camX = state_.px - tilesX/2;  // 카메라 중심 = 플레이어

for (int ty = 0; ty < tilesY; ty++) {
    for (int tx = 0; tx < tilesX; tx++) {
        int mx = camX + tx, my = camY + ty;
        // 1) 현재 맵 안이면 → getTile(m, mx, my)
        // 2) outdoor 이웃 맵 경계 너머면 → getTile(neighborMap, ...)  ← seamless scroll
        // 3) 그 외 → '$' (tree) 또는 'continue' (indoor)

        char tile_char = ...;
        const TileArt* art = getTileArt(tile_char, artMapId);  // mapId-aware dispatch
        for (int r = 0; r < 8; r++)
            ren_.printRaw(tx*16, ty*8+r, art->rows[r]);
    }
}
```

타일은 항상 **16 chars × 8 char-rows = 1 step**으로 고정. mapId에 따라 OW3 (overworld), FR (forest), GT (gate), PC (pokecenter), PG (pewter gym), RH (reds house), OL (oaklab) 중 적절한 타일셋으로 dispatch.

### Step 3: 스프라이트 오버레이

플레이어(16char × 8row)와 NPC가 타일 위에 합성됨. 투명 마커는 `\x1f` 문자:

```cpp
// renderer printRaw 처리
if (*s == '\x1f') { cx++; continue; }  // 셀 변경 없이 커서만 진행
```

플레이어 스프라이트는 8 frames (Down idle/walk, Up idle/walk, Left idle/walk, Right idle/walk - 오른쪽은 픽셀 미러 생성).


## 게임 로직 레이어

### Scene State Machine (`game.cpp`)

전체 게임은 `Scene` enum 기반 상태 머신:

```cpp
enum class Scene {
    INTRO, NAME_INPUT, RIVAL_NAME_INPUT,
    LAB_INTRO, STARTER_SELECT, RIVAL_INTERCEPT, RIVAL_BATTLE,
    RECEIVE_DEX,
    OVERWORLD,        // 메인 탐험
    WILD_BATTLE, TRAINER_BATTLE, BOSS_BATTLE,
    POKEMON_CENTER, MART_EVENT,
    GAME_OVER, ENDING,
    WARP_MENU,        // Ctrl+M 디버그
};
```

메인 루프(`run()` in game.cpp):
```cpp
while (running) {
    renderer.clear();
    render();              // 현재 씬 렌더
    renderKorean();        // 한글 텍스트 오버레이
    renderer.flush();
    Sleep(16);             // ~60 FPS

    Key key = Input::poll();
    update(key);           // 현재 씬 업데이트
}
```

씬 전환은 `changeScene(Scene::OVERWORLD)` 호출. 각 씬은 `update`/`render` 두 함수를 가짐.

### Overworld 시스템 (`overworld.cpp`)

```cpp
struct OwState {
    int mapId, px, py, dir;
    int moving, stepDx, stepDy;     // 걷기 애니메이션 4프레임 보간
    NpcDialogState dialog;          // 대화 중인 NPC
    OwEvent pendingEvent;           // 배틀/회복/엔딩 트리거
    OwCutscene cutscene;            // 오박사 인터셉트 등
    int warpFlashFrames;            // 워프 페이드아웃 12프레임
    int pendingWarpMap;             // 페이드 중간(6프레임)에 실제 워프 실행
};
```

**이동 처리 (`tryMove`)**:
1. 다음 타일이 `tileWalkable()` true → 걷기 시작 (4 frame 보간)
2. false면 워프 트리거 char (`'D','L','C','M','G','?','j','w','p'`) → `checkWarps()`
3. 그 외 → 차단

**절벽 점프** (남쪽 ledge `'0','5','6','<'`로 진입 시):
```cpp
if (isLedge && dy > 0) {
    int jy = ny + 1;  // 2칸 점프
    if (tileWalkable(getTile(m, nx, jy))) {
        state_.py = jy;
        state_.moving = 8;  // 점프는 더 긴 애니메이션
    }
}
```

**Cross-map seamless scroll**: 카메라 밖 셀이 outdoor 이웃 맵이면 그 맵의 실제 타일 픽업. Indoor면 검정.

**워프 페이드**: 워프 진입 → `warpFlashFrames = 12` → 매 프레임 카운트다운 → 6에서 mapId/px/py 갱신 → 0까지 검정 화면.

### Battle 시스템 (`battle.cpp`)

```cpp
enum class BattlePhase {
    INTRO_TRANSITION,    // 풀바디 트레이너 등장
    SHOW_MSG,            // 대사 표시
    CHOOSE_COMMAND,      // 싸운다 / 가방 / 교체 / 도망
    CHOOSE_MOVE,         // 4기술 메뉴
    PLAYER_ACTION,       // 데미지 계산 + 메시지
    ENEMY_ACTION,
    CHECK_FAINT, GRANT_EXP, LEVEL_UP,
    CAUGHT, SWITCH, DONE
};
```

**데미지 공식** (Gen1 단순화):
```cpp
damage = ((2*level/5 + 2) * power * atk / def) / 50 + 2
       * type_effectiveness * stab * critRand;
```

**적 AI**: 4기술 중 데미지 기댓값 가장 큰 것 선택.


## 데이터 구조 (data/)

### MapDef (map_data.h)

```cpp
struct MapDef {
    int         id;
    int         mapW, mapH;
    const char* tiles[73];        // tile chars per row
    NpcDef      npcs[8];          // 위치 + 대사 + 스프라이트 ID + trigger
    TrainerDef  trainers[4];      // 시야 검사 + 파티
    WarpDef     warps[12];        // (srcX, srcY) → destMap (destX, destY)
    EncounterEntry encounters[6]; // species + weight (15% 확률로 풀숲에서 트리거)
    int northMap, southMap;       // 경계 스크롤 이웃
    int northEntryX, southEntryX; // 오프셋 (Pewter 5너비 다른 폭과 정렬)
    const wchar_t* bgmFile;
};
```

총 **18개 맵** (Pallet, Route 1, Viridian, Route 2, Forest, Pewter, OakLab, Player House 1F/2F, Rival House, PC x 2, Mart x 2, Route 2 Gate, Forest Gate x 2, Pewter Gym).

### Tile dispatch (tiles.h)

```cpp
inline const TileArt* getTileArt(char c, int mapId) {
    if (mapId == 8) return getPewterGymTile(c);     // PG_*
    if (mapId == 4) return getForestTile(c);        // FR_*
    if (mapId == 15 || 16 || 17) return getGateTile(c);  // GT_*
    if (mapId >= 0 && mapId <= 5 && mapId != 4)
        return getOverworldTile(c);                 // OW3_*
    if (mapId == 11..14) return getPokecenterTile(c);  // PC_*
    return getIndoorFallback(c);  // RH_* (Reds House) + OL_* (OakLab)
}
```

같은 char (`'!'`)가 mapId에 따라 다른 타일 (overworld 풀밭 vs gate 바닥 vs gym 바닥)로 렌더링됨.

### TileWalkable (map_data.h)

map별로 통과 가능 char 목록이 다름:
```cpp
if (mapId == 4) /*forest*/  return t == ' '||'!'||'*'||','||'-'||';';
if (mapId == 8) /*gym*/     return t == '!'||'('||'?';
if (mapId == 15..17) /*gate*/ return t == ' '||'!'||'*'||'+';
if (mapId == 0..5) /*overworld*/
    return t == ' '||'!'||'%'||'&'||'('||'+'||'-'||'4'||';'||'?'||'C'||'D'||'G'||'M';
// 실내 fallback
return t == ' '||'f'||'i'||'u'||'+';
```

pokered 원본 `Overworld_Coll::coll_tiles $00, $10, ...` 데이터를 디코드한 결과.


## 자산 파이프라인

```
../pokered (원본 디스어셈블리)
  - maps/*.blk          맵 블록 ID 배열
  - gfx/blocksets/*.bst block → 16 atom IDs
  - gfx/tilesets/*.png  atom 그래픽
  - data/tilesets/      collision/ledge 정의

         ↓ Python 디코더

tools/
  - gen_overworld_tiles_v3.py    OW3_* (도시+도로)
  - gen_gate_forest_tiles.py     FR_* + GT_*
  - gen_pokecenter_mart_tiles.py PC_*
  - gen_pewter_gym_tiles.py      PG_*
  - gen_oaklab_tiles.py          OL_*
  - gen_sprites.py               SPR_* + IntroSprite + OwPlayerFrame

         ↓

src/data/{tiles,sprites,map_data}.h (자동 생성 헤더)

         ↓ MinGW g++ -std=c++17

build/PokemonRed.exe
```

각 generator는 다음을 함:
1. pokered `.blk` 읽기 → block ID 배열
2. `.bst` 읽기 → block → 16 atom ID
3. step (16x16) 단위로 atom 4-tuple 추출
4. unique variant마다 ASCII char 할당 (essential atoms 우선, 나머지 빈도순)
5. 각 atom을 8x8 픽셀에서 4단계 회색 → ANSI 5-color half-block 변환
6. C++ TileArt 정의로 출력 + `getTileArt` switch case 추가
7. `MAP_X` tiles[] 레이아웃 자동 패치


## 핵심 메커니즘 요약

| 메커니즘 | 구현 위치 | 트리거 |
|---------|-----------|--------|
| 타일 렌더 | `overworld.cpp::renderKorean` | 매 프레임 |
| 걷기 보간 | `tryMove` → `state_.moving = 4` | 방향키 → walkable |
| 워프 페이드 | `checkWarps` → `warpFlashFrames = 12` | 도어 char 접촉 |
| 절벽 점프 | `tryMove` isLedge 분기 | south 이동 + ledge char |
| 트레이너 시야 | `checkTrainerSight` | 매 걸음 종료 시 |
| 야생 인카운터 | `checkSpecialTiles` | `;` (풀숲) + 15% 랜덤 |
| NPC 대화 | A키 → `state_.dialog.active` | 인접 + 마주봄 |
| 카운터 너머 대화 | NPC 두 칸 앞 + 사이 unwalkable | 마트/PC 점원 |
| 씬 전환 | `Game::changeScene` + `redrawAll` | 이벤트/배틀 종료 |
| Ctrl+M 워프 | `Key::WARP_MENU` → `Scene::WARP_MENU` | 디버그용 |
| 표지판 | `findSign(mapId, x, y)` | A키 |
| 잠긴 문 | warp destMap == -1 | 워프 char 접촉 |
| 풀베기 | A키 + 'T' 타일 + 파티에 Cut 보유 | A키 |


## 디자인 철학

1. **원본 충실** - pokered `.blk` 데이터를 그대로 디코드. 손편집은 NPC/대사/한글화만.
2. **데이터 자동생성** - 맵 한 줄도 손으로 안 그림. Python 스크립트가 원본 atom을 분석해서 텍스트 레이아웃 + 타일 그래픽을 모두 생성.
3. **mapId 기반 dispatch** - 같은 char가 다른 맵에서 다른 그래픽으로 렌더링됨. 한 문자셋으로 7개 타일셋 표현.
4. **순수 stdout 렌더** - DirectX, ncurses, SDL 등 외부 라이브러리 ZERO. Windows 콘솔 + VT100 ANSI만 사용.
5. **diff-based flush** - 매 프레임 200x60 = 12000 cell 중 변한 셀만 출력. 키 입력 응답성 유지.
