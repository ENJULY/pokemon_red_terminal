#pragma once
#include "../data/pokemon_data.h"
#include <cstring>

// ─── 포켓몬 인스턴스 ─────────────────────────────────────────
struct PokemonMove {
    int moveId;
    int pp;
};

struct Pokemon {
    const PokemonSpecies* species; // nullptr = 빈 슬롯
    int  level;
    int  currentHP, maxHP;
    int  atk, def, spe, spc;
    PokemonMove moves[4];
    int  numMoves;
    int  exp;
    // 스테이터스 변화 스테이지 (-6 ~ +6)
    int  atkStage, defStage, speStage, accStage;
};

// ─── 플레이어 ────────────────────────────────────────────────
struct Player {
    wchar_t name[16];
    wchar_t rivalName[16];
    Pokemon party[6];
    int     partySize;

    // 아이템
    int pokeballs;
    int superballs;
    int potions;
    int superpotions;
    bool hasPokedex;

    // 스토리 플래그
    bool beatenRival1;       // 연구소 라이벌 배틀
    bool deliveredParcel;    // 상록시티 심부름
    bool beatenGymTrainer1;  // 체육관 트레이너1
    bool beatenGymTrainer2;  // 체육관 트레이너2
    bool beatenBrock;        // 브록 클리어

    // 위치
    int mapId;
    int x, y;
    int dir; // 0=아래 1=위 2=왼 3=오른

    // 포켓몬센터 치료 여부 (상록시티 등)
    bool viridianHealed;
    bool pewterHealed;

    // 인트로 직후 침실에서 깨어나는 시퀀스 트리거
    bool justWokeUp;

    // 풀베기로 베어낸 나무 위치 기록 (최대 16개) — 게임 재시작 시 리셋됨
    static constexpr int MAX_CUT_TREES = 16;
    int  cutTreeMapId[MAX_CUT_TREES];
    int  cutTreeX[MAX_CUT_TREES];
    int  cutTreeY[MAX_CUT_TREES];
    int  cutTreeCount;
};

// 풀베기(Cut) move ID 상수
static constexpr int MOVE_CUT = 18;

// 파티에 풀베기를 아는 포켓몬이 있는지 확인
inline bool playerHasCut(const Player& pl) {
    for (int i = 0; i < pl.partySize; i++) {
        const Pokemon& p = pl.party[i];
        if (!p.species) continue;
        for (int j = 0; j < p.numMoves; j++) {
            if (p.moves[j].moveId == MOVE_CUT) return true;
        }
    }
    return false;
}

// (mapId, x, y) 위치의 풀베기 나무가 이미 베어졌는지 확인
inline bool isTreeCut(const Player& pl, int mapId, int x, int y) {
    for (int i = 0; i < pl.cutTreeCount; i++) {
        if (pl.cutTreeMapId[i] == mapId && pl.cutTreeX[i] == x && pl.cutTreeY[i] == y)
            return true;
    }
    return false;
}

// 베어낸 나무 위치 등록
inline bool addCutTree(Player& pl, int mapId, int x, int y) {
    if (pl.cutTreeCount >= Player::MAX_CUT_TREES) return false;
    pl.cutTreeMapId[pl.cutTreeCount] = mapId;
    pl.cutTreeX[pl.cutTreeCount] = x;
    pl.cutTreeY[pl.cutTreeCount] = y;
    pl.cutTreeCount++;
    return true;
}

// ─── 포켓몬 생성 ─────────────────────────────────────────────
inline void recalcStats(Pokemon& p) {
    if (!p.species) return;
    p.maxHP  = calcHP  (p.species->baseHP,  p.level);
    p.atk    = calcStat(p.species->baseAtk, p.level);
    p.def    = calcStat(p.species->baseDef, p.level);
    p.spe    = calcStat(p.species->baseSpe, p.level);
    p.spc    = calcStat(p.species->baseSpc, p.level);
}

inline Pokemon makePokemon(int speciesId, int level) {
    Pokemon p = {};
    p.species = getSpecies(speciesId);
    if (!p.species) return p;
    p.level   = level;
    recalcStats(p);
    p.currentHP = p.maxHP;
    p.exp = expForLevel(level);
    p.atkStage = p.defStage = p.speStage = p.accStage = 0;

    // 초기 기술
    p.numMoves = 0;
    for (int i = 0; i < 4 && p.species->startMoves[i] != 0; i++) {
        int mid = p.species->startMoves[i];
        p.moves[p.numMoves].moveId = mid;
        p.moves[p.numMoves].pp     = getMoveData(mid).maxPP;
        p.numMoves++;
    }
    // 레벨업 기술 적용
    for (int i = 0; i < 8; i++) {
        const LearnMove& lm = p.species->learnset[i];
        if (lm.level == 0) break;
        if (lm.level <= level && p.numMoves < 4) {
            // 이미 있으면 스킵
            bool exists = false;
            for (int j = 0; j < p.numMoves; j++)
                if (p.moves[j].moveId == lm.moveId) { exists = true; break; }
            if (!exists) {
                p.moves[p.numMoves].moveId = lm.moveId;
                p.moves[p.numMoves].pp     = getMoveData(lm.moveId).maxPP;
                p.numMoves++;
            }
        }
    }
    return p;
}

inline bool pokemonFainted(const Pokemon& p) {
    return p.species && p.currentHP <= 0;
}

inline bool allFainted(const Player& pl) {
    for (int i = 0; i < pl.partySize; i++)
        if (!pokemonFainted(pl.party[i])) return false;
    return pl.partySize > 0;
}

// 첫 번째 살아있는 포켓몬 인덱스
inline int firstAlive(const Player& pl) {
    for (int i = 0; i < pl.partySize; i++)
        if (!pokemonFainted(pl.party[i])) return i;
    return -1;
}

inline void healAll(Player& pl) {
    for (int i = 0; i < pl.partySize; i++) {
        Pokemon& p = pl.party[i];
        if (!p.species) continue;
        p.currentHP = p.maxHP;
        for (int j = 0; j < p.numMoves; j++)
            p.moves[j].pp = getMoveData(p.moves[j].moveId).maxPP;
        p.atkStage = p.defStage = p.speStage = p.accStage = 0;
    }
}
