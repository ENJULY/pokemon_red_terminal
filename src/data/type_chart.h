#pragma once
#include "pokemon_data.h"

// type_eff[공격타입][방어타입]
// 0=무효 5=비효과적(0.5x) 10=보통(1x) 20=효과적(2x)
// 타입 순서: NOR FIR WAT GRS ELC BUG ROK GRO POI FLY PSY GHO
static const int TYPE_EFF[12][12] = {
    //       NOR FIR WAT GRS ELC BUG ROK GRO POI FLY PSY GHO
    /*NOR*/{ 10, 10, 10, 10, 10, 10,  5, 10, 10, 10, 10,  0},
    /*FIR*/{ 10,  5,  5, 20, 10, 20,  5, 10, 10, 10, 10, 10},
    /*WAT*/{ 10, 20,  5,  5, 10, 10, 20, 20, 10, 10, 10, 10},
    /*GRS*/{ 10,  5, 20,  5, 10,  5, 20, 20,  5,  5, 10, 10},
    /*ELC*/{ 10, 10, 20,  5,  5, 10, 10,  0, 10, 20, 10, 10},
    /*BUG*/{ 10,  5, 10, 20, 10, 10,  5, 10, 20,  5, 20, 10},
    /*ROK*/{ 10, 20, 10, 10, 10, 20, 10,  5, 10, 20,  5, 10},
    /*GRO*/{ 10, 20, 10,  5, 20, 10, 20, 10, 20,  0, 10, 10},
    /*POI*/{ 10, 10, 10, 20, 10,  5, 20,  5,  5, 10, 10,  5},
    /*FLY*/{ 10, 10, 10, 20, 10, 20,  5, 10, 10, 10, 10, 10},
    /*PSY*/{ 10, 10, 10, 10, 10, 10, 10, 10, 20, 10,  5, 10},
    /*GHO*/{  0, 10, 10, 10, 10, 10, 10, 10, 10, 10,  0, 20},
};

// 배율 반환 (정수 x10: 0,5,10,20)
// 이중타입 방어 시 두 타입 모두 곱함
inline int getEffInt(Type atk, Type def1, Type def2) {
    int a = (int)atk;
    if (a < 0 || a >= 12) return 10;
    int eff = 10;
    int d1 = (int)def1;
    if (d1 >= 0 && d1 < 12) eff = eff * TYPE_EFF[a][d1] / 10;
    int d2 = (int)def2;
    if (def2 != Type::NONE && d2 >= 0 && d2 < 12)
        eff = eff * TYPE_EFF[a][d2] / 10;
    return eff;
}

// 효과 텍스트 반환 (0=무효, 5=비효과, 20+=효과)
inline const wchar_t* effText(int eff) {
    if (eff == 0)  return L"효과가 없다!";
    if (eff <= 5)  return L"효과가 별로인 것 같다...";
    if (eff >= 20) return L"효과가 굉장했다!";
    return nullptr;
}
