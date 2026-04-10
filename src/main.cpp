#include "game/game.h"
#include <windows.h>

int main() {
    // Windows Terminal / CMD에서 제목 설정
    SetConsoleTitleA("Pokemon Red - Terminal Edition");

    Game game;
    game.run();

    return 0;
}
