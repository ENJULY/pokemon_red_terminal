#pragma once
#include "player.h"
#include "../engine/renderer.h"
#include "../engine/input.h"

enum class BattleType { WILD, TRAINER, BOSS };

enum class BattlePhase {
    CHOOSE_ACTION,   // 커맨드 선택 (싸운다/가방/포켓몬/도망)
    CHOOSE_MOVE,     // 기술 선택
    CHOOSE_ITEM,     // 아이템 선택
    CHOOSE_POKEMON,  // 파티에서 포켓몬 교체 선택
    EXECUTE_PLAYER,  // 플레이어 행동 실행
    EXECUTE_ENEMY,   // 적 행동 실행
    SHOW_MSG,        // 메시지 표시 후 다음 단계로
    LEVEL_UP_MSG,    // 레벨업 메시지
    FAINT_PLAYER,    // 내 포켓몬 기절
    FAINT_ENEMY,     // 적 포켓몬 기절
    VICTORY,         // 승리
    DEFEAT,          // 패배
    ESCAPED,         // 도망
    DONE,            // 배틀 종료 (다음 씬으로)
};

enum class BattleResult {
    NONE, WIN, LOSE, ESCAPE
};

struct BattleState {
    BattleType   type;
    Pokemon      enemy;          // 현재 상대 포켓몬
    int          enemyPartyIdx;  // 트레이너 파티에서 몇 번째
    Pokemon      enemyParty[3];
    int          enemyPartySize;
    const wchar_t* trainerName;
    const wchar_t* trainerPreText; // 배틀 전 대사

    int          playerPartyIdx; // 현재 내 포켓몬 인덱스

    BattlePhase  phase;
    BattleResult result;
    int          cursor;     // 커맨드/기술 커서
    bool         awaitKey;   // 키 입력 대기

    wchar_t      msg[128];   // 현재 메시지 (한글)
    wchar_t      msg2[128];  // 두 번째 줄
    int          msgWait;    // 메시지 대기 프레임

    int          pendingMoveId; // 선택한 기술
    bool         playerFirst;   // 플레이어가 먼저 행동하는지

    bool         expGained;
    int          expAmount;
    bool         leveledUp;
    int          newLevel;

    bool         enemyWentFirst;   // static 제거 → 인스턴스 멤버
    bool         switchAfterFaint; // 기절 후 강제 교체 여부

    int          frame;
};

class Battle {
public:
    Battle(Renderer& r, Player& pl);

    // 야생 배틀 시작
    void startWild(int speciesId, int level);
    // 트레이너 배틀 시작
    void startTrainer(const wchar_t* name, const wchar_t* preText,
                      int* partyIds, int* partyLevels, int partySize);
    // 보스(브록) 배틀
    void startBrock();

    void update(Key key);
    void render();
    void renderKorean();

    BattleResult result() const { return state_.result; }
    bool isDone() const { return state_.phase == BattlePhase::DONE; }

private:
    Renderer& ren_;
    Player&   pl_;
    BattleState state_;

    void setMsg(const wchar_t* m, const wchar_t* m2 = nullptr);
    void nextPhase(BattlePhase p, int waitFrames = 40);

    void executePlayerMove();
    void executeEnemyMove();
    void applyDamage(Pokemon& attacker, Pokemon& defender, int moveId, bool& superEff, bool& notEff, bool& noEff, int& damage);
    void applyStatEffect(Pokemon& target, int effect);
    void checkFaint();
    void grantExp();
    void checkLevelUp(Pokemon& p);
    int  chooseEnemyMove();
    void drawHPBar(int x, int y, int cur, int maxHP, const std::string& color);
    void drawSprite(int x, int y, int speciesId, bool back);
};
