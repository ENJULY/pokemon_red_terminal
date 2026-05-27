#include "input.h"

static Key vkToKey(WORD vk) {
    switch (vk) {
        case VK_UP:    case 'W': return Key::UP;
        case VK_DOWN:  case 'S': return Key::DOWN;
        case VK_LEFT:  case 'A': return Key::LEFT;
        case VK_RIGHT: case 'D': return Key::RIGHT;
        case 'Z':
        case VK_RETURN:          return Key::A;
        case 'X':
        case VK_BACK:            return Key::B;
        case VK_SPACE:           return Key::START;
        case VK_ESCAPE:          return Key::ESCAPE;
        case 'M':                return Key::MENU;
        default:                 return Key::UNKNOWN;
    }
}

char Input::pollChar() {
    HANDLE hIn = GetStdHandle(STD_INPUT_HANDLE);
    DWORD numEvents = 0;
    GetNumberOfConsoleInputEvents(hIn, &numEvents);
    if (numEvents == 0) return 0;

    INPUT_RECORD rec;
    DWORD read = 0;
    // Peek without consuming, to not interfere with poll()
    PeekConsoleInputA(hIn, &rec, 1, &read);
    if (read == 0) return 0;
    if (rec.EventType != KEY_EVENT || !rec.Event.KeyEvent.bKeyDown) {
        ReadConsoleInputA(hIn, &rec, 1, &read);
        return 0;
    }
    WORD vk = rec.Event.KeyEvent.wVirtualKeyCode;
    char ch = rec.Event.KeyEvent.uChar.AsciiChar;
    ReadConsoleInputA(hIn, &rec, 1, &read);

    if (vk == VK_BACK)   return 8;
    if (vk == VK_RETURN) return 13;
    if (vk == VK_ESCAPE) return 27;
    if (ch >= 32 && ch <= 126) return ch;
    return 0;
}

Key Input::poll() {
    HANDLE hIn = GetStdHandle(STD_INPUT_HANDLE);
    DWORD numEvents = 0;
    GetNumberOfConsoleInputEvents(hIn, &numEvents);
    if (numEvents == 0) return Key::NONE;

    INPUT_RECORD rec;
    DWORD read = 0;
    ReadConsoleInputA(hIn, &rec, 1, &read);
    if (read == 0) return Key::NONE;
    if (rec.EventType != KEY_EVENT || !rec.Event.KeyEvent.bKeyDown) return Key::NONE;

    WORD vk = rec.Event.KeyEvent.wVirtualKeyCode;
    DWORD ctrlState = rec.Event.KeyEvent.dwControlKeyState;
    bool ctrl = (ctrlState & (LEFT_CTRL_PRESSED | RIGHT_CTRL_PRESSED)) != 0;

    // Ctrl+M → 디버그 워프 메뉴
    if (ctrl && vk == 'M') return Key::WARP_MENU;

    return vkToKey(vk);
}
