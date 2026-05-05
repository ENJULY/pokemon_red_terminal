#pragma once
#include "../engine/renderer.h"
#include "../engine/input.h"
#include "../engine/audio.h"
#include "player.h"
#include "battle.h"
#include "overworld.h"
#include <string>

enum class Scene {
    INTRO,              // 오박사 나레이션 (전/후반 공용)
    NAME_INPUT,         // 플레이어 이름 입력
    RIVAL_NAME_INPUT,   // 라이벌 이름 입력
    LAB_INTRO,          // 연구소 인트로 (오박사 설명 + 블루 등장)
    STARTER_SELECT,     // 스타터 선택
    RIVAL_INTERCEPT,    // 스타터 받고 출구 향할 때 블루가 가로막음
    RIVAL_BATTLE,       // 라이벌 배틀 (강제)
    RECEIVE_DEX,        // 포켓덱스 수령 대사
    OVERWORLD,          // 오버월드 탐험
    WILD_BATTLE,        // 야생 포켓몬 배틀
    TRAINER_BATTLE,     // 트레이너 배틀
    BOSS_BATTLE,        // 브록 배틀
    POKEMON_CENTER,     // 포켓몬센터 이벤트
    MART_EVENT,         // 마트 소포 이벤트
    GAME_OVER,          // 전멸
    ENDING,             // 브록 클리어 엔딩
};

class Game {
public:
    Game();
    void run();

    Renderer renderer;
    bool running = true;

private:
    Scene    scene_   = Scene::INTRO;
    int      frame_   = 0;
    Player   player_  = {};
    Battle*  battle_  = nullptr;
    Overworld* ow_    = nullptr;

    void changeScene(Scene next);
    void update(Key key);
    void render();
    void renderKorean();

    // ─── 인트로 ───
    // 0~4: 나레이션, 5: 레드 이름 확인, 6~8: 라이벌 소개, 9~11: 마무리 → OVERWORLD
    static const int INTRO_COUNT = 12;
    static const wchar_t* INTRO_LINES[INTRO_COUNT];
    int  introStep_     = 0;

    void updateIntro(Key key);
    void renderIntro();
    void renderIntroKorean();

    // ─── 이름 입력 ───
    char     nameBuf_[16] = {};
    int      nameLen_     = 0;

    void updateNameInput(Key key, char ch);
    void renderNameInput();
    void renderNameInputKorean();

    // ─── 라이벌 이름 입력 ───
    char     rivalNameBuf_[16] = {};
    int      rivalNameLen_     = 0;

    void updateRivalNameInput(Key key, char ch);
    void renderRivalNameInput();
    void renderRivalNameInputKorean();

    // ─── 스타터 선택 ───
    int  starterCursor_ = 0;

    void updateStarterSelect(Key key);
    void renderStarterSelect();
    void renderStarterSelectKorean();

    // ─── 라이벌 배틀 ───
    bool rivalBattleInit_ = false;

    // ─── 라이벌 인터셉트 (스타터 받고 출구 향할 때 블루가 가로막음) ───
    // pokered _OaksLabRivalIllTakeYouOnText 기반.
    static const int RIVAL_INTERCEPT_COUNT = 4;
    static const wchar_t* RIVAL_INTERCEPT_LINES[RIVAL_INTERCEPT_COUNT];
    int rivalInterceptStep_ = 0;
    void updateRivalIntercept(Key key);
    void renderRivalIntercept();
    void renderRivalInterceptKorean();

    // ─── 오박사 인터셉트 시퀀스 ───
    // 0~3: 풀숲에서 오박사 등장 + 대사 (오박사 풀바디)
    // 4:   "...오박사를 따라 연구소로 갔다..." 전환
    // 종료 후 → 연구소 OVERWORLD로 진입. NPC들과 직접 상호작용.
    static const int LAB_INTRO_COUNT = 5;
    static const wchar_t* LAB_INTRO_LINES[LAB_INTRO_COUNT];
    int labIntroStep_ = 0;
    void updateLabIntro(Key key);
    void renderLabIntro();
    void renderLabIntroKorean();

    // ─── 포켓덱스 수령 ───
    int  dexStep_ = 0;
    static const wchar_t* DEX_LINES[5];

    void updateReceiveDex(Key key);
    void renderReceiveDex();
    void renderReceiveDexKorean();

    // ─── 포켓몬센터 ───
    int  centerStep_ = 0;

    void updateCenter(Key key);
    void renderCenter();
    void renderCenterKorean();

    // ─── 마트 이벤트 ───
    int  martStep_ = 0;

    void updateMart(Key key);
    void renderMart();
    void renderMartKorean();

    // ─── 게임 오버 ───
    void renderGameOver();
    void updateGameOver(Key key);

    // ─── 엔딩 ───
    int endingStep_ = 0;

    void updateEnding(Key key);
    void renderEnding();
    void renderEndingKorean();

    // ─── 공통 대화창 ───
    const wchar_t* dialogLines_[6] = {};
    int  dialogCount_  = 0;
    int  dialogStep_   = 0;
    Scene dialogNext_  = Scene::OVERWORLD;

    void startDialog(const wchar_t** lines, int count, Scene next);
};
