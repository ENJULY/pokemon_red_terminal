#include "battle.h"
#include "../data/type_chart.h"
#include "../data/sprites.h"
#include <cstdlib>
#include <cstring>
#include <cstdio>
#include <wchar.h>
#ifndef swprintf
#define swprintf _snwprintf
#endif

static int stageMultNum(int stage) {
    // 스테이지 -6~+6, 분자 반환 (분모=8)
    static const int tbl[13] = {2,2,3,3,4,5,6,7,8,9,10,11,12}; // stage+6 → tbl[stage+6]
    return tbl[stage + 6];
}
static int applyStage(int stat, int stage) {
    if (stage == 0) return stat;
    if (stage > 0) return stat * stageMultNum(stage) / 8;
    return stat * 8 / stageMultNum(-stage);
}

static bool specialType(Type t) {
    return t == Type::FIRE || t == Type::WATER || t == Type::GRASS ||
           t == Type::ELECTRIC || t == Type::PSYCHIC;
}

Battle::Battle(Renderer& r, Player& pl)
    : ren_(r), pl_(pl), state_{}
{}

void Battle::startWild(int speciesId, int level) {
    state_ = {};
    state_.type = BattleType::WILD;
    state_.enemy = makePokemon(speciesId, level);
    state_.enemyPartySize = 1;
    state_.enemyParty[0] = state_.enemy;
    state_.enemyPartyIdx = 0;
    state_.playerPartyIdx = firstAlive(pl_);
    state_.phase = BattlePhase::SHOW_MSG;
    state_.result = BattleResult::NONE;
    state_.cursor = 0;
    state_.awaitKey = true;
    state_.frame = 0;
    swprintf(state_.msg, 128, L"야생 %ls이(가) 나타났다!", state_.enemy.species->name);
    state_.msgWait = 0;
}

void Battle::startTrainer(const wchar_t* name, const wchar_t* preText,
                          int* ids, int* levels, int sz) {
    state_ = {};
    state_.type = BattleType::TRAINER;
    state_.trainerName = name;
    state_.trainerPreText = preText;
    state_.enemyPartySize = sz;
    for (int i = 0; i < sz && i < 3; i++)
        state_.enemyParty[i] = makePokemon(ids[i], levels[i]);
    state_.enemyPartyIdx = 0;
    state_.enemy = state_.enemyParty[0];
    state_.playerPartyIdx = firstAlive(pl_);
    state_.phase = BattlePhase::SHOW_MSG;
    state_.result = BattleResult::NONE;
    state_.cursor = 0;
    state_.awaitKey = true;
    state_.frame = 0;
    swprintf(state_.msg, 128, L"%ls이(가) 승부를 걸어왔다!", name);
    state_.msgWait = 0;
}

void Battle::startBrock() {
    int ids[]    = {74, 95};
    int lvls[]   = {12, 14};
    startTrainer(L"관장 브록", L"바위 포켓몬은 최강이다!", ids, lvls, 2);
    state_.type = BattleType::BOSS;
}

void Battle::setMsg(const wchar_t* m, const wchar_t* m2) {
    wcsncpy(state_.msg,  m  ? m  : L"", 127);
    wcsncpy(state_.msg2, m2 ? m2 : L"", 127);
    state_.awaitKey = true;
}

void Battle::nextPhase(BattlePhase p, int) {
    state_.phase = p;
    state_.awaitKey = false;
}

// ─── 데미지 계산 ─────────────────────────────────────────────
void Battle::applyDamage(Pokemon& atk, Pokemon& def, int moveId,
                         bool& superEff, bool& notEff, bool& noEff, int& damage) {
    const MoveData& mv = getMoveData(moveId);
    superEff = notEff = noEff = false;
    damage = 0;
    if (mv.power == 0) return;

    bool spec = specialType(mv.type);
    int atkStat = spec ? applyStage(atk.spc, atk.atkStage)
                       : applyStage(atk.atk, atk.atkStage);
    int defStat = spec ? applyStage(def.spc, def.defStage)
                       : applyStage(def.def, def.defStage);
    if (defStat < 1) defStat = 1;

    int base = ((2 * atk.level / 5 + 2) * mv.power * atkStat / defStat) / 50 + 2;

    // 타입 상성
    int eff = getEffInt(mv.type, def.species->type1, def.species->type2);
    if (eff == 0) { noEff = true; damage = 0; return; }
    notEff  = (eff < 10);
    superEff = (eff >= 20);
    base = base * eff / 10;

    // STAB
    if (mv.type == atk.species->type1 || mv.type == atk.species->type2)
        base = base * 3 / 2;

    // 랜덤 85~100%
    int r = 85 + rand() % 16;
    base = base * r / 100;
    damage = base > 0 ? base : 1;
}

void Battle::applyStatEffect(Pokemon& target, int effect) {
    switch (effect) {
        case 1: if (target.atkStage > -6) target.atkStage--; break;
        case 2: if (target.defStage > -6) target.defStage--; break;
        case 3: if (target.speStage > -6) target.speStage--; break;
        case 4: if (target.defStage <  6) target.defStage++; break;
        case 5: if (target.accStage > -6) target.accStage--; break;
    }
}

int Battle::chooseEnemyMove() {
    Pokemon& en = state_.enemy;
    // 사용 가능한 기술 중 랜덤 선택
    int valid[4]; int cnt = 0;
    for (int i = 0; i < en.numMoves; i++)
        if (en.moves[i].pp > 0) valid[cnt++] = i;
    if (cnt == 0) return 0;
    return en.moves[valid[rand() % cnt]].moveId;
}

void Battle::grantExp() {
    int idx = state_.playerPartyIdx;
    Pokemon& p = pl_.party[idx];
    int base = state_.enemy.species->baseExp;
    int gain = base * state_.enemy.level / 7;
    if (gain < 1) gain = 1;
    p.exp += gain;
    state_.expAmount = gain;
    state_.expGained = true;
}

void Battle::checkLevelUp(Pokemon& p) {
    state_.leveledUp = false;
    while (p.exp >= expForLevel(p.level + 1)) {
        p.level++;
        recalcStats(p);
        if (p.currentHP > p.maxHP) p.currentHP = p.maxHP;
        // 새 기술 습득
        for (int i = 0; i < 8; i++) {
            const LearnMove& lm = p.species->learnset[i];
            if (lm.level == 0) break;
            if (lm.level == p.level && p.numMoves < 4) {
                bool has = false;
                for (int j = 0; j < p.numMoves; j++)
                    if (p.moves[j].moveId == lm.moveId) { has = true; break; }
                if (!has) {
                    p.moves[p.numMoves].moveId = lm.moveId;
                    p.moves[p.numMoves].pp = getMoveData(lm.moveId).maxPP;
                    p.numMoves++;
                }
            }
        }
        state_.leveledUp = true;
        state_.newLevel = p.level;
    }
}

// ─── 행동 실행 ────────────────────────────────────────────────
void Battle::executePlayerMove() {
    Pokemon& myPoke = pl_.party[state_.playerPartyIdx];
    Pokemon& en     = state_.enemy;
    int mid = state_.pendingMoveId;
    const MoveData& mv = getMoveData(mid);

    // PP 소모
    for (int i = 0; i < myPoke.numMoves; i++)
        if (myPoke.moves[i].moveId == mid) { myPoke.moves[i].pp--; break; }

    // 명중 판정
    int acc = mv.accuracy;
    // acc stage 적용 (간략화)
    bool hit = ((rand() % 100) < acc);
    if (!hit) {
        swprintf(state_.msg, 128, L"공격이 빗나갔다!");
        state_.msg2[0] = 0;
        state_.phase = BattlePhase::SHOW_MSG;
        state_.awaitKey = true;
        return;
    }

    if (mv.power > 0) {
        bool se, ne, noe; int dmg;
        applyDamage(myPoke, en, mid, se, ne, noe, dmg);
        if (noe) {
            swprintf(state_.msg, 128, L"%ls의 %ls!", myPoke.species->name, mv.name);
            swprintf(state_.msg2, 128, L"효과가 없다!");
        } else {
            en.currentHP -= dmg;
            if (en.currentHP < 0) en.currentHP = 0;
            swprintf(state_.msg, 128, L"%ls의 %ls!", myPoke.species->name, mv.name);
            if (se) swprintf(state_.msg2, 128, L"효과가 굉장했다!");
            else if (ne) swprintf(state_.msg2, 128, L"효과가 별로인 것 같다...");
            else state_.msg2[0] = 0;
        }
    } else {
        // 변화기술
        applyStatEffect(en, mv.effect);
        swprintf(state_.msg, 128, L"%ls의 %ls!", myPoke.species->name, mv.name);
        state_.msg2[0] = 0;
    }
    state_.phase = BattlePhase::SHOW_MSG;
    state_.awaitKey = true;
    // 다음은 checkFaint → executeEnemy 순서로
}

void Battle::executeEnemyMove() {
    Pokemon& myPoke = pl_.party[state_.playerPartyIdx];
    Pokemon& en     = state_.enemy;

    int mid = chooseEnemyMove();
    const MoveData& mv = getMoveData(mid);

    // PP 소모 (적)
    for (int i = 0; i < en.numMoves; i++)
        if (en.moves[i].moveId == mid) { en.moves[i].pp--; break; }

    bool hit = ((rand() % 100) < mv.accuracy);
    if (!hit) {
        swprintf(state_.msg, 128, L"상대의 %ls이(가) 빗나갔다!", mv.name);
        state_.msg2[0] = 0;
        state_.phase = BattlePhase::SHOW_MSG;
        state_.awaitKey = true;
        return;
    }

    if (mv.power > 0) {
        bool se, ne, noe; int dmg;
        applyDamage(en, myPoke, mid, se, ne, noe, dmg);
        if (noe) {
            swprintf(state_.msg, 128, L"상대 %ls의 %ls!", en.species->name, mv.name);
            swprintf(state_.msg2, 128, L"효과가 없다!");
        } else {
            myPoke.currentHP -= dmg;
            if (myPoke.currentHP < 0) myPoke.currentHP = 0;
            swprintf(state_.msg, 128, L"상대 %ls의 %ls!", en.species->name, mv.name);
            if (se) swprintf(state_.msg2, 128, L"효과가 굉장했다!");
            else if (ne) swprintf(state_.msg2, 128, L"효과가 별로인 것 같다...");
            else state_.msg2[0] = 0;
        }
    } else {
        applyStatEffect(myPoke, mv.effect);
        swprintf(state_.msg, 128, L"상대 %ls의 %ls!", en.species->name, mv.name);
        state_.msg2[0] = 0;
    }
    state_.phase = BattlePhase::SHOW_MSG;
    state_.awaitKey = true;
}

// ─── 업데이트 ────────────────────────────────────────────────
void Battle::update(Key key) {
    state_.frame++;
    Pokemon& myPoke = pl_.party[state_.playerPartyIdx];
    Pokemon& en     = state_.enemy;

    switch (state_.phase) {

    case BattlePhase::CHOOSE_ACTION: {
        // 2×2 그리드: 0=싸운다 1=가방 2=포켓몬 3=도망
        static const int ACTION_COLS = 2;
        static const int ACTION_ROWS = 2;
        if (key == Key::RIGHT) state_.cursor = (state_.cursor % ACTION_COLS == ACTION_COLS-1)
                                               ? state_.cursor - (ACTION_COLS-1)
                                               : state_.cursor + 1;
        if (key == Key::LEFT)  state_.cursor = (state_.cursor % ACTION_COLS == 0)
                                               ? state_.cursor + (ACTION_COLS-1)
                                               : state_.cursor - 1;
        if (key == Key::DOWN)  state_.cursor = (state_.cursor + ACTION_COLS) % (ACTION_COLS * ACTION_ROWS);
        if (key == Key::UP)    state_.cursor = (state_.cursor - ACTION_COLS + ACTION_COLS * ACTION_ROWS)
                                               % (ACTION_COLS * ACTION_ROWS);
        if (key == Key::A) {
            switch (state_.cursor) {
            case 0: // 싸운다
                state_.phase = BattlePhase::CHOOSE_MOVE;
                state_.cursor = 0;
                break;
            case 1: // 가방 (몬스터볼만 구현)
                if (state_.type == BattleType::WILD && pl_.pokeballs > 0) {
                    pl_.pokeballs--;
                    int catchRate = 50 + (en.maxHP - en.currentHP) * 100 / en.maxHP;
                    if (rand() % 100 < catchRate && pl_.partySize < 6) {
                        pl_.party[pl_.partySize++] = en;
                        pl_.party[pl_.partySize-1].currentHP = pl_.party[pl_.partySize-1].maxHP;
                        swprintf(state_.msg, 128, L"%ls을(를) 잡았다!", en.species->name);
                        state_.msg2[0] = 0;
                        state_.phase = BattlePhase::SHOW_MSG;
                        state_.awaitKey = true;
                        state_.result = BattleResult::WIN;
                    } else {
                        swprintf(state_.msg, 128, L"아, 아쉽다! 조금만 더!");
                        state_.msg2[0] = 0;
                        state_.phase = BattlePhase::SHOW_MSG;
                        state_.awaitKey = true;
                    }
                } else if (state_.type != BattleType::WILD) {
                    swprintf(state_.msg, 128, L"트레이너 배틀에서는 몬스터볼을 쓸 수 없다!");
                    state_.msg2[0] = 0;
                    state_.phase = BattlePhase::SHOW_MSG;
                    state_.awaitKey = true;
                } else {
                    swprintf(state_.msg, 128, L"몬스터볼이 없다!");
                    state_.msg2[0] = 0;
                    state_.phase = BattlePhase::SHOW_MSG;
                    state_.awaitKey = true;
                }
                break;
            case 2: // 포켓몬 교체
                state_.phase = BattlePhase::CHOOSE_POKEMON;
                state_.cursor = 0;
                break;
            case 3: // 도망
                if (state_.type == BattleType::WILD) {
                    state_.result = BattleResult::ESCAPE;
                    state_.phase = BattlePhase::DONE;
                } else {
                    swprintf(state_.msg, 128, L"트레이너 배틀에서는 도망칠 수 없다!");
                    state_.msg2[0] = 0;
                    state_.phase = BattlePhase::SHOW_MSG;
                    state_.awaitKey = true;
                }
                break;
            }
        }
        break;
    }

    case BattlePhase::CHOOSE_MOVE: {
        int nMoves = myPoke.numMoves;
        if (key == Key::RIGHT) state_.cursor = (state_.cursor + 1) % nMoves;
        if (key == Key::LEFT)  state_.cursor = ((state_.cursor - 1) + nMoves) % nMoves;
        if (key == Key::DOWN)  state_.cursor = (state_.cursor + 2 < nMoves) ? state_.cursor + 2 : state_.cursor;
        if (key == Key::UP)    state_.cursor = (state_.cursor - 2 >= 0) ? state_.cursor - 2 : state_.cursor;
        if (key == Key::A) {
            if (myPoke.moves[state_.cursor].pp > 0) {
                state_.pendingMoveId = myPoke.moves[state_.cursor].moveId;
                // 속도 비교로 선공 결정
                int mySpe = applyStage(myPoke.spe, myPoke.speStage);
                int enSpe = applyStage(en.spe, en.speStage);
                state_.playerFirst = (mySpe >= enSpe);
                state_.phase = BattlePhase::EXECUTE_PLAYER;
                if (!state_.playerFirst)
                    state_.phase = BattlePhase::EXECUTE_ENEMY;
            }
        }
        if (key == Key::B) {
            state_.phase = BattlePhase::CHOOSE_ACTION;
            state_.cursor = 0;
        }
        break;
    }

    case BattlePhase::CHOOSE_POKEMON: {
        // 파티 선택 (현재 포켓몬 제외, 기절 제외)
        if (key == Key::UP)   state_.cursor = (state_.cursor - 1 + pl_.partySize) % pl_.partySize;
        if (key == Key::DOWN) state_.cursor = (state_.cursor + 1) % pl_.partySize;
        if (key == Key::A) {
            int sel = state_.cursor;
            if (sel == state_.playerPartyIdx) {
                swprintf(state_.msg, 128, L"이미 싸우고 있는 포켓몬이야!");
                state_.msg2[0] = 0;
                state_.phase = BattlePhase::SHOW_MSG;
                state_.awaitKey = true;
            } else if (pokemonFainted(pl_.party[sel])) {
                swprintf(state_.msg, 128, L"%ls은(는) 쓰러져서 싸울 수 없어!", pl_.party[sel].species->name);
                state_.msg2[0] = 0;
                state_.phase = BattlePhase::SHOW_MSG;
                state_.awaitKey = true;
            } else {
                state_.playerPartyIdx = sel;
                swprintf(state_.msg, 128, L"%ls! 나가줘!", pl_.party[sel].species->name);
                state_.msg2[0] = 0;
                // 교체 후 적이 공격 (교체는 턴 소모)
                state_.phase = BattlePhase::SHOW_MSG;
                state_.awaitKey = true;
                state_.playerFirst = false; // 교체 후 적 선공
            }
        }
        if (key == Key::B) {
            // 기절 후 강제 교체가 아닌 경우에만 취소 가능
            if (!state_.switchAfterFaint) {
                state_.phase = BattlePhase::CHOOSE_ACTION;
                state_.cursor = 0;
            }
        }
        break;
    }

    case BattlePhase::EXECUTE_PLAYER:
        if (pokemonFainted(en)) {
            state_.phase = BattlePhase::FAINT_ENEMY;
            swprintf(state_.msg, 128, L"상대 %ls이(가) 쓰러졌다!", en.species->name);
            state_.msg2[0] = 0;
            state_.awaitKey = true;
            grantExp();
        } else {
            executePlayerMove();
            // 다음 단계는 SHOW_MSG → 적 행동
        }
        break;

    case BattlePhase::EXECUTE_ENEMY:
        if (pokemonFainted(myPoke)) {
            state_.phase = BattlePhase::FAINT_PLAYER;
            swprintf(state_.msg, 128, L"%ls이(가) 쓰러졌다!", myPoke.species->name);
            state_.msg2[0] = 0;
            state_.awaitKey = true;
        } else {
            executeEnemyMove();
        }
        break;

    case BattlePhase::SHOW_MSG:
        if (!state_.awaitKey) break;
        if (key == Key::A || key == Key::B) {
            // 잡기 성공 시 DONE
            if (state_.result == BattleResult::WIN &&
                state_.type == BattleType::WILD) {
                state_.phase = BattlePhase::DONE;
                break;
            }
            // 적 기절 체크
            if (pokemonFainted(en)) {
                state_.phase = BattlePhase::FAINT_ENEMY;
                swprintf(state_.msg, 128, L"상대 %ls이(가) 쓰러졌다!", en.species->name);
                state_.msg2[0] = 0;
                state_.awaitKey = true;
                grantExp();
                break;
            }
            // 내 포켓몬 기절 체크
            if (pokemonFainted(myPoke)) {
                state_.phase = BattlePhase::FAINT_PLAYER;
                swprintf(state_.msg, 128, L"%ls이(가) 쓰러졌다!", myPoke.species->name);
                state_.msg2[0] = 0;
                state_.awaitKey = true;
                break;
            }
            // 교체 후 적 선공
            if (!state_.playerFirst) {
                if (!state_.enemyWentFirst) {
                    // 적 행동 차례
                    state_.phase = BattlePhase::EXECUTE_ENEMY;
                    state_.enemyWentFirst = true;
                } else {
                    // 적 행동 완료 → 다음 턴
                    state_.enemyWentFirst = false;
                    state_.phase = BattlePhase::CHOOSE_ACTION;
                    state_.cursor = 0;
                }
            } else {
                // 플레이어 선공 → 이제 적 행동
                state_.phase = BattlePhase::EXECUTE_ENEMY;
                state_.playerFirst = false;
            }
        }
        break;

    case BattlePhase::FAINT_ENEMY:
        if (key == Key::A || key == Key::B) {
            // 경험치 메시지
            if (state_.expGained) {
                state_.expGained = false;
                Pokemon& pp = pl_.party[state_.playerPartyIdx];
                checkLevelUp(pp);
                if (state_.leveledUp) {
                    swprintf(state_.msg, 128, L"%ls이(가) 레벨 %d이(가) 되었다!",
                        pp.species->name, state_.newLevel);
                    state_.msg2[0] = 0;
                    state_.phase = BattlePhase::LEVEL_UP_MSG;
                    state_.awaitKey = true;
                    break;
                }
                swprintf(state_.msg, 128, L"%ls은(는) 경험치 %d를 획득했다!",
                    pp.species->name, state_.expAmount);
                state_.msg2[0] = 0;
                state_.awaitKey = true;
                break;
            }
            // 다음 상대 포켓몬?
            state_.enemyPartyIdx++;
            if (state_.enemyPartyIdx < state_.enemyPartySize) {
                state_.enemy = state_.enemyParty[state_.enemyPartyIdx];
                swprintf(state_.msg, 128, L"%ls! %ls 나와라!",
                    state_.trainerName ? state_.trainerName : L"",
                    state_.enemy.species->name);
                state_.msg2[0] = 0;
                state_.awaitKey = true;
                state_.phase = BattlePhase::SHOW_MSG;
                // 다음 SHOW_MSG에서 CHOOSE_ACTION으로
                state_.playerFirst = true;
            } else {
                state_.result = BattleResult::WIN;
                state_.phase = BattlePhase::VICTORY;
                if (state_.type == BattleType::BOSS)
                    swprintf(state_.msg, 128, L"브록을 물리쳤다! 바위배지를 획득!");
                else if (state_.type == BattleType::TRAINER)
                    swprintf(state_.msg, 128, L"%ls을(를) 물리쳤다!", state_.trainerName);
                else
                    swprintf(state_.msg, 128, L"승리했다!");
                state_.msg2[0] = 0;
                state_.awaitKey = true;
            }
        }
        break;

    case BattlePhase::FAINT_PLAYER:
        if (key == Key::A || key == Key::B) {
            // 살아있는 포켓몬이 있으면 강제 교체 화면으로
            bool hasAlive = false;
            for (int i = 0; i < pl_.partySize; i++)
                if (i != state_.playerPartyIdx && !pokemonFainted(pl_.party[i]))
                    { hasAlive = true; break; }

            if (hasAlive) {
                state_.switchAfterFaint = true;
                state_.phase = BattlePhase::CHOOSE_POKEMON;
                state_.cursor = 0;
            } else {
                state_.result = BattleResult::LOSE;
                state_.phase = BattlePhase::DEFEAT;
                swprintf(state_.msg, 128, L"모든 포켓몬이 쓰러졌다...");
                swprintf(state_.msg2, 128, L"포켓몬 센터로 달려갔다.");
                state_.awaitKey = true;
            }
        }
        break;

    case BattlePhase::LEVEL_UP_MSG:
        if (key == Key::A || key == Key::B) {
            Pokemon& pp = pl_.party[state_.playerPartyIdx];
            swprintf(state_.msg, 128, L"%ls은(는) 경험치 %d를 획득했다!",
                pp.species->name, state_.expAmount);
            state_.msg2[0] = 0;
            state_.phase = BattlePhase::FAINT_ENEMY;
            state_.expGained = false;
            state_.awaitKey = true;
        }
        break;

    case BattlePhase::VICTORY:
        if (key == Key::A || key == Key::B)
            state_.phase = BattlePhase::DONE;
        break;

    case BattlePhase::DEFEAT:
        if (key == Key::A || key == Key::B)
            state_.phase = BattlePhase::DONE;
        break;

    case BattlePhase::DONE:
        break;

    default: break;
    }
}

// ─── 스프라이트 그리기 ────────────────────────────────────────
void Battle::drawSprite(int x, int y, int speciesId, bool back) {
    const SpriteData* spr = back ? getSpriteBack(speciesId) : getSpriteFront(speciesId);
    if (!spr && back) spr = getSpriteFront(speciesId);  // 뒷모습 없으면 앞모습으로 대체
    if (!spr) return;
    for (int r = 0; r < SPR_H; r++) {
        if (!spr->rows[r]) break;
        ren_.printRaw(x, y + r, spr->rows[r]);
    }
}

// ─── HP 바 그리기 ────────────────────────────────────────────
void Battle::drawHPBar(int x, int y, int cur, int maxHP, const std::string& col) {
    int total = 14;
    int filled = (cur > 0) ? (cur * total / maxHP) : 0;
    std::string bar(filled, '|');
    bar += std::string(total - filled, '-');
    ren_.print(x, y, "[" + bar + "]", col);
}

// ─── 렌더 ────────────────────────────────────────────────────
void Battle::render() {
    int W = ren_.width;
    int H = ren_.height;

    ren_.fillRect(0, 0, W, H, ' ', std::string(Color::BG_BLACK) + Color::WHITE);

    Pokemon& myPoke = pl_.party[state_.playerPartyIdx];
    Pokemon& en     = state_.enemy;

    // ── 포켓몬 교체 화면 ──────────────────────────────────────
    if (state_.phase == BattlePhase::CHOOSE_POKEMON) {
        int boxH = H - 2, boxW = 40, boxX = (W - 40) / 2, boxY = 1;
        ren_.drawBox(boxX, boxY, boxW, boxH, std::string(Color::BG_BLACK) + Color::WHITE);
        ren_.fillRect(boxX+1, boxY+1, boxW-2, boxH-2, ' ', std::string(Color::BG_BLACK) + Color::WHITE);
        for (int i = 0; i < pl_.partySize; i++) {
            int cy = boxY + 2 + i * 2;
            bool isCur = (i == state_.cursor);
            std::string cc = std::string(Color::BG_BLACK) +
                (isCur ? Color::BRIGHT_YELLOW : Color::WHITE);
            if (isCur) ren_.print(boxX+2, cy, ">", cc);
            char hpStr[32];
            snprintf(hpStr, sizeof(hpStr), "  HP:%d/%d",
                pl_.party[i].currentHP, pl_.party[i].maxHP);
            ren_.print(boxX+12, cy, hpStr, std::string(Color::BG_BLACK) + Color::WHITE);
        }
        return;
    }

    // ── 통상 배틀 화면 ────────────────────────────────────────
    int mid = H / 2;
    ren_.print(0, mid, std::string(W, '-'), std::string(Color::BG_BLACK) + Color::BRIGHT_BLACK);

    // 적 포켓몬 정보 (우측 상단)
    int enInfoX = W - 32;
    int enInfoY = 1;
    int enHpPct = en.currentHP * 100 / (en.maxHP > 0 ? en.maxHP : 1);
    std::string enHpCol = std::string(Color::BG_BLACK) +
        ((enHpPct > 50) ? Color::BRIGHT_GREEN :
         (enHpPct > 20) ? Color::BRIGHT_YELLOW : Color::BRIGHT_RED);
    drawHPBar(enInfoX, enInfoY + 2, en.currentHP, en.maxHP, enHpCol);

    // 내 포켓몬 정보 (좌측 중간)
    int myInfoX = 4;
    int myInfoY = mid + 1;
    int myHpPct = myPoke.currentHP * 100 / (myPoke.maxHP > 0 ? myPoke.maxHP : 1);
    std::string myHpCol = std::string(Color::BG_BLACK) +
        ((myHpPct > 50) ? Color::BRIGHT_GREEN :
         (myHpPct > 20) ? Color::BRIGHT_YELLOW : Color::BRIGHT_RED);
    drawHPBar(myInfoX, myInfoY + 2, myPoke.currentHP, myPoke.maxHP, myHpCol);

    // 대화창 박스 (하단)
    int boxH = 6;
    int boxY = H - boxH - 1;
    int boxW = W - 4;
    int boxX = 2;
    ren_.drawBox(boxX, boxY, boxW, boxH, std::string(Color::BG_BLACK) + Color::WHITE);
    ren_.fillRect(boxX+1, boxY+1, boxW-2, boxH-2, ' ', std::string(Color::BG_BLACK) + Color::WHITE);

    // 커맨드 메뉴 (CHOOSE_ACTION) - 2×2 그리드
    if (state_.phase == BattlePhase::CHOOSE_ACTION) {
        // 오른쪽 절반에 커맨드 박스
        int cmdX = boxX + boxW / 2;
        ren_.drawBox(cmdX, boxY, boxW/2, boxH, std::string(Color::BG_BLACK) + Color::WHITE);
        ren_.fillRect(cmdX+1, boxY+1, boxW/2-2, boxH-2, ' ', std::string(Color::BG_BLACK) + Color::WHITE);
        const char* cmds[4] = {"FIGHT", "BAG", "POKEMON", "RUN"};
        for (int i = 0; i < 4; i++) {
            int cx = cmdX + 2 + (i % 2) * 10;
            int cy = boxY + 2 + (i / 2);
            std::string cc = std::string(Color::BG_BLACK) +
                (state_.cursor == i ? Color::BRIGHT_WHITE : Color::WHITE);
            if (state_.cursor == i) ren_.print(cx - 2, cy, ">", cc);
            ren_.print(cx, cy, cmds[i], cc);
        }
    }

    // 기술 선택 메뉴 (CHOOSE_MOVE)
    if (state_.phase == BattlePhase::CHOOSE_MOVE) {
        for (int i = 0; i < myPoke.numMoves; i++) {
            int cx = boxX + 2 + (i % 2) * 16;
            int cy = boxY + 2 + (i / 2);
            char ppStr[16];
            snprintf(ppStr, sizeof(ppStr), " PP:%d", myPoke.moves[i].pp);
            std::string cc = std::string(Color::BG_BLACK) +
                (state_.cursor == i ? Color::BRIGHT_YELLOW : Color::WHITE);
            if (state_.cursor == i) ren_.print(cx - 2, cy, ">", cc);
            ren_.print(cx + 8, cy, ppStr, std::string(Color::BG_BLACK) + Color::BRIGHT_BLACK);
        }
        ren_.print(boxX + 2, boxY + boxH - 2, "[ B: back ]",
            std::string(Color::BG_BLACK) + Color::BRIGHT_BLACK);
    }
}

void Battle::renderKorean() {
    int W = ren_.width;
    int H = ren_.height;
    int mid = H / 2;

    Pokemon& myPoke = pl_.party[state_.playerPartyIdx];
    Pokemon& en     = state_.enemy;

    // ── 포켓몬 교체 화면 한글 ─────────────────────────────────
    if (state_.phase == BattlePhase::CHOOSE_POKEMON) {
        int boxX = (W - 40) / 2;
        int boxY = 1;
        ren_.printW(boxX + 2, boxY + 1, L"포켓몬 선택:",
            std::string(Color::BG_BLACK) + Color::BRIGHT_YELLOW);
        for (int i = 0; i < pl_.partySize; i++) {
            int cy = boxY + 2 + i * 2;
            std::string cc = std::string(Color::BG_BLACK) +
                (i == state_.cursor ? Color::BRIGHT_YELLOW : Color::WHITE);
            if (pl_.party[i].species) {
                wchar_t buf[32];
                swprintf(buf, 32, L"%ls Lv.%d", pl_.party[i].species->name, pl_.party[i].level);
                ren_.printW(boxX + 4, cy, buf, cc);
            }
        }
        if (state_.switchAfterFaint)
            ren_.printW(boxX + 2, boxY + 14, L"다음 포켓몬을 선택하세요!",
                std::string(Color::BG_BLACK) + Color::BRIGHT_RED);
        else
            ren_.printW(boxX + 2, boxY + 14, L"[ B: 취소 ]",
                std::string(Color::BG_BLACK) + Color::BRIGHT_BLACK);
        return;
    }

    wchar_t buf[64];

    // ── 스프라이트 출력 (flush 후 호출) ─────────────────────────
    // 적 스프라이트 (우측 상단)
    int enSprX = W / 2 + 2;
    int enSprY = 1;
    drawSprite(enSprX, enSprY, en.species->id, false);

    // 내 포켓몬 스프라이트 (좌측 중간)
    int mySprX = 4;
    int mySprY = mid - SPR_H + 1;
    drawSprite(mySprX, mySprY, myPoke.species->id, true);

    // ── 적 포켓몬 이름/레벨 ──────────────────────────────────
    int enInfoX = W - 32;
    int enInfoY = 1;
    swprintf(buf, 64, L"상대 %ls  Lv.%d", en.species->name, en.level);
    ren_.printW(enInfoX, enInfoY, buf,
        std::string(Color::BG_BLACK) + Color::BRIGHT_WHITE);
    swprintf(buf, 64, L"HP: %d / %d", en.currentHP, en.maxHP);
    ren_.printW(enInfoX, enInfoY + 1, buf,
        std::string(Color::BG_BLACK) + Color::WHITE);

    // ── 내 포켓몬 이름/레벨 ──────────────────────────────────
    int myInfoX = W / 2 + 2;
    int myInfoY = mid + 1;
    swprintf(buf, 64, L"%ls  Lv.%d", myPoke.species->name, myPoke.level);
    ren_.printW(myInfoX, myInfoY, buf,
        std::string(Color::BG_BLACK) + Color::BRIGHT_CYAN);
    swprintf(buf, 64, L"HP: %d / %d", myPoke.currentHP, myPoke.maxHP);
    ren_.printW(myInfoX, myInfoY + 1, buf,
        std::string(Color::BG_BLACK) + Color::CYAN);

    // ── 대화창 메시지 ─────────────────────────────────────────
    int boxH = 6;
    int boxY = H - boxH - 1;
    int boxX = 2;

    if (state_.phase == BattlePhase::SHOW_MSG ||
        state_.phase == BattlePhase::FAINT_ENEMY ||
        state_.phase == BattlePhase::FAINT_PLAYER ||
        state_.phase == BattlePhase::LEVEL_UP_MSG ||
        state_.phase == BattlePhase::VICTORY ||
        state_.phase == BattlePhase::DEFEAT) {
        ren_.printW(boxX + 2, boxY + 2, state_.msg,
            std::string(Color::BG_BLACK) + Color::BRIGHT_WHITE);
        if (state_.msg2[0])
            ren_.printW(boxX + 2, boxY + 3, state_.msg2,
                std::string(Color::BG_BLACK) + Color::BRIGHT_YELLOW);
        if (state_.awaitKey)
            ren_.printW(boxX + 2, boxY + boxH - 2, L"[ Z / Enter: 계속 ]",
                std::string(Color::BG_BLACK) + Color::BRIGHT_BLACK);
    }

    // ── 기술 이름 (CHOOSE_MOVE) ───────────────────────────────
    if (state_.phase == BattlePhase::CHOOSE_MOVE) {
        ren_.printW(boxX + 2, boxY + 1, L"기술 선택:",
            std::string(Color::BG_BLACK) + Color::BRIGHT_YELLOW);
        for (int i = 0; i < myPoke.numMoves; i++) {
            int cx = boxX + 2 + (i % 2) * 16;
            int cy = boxY + 2 + (i / 2);
            const MoveData& mv = getMoveData(myPoke.moves[i].moveId);
            std::string cc = std::string(Color::BG_BLACK) +
                (state_.cursor == i ? Color::BRIGHT_YELLOW : Color::WHITE);
            ren_.printW(cx, cy, mv.name, cc);
        }
    }

    // ── 커맨드 헤더 (CHOOSE_ACTION) ───────────────────────────
    if (state_.phase == BattlePhase::CHOOSE_ACTION) {
        swprintf(buf, 64, L"%ls은(는) 어떻게 할까?", myPoke.species->name);
        ren_.printW(boxX + 2, boxY + 1, buf,
            std::string(Color::BG_BLACK) + Color::BRIGHT_WHITE);
    }
}
