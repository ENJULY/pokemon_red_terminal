#include "renderer.h"
#include <cstdio>
#include <string>

void Renderer::init() {
    enableAnsi();
    setupInput();

    HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);

    // 목표 창 크기
    const int TARGET_W = 200;
    const int TARGET_H = 60;

    // 1) 버퍼 먼저 키움 (창 확장 가능 조건). 실패해도 무시.
    COORD bufSize = { (SHORT)TARGET_W, (SHORT)TARGET_H };
    SetConsoleScreenBufferSize(hOut, bufSize);

    // 2) Windows 콘솔 창 영역 직접 확장 (legacy cmd에서 작동)
    SMALL_RECT winRect = { 0, 0, (SHORT)(TARGET_W - 1), (SHORT)(TARGET_H - 1) };
    SetConsoleWindowInfo(hOut, TRUE, &winRect);

    // 3) 창 자체를 최대화 (visual)
    HWND hwnd = GetConsoleWindow();
    if (hwnd) ShowWindow(hwnd, SW_MAXIMIZE);
    Sleep(100);

    // 4) ANSI 리사이즈 escape — Windows Terminal 호환
    printf("\x1b[8;%d;%dt", TARGET_H, TARGET_W);
    fflush(stdout);
    Sleep(150);

    // 5) 최종 창 크기 읽기
    CONSOLE_SCREEN_BUFFER_INFO csbi;
    GetConsoleScreenBufferInfo(hOut, &csbi);
    width  = csbi.srWindow.Right  - csbi.srWindow.Left + 1;
    height = csbi.srWindow.Bottom - csbi.srWindow.Top  + 1;
    if (width  < 80) width  = TARGET_W;
    if (height < 25) height = TARGET_H;

    hideCursor();

    curr_.assign(width * height, Cell{});
    // prev_는 처음엔 의도적으로 curr_와 다르게 — 첫 flush가 모든 셀을 그리도록
    prev_.assign(width * height, Cell{"\x01", "", });

    // 화면 클리어
    printf("\033[2J\033[H");
    fflush(stdout);
}

void Renderer::enableAnsi() {
    HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
    DWORD mode = 0;
    GetConsoleMode(hOut, &mode);
#ifndef ENABLE_VIRTUAL_TERMINAL_PROCESSING
#define ENABLE_VIRTUAL_TERMINAL_PROCESSING 0x0004
#endif
    mode |= ENABLE_VIRTUAL_TERMINAL_PROCESSING;
    SetConsoleMode(hOut, mode);
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);
}

void Renderer::setupInput() {
    // ReadConsoleInput 방식은 모드 변경 불필요
    // Ctrl+C 등 시스템 키도 살려둠
}

void Renderer::hideCursor() {
    HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
    CONSOLE_CURSOR_INFO ci = { 1, FALSE };
    SetConsoleCursorInfo(hOut, &ci);
    printf("\033[?25l");
    fflush(stdout);
}

void Renderer::moveCursor(int x, int y) {
    printf("\033[%d;%dH", y + 1, x + 1);
}

// ─────────────────────────────────────────────
//  버퍼 쓰기
// ─────────────────────────────────────────────
void Renderer::setCell(int x, int y, char ch, const std::string& color) {
    if (x < 0 || x >= width || y < 0 || y >= height) return;
    Cell& c = curr_[idx(x, y)];
    c.ch.assign(1, ch);
    c.color = color;
}

// 멀티바이트 UTF-8 1글자(예: ▀ = 3 bytes) 셀에 직접 쓰기
void Renderer::setCellU(int x, int y, const std::string& utf8, const std::string& color) {
    if (x < 0 || x >= width || y < 0 || y >= height) return;
    Cell& c = curr_[idx(x, y)];
    c.ch    = utf8;
    c.color = color;
}

void Renderer::print(int x, int y, const std::string& text, const std::string& color) {
    for (int i = 0; i < (int)text.size(); i++)
        setCell(x + i, y, text[i], color);
}

void Renderer::drawBox(int x, int y, int w, int h, const std::string& color) {
    if (w < 2 || h < 2) return;
    // 유니코드 box-drawing char로 진한 직선 테두리 (이전: + - | 점선 같음)
    setCellU(x,         y,         "\xe2\x94\x8c", color);  // ┌
    setCellU(x + w - 1, y,         "\xe2\x94\x90", color);  // ┐
    setCellU(x,         y + h - 1, "\xe2\x94\x94", color);  // └
    setCellU(x + w - 1, y + h - 1, "\xe2\x94\x98", color);  // ┘
    for (int i = 1; i < w - 1; i++) {
        setCellU(x + i, y,         "\xe2\x94\x80", color);  // ─
        setCellU(x + i, y + h - 1, "\xe2\x94\x80", color);  // ─
    }
    for (int j = 1; j < h - 1; j++) {
        setCellU(x,         y + j, "\xe2\x94\x82", color);  // │
        setCellU(x + w - 1, y + j, "\xe2\x94\x82", color);  // │
    }
}

void Renderer::fillRect(int x, int y, int w, int h, char ch, const std::string& color) {
    for (int j = y; j < y + h && j < height; j++)
        for (int i = x; i < x + w && i < width; i++)
            setCell(i, j, ch, color);
}

void Renderer::printW(int x, int y, const std::wstring& text, const std::string& color) {
    // 한글 잔상 fix: wchar를 UTF-8로 변환 후 buffer cell에 저장 → flush가 diff 출력.
    // wide char (한글/한자, CJK 0x1100~0xFFFF)는 콘솔 2 cell 차지 → 두 번째 cell은
    // sentinel "\x02"로 두고 flush에서 skip 처리.
    int cx = x;
    for (wchar_t wc : text) {
        if (cx >= width) break;
        std::string utf8;
        if (wc < 0x80) {
            utf8 = std::string(1, (char)wc);
        } else if (wc < 0x800) {
            char b[3] = { (char)(0xC0 | (wc >> 6)),
                          (char)(0x80 | (wc & 0x3F)), 0 };
            utf8 = b;
        } else {
            char b[4] = { (char)(0xE0 | (wc >> 12)),
                          (char)(0x80 | ((wc >> 6) & 0x3F)),
                          (char)(0x80 | (wc & 0x3F)), 0 };
            utf8 = b;
        }
        setCellU(cx, y, utf8, color);
        cx++;
        // wide char (CJK)는 콘솔에서 2 cell 차지 — 두 번째 자리에 sentinel
        bool wide = (wc >= 0x1100 && wc <= 0xFFFF) &&
                    !(wc >= 0xFF61 && wc <= 0xFF9F);  // 반각 가나는 single
        if (wide && cx < width) {
            setCellU(cx, y, "\x02", color);  // flush가 skip할 sentinel
            cx++;
        }
    }
}

// ANSI+UTF-8 문자열을 셀 단위로 파싱하여 버퍼에 저장.
// 다음 flush에서 diff로 출력 → 깜빡임 없음.
// 입력 형식: "\x1b[48;5;106m \x1b[0m\x1b[38;5;28m▀..." 같은 ANSI 코드 + 가시 문자 혼합.
void Renderer::printRaw(int x, int y, const char* utf8_ansi) {
    if (!utf8_ansi) return;
    int cx = x;
    std::string color = "";  // 누적되는 현재 색상
    const char* s = utf8_ansi;
    while (*s) {
        if (*s == '\x1b' && s[1] == '[') {
            // ANSI escape sequence: ESC [ ... letter
            const char* start = s;
            s += 2;
            while (*s && !((*s >= 'A' && *s <= 'Z') || (*s >= 'a' && *s <= 'z'))) s++;
            if (*s) s++;  // 종결 letter 포함
            color.assign(start, s - start);
            continue;
        }
        if (*s == '\x1f') {
            // 투명 셀 마커 — 버퍼 변경 안 함, 커서만 이동 (맵 타일이 비침)
            cx++;
            s++;
            continue;
        }
        if ((unsigned char)*s < 0x20) { s++; continue; }  // 그 외 제어문자 스킵
        // 가시 문자 (UTF-8 1~4 bytes)
        unsigned char b = (unsigned char)*s;
        int len;
        if      ((b & 0x80) == 0)    len = 1;
        else if ((b & 0xE0) == 0xC0) len = 2;
        else if ((b & 0xF0) == 0xE0) len = 3;
        else if ((b & 0xF8) == 0xF0) len = 4;
        else                          len = 1;
        std::string utf8(s, len);
        s += len;
        if (cx >= 0 && cx < width && y >= 0 && y < height) {
            Cell& c = curr_[idx(cx, y)];
            c.ch    = utf8;
            c.color = color;
        }
        cx++;
    }
}

// ─────────────────────────────────────────────
//  flush: 진짜 diff 렌더 — curr_와 prev_가 다른 셀만 출력
//  변경 안 된 셀은 건드리지 않음 → 깜빡임 없음
// ─────────────────────────────────────────────
void Renderer::flush() {
    for (int y = 0; y < height; y++) {
        int x = 0;
        while (x < width) {
            // prev와 동일한 셀은 건너뜀
            while (x < width) {
                int i = idx(x, y);
                if (curr_[i].ch != prev_[i].ch || curr_[i].color != prev_[i].color)
                    break;
                x++;
            }
            if (x >= width) break;

            // 차이 나는 연속 구간만 커서 이동 후 출력
            int start = x;
            moveCursor(start, y);
            std::string lastColor = "\x02";  // 첫 셀에서 색상 강제 설정

            while (x < width) {
                int i = idx(x, y);
                if (curr_[i].ch == prev_[i].ch && curr_[i].color == prev_[i].color)
                    break;
                // wide char(한글) 두 번째 자리 sentinel — 콘솔엔 이미 wide char가 차지.
                // 출력 안 하고 prev 동기화만 (buffer index만 +1, console cursor는 자동 +2됨)
                if (curr_[i].ch == "\x02") {
                    prev_[i] = curr_[i];
                    x++;
                    continue;
                }
                const std::string& col = curr_[i].color;
                if (col != lastColor) {
                    if (col.empty()) printf("%s", Color::RESET);
                    else             fwrite(col.data(), 1, col.size(), stdout);
                    lastColor = col;
                }
                fwrite(curr_[i].ch.data(), 1, curr_[i].ch.size(), stdout);
                prev_[i] = curr_[i];
                x++;
            }
        }
    }
    printf("%s", Color::RESET);
    fflush(stdout);
}

// 매 프레임 시작 시: curr_를 배경(공백)으로 리셋.
// flush는 prev_와 비교해서 다른 셀만 출력 — 배경이 그대로면 다시 안 그림.
void Renderer::clear() {
    for (auto& c : curr_) {
        c.ch.assign(" ");
        c.color.clear();
    }
}

// 씬 전환 시: 터미널 전체를 한번 깨끗이 비우고, prev_를 무효화하여
// 다음 flush가 curr_의 모든 셀을 새로 그리게 함.
// (printW 같은 직접 출력으로 남은 한국어 텍스트도 같이 지움)
void Renderer::redrawAll() {
    printf("\033[2J\033[H");
    fflush(stdout);
    for (auto& c : prev_) {
        c.ch = "\x01";  // 어떤 가시 문자와도 다른 sentinel
        c.color = "\x01";
    }
}
