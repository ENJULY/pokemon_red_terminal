#pragma once

// ─── 타입 ────────────────────────────────────────────────────
enum class Type : int {
    NORMAL=0, FIRE, WATER, GRASS, ELECTRIC,
    BUG, ROCK, GROUND, POISON, FLYING,
    PSYCHIC, GHOST, NONE
};
static const int NUM_TYPES = 12;

// ─── 기술 데이터 ─────────────────────────────────────────────
// effect: 0=없음 1=상대ATK↓ 2=상대DEF↓ 3=상대SPE↓ 4=자신DEF↑ 5=상대명중↓
struct MoveData {
    int             id;
    const wchar_t*  name;
    Type            type;
    int             power;    // 0 = 변화기술
    int             accuracy; // 100 = 확중
    int             maxPP;
    int             effect;
};

inline constexpr MoveData MOVES[] = {
    { 0, L"",           Type::NORMAL,    0,   0,  0, 0}, // null slot
    // Gen1 실제 수치 기준
    { 1, L"몸통박치기",  Type::NORMAL,   35,  95, 35, 0}, // Tackle: 35pwr 95acc
    { 2, L"울음소리",    Type::NORMAL,    0, 100, 40, 1}, // Growl: ATK↓
    { 3, L"불씨",        Type::FIRE,     40, 100, 25, 0}, // Ember
    { 4, L"물총",        Type::WATER,    40, 100, 25, 0}, // Water Gun
    { 5, L"넝쿨채찍",    Type::GRASS,    35, 100, 10, 0}, // Vine Whip
    { 6, L"모래뿌리기",  Type::NORMAL,    0, 100, 15, 5}, // Sand Attack: ACC↓
    { 7, L"날개치기",    Type::FLYING,   35, 100, 35, 0}, // Wing Attack
    { 8, L"실뱉기",      Type::BUG,       0,  95, 40, 3}, // String Shot: SPE↓
    { 9, L"껍질굳히기",  Type::NORMAL,    0, 100, 30, 4}, // Harden/Withdraw/Defense Curl: DEF↑
    {10, L"돌던지기",    Type::ROCK,     50,  65, 15, 0}, // Rock Throw: 50pwr 65acc
    {11, L"칭칭감기",    Type::NORMAL,   15,  85, 20, 0}, // Bind: 85acc
    {12, L"새된소리",    Type::NORMAL,    0,  85, 40, 2}, // Screech: DEF↓↓ 85acc
    {13, L"전기충격",    Type::ELECTRIC, 40, 100, 30, 0}, // ThunderShock
    {14, L"꼬리치기",    Type::NORMAL,    0, 100, 30, 2}, // Tail Whip: DEF↓
    {15, L"할퀴기",      Type::NORMAL,   40, 100, 35, 0}, // Scratch
    {16, L"독침붕",      Type::POISON,   15, 100, 35, 0}, // Poison Sting
    {17, L"돌풍",        Type::NORMAL,   40, 100, 35, 0}, // Gust (Gen1: Normal타입)
};
inline constexpr int NUM_MOVES_DATA = 18;

inline const MoveData& getMoveData(int id) {
    if (id > 0 && id < NUM_MOVES_DATA) return MOVES[id];
    return MOVES[0];
}

// ─── 레벨업 기술 ─────────────────────────────────────────────
struct LearnMove { int level; int moveId; };

// ─── 포켓몬 종족값 ────────────────────────────────────────────
struct PokemonSpecies {
    int            id;
    const wchar_t* name;
    Type           type1, type2;
    int            baseHP, baseAtk, baseDef, baseSpe, baseSpc;
    int            baseExp;     // 기절 시 경험치 기준
    int            startMoves[4];  // 초기 기술 ID (0=없음)
    LearnMove      learnset[8];    // 레벨업 기술 ({0,0} 종료)
};

inline constexpr PokemonSpecies SPECIES[] = {
    // ── 스타터 ────────────────────────────────────────────────
    // 이상해씨: Lv7 씨뿌리기→ 여기선 넝쿨채찍으로 단순화, Lv13 넝쿨채찍
    { 1, L"이상해씨", Type::GRASS,    Type::POISON,  45,49,49,45,65, 64,
      {1,2,0,0}, {{7,5},{13,5},{0,0},{0,0},{0,0},{0,0},{0,0},{0,0}} },
    // 파이리: Scratch+Growl 시작, Lv9 불씨, Lv15 눈흘기기(꼬리치기로 대체)
    { 4, L"파이리",   Type::FIRE,     Type::NONE,    39,52,43,65,50, 62,
      {15,2,0,0}, {{9,3},{15,14},{0,0},{0,0},{0,0},{0,0},{0,0},{0,0}} },
    // 꼬부기: Tackle+꼬리치기 시작, Lv7 물총
    { 7, L"꼬부기",   Type::WATER,    Type::NONE,    44,48,65,43,50, 63,
      {1,14,0,0}, {{7,4},{13,9},{0,0},{0,0},{0,0},{0,0},{0,0},{0,0}} },
    // ── 야생 포켓몬 ───────────────────────────────────────────
    // 구구: 돌풍(Gust)으로 시작, Lv5 모래뿌리기, Lv9 날개치기
    {16, L"구구",     Type::NORMAL,   Type::FLYING,  40,45,40,56,35, 55,
      {17,0,0,0}, {{5,6},{9,7},{0,0},{0,0},{0,0},{0,0},{0,0},{0,0}} },
    // 꼬렛: Tackle+꼬리치기 시작
    {19, L"꼬렛",     Type::NORMAL,   Type::NONE,    30,56,35,72,25, 57,
      {1,14,0,0}, {{0,0},{0,0},{0,0},{0,0},{0,0},{0,0},{0,0},{0,0}} },
    // 캐터피: Tackle+실뱉기 시작
    {10, L"캐터피",   Type::BUG,      Type::NONE,    45,30,35,45,20, 53,
      {1,8,0,0}, {{0,0},{0,0},{0,0},{0,0},{0,0},{0,0},{0,0},{0,0}} },
    // 메타포드: 껍질굳히기만 사용 (HP45 Def55)
    {11, L"메타포드",  Type::BUG,      Type::NONE,    50,20,55,30,25, 72,
      {9,0,0,0}, {{0,0},{0,0},{0,0},{0,0},{0,0},{0,0},{0,0},{0,0}} },
    // 뿔충이: Speed 50 (원작 수치), 독침붕+실뱉기 시작
    {13, L"뿔충이",   Type::BUG,      Type::POISON,  40,35,30,50,20, 52,
      {16,8,0,0}, {{0,0},{0,0},{0,0},{0,0},{0,0},{0,0},{0,0},{0,0}} },
    // ── 브록 포켓몬 ───────────────────────────────────────────
    // 꼬마돌: Tackle만 시작, Lv11 돌던지기, Lv21 껍질굳히기
    {74, L"꼬마돌",   Type::ROCK,     Type::GROUND,  40,80,100,20,30, 86,
      {1,0,0,0}, {{11,10},{21,9},{0,0},{0,0},{0,0},{0,0},{0,0},{0,0}} },
    // 롱스톤: Tackle+새된소리 시작, Lv9 칭칭감기, Lv14 돌던지기
    {95, L"롱스톤",   Type::ROCK,     Type::GROUND,  35,45,160,70,30, 108,
      {1,12,0,0}, {{9,11},{14,10},{0,0},{0,0},{0,0},{0,0},{0,0},{0,0}} },
    // ── 라이벌 ────────────────────────────────────────────────
    // 피카츄: 전기충격+울음소리 시작 (원작과 동일)
    {25, L"피카츄",   Type::ELECTRIC, Type::NONE,    35,55,30,90,50, 82,
      {13,2,0,0}, {{0,0},{0,0},{0,0},{0,0},{0,0},{0,0},{0,0},{0,0}} },
};
inline constexpr int NUM_SPECIES_DATA = 11;

inline const PokemonSpecies* getSpecies(int id) {
    for (int i = 0; i < NUM_SPECIES_DATA; i++)
        if (SPECIES[i].id == id) return &SPECIES[i];
    return nullptr;
}

// Gen1 스탯 공식 (간략화: IV/EV 없음)
inline int calcHP(int base, int level) {
    return (2 * base * level / 100) + level + 10;
}
inline int calcStat(int base, int level) {
    int v = (2 * base * level / 100) + 5;
    return v > 0 ? v : 1;
}

// Medium Fast 경험치 공식
inline int expForLevel(int level) {
    return level * level * level;
}
