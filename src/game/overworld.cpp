#include "overworld.h"
#include <cstdlib>
#include <cstring>
#include <wchar.h>
#ifndef swprintf
#define swprintf _snwprintf
#endif

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

// ─── 타일 렌더 ───────────────────────────────────────────────
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

    // 맵 경계 이탈 → 인접 맵 이동
    if (ny < 0 && m->northMap >= 0) {
        MapDef* nm = getMap(m->northMap);
        if (nm) {
            state_.mapId = m->northMap;
            state_.px = m->northEntryX;
            state_.py = MAP_H - 1;
            pl_.mapId = state_.mapId;
            pl_.x = state_.px; pl_.y = state_.py;
        }
        return;
    }
    if (ny >= MAP_H && m->southMap >= 0) {
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
            state_.mapId = w.destMap;
            state_.px    = w.destX;
            state_.py    = w.destY;
            pl_.mapId    = state_.mapId;
            pl_.x        = state_.px;
            pl_.y        = state_.py;
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
            state_.pendingEvent = OwEvent::TRAINER_BATTLE;
            state_.eventData = i;
            // 체육관 보스는 BOSS_BATTLE
            if (state_.mapId == MAP_PEWTER_GYM && i >= 2) {
                state_.pendingEvent = OwEvent::BOSS_BATTLE;
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
void Overworld::render() {
    int W = ren_.width;
    int H = ren_.height;

    // 뷰포트: 맵 영역은 H-6 행, 대화창 6행
    int viewH = H - 6;
    int viewW = W;

    // 카메라: 플레이어 중심
    int camX = state_.px - viewW / 2;
    int camY = state_.py - viewH / 2;

    MapDef* m = curMap();

    // 맵 타일 렌더
    for (int sy = 0; sy < viewH; sy++) {
        for (int sx = 0; sx < viewW; sx++) {
            int mx = camX + sx;
            int my = camY + sy;
            if (m && mx >= 0 && mx < MAP_W && my >= 0 && my < MAP_H) {
                char t = getTile(m, mx, my);
                // NPC/트레이너 위치 표시
                bool hasNpc = false;
                for (int i = 0; i < m->numNpcs; i++)
                    if (m->npcs[i].x == mx && m->npcs[i].y == my) { hasNpc = true; break; }
                bool hasTr = false;
                for (int i = 0; i < m->numTrainers; i++)
                    if (!m->trainers[i].defeated &&
                        m->trainers[i].x == mx && m->trainers[i].y == my) { hasTr = true; break; }

                if (mx == state_.px && my == state_.py) {
                    // 플레이어
                    ren_.setCell(sx, sy, '@',
                        std::string(Color::BG_BLACK) + Color::BRIGHT_YELLOW);
                } else if (hasNpc) {
                    ren_.setCell(sx, sy, 'N',
                        std::string(Color::BG_BLACK) + Color::BRIGHT_CYAN);
                } else if (hasTr) {
                    ren_.setCell(sx, sy, 'T',
                        std::string(Color::BG_BLACK) + Color::BRIGHT_RED);
                } else {
                    drawTile(sx, sy, t);
                }
            } else {
                ren_.setCell(sx, sy, ' ', std::string(Color::BG_BLACK) + Color::BLACK);
            }
        }
    }

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

    // 맵 이름 (왼쪽 상단)
    if (m && m->nameW)
        ren_.printW(1, 0, m->nameW,
            std::string(Color::BG_BLACK) + Color::BRIGHT_WHITE);

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
