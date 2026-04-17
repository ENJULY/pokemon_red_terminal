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
// 원작 대사 순서 (pokered 동일):
// 0~4: 나레이션, 5: 이름 확인(동적), 6~8: 라이벌 소개, 9: 라이벌 이름 확인(동적), 10~11: 마무리
// nullptr = 동적으로 swprintf 처리
const wchar_t* Game::INTRO_LINES[Game::INTRO_COUNT] = {
    L"이 세계에는 포켓몬이라 불리는 생물이 서식하고 있습니다!",   // 0
    L"어떤 사람들에게 포켓몬은 애완동물이기도 합니다.",            // 1
    L"어떤 사람들은 포켓몬을 싸움에 사용하기도 하죠.",             // 2
    L"나는... 포켓몬을 연구하는 사람입니다.",                      // 3
    L"그런데 당신은 누구입니까? 먼저 이름을 알려주세요.",          // 4  → NAME_INPUT
    nullptr,                                                        // 5  → "그렇군요! [이름]이군요!" (동적)
    L"그리고 이 아이는 나의 손자입니다.",                          // 6
    L"태어날 때부터 당신의 라이벌이었지요.",                       // 7
    L"...음, 이름이 뭐였더라?",                                    // 8  → RIVAL_NAME_INPUT
    nullptr,                                                        // 9  → "맞아! [라이벌]이야!" (동적)
    L"당신만의 포켓몬 전설이 지금 시작되려 하고 있습니다!",        // 10
    L"꿈과 모험이 가득한 포켓몬 세계로, 출발!",                    // 11 → STARTER_SELECT
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

const wchar_t* Game::LAB_INTRO_LINES[Game::LAB_INTRO_COUNT] = {
    L"Oak: 나에게는 연구에 사용하던 세 마리의 포켓몬이 있어.",
    L"Oak: 저 테이블 위 몬스터볼에 들어있지. 하나를 골라라!",
    L"블루: 저도 하나 가져가겠습니다!",
    L"Oak: 자, 어서 골라봐!",
};

// ─── 초기화 ──────────────────────────────────────────────────
Game::Game() {
    srand((unsigned)time(nullptr));

    // 플레이어 초기 설정
    memset(&player_, 0, sizeof(player_));
    wcscpy(player_.name, L"RED");
    wcscpy(player_.rivalName, L"블루");
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

        // 이름 입력 씬: poll()이 키다운을 소모하므로 pollChar() 단독 사용
        if (scene_ == Scene::NAME_INPUT || scene_ == Scene::RIVAL_NAME_INPUT) {
            char ch = Input::pollChar();
            if (ch == 27) { running = false; continue; } // ESC
            if (scene_ == Scene::NAME_INPUT)       updateNameInput(Key::NONE, ch);
            else                                   updateRivalNameInput(Key::NONE, ch);
        } else {
            Key key = Input::poll();
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
    case Scene::INTRO:              updateIntro(key);          break;
    case Scene::NAME_INPUT:         /* handled separately */   break;
    case Scene::RIVAL_NAME_INPUT:   /* handled separately */   break;
    case Scene::LAB_INTRO:          updateLabIntro(key);       break;
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
        case OwEvent::OAK_INTERCEPT:
            labIntroStep_ = 0;
            // 플레이어를 연구소 위치로 이동
            player_.mapId = MAP_OAK_LAB;
            player_.x = 5; player_.y = 8;
            changeScene(Scene::LAB_INTRO);
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
    case Scene::INTRO:              renderIntro();          break;
    case Scene::NAME_INPUT:         renderNameInput();      break;
    case Scene::RIVAL_NAME_INPUT:   renderRivalNameInput(); break;
    case Scene::LAB_INTRO:          renderLabIntro();       break;
    case Scene::STARTER_SELECT:     renderStarterSelect();  break;
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
    case Scene::INTRO:              renderIntroKorean();          break;
    case Scene::NAME_INPUT:         renderNameInputKorean();      break;
    case Scene::RIVAL_NAME_INPUT:   renderRivalNameInputKorean(); break;
    case Scene::LAB_INTRO:          renderLabIntroKorean();       break;
    case Scene::STARTER_SELECT:     renderStarterSelectKorean();  break;
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
    if (key != Key::A) return;

    // step 4: "이름을 알려주세요" → NAME_INPUT
    if (introStep_ == 4) {
        changeScene(Scene::NAME_INPUT);
        nameLen_ = 0;
        memset(nameBuf_, 0, sizeof(nameBuf_));
        return;
    }
    // step 8: "이름이 뭐였더라?" → RIVAL_NAME_INPUT
    if (introStep_ == 8) {
        changeScene(Scene::RIVAL_NAME_INPUT);
        rivalNameLen_ = 0;
        memset(rivalNameBuf_, 0, sizeof(rivalNameBuf_));
        return;
    }
    // step 11 (마지막): → OVERWORLD (주인공 집 2층 시작 - 원작과 동일)
    if (introStep_ == 11) {
        player_.x = 4; player_.y = 4;  // 집 내부 중앙 (8×8 맵)
        player_.mapId = MAP_PLAYER_HOUSE;
        if (!ow_) ow_ = new Overworld(renderer, player_);
        ow_->init();
        changeScene(Scene::OVERWORLD);
        return;
    }

    introStep_++;
}

void Game::renderIntro() {
    int W = renderer.width, H = renderer.height;
    renderer.fillRect(0,0,W,H,' ', std::string(Color::BG_BLACK)+Color::BLACK);

    int cx = W/2, cy = H/2 - 3;

    if (introStep_ <= 2) {
        // 단계 0~2: 포켓몬 세계 풍경 (타이틀)
        renderer.print(cx-10, cy-2, "~~~~~~~~~~~~~~~~~~",
            std::string(Color::BG_BLACK)+Color::BRIGHT_BLUE);
        renderer.print(cx-10, cy-1, " TTTTTTT  TTTTTTT ",
            std::string(Color::BG_BLACK)+Color::GREEN);
        renderer.print(cx-10, cy,   "  TTTTT    TTTTT  ",
            std::string(Color::BG_BLACK)+Color::GREEN);
        renderer.print(cx-8,  cy+1, "..  ;;  ..  ;;  ..",
            std::string(Color::BG_BLACK)+Color::BRIGHT_GREEN);
        renderer.print(cx-8,  cy+2, "..  ;;  ..  ;;  ..",
            std::string(Color::BG_BLACK)+Color::BRIGHT_GREEN);
        renderer.print(cx-7,  cy-4, "* POKEMON  RED *",
            std::string(Color::BG_BLACK)+Color::BRIGHT_RED);
    } else if (introStep_ <= 4) {
        // 단계 3~4: 오박사 등장
        int sprX = cx - 8, sprY = cy - 6;
        for (int i = 0; i < 12; i++)
            renderer.print(sprX, sprY+i, OAK_SPRITE[i],
                std::string(Color::BG_BLACK)+Color::BRIGHT_WHITE);
        renderer.print(cx-5, cy+7, "Prof. OAK",
            std::string(Color::BG_BLACK)+Color::BRIGHT_YELLOW);
    } else if (introStep_ <= 8) {
        // 단계 5~8: 오박사 + 라이벌 소개
        int sprX = cx - 14, sprY = cy - 5;
        for (int i = 0; i < 12; i++)
            renderer.print(sprX, sprY+i, OAK_SPRITE[i],
                std::string(Color::BG_BLACK)+Color::BRIGHT_WHITE);
        // 라이벌 실루엣
        renderer.print(cx+2, cy-2, "  .--.",
            std::string(Color::BG_BLACK)+Color::BRIGHT_CYAN);
        renderer.print(cx+2, cy-1, " /O  O\\",
            std::string(Color::BG_BLACK)+Color::BRIGHT_CYAN);
        renderer.print(cx+2, cy,   " | -- |",
            std::string(Color::BG_BLACK)+Color::BRIGHT_CYAN);
        renderer.print(cx+2, cy+1, " \\    /",
            std::string(Color::BG_BLACK)+Color::BRIGHT_CYAN);
        renderer.print(cx+2, cy+2, "  `--'",
            std::string(Color::BG_BLACK)+Color::BRIGHT_CYAN);
        renderer.print(cx+3, cy+3, "RIVAL",
            std::string(Color::BG_BLACK)+Color::BRIGHT_CYAN);
    } else {
        // 단계 9~11: 마무리
        int sprX = cx - 8, sprY = cy - 5;
        for (int i = 0; i < 12; i++)
            renderer.print(sprX, sprY+i, OAK_SPRITE[i],
                std::string(Color::BG_BLACK)+Color::BRIGHT_WHITE);
        renderer.print(cx-6, cy+8, "* * * * * *",
            std::string(Color::BG_BLACK)+Color::BRIGHT_YELLOW);
    }

    // 대화창
    int bH=6, bY=H-bH-1, bW=W-4, bX=2;
    renderer.drawBox(bX,bY,bW,bH, std::string(Color::BG_BLACK)+Color::WHITE);
    renderer.fillRect(bX+1,bY+1,bW-2,bH-2,' ',std::string(Color::BG_BLACK)+Color::WHITE);
    renderer.print(bX+2,bY+1,"[OAK]",
        std::string(Color::BG_BLACK)+Color::BRIGHT_YELLOW);
    if ((frame_/8)%2==0)
        renderer.print(bX+bW-4,bY+bH-2," v ",
            std::string(Color::BG_BLACK)+Color::BRIGHT_WHITE);
    renderer.print(bX+2,bY+bH-2,"[ Z / Enter: 다음 ]",
        std::string(Color::BG_BLACK)+Color::BRIGHT_BLACK);
}

void Game::renderIntroKorean() {
    int H = renderer.height;
    int bH=6, bY=H-bH-1, bX=2;
    if (introStep_ >= INTRO_COUNT) return;

    // step 5: "그렇군요! [이름]이군요!" — 동적 생성
    if (introStep_ == 5) {
        wchar_t buf[64];
        swprintf(buf, 64, L"그렇군요! 이름은 %ls이군요!", player_.name);
        renderer.printW(bX+2, bY+3, buf, std::string(Color::BG_BLACK)+Color::BRIGHT_WHITE);
        return;
    }
    // step 9: "맞아! [라이벌]이야!" — 동적 생성
    if (introStep_ == 9) {
        wchar_t buf[64];
        swprintf(buf, 64, L"맞아! 이름은 %ls이야!", player_.rivalName);
        renderer.printW(bX+2, bY+3, buf, std::string(Color::BG_BLACK)+Color::BRIGHT_WHITE);
        return;
    }

    if (INTRO_LINES[introStep_])
        renderer.printW(bX+2, bY+3, INTRO_LINES[introStep_],
            std::string(Color::BG_BLACK)+Color::BRIGHT_WHITE);
}

// ─── 이름 입력 ───────────────────────────────────────────────
void Game::updateNameInput(Key, char ch) {
    if (ch == 13) { // Enter → 확정
        if (nameLen_ == 0) {
            wcscpy(player_.name, L"RED");
        } else {
            for (int i = 0; i < nameLen_ && i < 8; i++)
                player_.name[i] = (wchar_t)(unsigned char)nameBuf_[i];
            player_.name[nameLen_] = 0;
        }
        // 인트로 step 5로 → "그렇군요! [이름]이군요!"
        introStep_ = 5;
        changeScene(Scene::INTRO);
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

// ─── 라이벌 이름 입력 ────────────────────────────────────────
void Game::updateRivalNameInput(Key, char ch) {
    if (ch == 13) { // Enter
        if (rivalNameLen_ == 0) {
            wcscpy(player_.rivalName, L"블루");
        } else {
            for (int i = 0; i < rivalNameLen_ && i < 8; i++)
                player_.rivalName[i] = (wchar_t)(unsigned char)rivalNameBuf_[i];
            player_.rivalName[rivalNameLen_] = 0;
        }
        // 인트로 step 9로 → "맞아! [라이벌]이야!"
        introStep_ = 9;
        changeScene(Scene::INTRO);
    } else if (ch == 8) {
        if (rivalNameLen_ > 0) rivalNameBuf_[--rivalNameLen_] = 0;
    } else if (ch >= 32 && ch <= 126 && rivalNameLen_ < 8) {
        rivalNameBuf_[rivalNameLen_++] = ch;
        rivalNameBuf_[rivalNameLen_]   = 0;
    }
}

void Game::renderRivalNameInput() {
    int W = renderer.width, H = renderer.height;
    renderer.fillRect(0,0,W,H,' ', std::string(Color::BG_BLACK)+Color::WHITE);
    int bH=8, bY=H/2-4, bW=40, bX=(W-40)/2;
    renderer.drawBox(bX,bY,bW,bH, std::string(Color::BG_BLACK)+Color::WHITE);
    renderer.fillRect(bX+1,bY+1,bW-2,bH-2,' ',std::string(Color::BG_BLACK)+Color::WHITE);
}

void Game::renderRivalNameInputKorean() {
    int W = renderer.width, H = renderer.height;
    int bY=H/2-4, bX=(W-40)/2;
    renderer.printW(bX+2, bY+2, L"라이벌의 이름을 입력하세요 (최대 8자):",
        std::string(Color::BG_BLACK)+Color::BRIGHT_WHITE);
    wchar_t disp[64] = L"> ";
    for (int i = 0; i < rivalNameLen_; i++)
        disp[2+i] = (wchar_t)(unsigned char)rivalNameBuf_[i];
    disp[2+rivalNameLen_] = 0;
    if ((frame_/8)%2==0) { disp[2+rivalNameLen_]='_'; disp[3+rivalNameLen_]=0; }
    renderer.printW(bX+2, bY+4, disp, std::string(Color::BG_BLACK)+Color::BRIGHT_CYAN);
    renderer.printW(bX+2, bY+6, L"[ Enter: 확인 / 공백시 블루 ]",
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
        // 블루는 상성 포켓몬 선택 (원작 동일)
        // Bulbasaur(0)→Charmander, Charmander(1)→Squirtle, Squirtle(2)→Bulbasaur
        int rivalStarterIdx = (starterCursor_ + 1) % 3;
        int rivalIds[]  = {STARTER_IDS[rivalStarterIdx], 0, 0};
        int rivalLvls[] = {5, 0, 0};
        if (!battle_) battle_ = new Battle(renderer, player_);
        battle_->startTrainer(player_.rivalName, L"잠깐, 배틀하자고!",
                              rivalIds, rivalLvls, 1);
        changeScene(Scene::TRAINER_BATTLE);
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
    renderer.printW(W/2-10, tableY-4, L"[ 오박사 연구소 ]",
        std::string(Color::BG_BLACK)+Color::BRIGHT_YELLOW);
    renderer.printW(W/2-12, tableY-3, L"블루가 지켜보고 있다...",
        std::string(Color::BG_BLACK)+Color::BRIGHT_CYAN);
    renderer.printW(W/2-8, tableY-2, L"포켓몬을 선택하세요!",
        std::string(Color::BG_BLACK)+Color::BRIGHT_WHITE);
    renderer.printW(W/2-12, tableY-1, L"← → 방향키로 선택, Z로 결정",
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

// ─── 연구소 인트로 ───────────────────────────────────────────
void Game::updateLabIntro(Key key) {
    if (key != Key::A) return;
    labIntroStep_++;
    if (labIntroStep_ >= LAB_INTRO_COUNT) {
        changeScene(Scene::STARTER_SELECT);
        starterCursor_ = 0;
    }
}

void Game::renderLabIntro() {
    int W = renderer.width, H = renderer.height;
    renderer.fillRect(0,0,W,H,' ', std::string(Color::BG_BLACK)+Color::WHITE);
    // 연구소 배경 - 테이블
    int cx = W/2;
    int ty = H/2 - 4;
    renderer.print(cx-8, ty,   "+--------+", std::string(Color::BG_BLACK)+Color::BRIGHT_WHITE);
    renderer.print(cx-8, ty+1, "|  (o)(o)(o)|", std::string(Color::BG_BLACK)+Color::BRIGHT_YELLOW);
    renderer.print(cx-8, ty+2, "+--------+", std::string(Color::BG_BLACK)+Color::BRIGHT_WHITE);
    renderer.print(cx-4, ty+3, "< BLUE >", std::string(Color::BG_BLACK)+Color::BRIGHT_CYAN);
    // 대화창
    int bH=6, bY=H-bH-1, bW=W-4, bX=2;
    renderer.drawBox(bX,bY,bW,bH, std::string(Color::BG_BLACK)+Color::WHITE);
    renderer.fillRect(bX+1,bY+1,bW-2,bH-2,' ',std::string(Color::BG_BLACK)+Color::WHITE);
    if ((frame_/8)%2==0)
        renderer.print(bX+bW-4,bY+bH-2," v ", std::string(Color::BG_BLACK)+Color::BRIGHT_WHITE);
}

void Game::renderLabIntroKorean() {
    int H = renderer.height;
    int bH=6, bY=H-bH-1, bX=2;
    if (labIntroStep_ < LAB_INTRO_COUNT)
        renderer.printW(bX+2, bY+3, LAB_INTRO_LINES[labIntroStep_],
            std::string(Color::BG_BLACK)+Color::BRIGHT_WHITE);
    renderer.printW(bX+2, bY+bH-2, L"[ Z: 계속 ]",
        std::string(Color::BG_BLACK)+Color::BRIGHT_BLACK);
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
