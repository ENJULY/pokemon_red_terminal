#include "game.h"
#include "../data/sprites.h"
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
// 스타터 ID (이상해씨/파이리/꼬부기) — update() 안에서도 사용하므로 파일 상단에 선언
static const int STARTER_IDS[3] = {1, 4, 7};

// pokered 원본 OakSpeechText 번역 (engine/movie/oak_speech + data/text/text_2.asm)
// 0~3: 오박사 자기소개+포켓몬 설명, 4: 이름 묻기 → NAME_INPUT,
// 5: 이름 확인(동적), 6~7: 라이벌 소개, 8: 라이벌 이름 묻기 → RIVAL_NAME_INPUT,
// 9: 라이벌 이름 확인(동적), 10~11: 마무리 → OVERWORLD
const wchar_t* Game::INTRO_LINES[Game::INTRO_COUNT] = {
    L"안녕! 포켓몬의 세계에 잘 왔어!",                                  // 0
    L"내 이름은 「오박사」. 사람들은 나를 포켓몬 박사라고도 부른단다.",  // 1
    L"이 세상엔 포켓몬이라 불리는 신비한 생물들이 함께 살고 있단다.",   // 2
    L"어떤 이에겐 포켓몬은 친구이고, 어떤 이는 시합 동료로 키우지.",    // 3
    L"나는 말이지... 포켓몬을 연구하는 사람이란다. 자, 이름이 뭐니?",  // 4  → NAME_INPUT
    nullptr,                                                             // 5  → "그래! 네 이름은 OO구나!" (동적)
    L"이 아이는 내 손자란다.",                                          // 6
    L"갓난아기 때부터 너의 라이벌이었지.",                              // 7
    L"...어라, 이 녀석 이름이 뭐였더라?",                                // 8  → RIVAL_NAME_INPUT
    nullptr,                                                             // 9  → "맞다! 이제 생각났어. 이 녀석 이름은 OO!" (동적)
    L"너만의 포켓몬 전설이 이제 막 펼쳐지려 하고 있어!",                 // 10
    L"꿈과 모험이 가득한 포켓몬의 세계로! 자, 가자!",                    // 11 → OVERWORLD
};

// (구) 인라인 데포르메 OAK_SPRITE 폐기 — 이제 sprites.h의 SPR_INTRO_OAK 사용

const wchar_t* Game::DEX_LINES[5] = {
    L"아, 잠깐! 줄 것이 있어!",
    L"이것이 포켓덱스! 만난 포켓몬을 자동으로 기록해줘.",
    L"그리고 몬스터볼도 5개 줄게.",
    L"야생 포켓몬은 약하게 만든 뒤 몬스터볼을 던져봐!",
    nullptr
};

// pokered _OaksLabRivalIllTakeYouOnText 기반 — 스타터 받고 출구 향할 때 블루가 가로막음
const wchar_t* Game::RIVAL_INTERCEPT_LINES[Game::RIVAL_INTERCEPT_COUNT] = {
    L"블루: 잠깐, 거기!",
    L"블루: 우리 포켓몬 한 번 비교해보자!",
    L"블루: 너에게 골라줬으니, 내가 이길 거야!",
    L"블루: 자, 한 판 붙어보자!",
};

const wchar_t* Game::LAB_INTRO_LINES[Game::LAB_INTRO_COUNT] = {
    // ── 인터셉트 (풀숲에서, 오박사 풀바디) ──
    L"오박사: 잠깐! 거긴 위험해!",
    L"오박사: 키 큰 풀숲엔 야생 포켓몬이 잔뜩 있단다.",
    L"오박사: 자기 포켓몬도 없이 가면 큰일 나!",
    L"오박사: 자, 같이 내 연구소로 가자.",
    // ── 전환 (검정 화면) ──
    L"...... 오박사를 따라 연구소로 갔다 ......",
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
        render();          // buffer 기반 배경/박스
        renderKorean();    // 타일/스프라이트(printRaw → 버퍼) + 한국어 텍스트(printW 직접)
        renderer.flush();  // 모든 버퍼 컨텐츠 diff 출력 — 마지막에 1번만

        DWORD elapsed = GetTickCount() - start;
        if (elapsed < (DWORD)FRAME_MS)
            Sleep(FRAME_MS - elapsed);
        frame_++;
    }
}

void Game::changeScene(Scene next) {
    scene_ = next;
    frame_ = 0;
    // 잔여 텍스트(printW 직접 출력) 정리 + 강제 재렌더
    renderer.redrawAll();
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
    case Scene::RIVAL_INTERCEPT: updateRivalIntercept(key);break;
    case Scene::RECEIVE_DEX:    updateReceiveDex(key);     break;
    case Scene::OVERWORLD: {
        if (!ow_) break;
        // Ctrl+M → 디버그 워프 메뉴
        if (key == Key::WARP_MENU) {
            changeScene(Scene::WARP_MENU);
            warpCursor_ = 0;
            break;
        }
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
                int pSize = tr.partyIds[2] ? 3 : (tr.partyIds[1] ? 2 : 1);
                battle_->startTrainer(tr.name, tr.preBattleText,
                    tr.partyIds, tr.partyLevels, pSize, tr.introSpriteId);
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
        case OwEvent::NURSE_HEAL:
            // 간호사 조이 대화 끝 → 조용히 회복 (대화 자체가 회복 안내)
            healAll(player_);
            break;
        case OwEvent::ENTER_MART:
            if (!player_.deliveredParcel) {
                changeScene(Scene::MART_EVENT);
                martStep_ = 0;
            }
            break;
        case OwEvent::OAK_INTERCEPT:
            // 풀숲에서 오박사 등장 — OW cutscene으로 (별도 Scene 안 거침)
            if (ow_) ow_->startOakIntercept();
            break;
        case OwEvent::CUTSCENE_END_OAK:
            // 오박사 cutscene 끝 → 연구소 OW로 워프
            player_.mapId = MAP_OAK_LAB;
            player_.x = 5; player_.y = 10;   // 정문 안쪽
            player_.dir = 1;                  // 위 (오박사 향함)
            if (ow_) ow_->init();             // OW 재초기화
            break;
        case OwEvent::STARTER_TRIGGER:
            // 연구소 OW에서 오박사 NPC 대화 끝 → 스타터 선택 화면
            starterCursor_ = 0;
            changeScene(Scene::STARTER_SELECT);
            break;
        case OwEvent::CUTSCENE_END_RIVAL: {
            // 라이벌 cutscene 끝 → 라이벌 배틀
            int rivalStarterIdx = (starterCursor_ + 1) % 3;
            int rivalIds[]  = {STARTER_IDS[rivalStarterIdx], 0, 0};
            int rivalLvls[] = {5, 0, 0};
            if (!battle_) battle_ = new Battle(renderer, player_);
            battle_->startTrainer(player_.rivalName, L"라이벌과의 첫 승부!",
                                  rivalIds, rivalLvls, 1);
            changeScene(Scene::TRAINER_BATTLE);
            break;
        }
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
    case Scene::WARP_MENU:
        updateWarpMenu(key);
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
    case Scene::RIVAL_INTERCEPT:    renderRivalIntercept(); break;
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
    case Scene::RIVAL_INTERCEPT:    renderRivalInterceptKorean(); break;
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
    case Scene::WARP_MENU:      renderWarpMenuKorean();       break;
    default: break;
    }
}

// ─── 디버그 워프 메뉴 (Ctrl+M) ────────────────────────────
struct WarpMenuEntry { int mapId; int x; int y; const wchar_t* name; };
static const WarpMenuEntry WARP_MAPS[] = {
    {0,  10, 10, L"팔레트시티"},
    {1,  10, 30, L"1번도로"},
    {2,  20, 30, L"상록시티"},
    {3,  10, 60, L"2번도로"},
    {4,  16, 40, L"상록숲"},
    {5,  20, 30, L"회색시티"},
    {6,  5,  10, L"오박사 연구소"},
    {7,  3,  6,  L"주인공의 집 1F"},
    {8,  4,  10, L"회색시티 체육관"},
    {9,  3,  6,  L"주인공의 방 2F"},
    {10, 3,  6,  L"라이벌의 집"},
    {11, 3,  7,  L"상록 포센"},
    {12, 3,  7,  L"펠터 포센"},
    {13, 3,  7,  L"상록 마트"},
    {14, 3,  7,  L"펠터 마트"},
    {15, 4,  7,  L"2번도로 게이트"},
    {16, 4,  7,  L"상록숲 북쪽 게이트"},
    {17, 4,  7,  L"상록숲 남쪽 게이트"},
};
static const int WARP_MAPS_COUNT = sizeof(WARP_MAPS) / sizeof(WARP_MAPS[0]);

void Game::updateWarpMenu(Key key) {
    if (key == Key::UP) {
        warpCursor_ = (warpCursor_ - 1 + WARP_MAPS_COUNT) % WARP_MAPS_COUNT;
    } else if (key == Key::DOWN) {
        warpCursor_ = (warpCursor_ + 1) % WARP_MAPS_COUNT;
    } else if (key == Key::A) {
        const WarpMenuEntry& w = WARP_MAPS[warpCursor_];
        player_.mapId = w.mapId;
        player_.x = w.x;
        player_.y = w.y;

        // 디버그 편의: 풀베기 가능한 포켓몬 없으면 더미 1마리 자동 지급
        // (이미 풀베기를 아는 포켓몬이 파티에 있으면 추가 안 함)
        if (!playerHasCut(player_) && player_.partySize < 6) {
            Pokemon dummy = makePokemon(1, 10);  // 이상해씨 L10 (풀 타입 → 풀베기 자연스러움)
            if (dummy.species) {
                // 첫 번째 기술 슬롯을 풀베기로 강제 교체
                dummy.moves[0].moveId = MOVE_CUT;
                dummy.moves[0].pp     = getMoveData(MOVE_CUT).maxPP;
                if (dummy.numMoves < 1) dummy.numMoves = 1;
                player_.party[player_.partySize++] = dummy;
            }
        }

        if (ow_) ow_->init();
        changeScene(Scene::OVERWORLD);
    } else if (key == Key::B || key == Key::ESCAPE) {
        changeScene(Scene::OVERWORLD);
    }
}

void Game::renderWarpMenuKorean() {
    int W = renderer.width, H = renderer.height;
    renderer.fillRect(0, 0, W, H, ' ', std::string(Color::BG_BLACK)+Color::WHITE);
    renderer.printW(2, 1, L"=== 디버그 워프 메뉴 (Ctrl+M) ===",
        std::string(Color::BG_BLACK)+Color::BRIGHT_CYAN);
    renderer.printW(2, 2, L"위/아래: 선택  A(Z/Enter): 확인  B/ESC: 취소",
        std::string(Color::BG_BLACK)+Color::WHITE);
    for (int i = 0; i < WARP_MAPS_COUNT; i++) {
        const wchar_t* arrow = (i == warpCursor_) ? L"▶ " : L"  ";
        wchar_t buf[80];
        swprintf(buf, 80, L"%lsMAP_%d  %ls", arrow, WARP_MAPS[i].mapId, WARP_MAPS[i].name);
        std::string color = (i == warpCursor_)
            ? std::string(Color::BG_BLACK)+Color::BRIGHT_YELLOW
            : std::string(Color::BG_BLACK)+Color::WHITE;
        renderer.printW(2, 4 + i, buf, color);
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
    // step 11 (마지막): → OVERWORLD (주인공 집 2층 침실에서 깨어남 — 원작과 동일)
    if (introStep_ == 11) {
        player_.mapId = MAP_PLAYER_HOUSE2;
        player_.x = 4; player_.y = 4;   // 침대 옆 floor
        player_.dir = 0;
        player_.justWokeUp = false;     // 깨어나는 대사 X — pokered 원작처럼 그냥 등장
        if (!ow_) ow_ = new Overworld(renderer, player_);
        ow_->init();
        changeScene(Scene::OVERWORLD);
        return;
    }

    introStep_++;
    // 단계 바뀔 때 화면 새로 그리기 — 이전 스프라이트/텍스트 잔영 제거
    renderer.redrawAll();
}

// 인트로 트레이너 풀바디 스프라이트 출력 (28chars × 9rows)
static void drawIntroSprite(Renderer& r, const IntroSprite& spr, int x, int y) {
    for (int i = 0; i < INTRO_SPR_H; i++) {
        if (spr.rows[i]) r.printRaw(x, y + i, spr.rows[i]);
    }
}

void Game::renderIntro() {
    int W = renderer.width, H = renderer.height;
    renderer.fillRect(0,0,W,H,' ', std::string(Color::BG_BLACK)+Color::BLACK);

    int cx = W/2;
    int bH=6, bY=H-bH-1, bW=W-4, bX=2;     // 대화창 위치
    int spriteAreaH = bY;                    // 그림 영역(대화창 위) 세로

    // 스프라이트는 대화창 바로 위에 위치 (하단 5행은 대화창 뒤로 가려짐)
    int sprY = bY - INTRO_SPR_H + 5;
    if (sprY < 1) sprY = 1;
    int halfW = INTRO_SPR_W / 2;

    // ── 스프라이트 (단계 3+) ────────────────────────────────────
    if (introStep_ >= 3 && introStep_ <= 4) {
        drawIntroSprite(renderer, SPR_INTRO_OAK, cx - halfW, sprY);
    } else if (introStep_ == 5) {
        drawIntroSprite(renderer, SPR_INTRO_RED, cx - halfW, sprY);
    } else if (introStep_ >= 6 && introStep_ <= 9) {
        drawIntroSprite(renderer, SPR_INTRO_RIVAL, cx - halfW, sprY);
    } else if (introStep_ >= 10) {
        drawIntroSprite(renderer, SPR_INTRO_RED, cx - halfW, sprY);
    }

    // ── 타이틀 화면 (단계 0~2): 큰 블록 글자 ─────────────────────
    if (introStep_ <= 2) {
        // POKEMON (7글자) — 각 글자 6 wide, 5 rows
        static const char* T_POKEMON[5] = {
            "#####  ####  ## ##  #####  ##   ##  ####  ##  ##",
            "##  ## ## ## ####   ##     ## # ## ##  ## ### ##",
            "#####  ## ## ###    ###    ##   ## ##  ## ######",
            "##     ## ## ## ##  ##     ##   ## ##  ## ## ###",
            "##      ####  ##  ## #####  ##   ##  ####  ##  ##",
        };
        // RED (3글자)
        static const char* T_RED[5] = {
            "#####  #####  #####",
            "##  ## ##     ##  ##",
            "#####  ###    ##  ##",
            "## ##  ##     ##  ##",
            "##  ## #####  #####",
        };
        int titleW = 49;       // POKEMON 줄 길이
        int titleH = 5 + 1 + 5; // 11 rows
        int titleY = (spriteAreaH - titleH) / 2;
        if (titleY < 1) titleY = 1;
        // POKEMON
        for (int i = 0; i < 5; i++)
            renderer.print(cx - titleW/2, titleY + i, T_POKEMON[i],
                std::string(Color::BG_BLACK)+Color::BRIGHT_RED);
        // RED (가운데 정렬, 한 줄 띄움)
        int redW = 19;
        for (int i = 0; i < 5; i++)
            renderer.print(cx - redW/2, titleY + 6 + i, T_RED[i],
                std::string(Color::BG_BLACK)+Color::BRIGHT_RED);
    }

    // ── 대화창 ───────────────────────────────────────────────────
    renderer.drawBox(bX,bY,bW,bH, std::string(Color::BG_BLACK)+Color::WHITE);
    renderer.fillRect(bX+1,bY+1,bW-2,bH-2,' ',std::string(Color::BG_BLACK)+Color::WHITE);
    const char* speaker = "[OAK]";
    renderer.print(bX+2,bY+1,speaker,
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

    // (스프라이트는 renderIntro에서 그려짐 — 대화창 뒤 레이어)

    // ── 대화창 텍스트 ─────────────────────────────────────────
    // step 5: 이름 확인 (pokered _YourNameIsText: "Right! So your name is X!")
    if (introStep_ == 5) {
        wchar_t buf[64];
        swprintf(buf, 64, L"그래! 네 이름은 %ls구나!", player_.name);
        renderer.printW(bX+2, bY+3, buf, std::string(Color::BG_BLACK)+Color::BRIGHT_WHITE);
        return;
    }
    // step 9: 라이벌 이름 확인 (pokered _HisNameIsText: "That's right! I remember now! His name is X!")
    if (introStep_ == 9) {
        wchar_t buf[64];
        swprintf(buf, 64, L"맞다! 이제 생각났어. 이 녀석 이름은 %ls!", player_.rivalName);
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
    renderer.printW(bX+2, bY+2, L"네 이름을 알려주겠니? (최대 8자)",
        std::string(Color::BG_BLACK)+Color::BRIGHT_WHITE);
    // 현재 입력 표시
    wchar_t disp[64] = L"> ";
    for (int i = 0; i < nameLen_; i++) disp[2+i] = (wchar_t)(unsigned char)nameBuf_[i];
    disp[2+nameLen_] = 0;
    if ((frame_/8)%2==0) { disp[2+nameLen_]='_'; disp[3+nameLen_]=0; }
    renderer.printW(bX+2, bY+4, disp, std::string(Color::BG_BLACK)+Color::BRIGHT_CYAN);
    renderer.printW(bX+2, bY+6, L"[ Enter: 확인 / 비우면 RED ]",
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
    renderer.printW(bX+2, bY+2, L"라이벌의 이름은 뭘로 할까? (최대 8자)",
        std::string(Color::BG_BLACK)+Color::BRIGHT_WHITE);
    wchar_t disp[64] = L"> ";
    for (int i = 0; i < rivalNameLen_; i++)
        disp[2+i] = (wchar_t)(unsigned char)rivalNameBuf_[i];
    disp[2+rivalNameLen_] = 0;
    if ((frame_/8)%2==0) { disp[2+rivalNameLen_]='_'; disp[3+rivalNameLen_]=0; }
    renderer.printW(bX+2, bY+4, disp, std::string(Color::BG_BLACK)+Color::BRIGHT_CYAN);
    renderer.printW(bX+2, bY+6, L"[ Enter: 확인 / 비우면 블루 ]",
        std::string(Color::BG_BLACK)+Color::BRIGHT_BLACK);
}

// ─── 스타터 선택 ─────────────────────────────────────────────
// (STARTER_IDS는 파일 상단으로 이동)
static const wchar_t* STARTER_NAMES[3] = {L"이상해씨", L"파이리", L"꼬부기"};
static const wchar_t* STARTER_TYPES[3] = {L"풀/독", L"불꽃", L"물"};

void Game::updateStarterSelect(Key key) {
    if (key == Key::LEFT)  starterCursor_ = (starterCursor_+2)%3;
    if (key == Key::RIGHT) starterCursor_ = (starterCursor_+1)%3;
    if (key == Key::A) {
        // 스타터 지급
        player_.party[0] = makePokemon(STARTER_IDS[starterCursor_], 5);
        player_.partySize = 1;
        // 원작: 스타터 받고 출구 향하면 블루가 가로막아 배틀.
        // 우리는 연구소 OW로 돌아간 뒤 즉시 RIVAL_BLOCK cutscene을 실행.
        if (!ow_) ow_ = new Overworld(renderer, player_);
        ow_->init();
        ow_->startRivalBlock();
        changeScene(Scene::OVERWORLD);
    }
}

// ─── 라이벌 인터셉트 ─────────────────────────────────────────
void Game::updateRivalIntercept(Key key) {
    if (key != Key::A) return;
    rivalInterceptStep_++;
    if (rivalInterceptStep_ >= RIVAL_INTERCEPT_COUNT) {
        // 블루는 상성 포켓몬 선택 (원작 동일)
        int rivalStarterIdx = (starterCursor_ + 1) % 3;
        int rivalIds[]  = {STARTER_IDS[rivalStarterIdx], 0, 0};
        int rivalLvls[] = {5, 0, 0};
        if (!battle_) battle_ = new Battle(renderer, player_);
        battle_->startTrainer(player_.rivalName, L"라이벌과의 첫 승부!",
                              rivalIds, rivalLvls, 1);
        changeScene(Scene::TRAINER_BATTLE);
    }
}

void Game::renderRivalIntercept() {
    int W = renderer.width, H = renderer.height;
    renderer.fillRect(0,0,W,H,' ', std::string(Color::BG_BLACK)+Color::BLACK);
    int cx = W/2;
    int bH=6, bY=H-bH-1, bW=W-4, bX=2;
    int sprY = bY - INTRO_SPR_H + 5;
    if (sprY < 1) sprY = 1;
    int halfW = INTRO_SPR_W / 2;
    // 블루 풀바디
    for (int i = 0; i < INTRO_SPR_H; i++)
        if (SPR_INTRO_RIVAL.rows[i])
            renderer.printRaw(cx - halfW, sprY + i, SPR_INTRO_RIVAL.rows[i]);
    // 대화창
    renderer.drawBox(bX,bY,bW,bH, std::string(Color::BG_BLACK)+Color::WHITE);
    renderer.fillRect(bX+1,bY+1,bW-2,bH-2,' ',std::string(Color::BG_BLACK)+Color::WHITE);
    renderer.print(bX+2,bY+1,"[BLUE]", std::string(Color::BG_BLACK)+Color::BRIGHT_CYAN);
    if ((frame_/8)%2==0)
        renderer.print(bX+bW-4,bY+bH-2," v ", std::string(Color::BG_BLACK)+Color::BRIGHT_WHITE);
}

void Game::renderRivalInterceptKorean() {
    int H = renderer.height;
    int bH=6, bY=H-bH-1, bX=2;
    if (rivalInterceptStep_ < RIVAL_INTERCEPT_COUNT)
        renderer.printW(bX+2, bY+3, RIVAL_INTERCEPT_LINES[rivalInterceptStep_],
            std::string(Color::BG_BLACK)+Color::BRIGHT_WHITE);
    renderer.printW(bX+2, bY+bH-2, L"[ Z: 계속 ]",
        std::string(Color::BG_BLACK)+Color::BRIGHT_BLACK);
}

void Game::renderStarterSelect() {
    int W = renderer.width, H = renderer.height;
    renderer.fillRect(0,0,W,H,' ', std::string(Color::BG_BLACK)+Color::BLACK);
    // sprite 사이즈는 종족별로 동일 (40×20). 3개를 가로로 배치.
    // 헤더 텍스트(상단 4줄)가 sprite 박스와 겹치지 않게 tableY를 row 7로 fix.
    int slotW = W / 3;
    int tableY = 7;
    for (int i = 0; i < 3; i++) {
        const SpriteData* spr = getSpriteFront(STARTER_IDS[i]);
        int sw = spr ? spr->width  : SPR_W;
        int sh = spr ? spr->height : SPR_H;
        int slotCx = slotW * i + slotW / 2;
        int cx = slotCx - sw / 2;
        if (cx < 1) cx = 1;
        std::string bord = (i == starterCursor_) ?
            std::string(Color::BG_BLACK)+Color::BRIGHT_YELLOW :
            std::string(Color::BG_BLACK)+Color::BRIGHT_BLACK;
        renderer.drawBox(cx-1, tableY-1, sw+2, sh+3, bord);
        if (spr) {
            for (int r = 0; r < sh; r++) {
                if (spr->rows[r])
                    renderer.printRaw(cx, tableY+r, spr->rows[r]);
            }
        }
        if (i == starterCursor_)
            renderer.print(slotCx - 1, tableY+sh+1, "^^^",
                std::string(Color::BG_BLACK)+Color::BRIGHT_YELLOW);
    }
}

void Game::renderStarterSelectKorean() {
    int W = renderer.width;
    int slotW = W / 3;
    int tableY = 7;  // renderStarterSelect와 일치
    // 헤더는 화면 상단 row 1~4 고정 (sprite 박스와 겹치지 않게)
    renderer.printW(W/2-10, 1, L"[ 오박사 연구소 ]",
        std::string(Color::BG_BLACK)+Color::BRIGHT_YELLOW);
    renderer.printW(W/2-12, 2, L"블루가 지켜보고 있다...",
        std::string(Color::BG_BLACK)+Color::BRIGHT_CYAN);
    renderer.printW(W/2-8, 3, L"포켓몬을 선택하세요!",
        std::string(Color::BG_BLACK)+Color::BRIGHT_WHITE);
    renderer.printW(W/2-14, 4, L"← → 방향키로 선택, Z로 결정",
        std::string(Color::BG_BLACK)+Color::BRIGHT_BLACK);
    // 이름/타입 — sprite 박스 아래 (tableY + SPR_H + 여백)
    int nameY = tableY + SPR_H + 3;
    for (int i = 0; i < 3; i++) {
        int slotCx = slotW * i + slotW / 2;
        int cx = slotCx - 4;
        std::string col = (i==starterCursor_) ?
            std::string(Color::BG_BLACK)+Color::BRIGHT_YELLOW :
            std::string(Color::BG_BLACK)+Color::WHITE;
        renderer.printW(cx, nameY, STARTER_NAMES[i], col);
        renderer.printW(cx, nameY+1, STARTER_TYPES[i],
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

// ─── 오박사 인터셉트 시퀀스 ──────────────────────────────────
// 풀숲에서 5단계 대사 후 → 연구소 OVERWORLD 진입.
// 연구소 안에서 플레이어가 오박사 NPC와 직접 상호작용 → STARTER_SELECT.
void Game::updateLabIntro(Key key) {
    if (key != Key::A) return;
    labIntroStep_++;
    if (labIntroStep_ >= LAB_INTRO_COUNT) {
        // 연구소 OW 진입 — 입구(5,11)에 도착, 위쪽 오박사를 향해 걷도록
        player_.mapId = MAP_OAK_LAB;
        player_.x = 5; player_.y = 10;  // 문 바로 안쪽
        player_.dir = 1;                  // 위 방향
        if (!ow_) ow_ = new Overworld(renderer, player_);
        ow_->init();
        changeScene(Scene::OVERWORLD);
    }
}

void Game::renderLabIntro() {
    int W = renderer.width, H = renderer.height;
    renderer.fillRect(0,0,W,H,' ', std::string(Color::BG_BLACK)+Color::BLACK);

    int cx = W/2;
    int bH=6, bY=H-bH-1, bW=W-4, bX=2;
    int sprY = bY - INTRO_SPR_H + 5;
    if (sprY < 1) sprY = 1;
    int halfW = INTRO_SPR_W / 2;

    // 단계별 화면:
    //   0~3: 풀숲 인터셉트 — 오박사 풀바디
    //   4:   화면 전환 (검정) — "...오박사를 따라 연구소로 갔다..."
    if (labIntroStep_ <= 3) {
        for (int i = 0; i < INTRO_SPR_H; i++)
            if (SPR_INTRO_OAK.rows[i])
                renderer.printRaw(cx - halfW, sprY + i, SPR_INTRO_OAK.rows[i]);
    }
    // step 4는 빈 화면 (전환 효과)

    // 대화창
    renderer.drawBox(bX,bY,bW,bH, std::string(Color::BG_BLACK)+Color::WHITE);
    renderer.fillRect(bX+1,bY+1,bW-2,bH-2,' ',std::string(Color::BG_BLACK)+Color::WHITE);
    const char* speaker = (labIntroStep_ == 4) ? "[ — ]" : "[OAK]";
    const char* speakerCol = (labIntroStep_ == 4) ?
        Color::BRIGHT_BLACK : Color::BRIGHT_YELLOW;
    renderer.print(bX+2,bY+1,speaker, std::string(Color::BG_BLACK)+speakerCol);
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
