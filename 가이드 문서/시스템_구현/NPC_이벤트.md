# NPC 이벤트 & 소포 퀘스트 시스템 구현 가이드

> **이 문서가 다루는 것**: NPC 이름 표시, 일회성 이벤트 시스템, 소포 퀘스트 흐름,
> 핵심 구조체 요약. 코드를 처음 보는 팀원이 "왜 이렇게 만들었는지"를 이해할 수 있도록 씀.
>
> M키 인게임 메뉴 및 포켓몬 상세 화면은 `시스템_구현/인게임_메뉴.md` 참조.

---

## 목차

1. [NPC 이름 표시 수정](#1-npc-이름-표시-수정)
2. [NPC 일회성 이벤트 시스템](#2-npc-일회성-이벤트-시스템)
3. [소포 퀘스트 흐름](#3-소포-퀘스트-흐름)
4. [핵심 구조체/열거형 요약](#4-핵심-구조체열거형-요약)

---

## 1. NPC 이름 표시 수정

이름 없이 대사만 있던 NPC들에 직접 이름 접두사를 붙였습니다.

**파일**: `src/data/map_data.h` — 각 맵의 `NpcDef` 배열 내 대사 문자열

```cpp
// 변경 전
{5, 3, {L"뭘 보는 거야!", ...}, NPC_SPR_GIRL, 0},

// 변경 후
{5, 3, {L"소녀: 뭘 보는 거야!", ...}, NPC_SPR_GIRL, 0},
```

적용 대상: 팔레트시티 소녀/낚시꾼, 1번도로 소년들, 상록시티 소년/할아버지/소녀/낚시꾼,
상록숲 소년들, 회색시티 연구생/연구원 등.

> **왜 `NpcDef`에 이름 필드를 추가하지 않았나?**
> 가장 단순한 방법을 선택했습니다. 별도 `name` 필드를 추가하면 렌더링 코드도
> 바꿔야 하지만, 대사에 직접 접두사를 넣으면 기존 코드를 건드리지 않아도 됩니다.

---

## 2. NPC 일회성 이벤트 시스템

### 문제

오박사, 블루, 상록 마트 점원은 특정 시점 이후 다른 대사를 해야 하고,
블루와 포켓볼은 조건에 따라 아예 사라져야 했습니다.

### 해결 구조

두 가지 메커니즘을 추가했습니다.

---

#### 2-A. `NpcTag` — NPC 신원 태그

**파일**: `src/data/map_data.h`

```cpp
enum NpcTag {
    NPC_TAG_NONE            = 0,
    NPC_TAG_OAK_LAB         = 1,   // 오박사
    NPC_TAG_BLUE_LAB        = 2,   // 블루 (연구소)
    NPC_TAG_BALL_BULBASAUR  = 3,   // 이상해씨 포켓볼
    NPC_TAG_BALL_CHARMANDER = 4,   // 파이리 포켓볼
    NPC_TAG_BALL_SQUIRTLE   = 5,   // 꼬부기 포켓볼
    NPC_TAG_VIRIDIAN_CLERK  = 6,   // 상록 마트 점원
};

struct NpcDef {
    int x, y;
    const wchar_t* lines[4];
    int spriteId = 0;
    int trigger  = 0;
    int tag      = 0;   // ← 신규 필드
};
```

`NpcDef` 에 `tag` 하나만 추가해서 코드에서 특정 NPC를 식별합니다.
이렇게 하면 맵 데이터에서 NPC 위치/대사는 바꾸지 않아도 됩니다.

---

#### 2-B. `isNpcHidden()` — NPC 숨김 판정

**파일**: `src/game/overworld.cpp`

```cpp
bool Overworld::isNpcHidden(const NpcDef& npc) const {
    // 선택한 포켓볼 자리를 빈 테이블로 바꿈
    if (npc.tag == NPC_TAG_BALL_BULBASAUR  && pl_.starterIdx == 0) return true;
    if (npc.tag == NPC_TAG_BALL_CHARMANDER && pl_.starterIdx == 1) return true;
    if (npc.tag == NPC_TAG_BALL_SQUIRTLE   && pl_.starterIdx == 2) return true;
    // 블루 첫 배틀 후 대화 완료 시 연구소에서 사라짐
    if (npc.tag == NPC_TAG_BLUE_LAB && pl_.rivalLabTalked)         return true;
    return false;
}
```

이 함수는 두 군데에서 호출됩니다.
1. **렌더링**: NPC 드로잉 루프에서 `isNpcHidden()` 이면 그리지 않음
2. **A키 상호작용**: `isNpcHidden()` 이면 대화 자체를 시작하지 않음

---

#### 2-C. `overrideLines_` — 동적 대사 오버라이드

**파일**: `src/game/overworld.h` (선언), `src/game/overworld.cpp` (구현)

```cpp
// overworld.h private 멤버
const wchar_t* overrideLines_[4];
bool           useOverrideLines_ = false;
```

NPC와 대화 시작 시 `startNpcDialog()` 가 스토리 플래그를 보고
`overrideLines_` 배열을 채웁니다. 이후 대화창 렌더링에서:

```cpp
const wchar_t* line = useOverrideLines_
    ? (li < 4 ? overrideLines_[li] : nullptr)
    : (li < 4 ? npc->lines[li] : nullptr);
```

정적 `npc->lines[]` 대신 동적 오버라이드 대사를 출력합니다.

> **왜 `NpcDef::lines[]`를 직접 바꾸지 않았나?**
> `NpcDef` 는 `map_data.h` 의 `constexpr` 전역 데이터입니다.
> 런타임에 수정이 불가능합니다. 그래서 `overrideLines_` 라는 별도 버퍼를
> Overworld 클래스 내부에 두고, 플래그에 따라 런타임에 채우는 방식을 썼습니다.

---

#### 2-D. `startNpcDialog()` — 대화 시작 진입점

**파일**: `src/game/overworld.cpp`

```cpp
void Overworld::startNpcDialog(const NpcDef* npc) {
    state_.dialog = { true, npc, 0 };
    useOverrideLines_ = false;
    // tag 별로 스토리 상태 확인 → overrideLines_ 설정
    if (npc->tag == NPC_TAG_OAK_LAB && pl_.partySize > 0) {
        useOverrideLines_ = true;
        if (pl_.gotParcel && !pl_.deliveredParcel) {
            // 소포 전달 대사
        } else if (pl_.deliveredParcel) {
            // 전달 완료 이후 일반 대사
        } else {
            // 스타터 받은 직후 대사
        }
    }
    if (npc->tag == NPC_TAG_BLUE_LAB && pl_.beatenRival1 && !pl_.rivalLabTalked) {
        // "운이 좋았던 거야" 일회성 대사
    }
    if (npc->tag == NPC_TAG_VIRIDIAN_CLERK && pl_.gotParcel) {
        // 소포 받은 이후 일반 점원 대사
    }
}
```

---

#### 2-E. 대화 종료 시 플래그 처리

**파일**: `src/game/overworld.cpp` — Z키 처리 블록

```cpp
// 대화 마지막 줄에서 Z 키
if (npc->tag == NPC_TAG_OAK_LAB && useOverrideLines_) {
    if (pl_.gotParcel && !pl_.deliveredParcel)
        pl_.deliveredParcel = true;   // 소포 전달 완료
}
if (npc->tag == NPC_TAG_BLUE_LAB && useOverrideLines_)
    pl_.rivalLabTalked = true;        // 블루 대화 완료 → 다음 렌더에서 숨김

// trigger == 1 은 partySize == 0 일 때만 STARTER_TRIGGER 발동
if (npc->trigger == 1 && pl_.partySize == 0) { ... }
// trigger == 3 은 !gotParcel 일 때만 소포 수령
if (npc->trigger == 3 && !pl_.gotParcel)    { pl_.gotParcel = true; }
```

---

## 3. 소포 퀘스트 흐름

```
[첫 상록시티 방문]
  ↓
ENTER_MART 이벤트 (gotParcel == false 일 때만)
  ↓ game.cpp updateMart() 종료
player_.gotParcel = true
player_.pokeballs += 3   ← 임시 보상 (추후 아이템 시스템 연동)

[오박사 연구소 방문]
  ↓ startNpcDialog() 에서 gotParcel=true, deliveredParcel=false 판정
override 대사: "이게 소포구나! ..."
  ↓ 대화 종료 시
player_.deliveredParcel = true
← 퀘스트 보상 지급 위치 (overworld.cpp, 시스템_구현/아이템_연동.md 참조)
```

관련 플래그는 `Player` 구조체 (`player.h`) 에 있습니다.

```cpp
bool gotParcel;       // 상록 마트에서 소포 수령
bool deliveredParcel; // 오박사에게 전달 완료
```

---

## 4. 핵심 구조체/열거형 요약

### 스토리 플래그 (`player.h`)

| 필드 | 초기값 | 세팅 위치 | 의미 |
|------|--------|-----------|------|
| `starterIdx` | -1 | `game.cpp updateStarterSelect()` | 선택한 스타터 (0/1/2) |
| `gotParcel` | false | `game.cpp updateMart()` | 소포 수령 여부 |
| `deliveredParcel` | false | `overworld.cpp` 대화 종료 | 오박사 전달 여부 |
| `rivalLabTalked` | false | `overworld.cpp` 대화 종료 | 블루 일회성 대화 완료 |
| `beatenRival1` | false | `game.cpp` 배틀 승리 시 | 첫 블루 배틀 승리 |

### 새로 추가된 코드 흐름 요약

```
A 키 (상호작용)
  └→ isNpcHidden()    ← 숨겨진 NPC면 무시
       └→ startNpcDialog()   ← tag 보고 overrideLines_ 설정
            └→ 대화 진행
                 └→ Z 키 (마지막 줄)
                      └→ tag/useOverrideLines_ 기반으로 플래그 세팅

렌더링
  └→ isNpcHidden() 로 숨긴 NPC는 그리지 않음
  └→ 대화창: useOverrideLines_ ? overrideLines_[li] : npc->lines[li]
```

---

## 관련 파일 빠른 참조

| 기능 | 파일 | 핵심 위치 |
|------|------|-----------|
| NPC 태그 정의 | `map_data.h` | `NpcTag` enum / `NpcDef::tag` |
| NPC 숨김/오버라이드 | `overworld.cpp` | `isNpcHidden()` / `startNpcDialog()` |
| 오버라이드 버퍼 | `overworld.h` | `overrideLines_[4]` / `useOverrideLines_` |
| 스토리 플래그 | `player.h` | `Player` 구조체 하단 bool 필드들 |
| 소포 퀘스트 보상 연동 | `시스템_구현/아이템_연동.md` | 섹션 6 |
