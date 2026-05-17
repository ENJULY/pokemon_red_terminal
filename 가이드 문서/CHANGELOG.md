# 작업 변경 이력 (Change Log)

> 이 프로젝트의 전체 진행 사항 + 팀원이 알아야 할 변경점 정리.
> 최신 → 과거 순서로 정렬.
>
> 더 자세한 인수인계 노트는 **TEAM_HANDOFF.md** 참조.
> 작업자 매뉴얼은 **NPC_GUIDE.md** 참조.



# 최근 작업

## 2번도로 게이트 NPC 제거

**위치**: `MAP_15` (Route 2 Gate)
**변경**: 기존에 있던 오박사 보조원 NPC 1명 제거
**이유**: 사용자 요청 — 게이트는 빈 통과 공간으로

```cpp
// 변경 전
{
    {1, 4, {L"보조원: ...", ...}, NPC_SPR_OAK, 0},
}, 1,  // npcs
// 변경 후
{}, 0,  // npcs (Route 2 Gate 보조원 NPC 제거됨)
```


## 풀베기 (HM01 Cut) 시스템

**파일**: `pokemon_data.h`, `player.h`, `overworld.cpp`, `game.cpp`

### 추가된 데이터
- 신규 기술 ID 18: `풀베기` (위력 50, 명중 95, PP 30, NORMAL 타입)
- `MOVE_CUT = 18` 상수
- Player 구조체에 `cutTreeMapId/X/Y[16]`, `cutTreeCount` 필드
- 헬퍼 함수: `playerHasCut()`, `isTreeCut()`, `addCutTree()`

### 동작 흐름
1. 'T' (Cut Tree) 타일 마주보고 Z 키
2. 파티에 풀베기 보유 포켓몬 있나 검사
3. **있으면**: `addCutTree()` 호출 → "포켓몬이 풀베기를 사용했다!" → 그 타일이 walkable 풀밭으로 변환
4. **없으면**: "이 나무는 풀베기로 베어낼 수 있을 것 같다. 포켓몬에게 풀베기를 가르쳐야 한다."

### 베어진 나무 처리
- `tryMove()`: `t == 'T' && isTreeCut() ? t = '!' : t` — 통과 가능
- `renderKorean()`: `tile_char == 'T' && isTreeCut() ? tile_char = '!' : tile_char` — 풀밭으로 렌더

### 디버그 편의
- `Ctrl+M` 워프 메뉴 사용 시 풀베기 모르는 상태면 **이상해씨 L10 더미 1마리 자동 지급** (`game.cpp::updateWarpMenu`)
- 이미 풀베기 포켓몬 있으면 추가 안 함 (중복 방지)
- 파티 가득 차면 (6마리) skip


## 디그다 동굴 입구 차단 NPC

**위치**: `MAP_3` (Route 2) 좌표 (12, 9)
**추가**: 연구원 NPC 1명 — 동굴 입구 워프 위에 배치
**대사**:
```
연구원: 안쪽은 위험한 디그다가 많아!
지금은 들어갈 수 없어. 다음에 다시 와줘.
```

NPC가 워프 타일을 차지해서 동굴 진입 차단. 워프 자체는 유지 (추후 NPC 제거 시 복원 가능).



# 시스템 추가/대형 변경


## 표지판 (Sign) 시스템

**파일**: `map_data.h`, `overworld.cpp`

### 구조
```cpp
struct SignDef {
    int mapId;
    int x, y;
    const wchar_t* lines[4];  // 최대 3줄 + nullptr
};

static const SignDef ALL_SIGNS[] = { ... };  // 전역 28개 등록
inline const SignDef* findSign(int mapId, int x, int y);
```

### 등록된 표지판 (28개)
- 팔레트시티: 4개 (주인공 집, 라이벌 집, 오박사 연구소 x 2)
- 1번도로: 1개 (방향 안내)
- 상록시티: 7개 (체육관, 마트, 센터, 22번도로 안내 등)
- 2번도로: 2개 (북/남 방향)
- 상록숲: 6개 (출입구 안내 x 2, 트레이너 팁 x 3, 안티독트 안내)
- 회색시티: 8개 (박물관, 체육관, 마트, 센터, 3번도로 안내 등)

### 동작
- A키 → NPC 검사 → 카운터 너머 NPC 검사 → **표지판 검사** (`findSign`)
- 표지판 발견 시 `_signBuffer` (전역 NpcDef) 에 내용 복사 → 기존 dialog 재활용


## 잠긴 문 메시지 시스템

**파일**: `overworld.cpp::checkWarps()`

미구현 건물 (warp `destMap == -1`) 진입 시 "문이 잠겨있다..." 메시지 표시.

### 적용 대상 (9개 미구현 건물)
- 상록시티: School House, Nickname House, Gym
- 회색시티: Museum 1F (x 2 입구), Nidoran House, Speech House
- 2번도로: Trade House (디그다 동굴은 NPC 차단으로 변경)


## 맵 경계 선택적 길 표시

**파일**: `overworld.cpp::renderKorean()`

카메라 밖 영역 렌더링 규칙:
- **실내 맵**: `continue` (검정 처리)
- **outdoor 이웃 맵 존재 + 경계 셀이 walkable**: `!` (풀밭) - "길 이어짐 표시"
- **그 외**: `$` (트리)

pokered처럼 이웃 맵 실타일을 그리진 않음 (워프 기반 이동 유지).


## 절벽/계단 충돌 정정 (pokered 검증)

**파일**: `map_data.h::tileWalkable`, `overworld.cpp::tryMove`

### pokered 원본 데이터 분석 결과
| Char | atoms | 역할 |
|------|-------|------|
| `0` | (44,44,55,55) | 남쪽 ledge (점프 only) |
| `5` | (57,57,54,55) | 남쪽 ledge (SW variant) |
| `6` | (57,57,55,55) | 남쪽 ledge (SE variant) |
| `<` | (44,44,55,52) | 남쪽 ledge (BL=55, 시각만 계단) |
| `?` | (44,44,60,60) | walkable 계단 (BL=60 passable) |
| `9` | (20,20,20,20) | ledge 아님 (path/dirt) |

### 수정 내용
- isLedge 검사 char 리스트: `'0' || '5' || '6' || '<'`
- walkable 리스트에 `?` 추가, `<` 제거
- pokered `LedgeTiles` + `Overworld_Coll` 데이터 기준


## 플레이어 스프라이트 8프레임 + 미러

**파일**: `tools/gen_sprites.py`, `sprites.h`, `overworld.cpp::playerFrameIdx`

pokered `red.png` (16x96) = 6 frames + 미러 2 추가 = 총 8 frames.

```
F0 = DOWN idle, F1 = UP idle, F2 = LEFT idle
F3 = DOWN walk, F4 = UP walk, F5 = LEFT walk
F6 = RIGHT idle (mirrored F2)
F7 = RIGHT walk (mirrored F5)
```

기존엔 좌/우 동일 프레임 사용 → 오른쪽 가도 왼쪽 보임 버그.
지금은 미러된 프레임 별도 생성.


## TrainerDef.isBoss 플래그

**파일**: `map_data.h::TrainerDef`, `overworld.cpp::checkTrainerSight`

- 체육관 관장만 `isBoss = true` → `BOSS_BATTLE` 트리거 (엔딩 연결)
- 일반 트레이너 `false` → 일반 `TRAINER_BATTLE`
- 이전: `trainers[i] index >= 2` 같은 hack 사용 → 명시적 플래그로 교체


## 배틀 풀바디 스프라이트 6종 추가

**파일**: `tools/gen_sprites.py::INTRO_TARGETS`, `battle.h::trainerIntroId`

| ID | 그림 |
|----|------|
| 0 | OAK |
| 1 | RIVAL |
| 2 | RED |
| 3 | BROCK |
| 4 | BUG_CATCHER |
| 5 | COOLTRAINER_M |

`BattleState.trainerIntroId` 로 어떤 그림 표시할지 결정.



# 맵 추가/수정 이력


## 추가된 맵

| MAP_N | 이름 | 크기 | 비고 |
|-------|------|------|------|
| MAP_4 | 상록숲 | 34x48 | 18x28 placeholder → 실제 pokered 디코딩 |
| MAP_16 | 상록숲 북쪽 게이트 | 10x8 | Route 2 (3,11) ↔ Forest (1,0)/(2,0) |
| MAP_17 | 상록숲 남쪽 게이트 | 10x8 | Route 2 (3,43) ↔ Forest (15-18, 47) |
| MAP_8 | 회색시티 체육관 | 10x14 | 8x14 placeholder → floor + door |


## 수정된 맵 데이터

### 카펫 처리 (MAP_7, MAP_10)
- 정문 `DD` → `CC` (RedsHouse 카펫 atoms 4,4,20,20)
- `tiles.h` indoor fallback에 `case 'C': return &TILE_RH_XC;` 추가

### 잘못된 타일 수정 (MAP_9)
- `##` → `ii` (overworld 풀밭 char → 카펫 floor)

### Pewter 동쪽 차단 (MAP_5)
- cols 36-39 rows 16-19에 `1!!!` 패턴 추가 (벽 + 풀밭)
- 3번도로 차단 NPC 추가 (할아버지)

### northMap/southMap 정정
- `MAP_3` (Route 2): northMap `MAP_VIR_FOREST` → `MAP_PEWTER`
- `MAP_5` (Pewter): southMap `MAP_VIR_FOREST` → `MAP_ROUTE2`

### MAP_15 (Route 2 Gate) NPC 제거
- 보조원 NPC 1명 제거 → 빈 게이트


## 워프 체인 정상화

전체 경로: Viridian → Route 2 → (남쪽 게이트) → Forest → (북쪽 게이트) → Route 2 → Pewter

### 수정된 워프
- 회색시티 체육관 dest: `(-1, -1)` → `(4, 12)` (이전엔 southEntry 폴백 = 잘못된 위치)
- Forest → South Gate 워프 4개 모두 `(5, 0)` 통일 (pokered 도어 블록 분석 결과)


## 타일 시각 수정

### 상록숲 나무
- `$/%/&/(/)` 모두 OW3 트리(풀바디)로 dispatch
- 작은 stump → 일반 맵과 동일

### Pewter Gym 도어
- `?` 가 OakLab 도어로 렌더링됨
- 트라이그래프 회피용 `?\?` 사용



# 빌드/배포 변경


## build.bat 재작성

### 변경 사항
- 절대경로(`C:\mingw64`) 제거 → 자동 탐색
- g++ 탐색 순서:
  1. PATH 등록 여부 (`where g++`)
  2. 7개 표준 설치 위치 (`C:\mingw64\bin`, `C:\msys64\mingw64\bin` 등)
- 빌드 성공 시 자동 게임 실행 (pause 없음)
- ASCII 메시지만 사용 (cmd.exe 코드페이지 호환성)
- CRLF 줄바꿈 (라인 컨티뉴에이션 동작 위해)
- `chcp 65001` 출력 UTF-8

### 빌드 메커니즘
다른 PC에서도 PATH 또는 표준 위치에 MinGW만 있으면 그대로 실행 가능.


## 프로젝트 정리

### 삭제된 파일 (12개)
- `tools/` 옛 generators 10개 (v1, v2, 패치 스크립트 등)
- `BACKUP/` 옛 백업 2개

### 현재 active tools (6개)
- `gen_overworld_tiles_v3.py` - 도시/도로
- `gen_gate_forest_tiles.py` - 게이트 + 상록숲
- `gen_pokecenter_mart_tiles.py` - 포센 + 마트
- `gen_pewter_gym_tiles.py` - 회색시티 체육관
- `gen_oaklab_tiles.py` - 오박사 연구소
- `gen_sprites.py` - 모든 캐릭터 스프라이트



# 문서화

다음 5개 md 파일이 다른 각도에서 같은 시스템을 설명:

| 파일 | 대상 | 길이 |
|------|------|------|
| README.md | 빌드/실행 개요 | 짧음 |
| BEGINNER_GUIDE.md | 코딩 입문자 | 길음 |
| PROJECT_OVERVIEW.md | 기술 상세 | 길음 |
| NPC_GUIDE.md | NPC/트레이너 작업자 | 중간 |
| TEAM_HANDOFF.md | 팀 인수인계 | 길음 |
| CHANGELOG.md (이 파일) | 변경 이력 | 중간 |

이전 DESIGN.md 는 초기 기획서로 유지 (역사적 참고).



# 다음 작업 후보

## 우선순위 높음
1. 가방/아이템 시스템 (포션, 몬스터볼, 안티독트)
2. 야생 포켓몬 포획 시스템
3. 상태이상 (독, 마비, 잠, 화상, 빙결)
4. MENU (START 키) - 가방/포켓몬/도감/저장 통합

## 중간 우선순위
5. 저장/로드 (파일 직렬화)
6. BGM 실제 재생 (winmm mciSendString)
7. 사운드 효과
8. HM01 베기 - 가방 아이템으로 추가 (현재는 풀베기만 골격 구현)

## 낮은 우선순위 / 분위기
9. 미구현 건물 채우기 (School/Speech/Nickname/Museum)
10. 트레이너 대각선 시야 / 시야 차단 구현
11. 동굴/실내 dark area 효과



# 빠른 참조 - 자주 묻는 작업 위치

| 작업 | 파일 | 위치 |
|------|------|------|
| NPC 추가 | `map_data.h` | `MAP_N` 의 `npcs[]` 배열 |
| 트레이너 추가 | `map_data.h` | `MAP_N` 의 `trainers[]` 배열 |
| 표지판 추가 | `map_data.h` | `ALL_SIGNS[]` 전역 배열 |
| 워프 변경 | `map_data.h` | `MAP_N` 의 `warps[]` 배열 |
| 새 포켓몬 | `pokemon_data.h` | `SPECIES[]` + `NUM_SPECIES_DATA` |
| 새 기술 | `pokemon_data.h` | `MOVES[]` + `NUM_MOVES_DATA` |
| 새 NPC 외형 | `gen_sprites.py` + sprites.h | NPC_SPR_* enum |
| 새 트레이너 풀바디 | `gen_sprites.py` | `INTRO_TARGETS` 리스트 |
| 워크어블 char 변경 | `map_data.h` | `tileWalkable()` 함수 |
| 타일 시각 변경 | `tiles.h` | `getTileArt()` dispatch |
| 배틀 데미지 공식 | `battle.cpp` | `applyDamage()` |
| Scene 추가 | `game.h` + `game.cpp` | `Scene` enum + run() switch |



# 트러블슈팅 빠른 참조

| 증상 | 원인 / 해결 |
|------|-------------|
| NPC 안 보임 | `}, N,  // npcs` 의 N 카운트 안 늘림 |
| 빌드 오류 "expected '}'" | 대사 끝에 `nullptr` 빠짐 |
| "too many initializers" | NPC lines 4개 이상 / 트레이너 preBattleText는 한 줄 |
| 트레이너 배틀 안 시작 | dir 잘못 / sightRange=0 / defeated=true |
| 적 풀바디가 OAK로 나옴 | introSpriteId 설정 안 함 |
| 보스 배틀 안 됨 | isBoss=true 누락 |
| 게임 충돌 | partyIds에 없는 포켓몬 ID 사용 |
| 한글 깨짐 | 파일 UTF-8 저장 (BOM 없이) |
| build.bat 실패 | g++ PATH 미등록 + `C:\mingw64` 없음 |
| 워프 후 검정 화면 | warpFlashFrames 카운트다운 중 (정상) |
| 풀숲 인카운터 안 됨 | `;` 타일 아님 / numEncounters=0 |



# 마지막 빌드 검증

마지막 작업 (NPC 제거 + 문서 갱신) 후 빌드 통과 확인:
- `build/PokemonRed.exe` 정상 생성
- 전체 시스템 동작 확인됨
