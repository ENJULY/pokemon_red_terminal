#include "overworld.h"
#include <cstdlib>
#include <cstring>
#include <wchar.h>
#include <cstdio>
#ifndef swprintf
#define swprintf _snwprintf
#endif

// ─── 주인공 ASCII 스프라이트 (방향별 2프레임) ──────────────────
// [방향 0~3][프레임 0~1][행 0~2], 3글자 너비
// 방향: 0=아래 1=위 2=왼 3=오른
#define ANSI_YEL "\x1b[1;33m"
#define ANSI_RST "\x1b[0m"
static const char* PLAYER_SPR[4][2][3] = {
    // 0: 아래 (플레이어를 향함)
    {{ANSI_YEL " o " ANSI_RST, ANSI_YEL"/|\\"ANSI_RST, ANSI_YEL"/ \\"ANSI_RST},
     {ANSI_YEL " o " ANSI_RST, ANSI_YEL"\\|/"ANSI_RST, ANSI_YEL" | "ANSI_RST}},
    // 1: 위 (등 보임)
    {{ANSI_YEL " o " ANSI_RST, ANSI_YEL"\\|/"ANSI_RST, ANSI_YEL"/ \\"ANSI_RST},
     {ANSI_YEL " o " ANSI_RST, ANSI_YEL"/|\\"ANSI_RST, ANSI_YEL" | "ANSI_RST}},
    // 2: 왼
    {{ANSI_YEL " o " ANSI_RST, ANSI_YEL"<| "ANSI_RST, ANSI_YEL"/ \\"ANSI_RST},
     {ANSI_YEL " o " ANSI_RST, ANSI_YEL"<- "ANSI_RST, ANSI_YEL" | "ANSI_RST}},
    // 3: 오른
    {{ANSI_YEL " o " ANSI_RST, ANSI_YEL" |>"ANSI_RST, ANSI_YEL"/ \\"ANSI_RST},
     {ANSI_YEL " o " ANSI_RST, ANSI_YEL" ->"ANSI_RST, ANSI_YEL" | "ANSI_RST}},
};

Overworld::Overworld(Renderer& r, Player& pl)
    : ren_(r), pl_(pl), state_{}
{}

MapDef* Overworld::curMap() const {
    return getMap(state_.mapId);
}

void Overworld::init() {
    state_ = {};
    state_.mapId = pl_.mapId;
    state_.px    = pl_.x;
    state_.py    = pl_.y;
    state_.dir   = pl_.dir;
    state_.pendingEvent = OwEvent::NONE;
}

OwEvent Overworld::popEvent() {
    OwEvent ev = state_.pendingEvent;
    state_.pendingEvent = OwEvent::NONE;
    return ev;
}

void Overworld::onReturnFromBattle(bool won) {
    // 패배 시 팔레트시티로 리셋
    if (!won) {
        state_.mapId = MAP_PALLET;
        state_.px = 11; state_.py = 7;
        pl_.mapId = state_.mapId;
        pl_.x = state_.px; pl_.y = state_.py;
        return;
    }
    pl_.mapId = state_.mapId;
    pl_.x = state_.px; pl_.y = state_.py;
}

void Overworld::onReturnFromCenter() {
    pl_.mapId = state_.mapId;
    pl_.x = state_.px; pl_.y = state_.py;
}

// ─── 타일 렌더 (1char 기반 버퍼 fallback - 더 이상 직접 호출 안 함) ─────
void Overworld::drawTile(int sx, int sy, char tile) {
    char ch;
    std::string col = Color::BG_BLACK;
    switch (tile) {
        case '#': ch = '#'; col += Color::BRIGHT_BLACK; break;
        case 'T': ch = 'Y'; col += Color::GREEN;        break;
        case '~': ch = '~'; col += Color::BRIGHT_BLUE;  break;
        case ',': ch = ','; col += Color::GREEN;         break;
        case ';': ch = '"'; col += Color::BRIGHT_GREEN;  break;
        case '.': ch = '.'; col += Color::WHITE;         break;
        case 'D': case 'L': case 'C': case 'M':
        case 'G': ch = '+'; col += Color::BRIGHT_YELLOW; break;
        case 's': ch = 's'; col += Color::BRIGHT_CYAN;   break;
        case 'N': ch = '!'; col += Color::BRIGHT_WHITE;  break;
        case 'H': ch = '['; col += Color::BRIGHT_BLACK;  break;
        case 'B': ch = 'B'; col += Color::BRIGHT_MAGENTA;break;
        default:  ch = ' '; col += Color::BLACK;         break;
    }
    ren_.setCell(sx, sy, ch, col);
}

// ─── 이동 처리 ───────────────────────────────────────────────
void Overworld::tryMove(int dx, int dy) {
    MapDef* m = curMap();
    if (!m) return;

    // 방향 설정
    if (dy > 0) state_.dir = 0;
    else if (dy < 0) state_.dir = 1;
    else if (dx < 0) state_.dir = 2;
    else if (dx > 0) state_.dir = 3;

    int nx = state_.px + dx;
    int ny = state_.py + dy;

    // 스타터 없는 상태에서 팔레트시티 북쪽 진입 → 오박사 저지
    if (ny < 0 && state_.mapId == MAP_PALLET && pl_.partySize == 0) {
        state_.pendingEvent = OwEvent::OAK_INTERCEPT;
        return;
    }

    // 맵 경계 이탈 → 인접 맵 이동
    if (ny < 0 && m->northMap >= 0) {
        MapDef* nm = getMap(m->northMap);
        if (nm) {
            state_.mapId = m->northMap;
            state_.px = m->northEntryX;
            state_.py = nm->mapH - 1;
            pl_.mapId = state_.mapId;
            pl_.x = state_.px; pl_.y = state_.py;
        }
        return;
    }
    if (ny >= m->mapH && m->southMap >= 0) {
        MapDef* sm = getMap(m->southMap);
        if (sm) {
            state_.mapId = m->southMap;
            state_.px = m->southEntryX;
            state_.py = 0;
            pl_.mapId = state_.mapId;
            pl_.x = state_.px; pl_.y = state_.py;
        }
        return;
    }

    char t = getTile(m, nx, ny);
    if (!tileWalkable(t)) {
        // 문 타일: 워프 체크
        if (t == 'D' || t == 'L' || t == 'C' || t == 'M' || t == 'G') {
            checkWarps(nx, ny);
        }
        return;
    }

    state_.px = nx;
    state_.py = ny;
    pl_.x = nx; pl_.y = ny;

    // 워프/특수 타일 체크
    checkWarps(nx, ny);
    checkSpecialTiles(nx, ny);
}

void Overworld::checkWarps(int x, int y) {
    MapDef* m = curMap();
    if (!m) return;
    char tile = getTile(m, x, y);

    // 포켓몬센터 문 → 치료 이벤트
    if (tile == 'C') {
        state_.pendingEvent = OwEvent::ENTER_POKEMON_CENTER;
        return;
    }
    // 상록시티 마트 문 → 소포 이벤트
    if (tile == 'M' && state_.mapId == MAP_VIRIDIAN && !pl_.deliveredParcel) {
        state_.pendingEvent = OwEvent::ENTER_MART;
        return;
    }

    for (int i = 0; i < m->numWarps; i++) {
        const WarpDef& w = m->warps[i];
        if (w.srcX == x && w.srcY == y) {
            MapDef* dest = getMap(w.destMap);
            if (!dest) return;
            state_.mapId = w.destMap;
            // destX/destY == -1 이면 목적지 맵의 southEntry (기본 진입점) 사용
            if (w.destX >= 0 && w.destY >= 0) {
                state_.px = w.destX;
                state_.py = w.destY;
            } else {
                state_.px = dest->southEntryX;
                state_.py = dest->southEntryY;
            }
            pl_.mapId = state_.mapId;
            pl_.x     = state_.px;
            pl_.y     = state_.py;
            return;
        }
    }
}

void Overworld::checkSpecialTiles(int x, int y) {
    MapDef* m = curMap();
    if (!m) return;

    // 긴 풀 인카운터
    if (tileIsEncounter(getTile(m, x, y)) && m->numEncounters > 0) {
        if (rand() % 100 < 15) { // 15% 확률
            triggerEncounter();
        }
    }

    // NPC 상호작용 체크는 update에서 A키로 처리
}

void Overworld::triggerEncounter() {
    MapDef* m = curMap();
    if (!m || m->numEncounters == 0) return;

    // 가중치 기반 랜덤 선택
    int total = 0;
    for (int i = 0; i < m->numEncounters; i++) total += m->encounters[i].weight;
    int r = rand() % total;
    int acc = 0;
    for (int i = 0; i < m->numEncounters; i++) {
        acc += m->encounters[i].weight;
        if (r < acc) {
            const EncounterEntry& e = m->encounters[i];
            state_.wildSpeciesId = e.speciesId;
            state_.wildLevel = e.minLevel + rand() % (e.maxLevel - e.minLevel + 1);
            state_.pendingEvent = OwEvent::WILD_ENCOUNTER;
            return;
        }
    }
}

bool Overworld::checkTrainerSight() {
    MapDef* m = curMap();
    if (!m) return false;
    for (int i = 0; i < m->numTrainers; i++) {
        TrainerDef& tr = const_cast<TrainerDef&>(m->trainers[i]);
        if (tr.defeated) continue;
        // 트레이너의 시야 방향 검사
        int dx = state_.px - tr.x;
        int dy = state_.py - tr.y;
        bool inSight = false;
        switch (tr.dir) {
            case 0: inSight = (dx == 0 && dy > 0 && dy <= tr.sightRange); break; // 아래
            case 1: inSight = (dx == 0 && dy < 0 && -dy <= tr.sightRange); break; // 위
            case 2: inSight = (dy == 0 && dx < 0 && -dx <= tr.sightRange); break; // 왼
            case 3: inSight = (dy == 0 && dx > 0 && dx <= tr.sightRange); break;  // 오른
        }
        if (inSight) {
            state_.eventData = i;
            // 체육관 트레이너 index 2 이상 = 브록(BOSS)
            if (state_.mapId == MAP_PEWTER_GYM && i >= 2) {
                state_.pendingEvent = OwEvent::BOSS_BATTLE;
            } else {
                state_.pendingEvent = OwEvent::TRAINER_BATTLE;
            }
            return true;
        }
    }
    return false;
}

// ─── 업데이트 ────────────────────────────────────────────────
void Overworld::update(Key key) {
    state_.frame++;

    // NPC 대화 중
    if (state_.dialog.active) {
        if (key == Key::A || key == Key::B) {
            state_.dialog.lineIdx++;
            const NpcDef* npc = state_.dialog.npc;
            if (state_.dialog.lineIdx >= 4 || !npc->lines[state_.dialog.lineIdx]) {
                state_.dialog.active = false;

                // 상록시티 첫 NPC (소포 배달) 이벤트
                if (state_.mapId == MAP_VIRIDIAN && npc->x == 3 && npc->y == 7) {
                    if (!pl_.deliveredParcel) {
                        // 마트에서 소포를 받아오게 트리거는 마트 진입 시 처리
                    }
                }
            }
        }
        return;
    }

    // 이동
    if (state_.pendingEvent != OwEvent::NONE) return;

    if (key == Key::UP)    tryMove(0, -1);
    if (key == Key::DOWN)  tryMove(0,  1);
    if (key == Key::LEFT)  tryMove(-1, 0);
    if (key == Key::RIGHT) tryMove(1,  0);

    if (state_.pendingEvent != OwEvent::NONE) return;

    // A키: 앞쪽 NPC/표지판과 상호작용
    if (key == Key::A) {
        int fx = state_.px, fy = state_.py;
        switch (state_.dir) {
            case 0: fy++; break;
            case 1: fy--; break;
            case 2: fx--; break;
            case 3: fx++; break;
        }
        MapDef* m = curMap();
        if (m) {
            for (int i = 0; i < m->numNpcs; i++) {
                if (m->npcs[i].x == fx && m->npcs[i].y == fy) {
                    state_.dialog.active = true;
                    state_.dialog.npc = &m->npcs[i];
                    state_.dialog.lineIdx = 0;
                    break;
                }
            }
            // 포켓몬센터 문에 A 누르면 치료
            char ft = getTile(m, fx, fy);
            if (ft == 'C') {
                state_.pendingEvent = OwEvent::ENTER_POKEMON_CENTER;
            }
            // 마트 문
            if (ft == 'M' && !pl_.deliveredParcel &&
                state_.mapId == MAP_VIRIDIAN) {
                state_.pendingEvent = OwEvent::ENTER_MART;
            }
        }
    }

    // 트레이너 시야 체크 (이동 후)
    if (state_.pendingEvent == OwEvent::NONE)
        checkTrainerSight();
}

// ─── 렌더 ────────────────────────────────────────────────────
// render(): 배경(검정)만 세팅. 실제 타일 아트는 renderKorean()에서 printRaw로 출력.
void Overworld::render() {
    int W = ren_.width;
    int H = ren_.height;
    int viewH = H - 6;

    // 맵 영역: 검정 배경
    ren_.fillRect(0, 0, W, viewH, ' ', std::string(Color::BG_BLACK) + Color::BLACK);

    // 대화창 박스 (하단 6행)
    int boxY = viewH;
    int boxH = 6;
    ren_.drawBox(0, boxY, W, boxH, std::string(Color::BG_BLACK) + Color::WHITE);
    ren_.fillRect(1, boxY+1, W-2, boxH-2, ' ', std::string(Color::BG_BLACK) + Color::WHITE);
}

void Overworld::renderKorean() {
    int W = ren_.width;
    int H = ren_.height;
    int viewH = H - 6;
    int boxY = viewH;

    MapDef* m = curMap();

    // ── 실제 타일 아트 렌더링 (pokered 스프라이트 기반) ──────────
    // 타일 1개 = 4chars 너비 × 2행 (TILE_COLS=2 blocks × 2chars each, TILE_ROWS=2)
    // 화면에 보이는 타일 수: tilesX = W/4, tilesY = viewH/2
    {
        int tilesX = W / 4;     // 가로 타일 수 (각 타일=4chars)
        int tilesY = viewH / 2; // 세로 타일 수 (각 타일=2행)
        int camX = state_.px - tilesX / 2;
        int camY = state_.py - tilesY / 2;

        for (int ty = 0; ty < tilesY; ty++) {
            for (int tx = 0; tx < tilesX; tx++) {
                int mx = camX + tx;
                int my = camY + ty;
                int sx = tx * 4;     // 터미널 X (각 타일 4chars)
                int sy = ty * 2;     // 터미널 Y (각 타일 2행)

                char tile_char = ' ';
                bool isNpc = false, isTr = false;

                if (m && mx >= 0 && mx < m->mapW && my >= 0 && my < m->mapH) {
                    tile_char = getTile(m, mx, my);
                    for (int i = 0; i < m->numNpcs; i++)
                        if (m->npcs[i].x == mx && m->npcs[i].y == my) { isNpc = true; break; }
                    for (int i = 0; i < m->numTrainers; i++)
                        if (!m->trainers[i].defeated &&
                            m->trainers[i].x == mx && m->trainers[i].y == my) { isTr = true; break; }
                }

                const TileArt* art = getTileArt(tile_char);

                // NPC/트레이너는 바탕 타일 위에 오버레이 (renderKorean에서 처리)
                for (int r = 0; r < TILE_ROWS && sy + r < viewH; r++) {
                    if (art->rows[r]) ren_.printRaw(sx, sy + r, art->rows[r]);
                }

                // NPC 오버레이: 밝은 심볼 (타일 위에 그림)
                if (isNpc) {
                    ren_.printRaw(sx, sy,     "\x1b[1;36m N  \x1b[0m");
                    ren_.printRaw(sx, sy + 1, "\x1b[1;36m/|\\ \x1b[0m");
                } else if (isTr) {
                    ren_.printRaw(sx, sy,     "\x1b[1;31m T  \x1b[0m");
                    ren_.printRaw(sx, sy + 1, "\x1b[1;31m/|\\ \x1b[0m");
                }
            }
        }
    }

    // 맵 이름 (왼쪽 상단)
    if (m && m->nameW)
        ren_.printW(1, 0, m->nameW,
            std::string(Color::BG_BLACK) + Color::BRIGHT_WHITE);

    // ── 주인공 스프라이트 (방향 + 걷기 애니메이션) ───────────────
    {
        int tilesX = W / 4;
        int tilesY = viewH / 2;
        int camX = state_.px - tilesX / 2;
        int camY = state_.py - tilesY / 2;
        // 타일 좌표 → 터미널 좌표 변환 (타일 1개 = 4chars × 2rows)
        int sx = (state_.px - camX) * 4;  // 터미널 X (4chars/tile)
        int sy = (state_.py - camY) * 2;  // 터미널 Y (2rows/tile)

        int dir = state_.dir;
        // 걷기 애니메이션 (frame 8단위 토글)
        int walkFrame = (state_.frame / 8) % 2;

        // 3행 스프라이트 (sy-1=머리, sy=몸통, sy+1=다리), 3칸 너비, 타일 내 좌측 정렬
        if (sy - 1 >= 0)
            ren_.printRaw(sx, sy - 1, PLAYER_SPR[dir][walkFrame][0]);
        ren_.printRaw(sx, sy,     PLAYER_SPR[dir][walkFrame][1]);
        if (sy + 1 < viewH)
            ren_.printRaw(sx, sy + 1, PLAYER_SPR[dir][walkFrame][2]);
    }

    // NPC 대화
    if (state_.dialog.active && state_.dialog.npc) {
        const NpcDef* npc = state_.dialog.npc;
        int li = state_.dialog.lineIdx;
        if (li < 4 && npc->lines[li]) {
            ren_.printW(2, boxY + 2, npc->lines[li],
                std::string(Color::BG_BLACK) + Color::BRIGHT_WHITE);
        }
        ren_.printW(2, boxY + 4, L"[ Z: 다음 ]",
            std::string(Color::BG_BLACK) + Color::BRIGHT_BLACK);
        return;
    }

    // 조작 안내
    ren_.printW(2, boxY + 2, L"방향키: 이동  Z: 상호작용",
        std::string(Color::BG_BLACK) + Color::BRIGHT_BLACK);

    // 파티 상태 (우측 상단)
    wchar_t buf[64];
    swprintf(buf, 64, L"파티: %d마리", pl_.partySize);
    ren_.printW(W - 12, 0, buf, std::string(Color::BG_BLACK) + Color::BRIGHT_CYAN);
    if (pl_.partySize > 0) {
        const Pokemon& lead = pl_.party[firstAlive(pl_) >= 0 ? firstAlive(pl_) : 0];
        if (lead.species) {
            swprintf(buf, 64, L"%ls Lv.%d HP:%d/%d",
                lead.species->name, lead.level, lead.currentHP, lead.maxHP);
            ren_.printW(W - 24, 1, buf, std::string(Color::BG_BLACK) + Color::CYAN);
        }
    }
}
