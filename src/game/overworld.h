#pragma once
#include "player.h"
#include "../engine/renderer.h"
#include "../engine/input.h"
#include "../data/map_data.h"

enum class OwEvent {
    NONE,
    WILD_ENCOUNTER,   // 야생 포켓몬 조우
    TRAINER_BATTLE,   // 트레이너 배틀 (트레이너 인덱스 eventData)
    BOSS_BATTLE,      // 브록
    NPC_TALK,         // NPC 대화
    ENTER_POKEMON_CENTER, // 포켓몬센터
    ENTER_MART,       // 마트 (소포 이벤트)
    GAME_WON,         // 브록 클리어 후
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

private:
    Renderer& ren_;
    Player&   pl_;
    OwState   state_;

    void tryMove(int dx, int dy);
    bool checkTrainerSight();
    void checkWarps(int x, int y);
    void checkSpecialTiles(int x, int y);
    void triggerEncounter();

    void drawTile(int screenX, int screenY, char tile);
    MapDef* curMap() const;
};
