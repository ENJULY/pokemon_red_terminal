# 팀 인수인계 노트 (Team Handoff)

> 이 프로젝트에서 작업할 팀원에게 전달하는 문서.
> "이미 알아둬야 할 것들 + 작업한 기록 + 함정"을 정리해둠.
>
> 다른 가이드:
> - **README.md** - 빌드/실행
> - **BEGINNER_GUIDE.md** - 입문자용 큰 그림
> - **PROJECT_OVERVIEW.md** - 기술 상세
> - **NPC_GUIDE.md** - NPC/트레이너 추가 매뉴얼
> - **CHANGELOG.md** - 최신 변경 이력



# 작업 기록 (최신 → 과거 순)


## 시스템 추가

### 풀베기(Cut) 시스템
- 새 기술 ID 18 (풀베기) 추가 (`pokemon_data.h`)
- Player 구조체에 `cutTreeMapId/X/Y[16]` + `cutTreeCount` 추가
- 헬퍼: `playerHasCut()`, `isTreeCut()`, `addCutTree()`
- 'T' 타일 마주보고 A: 파티에 풀베기 보유 시 베어냄, 아니면 힌트 메시지
- 베어진 'T' 는 walkable 풀밭으로 처리됨 (이동/렌더 양쪽)
- Ctrl+M 워프 시 풀베기 모르는 상태면 이상해씨 L10 더미 자동 지급 (디버그)

### 표지판 시스템
- `SignDef` 구조체 + 전역 `ALL_SIGNS[]` 테이블 (`map_data.h`)
- A키로 표지판 앞에서 상호작용 → 텍스트 출력
- 현재 28개 표지판 등록 (전 맵에 걸쳐)
- 표지판 추가 시: `ALL_SIGNS[]` 에 항목 추가만 하면 됨 (개수 카운트 없음 - `sizeof`로 자동)

### 잠긴 문 메시지
- 워프의 `destMap == -1` (미구현 건물) 인 경우, "문이 잠겨있다..." 메시지 표시
- 적용 대상: 상록시티 School/Nickname/Gym, 회색시티 Museum/Nidoran/Speech, Route 2 Trade House 등

### 디그다 동굴 차단 NPC
- Route 2 (12, 9) 워프 자리에 NPC 배치 → 동굴 진입 차단
- NPC 대사: "안쪽은 위험한 디그다가 많아!"

### 맵 경계 선택적 길 표시
- `overworld.cpp` 렌더링: 카메라 밖이 outdoor 이웃 맵일 때, **경계 셀이 walkable이면 풀밭(`!`)**, 아니면 트리(`$`) 표시
- pokered처럼 이웃 맵 실제 타일을 그리진 않음 (워프 기반 이동 유지)
- 실내 맵 경계는 검정 처리 (`continue`)

### 절벽/계단 시스템 정정 (pokered 검증)
| Char | atoms | 역할 |
|------|-------|------|
| `0` | (44,44,55,55) | 남쪽 ledge (점프 only) |
| `5` | (57,57,54,55) | 남쪽 ledge (SW variant) |
| `6` | (57,57,55,55) | 남쪽 ledge (SE variant) |
| `<` | (44,44,55,52) | 남쪽 ledge (시각은 계단이지만 BL=55) |
| `?` | (44,44,60,60) | walkable 계단 (BL=60 passable) |
| `9` | (20,20,20,20) | ledge 아님 (path/dirt) |

- pokered `LedgeTiles` + `Overworld_Coll` 데이터 직접 분석한 결과
- isLedge 검사: `'0' || '5' || '6' || '<'`
- walkable: `?` 만 포함

### 플레이어 스프라이트 8프레임
- pokered `red.png` 16x96 = 6 frames + 미러 2 추가 = 8 frames
- 프레임 매핑:
  ```
  F0 = DOWN idle, F1 = UP idle, F2 = LEFT idle
  F3 = DOWN walk, F4 = UP walk, F5 = LEFT walk
  F6 = RIGHT idle (mirrored F2), F7 = RIGHT walk (mirrored F5)
  ```
- `tools/gen_sprites.py` 의 `convert_ow_player_frame_mirrored()` 가 미러 생성

### TrainerDef.isBoss 플래그
- 체육관 관장은 `isBoss = true` → `BOSS_BATTLE` 트리거 (엔딩 연결)
- 일반 트레이너는 `false` → 일반 `TRAINER_BATTLE`
- 이전엔 `trainers[i] index >= 2` 라는 hack을 썼지만 명시적 플래그로 변경

### 배틀 풀바디 스프라이트 6종
- `tools/gen_sprites.py` 의 `INTRO_TARGETS` 에 추가:
  - 0: OAK, 1: RIVAL, 2: RED
  - 3: BROCK, 4: BUG_CATCHER, 5: COOLTRAINER_M
- `BattleState.trainerIntroId` 로 어느 그림 표시할지 결정


## 맵 추가 및 수정

### 추가된 맵
- **MAP_4 (상록숲)**: 18x28 placeholder → 34x48 실제 pokered 디코딩
- **MAP_16 (상록숲 북쪽 게이트)**: 10x8 - Route 2 (3,11) <-> Forest (1,0)/(2,0)
- **MAP_17 (상록숲 남쪽 게이트)**: 10x8 - Route 2 (3,43) <-> Forest (15-18, 47)
- **MAP_8 (회색시티 체육관)**: 8x14 placeholder → 10x14 with floor + door

### 수정된 맵 데이터
- **MAP_7 (주인공 집)**: 정문 `DD` → `CC` (카펫 시각으로 변경)
- **MAP_9 (주인공 방 2F)**: `##` → `ii` (overworld 풀밭 → 카펫 floor)
- **MAP_10 (라이벌 집)**: 정문 `DD` → `CC` 카펫 처리
- **MAP_2 (상록시티)**: 22번도로 차단 (할아버지 NPC at (5, 17)) 이미 있음
- **MAP_5 (회색시티)**: 3번도로 차단 추가 - 동쪽 cols 36-39 rows 16-19에 `1!!!` 패턴 (벽 + 풀밭)
- **MAP_3 (Route 2)**: northMap을 `MAP_VIR_FOREST` → **`MAP_PEWTER`** 로 수정 (pokered 일치)
- **MAP_5 (Pewter)**: southMap을 `MAP_VIR_FOREST` → **`MAP_ROUTE2`** 로 수정
- **MAP_15 (Route 2 Gate)**: 내부 보조원 NPC 제거 (빈 게이트)

### 워프 연결
- 전체 게이트 체인 정상화: Viridian → Route 2 → (남쪽 게이트) → Forest → (북쪽 게이트) → Route 2 → Pewter
- 회색시티 체육관 워프 destination을 `(4, 12)` 로 명시 (이전엔 -1 → southEntry 폴백 = (4, 0) 잘못된 위치)
- Forest → South Gate 워프 4개 모두 `(5, 0)` 통일 (pokered 도어 블록 0x73의 BL atom 분석 결과)

### 타일 시각 수정
- **상록숲 나무**: `$/%/&/(/)` 모두 OW3 트리(풀바디)로 dispatch - 작은 stump → 일반 맵과 동일
- **Pewter Gym 도어**: `?` 가 OakLab 도어로 렌더링됨 (트라이그래프 회피용 `?\?`)
- **MAP_7/10 카펫**: `case 'C': return &TILE_RH_XC;` 추가 (RedsHouse 카펫 atoms 4,4,20,20)


## 시스템 수정

### overworld.cpp
- 카운터 너머 NPC 대화 (`!talked && !tileWalkable`) 후 `talked = true` 누락 수정
- isLedge 분기 - 절벽 char 리스트 + 점프 도착지 walkable 검증
- 표지판 검사 (`findSign(mapId, x, y)`)
- 잠긴 문 dialog 표시 (`_lockedDoorBuffer`)
- 풀베기 dialog 표시 (`_cutTreeUsedBuffer`, `_cutTreeNeedSkillBuffer`)
- 'T' 타일 walkable 처리 (베어진 경우)

### map_data.h
- `TrainerDef` 에 `bool isBoss = false` 필드 추가
- `SignDef` 구조체 + `ALL_SIGNS[]` + `findSign()` 헬퍼

### tiles.h
- MAP_4 dispatch에서 `$/%/&/(/) ` 모두 `TILE_OW3_C036` 사용
- 실내 fallback에 `case 'C': return &TILE_RH_XC;` 추가
- MAP_15/16/17 게이트 통합 dispatch


## 빌드/배포

### build.bat 재작성
- 절대경로(`C:\mingw64`) 제거, 다른 PC에서도 동작
- g++ 자동 탐색: PATH → 7개 표준 설치 위치
- 빌드 → DLL 자동 복사 → 게임 자동 실행 (pause 없음)
- ASCII 메시지만 사용 (cmd.exe 코드페이지 호환성)
- CRLF 줄바꿈 + `chcp 65001` 출력 UTF-8

### 프로젝트 정리
- `tools/` 에서 옛 generators 10개 삭제 (v1, v2, 패치 스크립트 등)
- `BACKUP/` 에서 옛 백업 2개 삭제, 최신만 유지
- 현재 active: `gen_overworld_tiles_v3.py`, `gen_gate_forest_tiles.py`, `gen_pokecenter_mart_tiles.py`, `gen_pewter_gym_tiles.py`, `gen_oaklab_tiles.py`, `gen_sprites.py`



# 팀원 필수 숙지 사항


## 1. 같은 char 가 맵마다 다른 의미

가장 큰 함정. **`!` 가 어떤 맵에선 walkable, 어떤 맵에선 wall**일 수 있음.

| 맵 종류 | `!` | `$` | `+` | `%` |
|--------|-----|-----|-----|-----|
| Overworld (0-5, 4 제외) | 풀밭 (walkable) | 나무 (벽) | path | path |
| Forest (4) | 풀밭 (walkable) | **OW3 나무** | 표지판 (벽) | 나무 |
| Gate (15-17) | 바닥 (walkable) | 벽 | walkable | 벽 |
| PC/Mart (11-14) | 바닥 (walkable) | 카운터 (벽) | walkable | 벽 |
| Indoor Gym (8) | 바닥 | - | - | - |

이 차이는 `tileWalkable(char, mapId)` 와 `getTileArt(char, mapId)` 가 처리.

**작업 시**: 새 tile char 추가하면 두 함수 모두 갱신 필요.


## 2. 워프 작동 메커니즘

```
플레이어가 워프 char(`D`, `L`, `C`, `M`, `G`, `?`, `j`, `w`, `p`)에 닿음
  → tryMove() 가 tileWalkable() = false 감지
  → 그래도 워프 char면 checkWarps() 호출
  → 일치하는 srcX/srcY 워프 찾음
  → destMap = -1 이면 "문이 잠겨있다" 메시지
  → 아니면 12프레임 페이드 → mid-fade에 mapId/px/py 교체
```

**중요**:
- 워프 src 좌표 = 워프 타일 위치
- 워프 dest 좌표 = 도착지 위치 (-1, -1이면 dest맵의 southEntry 사용)
- 워프 후 도착 타일에 다시 워프 자동 트리거 안 됨 (재트리거하려면 이동 후 재진입 필요)


## 3. NPC vs 트레이너 vs 표지판

세 가지가 비슷한 dialog 시스템을 공유하지만 데이터 위치가 다름:

| 타입 | 데이터 위치 | 트리거 | 차단 |
|------|------------|--------|------|
| NPC | `MapDef::npcs[]` (맵별) | A키 | 타일 점유 |
| 트레이너 | `MapDef::trainers[]` (맵별) | 시야 진입 시 자동 | 타일 점유 + 시야 |
| 표지판 | `ALL_SIGNS[]` (전역) | A키 | (보통 이미 벽 타일에 배치) |

표지판 데이터는 전역이라서 mapId만 맞으면 어디든 추가 가능.


## 4. ALL_SIGNS 좌표는 실제 sign tile 위치

**주의**: pokered의 `bg_event` 좌표를 그대로 쓰면 안 됨.
우리 맵 생성 결과에서 sign tile은 다른 위치에 있을 수 있음.

**확인 방법**: 맵 데이터에서 sign char (`=`, `O`, `R`, `S`, `+` 등) 직접 찾아서 그 좌표 사용.


## 5. Trainer 시야 (sightRange)

플레이어가 시야 라인에 들어가는 순간 자동 배틀. 시야는:
```
dir = 0 (↓):  (trainer.x, y) where y in [trainer.y+1, trainer.y+sightRange]
dir = 1 (↑):  (trainer.x, y) where y in [trainer.y-sightRange, trainer.y-1]
dir = 2 (←):  (x, trainer.y) where x in [trainer.x-sightRange, trainer.x-1]
dir = 3 (→):  (x, trainer.y) where x in [trainer.x+1, trainer.x+sightRange]
```

**대각선 시야 없음**. 시야 라인 중간에 벽/NPC가 있어도 시야는 통과 (구현 한계).


## 6. 포켓몬 종족 추가

`pokemon_data.h` 의 `SPECIES[]` 배열에 추가하고 **반드시 `NUM_SPECIES_DATA` 갱신**.
없는 ID를 트레이너 파티에 쓰면 게임 충돌.

현재 추가된 12종: 1, 4, 7, 10, 11, 13, 14, 16, 19, 25, 74, 95


## 7. 자산 재생성 시 주의

`tools/gen_*.py` 스크립트는 **자동 생성 영역만** 덮어씀:
- `// --- OW3_AUTO_BEGIN ...` 마커 사이만 갱신
- 수동 추가한 NPC/트레이너/표지판은 보존됨

하지만 **MAP_X tiles 레이아웃은 통째로 교체**될 수 있음. 손으로 수정한 타일은 백업 필수.


## 8. 빌드 환경

- MinGW-w64 필요 (PATH 또는 `C:\mingw64`)
- C++17
- Windows 전용 (Win32 콘솔 API 사용)
- `build.bat` 더블클릭 → 자동 컴파일 + 실행

**다른 PC에서**: `build.bat` 는 PATH/표준 위치 자동 탐색하므로 그대로 실행 가능.


## 9. build.bat 편집 시 주의

**주의**: 배치 파일 특유 함정:
- **CRLF 줄바꿈** 필수 (`^` 라인 컨티뉴에이션 동작 위해)
- **ASCII만 사용** (한글 메시지는 cmd.exe 파싱 깨짐)
- **`()` 괄호** 는 `if (...)` 블록과 충돌 - echo 문 안에 쓰지 말 것
  ```
  잘못된 예: echo Failed (code %BUILDRC%)
  올바른 예: echo Failed, code %BUILDRC%
  ```


## 10. 자동 재생성 vs 수동 편집

자동 생성 영역 (`// SEMI-AUTO-GENERATED` 마커):
- `src/data/tiles.h` - 전체 자동
- `src/data/sprites.h` - 전체 자동
- `src/data/map_data.h` - tiles[] 영역만 자동, NPC/트레이너/워프/표지판은 손편집

손편집 시 `gen_*.py` 다시 안 돌리는 게 안전. 돌려야 할 때:
1. 손편집 영역 백업
2. `gen_*.py` 실행
3. 백업 영역 다시 복원



# 알려진 한계/이슈

| 항목 | 상태 |
|------|------|
| 가방 시스템 (포션, 몬스터볼 등) | 미구현 |
| 야생 포켓몬 포획 | 미구현 |
| 상태이상 (마비, 독, 잠 등) | 미구현 |
| HM 기술 (베기, 파도타기 등) | 풀베기만 골격 구현 |
| 메뉴 (START 버튼) | 미구현 |
| 세이브/로드 | 미구현 |
| BGM | winmm 골격만, 실제 재생 미구현 |
| 사운드 효과 | 미구현 |
| 트레이너 대각선 시야 | 미구현 (직선만) |
| 트레이너 시야 차단 (NPC/벽 사이 끼면 무시) | 미구현 |
| 트레이너 패배 후 다시 안 봄 처리 | 미구현 (저장 없어서) |
| 미구현 건물 9개 | 잠긴 문으로 처리 중 |
| 동서(east/west) 맵 스크롤 연결 | 미구현 (north/south만) |
| 동굴/실내 dark area 효과 | 미구현 |
| 자전거 | 미구현 |



# 다음 작업 후보

1. **상태이상 시스템** - 독, 마비, 잠, 화상, 빙결
2. **가방/아이템** - 몬스터볼, 포션, 안티독트
3. **포켓몬 포획** - 가방 + 야생 배틀에서 사용
4. **MENU (START 키)** - 가방/포켓몬/도감/저장 통합
5. **저장/로드** - 파일 1개로 게임 상태 직렬화
6. **BGM 실제 재생** - winmm `mciSendString` 으로 wav/mid 재생
7. **사운드 효과** - 메뉴 클릭, 배틀 사운드 등
8. **미구현 건물 채우기** - School/Speech/Nickname/Museum 등
9. **HM01 베기** - 가방 아이템으로 HM01 추가 (현재는 풀베기만 구현됨)
10. **트레이너 대각선/시야 차단** - 시야 검사 고도화



# 핵심 파일 빠른 참조

```
src/data/map_data.h    맵/NPC/트레이너/표지판 - 가장 자주 편집
src/data/tiles.h       타일 그래픽 (자동생성, 손편집 금지)
src/data/sprites.h     캐릭터 그래픽 (자동생성)
src/data/pokemon_data.h 종족값/기술 - 새 포켓몬 추가 시
src/game/overworld.cpp 이동/충돌/NPC 상호작용 로직
src/game/battle.cpp    배틀 엔진
src/game/game.cpp      Scene 상태머신
src/game/player.h      플레이어 상태 + 헬퍼
tools/gen_*.py         자산 재생성 (필요시만)
build.bat              빌드 + 실행
```



# 마지막으로

이 프로젝트는 **데이터-주도 (data-driven)** 설계가 핵심.
새 기능 추가는 대부분 `map_data.h` 의 데이터 항목을 늘리는 것으로 끝남.

**의문이 생길 때 우선 확인**:
1. `map_data.h` 의 비슷한 기존 항목 (NPC/트레이너/워프/표지판)을 본다
2. `overworld.cpp` 의 어디서 그 데이터를 읽는지 grep으로 추적
3. `tileWalkable()` / `getTileArt()` 가 mapId 별로 분기하는 패턴 확인

문서들 (README/BEGINNER/PROJECT/NPC/CHANGELOG) 이 다른 각도에서 같은 시스템을 설명하므로,
이해 안 되면 다른 문서를 보는 것도 방법.

행운을 빕니다.
