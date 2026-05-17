#pragma once
#include <windows.h>

enum class Key {
    NONE,
    UP, DOWN, LEFT, RIGHT,
    A,       // Z 키 or Enter - 확인
    B,       // X 키 - 취소
    START,   // Space / Enter - 메뉴
    ESCAPE,
    WARP_MENU,   // Ctrl+M — 디버그 워프 메뉴
    UNKNOWN
};

class Input {
public:
    // 논블로킹: 키 없으면 Key::NONE 반환
    static Key poll();

    // 논블로킹 문자 입력: 출력 가능한 ASCII 문자 반환 (0=없음)
    // 8=backspace, 13=enter
    static char pollChar();
};
