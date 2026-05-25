#pragma once
#include "player.h"
#include "../engine/renderer.h"
#include "../engine/input.h"
#include "../data/map_data.h"
#include "../data/tiles.h"

enum class OwEvent {
    NONE,
    WILD_ENCOUNTER,   // 야생 포켓몬 조우
    TRAINER_BATTLE,   // 트레이너 배틀 (트레이너 인덱스 eventData)
    BOSS_BATTLE,      // 브록
    NPC_TALK,         // NPC 대화
    ENTER_POKEMON_CENTER, // 포켓몬센터
    ENTER_MART,       // 마트 (소포 이벤트)
    GAME_WON,         // 브록 클리어 후
    OAK_INTERCEPT,    // 팔레트시티 북쪽 진입 → 오박사 등장 (cutscene)
    STARTER_TRIGGER,  // 연구소 오박사 NPC 대화 끝 → 스타터 선택
    CUTSCENE_END_OAK, // 오박사 cutscene 끝 → 연구소 OW로 워프
    CUTSCENE_END_RIVAL, // 라이벌 cutscene 끝 → 라이벌 배틀
    NURSE_HEAL,       // 간호사 조이 대화 끝 → 포켓몬 회복
};

// ─── OW cutscene (오박사/블루 인터럽트) ─────────────────────────
// 별도 텍스트 Scene 대신 오버월드 위에서 NPC가 등장 → 자동 걷기 → 대사 → 이벤트.
enum class CutsceneType { NONE, OAK_INTERCEPT, RIVAL_BLOCK };

struct OwCutscene {
    CutsceneType type = CutsceneType::NONE;
    int  step    = 0;        // 0=spawn, 1=walking, 2~=dialog
    int  ax, ay;             // actor 좌표
    int  targetX, targetY;   // 도착 목표
    int  adir;               // actor 방향 (0=down 1=up 2=left 3=right)
    int  spriteId;           // NPC sprite ID (sprites.h NpcSpriteId)
    int  walkTimer;          // 걷기 페이스 (frame 단위 카운트다운)
    int  dialogIdx;          // 대사 진행
    bool fadingOut;          // 대사 끝, 화면 페이드 중
    int  fadeFrame;          // 페이드 카운트다운
};

struct NpcDialogState {
    bool active;
    const NpcDef* npc;
    int lineIdx;
};

struct OwState {
    int  mapId;
    int  px, py;   // 플레이어 위치
    int  dir;      // 0=아래 1=위 2=왼 3=오른
    int  moveTimer;
    int  frame;

    NpcDialogState dialog;

    OwEvent pendingEvent;
    int     eventData;      // 트레이너 인덱스 등
    int     wildSpeciesId;
    int     wildLevel;

    // 깨어나는 시퀀스 (인트로 종료 직후 1회 재생)
    int     wakeStep;       // 0=비활성, 1~N=대사 단계

    // 걷기 애니메이션 (한 타일 슬라이드)
    int     moving;         // 남은 애니메이션 프레임 (0=정지)
    int     stepDx, stepDy; // 현재 진행 중인 걸음의 방향 (-1,0,+1)
    int     walkStep;       // 누적 걸음수 (포즈 토글용)

    OwCutscene cutscene;    // OW cutscene (오박사/블루 인터럽트)

    // 워프 페이드 (건물 입구 등)
    int  warpFlashFrames; // > 0이면 검정 페이드 중. 8 → 0
    int  pendingWarpMap;  // 페이드 중간에 적용할 destMap
    int  pendingWarpX;
    int  pendingWarpY;
};

class Overworld {
public:
    Overworld(Renderer& r, Player& pl);

    void init();
    void update(Key key);
    void render();
    void renderKorean();

    OwEvent popEvent();
    int     eventData() const { return state_.eventData; }
    int     wildSpeciesId() const { return state_.wildSpeciesId; }
    int     wildLevel()     const { return state_.wildLevel; }

    void onReturnFromBattle(bool won);
    void onReturnFromCenter();

    // OW cutscene 시작 (game.cpp가 호출)
    void startOakIntercept();   // 풀숲 진입 시 오박사 등장 + 걸어옴
    void startRivalBlock();     // 스타터 받은 후 블루가 출구 막음

private:
    Renderer& ren_;
    Player&   pl_;
    OwState   state_;

    // NPC 동적 대화 오버라이드 (Oak/Blue/Clerk 조건부 대사)
    const wchar_t* overrideLines_[4];
    bool           useOverrideLines_ = false;

    void tryMove(int dx, int dy);
    bool checkTrainerSight();
    void checkWarps(int x, int y);
    void checkSpecialTiles(int x, int y);
    void triggerEncounter();
    bool isNpcHidden(const NpcDef& npc) const;
    void startNpcDialog(const NpcDef* npc);

    // OW cutscene
    bool cutsceneActive() const;
    void updateCutscene(Key key);

    void drawTile(int screenX, int screenY, char tile);
    MapDef* curMap() const;
};
