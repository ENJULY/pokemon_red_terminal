#include "game.h"
#include <windows.h>
#include <cstring>
#include <cstdio>
#include <wchar.h>
#include <cstdlib>
#include <ctime>
#ifndef swprintf
#define swprintf _snwprintf
#endif

// ─── 정적 데이터 ──────────────────────────────────────────────
const wchar_t* Game::INTRO_LINES[Game::INTRO_COUNT] = {
    L"안녕하세요! 포켓몬의 세계에 오신 것을 환영합니다!",
    L"제 이름은 오박사!",
    L"사람들은 저를 포켓몬 박사라고 부르죠.",
    L"이 세계에는 포켓몬이라 불리는 신기한 생물이 살고 있습니다!",
    L"어떤 사람에게 포켓몬은 애완동물입니다.",
    L"어떤 사람은 싸움에 사용하기도 하죠.",
    L"나는... 포켓몬을 연구하는 사람입니다.",
    L"자, 먼저 당신의 이름을 알려주세요!"
};

const char* Game::OAK_SPRITE[12] = {
    "    .------.",
    "   / O    O \\",
    "  |    __    |",
    "  |  (____) |",
    "   \\        /",
    "    `------'",
    "      |  |",
    "   ___|  |___",
    "  |   \\  /   |",
    "  |    \\/    |",
    "       ||",
    "      /  \\",
};

const wchar_t* Game::DEX_LINES[5] = {
    L"아, 잠깐! 줄 것이 있어!",
    L"이것이 포켓덱스! 만난 포켓몬을 자동으로 기록해줘.",
    L"그리고 몬스터볼도 5개 줄게.",
    L"야생 포켓몬은 약하게 만든 뒤 몬스터볼을 던져봐!",
    nullptr
};

// ─── 초기화 ──────────────────────────────────────────────────
Game::Game() {
    srand((unsigned)time(nullptr));

    // 플레이어 초기 설정
    memset(&player_, 0, sizeof(player_));
    wcscpy(player_.name, L"RED");
    player_.mapId = MAP_PALLET;
    player_.x = 11; player_.y = 7;
    player_.dir = 0;
    player_.pokeballs = 0;
    player_.hasPokedex = false;

    battle_ = nullptr;
    ow_     = nullptr;
}

void Game::run() {
    renderer.init();
    const int FRAME_MS = 62;

    while (running) {
        DWORD start = GetTickCount();

        Key  key = Input::poll();
        char ch  = Input::pollChar();

        // Name input: pollChar 우선 처리
        if (scene_ == Scene::NAME_INPUT) {
            update(key);
            updateNameInput(key, ch);
        } else {
            update(key);
        }

        renderer.clear();
        render();
        renderer.flush();
        renderKorean();

        DWORD elapsed = GetTickCount() - start;
        if (elapsed < (DWORD)FRAME_MS)
            Sleep(FRAME_MS - elapsed);
        frame_++;
    }
}

void Game::changeScene(Scene next) {
    scene_ = next;
    frame_ = 0;
}

// ─── 씬 라우팅 ───────────────────────────────────────────────
void Game::update(Key key) {
    if (key == Key::ESCAPE) { running = false; return; }

    switch (scene_) {
    case Scene::INTRO:          updateIntro(key);          break;
    case Scene::NAME_INPUT:     /* handled separately */   break;
    case Scene::STARTER_SELECT: updateStarterSelect(key);  break;
    case Scene::RECEIVE_DEX:    updateReceiveDex(key);     break;
    case Scene::OVERWORLD: {
        if (!ow_) break;
        ow_->update(key);
        OwEvent ev = ow_->popEvent();
        switch (ev) {
        case OwEvent::WILD_ENCOUNTER:
            if (!battle_) battle_ = new Battle(renderer, player_);
            battle_->startWild(ow_->wildSpeciesId(), ow_->wildLevel());
            changeScene(Scene::WILD_BATTLE);
            break;
        case OwEvent::TRAINER_BATTLE: {
            MapDef* m = getMap(player_.mapId);
            if (m && ow_->eventData() < m->numTrainers) {
                TrainerDef& tr = const_cast<TrainerDef&>(m->trainers[ow_->eventData()]);
                if (!battle_) battle_ = new Battle(renderer, player_);
                battle_->startTrainer(tr.name, tr.preBattleText,
                    tr.partyIds, tr.partyLevels,
                    (tr.partyIds[1] ? 2 : 1));
                changeScene(Scene::TRAINER_BATTLE);
            }
            break;
        }
        case OwEvent::BOSS_BATTLE:
            if (!battle_) battle_ = new Battle(renderer, player_);
            battle_->startBrock();
            changeScene(Scene::BOSS_BATTLE);
            break;
        case OwEvent::ENTER_POKEMON_CENTER:
            changeScene(Scene::POKEMON_CENTER);
            centerStep_ = 0;
            break;
        case OwEvent::ENTER_MART:
            if (!player_.deliveredParcel) {
                changeScene(Scene::MART_EVENT);
                martStep_ = 0;
            }
            break;
        default: break;
        }
        break;
    }
    case Scene::WILD_BATTLE:
    case Scene::TRAINER_BATTLE:
    case Scene::BOSS_BATTLE:
        if (battle_) {
            battle_->update(key);
            if (battle_->isDone()) {
                BattleResult res = battle_->result();
                bool won = (res == BattleResult::WIN);

                if (scene_ == Scene::BOSS_BATTLE && won) {
                    player_.beatenBrock = true;
                    changeScene(Scene::ENDING);
                    endingStep_ = 0;
                    delete battle_; battle_ = nullptr;
                    break;
                }

                // 트레이너 처리
                if (scene_ == Scene::TRAINER_BATTLE && won) {
                    MapDef* m = getMap(player_.mapId);
                    if (m) {
                        int idx = ow_ ? ow_->eventData() : -1;
                        if (idx >= 0 && idx < m->numTrainers)
                            const_cast<TrainerDef&>(m->trainers[idx]).defeated = true;
                    }
                }

                if (!won && res == BattleResult::LOSE) {
                    // 전멸 → 팔레트시티로
                    healAll(player_);
                    player_.mapId = MAP_PALLET;
                    player_.x = 11; player_.y = 7;
                    if (ow_) ow_->onReturnFromBattle(false);
                    changeScene(Scene::GAME_OVER);
                } else if (!player_.beatenRival1 && won) {
                    // 라이벌 첫 배틀 승리 → 포켓덱스 수령
                    player_.beatenRival1 = true;
                    dexStep_ = 0;
                    changeScene(Scene::RECEIVE_DEX);
                } else {
                    if (ow_) ow_->onReturnFromBattle(won);
                    else {
                        if (!ow_) ow_ = new Overworld(renderer, player_);
                        ow_->init();
                    }
                    changeScene(Scene::OVERWORLD);
                }
                delete battle_; battle_ = nullptr;
            }
        }
        break;
    case Scene::POKEMON_CENTER:
        updateCenter(key);
        break;
    case Scene::MART_EVENT:
        updateMart(key);
        break;
    case Scene::GAME_OVER:
        updateGameOver(key);
        break;
    case Scene::ENDING:
        updateEnding(key);
        break;
    default: break;
    }
}

void Game::render() {
    switch (scene_) {
    case Scene::INTRO:          renderIntro();          break;
    case Scene::NAME_INPUT:     renderNameInput();      break;
    case Scene::STARTER_SELECT: renderStarterSelect();  break;
    case Scene::RECEIVE_DEX:    renderReceiveDex();     break;
    case Scene::OVERWORLD:
        if (ow_) ow_->render();
        break;
    case Scene::WILD_BATTLE:
    case Scene::TRAINER_BATTLE:
    case Scene::BOSS_BATTLE:
        if (battle_) battle_->render();
        break;
    case Scene::POKEMON_CENTER: renderCenter();         break;
    case Scene::MART_EVENT:     renderMart();           break;
    case Scene::GAME_OVER:      renderGameOver();       break;
    case Scene::ENDING:         renderEnding();         break;
    default: break;
    }
}

void Game::renderKorean() {
    switch (scene_) {
    case Scene::INTRO:          renderIntroKorean();          break;
    case Scene::NAME_INPUT:     renderNameInputKorean();      break;
    case Scene::STARTER_SELECT: renderStarterSelectKorean();  break;
    case Scene::RECEIVE_DEX:    renderReceiveDexKorean();     break;
    case Scene::OVERWORLD:
        if (ow_) ow_->renderKorean();
        break;
    case Scene::WILD_BATTLE:
    case Scene::TRAINER_BATTLE:
    case Scene::BOSS_BATTLE:
        if (battle_) battle_->renderKorean();
        break;
    case Scene::POKEMON_CENTER: renderCenterKorean();         break;
    case Scene::MART_EVENT:     renderMartKorean();           break;
    case Scene::ENDING:         renderEndingKorean();         break;
    default: break;
    }
}

// ─── 인트로 씬 ───────────────────────────────────────────────
void Game::updateIntro(Key key) {
    if (key == Key::A) {
        introStep_++;
        if (introStep_ >= INTRO_COUNT) {
            changeScene(Scene::NAME_INPUT);
            nameLen_ = 0;
            memset(nameBuf_, 0, sizeof(nameBuf_));
        }
    }
}

void Game::renderIntro() {
    int W = renderer.width, H = renderer.height;
    renderer.fillRect(0,0,W,H,' ', std::string(Color::BG_BLACK)+Color::WHITE);

    int titleX = W/2 - 8;
    renderer.print(titleX, 2, " POKEMON RED ",
        std::string(Color::BG_BLACK)+Color::BRIGHT_RED);
    renderer.print(titleX, 3, "=============",
        std::string(Color::BG_BLACK)+Color::RED);

    // 오박사 스프라이트
    int sprX = W/2 - 8, sprY = 5;
    for (int i = 0; i < 12; i++)
        renderer.print(sprX, sprY+i, OAK_SPRITE[i],
            std::string(Color::BG_BLACK)+Color::BRIGHT_WHITE);

    // 대화창
    int bH=6, bY=H-bH-1, bW=W-4, bX=2;
    renderer.drawBox(bX,bY,bW,bH, std::string(Color::BG_BLACK)+Color::WHITE);
    renderer.fillRect(bX+1,bY+1,bW-2,bH-2,' ',std::string(Color::BG_BLACK)+Color::WHITE);
    renderer.print(bX+2,bY+1,"[OAK]",
        std::string(Color::BG_BLACK)+Color::BRIGHT_YELLOW);
    // 커서 깜박임
    if ((frame_/8)%2==0)
        renderer.print(bX+bW-4,bY+bH-2," v ",
            std::string(Color::BG_BLACK)+Color::BRIGHT_WHITE);
    renderer.print(bX+2,bY+bH-2,"[ Z/Enter: next ]",
        std::string(Color::BG_BLACK)+Color::BRIGHT_BLACK);
}

void Game::renderIntroKorean() {
    int H = renderer.height;
    int bH=6, bY=H-bH-1, bX=2;
    if (introStep_ < INTRO_COUNT)
        renderer.printW(bX+2, bY+3, INTRO_LINES[introStep_],
            std::string(Color::BG_BLACK)+Color::BRIGHT_WHITE);
}

// ─── 이름 입력 ───────────────────────────────────────────────
void Game::updateNameInput(Key, char ch) {
    if (ch == 13) { // Enter → 확정
        if (nameLen_ == 0) {
            // 기본 이름
            wcscpy(player_.name, L"RED");
        } else {
            // ASCII → wchar_t
            for (int i = 0; i < nameLen_ && i < 8; i++)
                player_.name[i] = (wchar_t)(unsigned char)nameBuf_[i];
            player_.name[nameLen_] = 0;
        }
        changeScene(Scene::STARTER_SELECT);
        starterCursor_ = 0;
    } else if (ch == 8) { // Backspace
        if (nameLen_ > 0) nameBuf_[--nameLen_] = 0;
    } else if (ch >= 32 && ch <= 126 && nameLen_ < 8) {
        nameBuf_[nameLen_++] = ch;
        nameBuf_[nameLen_]   = 0;
    }
}

void Game::renderNameInput() {
    int W = renderer.width, H = renderer.height;
    renderer.fillRect(0,0,W,H,' ', std::string(Color::BG_BLACK)+Color::WHITE);
    int bH=8, bY=H/2-4, bW=40, bX=(W-40)/2;
    renderer.drawBox(bX,bY,bW,bH, std::string(Color::BG_BLACK)+Color::WHITE);
    renderer.fillRect(bX+1,bY+1,bW-2,bH-2,' ',std::string(Color::BG_BLACK)+Color::WHITE);
}

void Game::renderNameInputKorean() {
    int W = renderer.width, H = renderer.height;
    int bY=H/2-4, bX=(W-40)/2;
    renderer.printW(bX+2, bY+2, L"이름을 입력하세요 (최대 8자):",
        std::string(Color::BG_BLACK)+Color::BRIGHT_WHITE);
    // 현재 입력 표시
    wchar_t disp[64] = L"> ";
    for (int i = 0; i < nameLen_; i++) disp[2+i] = (wchar_t)(unsigned char)nameBuf_[i];
    disp[2+nameLen_] = 0;
    if ((frame_/8)%2==0) { disp[2+nameLen_]='_'; disp[3+nameLen_]=0; }
    renderer.printW(bX+2, bY+4, disp, std::string(Color::BG_BLACK)+Color::BRIGHT_CYAN);
    renderer.printW(bX+2, bY+6, L"[ Enter: 확인 / 공백시 RED ]",
        std::string(Color::BG_BLACK)+Color::BRIGHT_BLACK);
}

// ─── 스타터 선택 ─────────────────────────────────────────────
static const int STARTER_IDS[3] = {1, 4, 7};
static const char* STARTER_ASCII[3][8] = {
    // 이상해씨
    {"  .--.",".O  O.","| -- |","\\    /",".----.",nullptr,nullptr,nullptr},
    // 파이리
    {" /\\  /\\"," (oo) "," |  | "," \\--/ ",nullptr,nullptr,nullptr,nullptr},
    // 꼬부기
    {" .----."," | OO |"," | -- |"," `----'",nullptr,nullptr,nullptr,nullptr},
};
static const wchar_t* STARTER_NAMES[3] = {L"이상해씨", L"파이리", L"꼬부기"};
static const wchar_t* STARTER_TYPES[3] = {L"풀/독", L"불꽃", L"물"};

void Game::updateStarterSelect(Key key) {
    if (key == Key::LEFT)  starterCursor_ = (starterCursor_+2)%3;
    if (key == Key::RIGHT) starterCursor_ = (starterCursor_+1)%3;
    if (key == Key::A) {
        // 스타터 지급
        player_.party[0] = makePokemon(STARTER_IDS[starterCursor_], 5);
        player_.partySize = 1;
        // 라이벌 배틀 시작
        if (!battle_) battle_ = new Battle(renderer, player_);
        int rivalId = 25; // 피카츄
        int rivalIds[] = {rivalId, 0, 0};
        int rivalLvls[] = {5, 0, 0};
        battle_->startTrainer(L"라이벌", L"잠깐, 배틀하자고!", rivalIds, rivalLvls, 1);
        changeScene(Scene::RIVAL_BATTLE);
        scene_ = Scene::TRAINER_BATTLE; // rival uses same battle system
    }
}

void Game::renderStarterSelect() {
    int W = renderer.width, H = renderer.height;
    renderer.fillRect(0,0,W,H,' ', std::string(Color::BG_BLACK)+Color::WHITE);
    // 테이블
    int tableY = H/2 - 6;
    int spacing = W/4;
    for (int i = 0; i < 3; i++) {
        int cx = spacing * (i+1) - 4;
        std::string bord = (i == starterCursor_) ?
            std::string(Color::BG_BLACK)+Color::BRIGHT_YELLOW :
            std::string(Color::BG_BLACK)+Color::BRIGHT_BLACK;
        renderer.drawBox(cx-2, tableY, 12, 10, bord);
        // 스프라이트
        for (int r = 0; r < 8 && STARTER_ASCII[i][r]; r++)
            renderer.print(cx-1, tableY+1+r, STARTER_ASCII[i][r],
                std::string(Color::BG_BLACK)+Color::WHITE);
        if (i == starterCursor_)
            renderer.print(cx+2, tableY+9, "^^^",
                std::string(Color::BG_BLACK)+Color::BRIGHT_YELLOW);
    }
}

void Game::renderStarterSelectKorean() {
    int W = renderer.width, H = renderer.height;
    int tableY = H/2 - 6;
    int spacing = W/4;
    renderer.printW(W/2-8, tableY-3, L"포켓몬을 선택하세요!",
        std::string(Color::BG_BLACK)+Color::BRIGHT_WHITE);
    renderer.printW(W/2-12, tableY-2, L"← → 방향키로 선택, Z로 결정",
        std::string(Color::BG_BLACK)+Color::BRIGHT_BLACK);
    for (int i = 0; i < 3; i++) {
        int cx = spacing * (i+1) - 4;
        std::string col = (i==starterCursor_) ?
            std::string(Color::BG_BLACK)+Color::BRIGHT_YELLOW :
            std::string(Color::BG_BLACK)+Color::WHITE;
        renderer.printW(cx-1, tableY+6, STARTER_NAMES[i], col);
        renderer.printW(cx-1, tableY+7, STARTER_TYPES[i],
            std::string(Color::BG_BLACK)+Color::BRIGHT_GREEN);
    }
}

// ─── 포켓덱스 수령 ───────────────────────────────────────────
void Game::updateReceiveDex(Key key) {
    if (key == Key::A) {
        dexStep_++;
        if (dexStep_ >= 4 || !DEX_LINES[dexStep_]) {
            // 오버월드로 전환
            player_.hasPokedex  = true;
            player_.pokeballs   = 5;
            player_.mapId = MAP_PALLET;
            player_.x = 11; player_.y = 7;
            if (!ow_) ow_ = new Overworld(renderer, player_);
            ow_->init();
            changeScene(Scene::OVERWORLD);
        }
    }
}

void Game::renderReceiveDex() {
    int W = renderer.width, H = renderer.height;
    renderer.fillRect(0,0,W,H,' ', std::string(Color::BG_BLACK)+Color::WHITE);
    int bH=6, bY=H-bH-1, bW=W-4, bX=2;
    renderer.drawBox(bX,bY,bW,bH, std::string(Color::BG_BLACK)+Color::WHITE);
    renderer.fillRect(bX+1,bY+1,bW-2,bH-2,' ',std::string(Color::BG_BLACK)+Color::WHITE);
    renderer.print(bX+2,bY+1,"[OAK]", std::string(Color::BG_BLACK)+Color::BRIGHT_YELLOW);
    if ((frame_/8)%2==0)
        renderer.print(bX+bW-4,bY+bH-2," v ",
            std::string(Color::BG_BLACK)+Color::BRIGHT_WHITE);
}

void Game::renderReceiveDexKorean() {
    int H = renderer.height;
    int bH=6, bY=H-bH-1, bX=2;
    if (dexStep_ < 4 && DEX_LINES[dexStep_])
        renderer.printW(bX+2, bY+3, DEX_LINES[dexStep_],
            std::string(Color::BG_BLACK)+Color::BRIGHT_WHITE);
}

// ─── 포켓몬센터 ──────────────────────────────────────────────
static const wchar_t* CENTER_LINES[] = {
    L"어서오세요! 포켓몬센터입니다.",
    L"포켓몬들을 치료해드리겠습니다.",
    L"완료! 포켓몬들이 완전히 회복됐습니다!",
    nullptr
};

void Game::updateCenter(Key key) {
    if (key == Key::A) {
        if (centerStep_ == 0) {
            centerStep_++;
        } else if (centerStep_ == 1) {
            healAll(player_);
            centerStep_++;
        } else {
            if (ow_) ow_->onReturnFromCenter();
            else {
                if (!ow_) ow_ = new Overworld(renderer, player_);
                ow_->init();
            }
            changeScene(Scene::OVERWORLD);
        }
    }
}

void Game::renderCenter() {
    int W = renderer.width, H = renderer.height;
    renderer.fillRect(0,0,W,H,' ', std::string(Color::BG_BLACK)+Color::WHITE);
    int bH=6, bY=H-bH-1, bW=W-4, bX=2;
    renderer.drawBox(bX,bY,bW,bH, std::string(Color::BG_BLACK)+Color::WHITE);
    renderer.fillRect(bX+1,bY+1,bW-2,bH-2,' ',std::string(Color::BG_BLACK)+Color::WHITE);
    renderer.print(bX+2,bY+1,"[CENTER]",
        std::string(Color::BG_BLACK)+Color::BRIGHT_CYAN);
}

void Game::renderCenterKorean() {
    int H = renderer.height;
    int bH=6, bY=H-bH-1, bX=2;
    if (CENTER_LINES[centerStep_])
        renderer.printW(bX+2, bY+3, CENTER_LINES[centerStep_],
            std::string(Color::BG_BLACK)+Color::BRIGHT_WHITE);
    renderer.printW(bX+2, bY+bH-2, L"[ Z: 계속 ]",
        std::string(Color::BG_BLACK)+Color::BRIGHT_BLACK);
}

// ─── 마트 이벤트 (소포 이벤트) ───────────────────────────────
static const wchar_t* MART_LINES[] = {
    L"어서오세요! 포켓몬마트입니다.",
    L"아, 혹시 팔레트시티에서 오셨나요?",
    L"오박사님께 전해드릴 소포가 있어요!",
    L"꼭 오박사님께 전해주세요!",
    nullptr
};

void Game::updateMart(Key key) {
    if (key == Key::A) {
        martStep_++;
        if (martStep_ >= 4 || !MART_LINES[martStep_]) {
            player_.deliveredParcel = true;
            player_.pokeballs += 3; // 소포 대신 몬스터볼 지급
            if (ow_) ow_->onReturnFromCenter();
            else {
                if (!ow_) ow_ = new Overworld(renderer, player_);
                ow_->init();
            }
            changeScene(Scene::OVERWORLD);
        }
    }
}

void Game::renderMart() {
    int W = renderer.width, H = renderer.height;
    renderer.fillRect(0,0,W,H,' ', std::string(Color::BG_BLACK)+Color::WHITE);
    int bH=6, bY=H-bH-1, bW=W-4, bX=2;
    renderer.drawBox(bX,bY,bW,bH, std::string(Color::BG_BLACK)+Color::WHITE);
    renderer.fillRect(bX+1,bY+1,bW-2,bH-2,' ',std::string(Color::BG_BLACK)+Color::WHITE);
    renderer.print(bX+2,bY+1,"[MART]",
        std::string(Color::BG_BLACK)+Color::BRIGHT_GREEN);
}

void Game::renderMartKorean() {
    int H = renderer.height;
    int bH=6, bY=H-bH-1, bX=2;
    if (MART_LINES[martStep_])
        renderer.printW(bX+2, bY+3, MART_LINES[martStep_],
            std::string(Color::BG_BLACK)+Color::BRIGHT_WHITE);
    renderer.printW(bX+2, bY+bH-2, L"[ Z: 계속 ]",
        std::string(Color::BG_BLACK)+Color::BRIGHT_BLACK);
}

// ─── 게임 오버 ───────────────────────────────────────────────
void Game::updateGameOver(Key key) {
    if (key == Key::A) {
        // 팔레트시티로 리스폰
        if (!ow_) ow_ = new Overworld(renderer, player_);
        ow_->init();
        changeScene(Scene::OVERWORLD);
    }
}

void Game::renderGameOver() {
    int W = renderer.width, H = renderer.height;
    renderer.fillRect(0,0,W,H,' ', std::string(Color::BG_BLACK)+Color::WHITE);
    int cx = W/2-5, cy = H/2;
    renderer.print(cx, cy-2, "GAME  OVER",
        std::string(Color::BG_BLACK)+Color::BRIGHT_RED);
    renderer.print(cx-2, cy+2, "[ Z: 계속 ]",
        std::string(Color::BG_BLACK)+Color::BRIGHT_BLACK);
}

// ─── 엔딩 ────────────────────────────────────────────────────
static const wchar_t* ENDING_LINES[] = {
    L"브록을 쓰러뜨렸다!",
    L"바위배지를 획득했다!",
    L"이것이 모험의 첫 걸음이다!",
    L"앞으로도 더 많은 포켓몬과 만남이 기다린다.",
    L"【 클리어! 플레이 감사합니다! 】",
    nullptr
};

void Game::updateEnding(Key key) {
    if (key == Key::A) {
        endingStep_++;
        if (!ENDING_LINES[endingStep_]) {
            running = false; // 게임 종료
        }
    }
}

void Game::renderEnding() {
    int W = renderer.width, H = renderer.height;
    renderer.fillRect(0,0,W,H,' ', std::string(Color::BG_BLACK)+Color::WHITE);
    // 배지 표시
    int cx = W/2;
    renderer.print(cx-6, H/2-4, " *** BADGE *** ",
        std::string(Color::BG_BLACK)+Color::BRIGHT_YELLOW);
    renderer.print(cx-5, H/2-3, "  [BOULDER]  ",
        std::string(Color::BG_BLACK)+Color::YELLOW);
    int bH=6, bY=H-bH-1, bW=W-4, bX=2;
    renderer.drawBox(bX,bY,bW,bH, std::string(Color::BG_BLACK)+Color::WHITE);
    renderer.fillRect(bX+1,bY+1,bW-2,bH-2,' ',std::string(Color::BG_BLACK)+Color::WHITE);
    if ((frame_/8)%2==0)
        renderer.print(bX+bW-4,bY+bH-2," v ",
            std::string(Color::BG_BLACK)+Color::BRIGHT_YELLOW);
}

void Game::renderEndingKorean() {
    int H = renderer.height;
    int bH=6, bY=H-bH-1, bX=2;
    if (ENDING_LINES[endingStep_])
        renderer.printW(bX+2, bY+3, ENDING_LINES[endingStep_],
            std::string(Color::BG_BLACK)+Color::BRIGHT_WHITE);
    renderer.printW(bX+2, bY+bH-2, L"[ Z: 계속 ]",
        std::string(Color::BG_BLACK)+Color::BRIGHT_BLACK);
}

// ─── (미사용) 공통 대화창 ────────────────────────────────────
void Game::startDialog(const wchar_t** lines, int count, Scene next) {
    for (int i = 0; i < count && i < 6; i++) dialogLines_[i] = lines[i];
    dialogCount_ = count;
    dialogStep_  = 0;
    dialogNext_  = next;
}
