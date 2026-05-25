#pragma once
#include <windows.h>

enum class Key {
    NONE,
    UP, DOWN, LEFT, RIGHT,
    A,          // Z / Enter — 확인
    B,          // X / Backspace — 취소·뒤로
    START,      // Space — 미사용 예약
    ESCAPE,
    WARP_MENU,  // Ctrl+M — 디버그 워프 메뉴
    MENU,       // M — 인게임 메뉴 열기
    UNKNOWN
};

class Input {
public:
    static Key  poll();      // 논블로킹, 키 없으면 NONE
    static char pollChar();  // 논블로킹 ASCII 문자 (0=없음, 8=BS, 13=Enter)
};
