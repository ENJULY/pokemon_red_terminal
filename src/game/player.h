#pragma once
#include "../data/pokemon_data.h"
#include <cstring>

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
    int  atkStage, defStage, speStage, accStage; // 스테이지 -6~+6
};

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

    bool beatenRival1;
    bool gotParcel;       // 상록 마트에서 소포 수령
    bool deliveredParcel; // 오박사에게 소포 전달
    bool beatenGymTrainer1;
    bool beatenGymTrainer2;
    bool beatenBrock;
    int  starterIdx;      // 선택한 스타터 인덱스 (0=이상해씨 1=파이리 2=꼬부기, -1=미선택)
    bool rivalLabTalked;  // beatenRival1 후 연구소 블루와 대화 완료

    int mapId;
    int x, y;
    int dir; // 0=아래 1=위 2=왼 3=오른

    bool viridianHealed;
    bool pewterHealed;
    bool justWokeUp;

    // 풀베기로 베어낸 나무 위치 — 게임 재시작 시 리셋
    static constexpr int MAX_CUT_TREES = 16;
    int  cutTreeMapId[MAX_CUT_TREES];
    int  cutTreeX[MAX_CUT_TREES];
    int  cutTreeY[MAX_CUT_TREES];
    int  cutTreeCount;
};

static constexpr int MOVE_CUT = 18;

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

inline bool isTreeCut(const Player& pl, int mapId, int x, int y) {
    for (int i = 0; i < pl.cutTreeCount; i++) {
        if (pl.cutTreeMapId[i] == mapId && pl.cutTreeX[i] == x && pl.cutTreeY[i] == y)
            return true;
    }
    return false;
}

inline bool addCutTree(Player& pl, int mapId, int x, int y) {
    if (pl.cutTreeCount >= Player::MAX_CUT_TREES) return false;
    pl.cutTreeMapId[pl.cutTreeCount] = mapId;
    pl.cutTreeX[pl.cutTreeCount] = x;
    pl.cutTreeY[pl.cutTreeCount] = y;
    pl.cutTreeCount++;
    return true;
}

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

    p.numMoves = 0;
    for (int i = 0; i < 4 && p.species->startMoves[i] != 0; i++) {
        int mid = p.species->startMoves[i];
        p.moves[p.numMoves].moveId = mid;
        p.moves[p.numMoves].pp     = getMoveData(mid).maxPP;
        p.numMoves++;
    }
    for (int i = 0; i < 8; i++) {
        const LearnMove& lm = p.species->learnset[i];
        if (lm.level == 0) break;
        if (lm.level <= level && p.numMoves < 4) {
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
