#include "renderer.h"
#include <cstdio>
#include <string>

void Renderer::init() {
    enableAnsi();
    setupInput();

    // 창 최대화
    HWND hwnd = GetConsoleWindow();
    if (hwnd) ShowWindow(hwnd, SW_MAXIMIZE);
    Sleep(200);

    // 최대화 후 실제 창 크기 읽기 (버퍼 강제 변경 안 함)
    HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
    CONSOLE_SCREEN_BUFFER_INFO csbi;
    GetConsoleScreenBufferInfo(hOut, &csbi);
    width  = csbi.srWindow.Right  - csbi.srWindow.Left + 1;
    height = csbi.srWindow.Bottom - csbi.srWindow.Top  + 1;
    if (width  < 40) width  = 80;
    if (height < 10) height = 24;

    hideCursor();

    curr_.assign(width * height, Cell{});
    prev_.assign(width * height, Cell{' ', "", false});

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
    int i = idx(x, y);
    if (curr_[i].ch != ch || curr_[i].color != color) {
        curr_[i].ch    = ch;
        curr_[i].color = color;
        curr_[i].dirty = true;
    }
}

void Renderer::print(int x, int y, const std::string& text, const std::string& color) {
    for (int i = 0; i < (int)text.size(); i++)
        setCell(x + i, y, text[i], color);
}

void Renderer::drawBox(int x, int y, int w, int h, const std::string& color) {
    if (w < 2 || h < 2) return;
    setCell(x,         y,         '+', color);
    setCell(x + w - 1, y,         '+', color);
    setCell(x,         y + h - 1, '+', color);
    setCell(x + w - 1, y + h - 1, '+', color);
    for (int i = 1; i < w - 1; i++) {
        setCell(x + i, y,         '-', color);
        setCell(x + i, y + h - 1, '-', color);
    }
    for (int j = 1; j < h - 1; j++) {
        setCell(x,         y + j, '|', color);
        setCell(x + w - 1, y + j, '|', color);
    }
}

void Renderer::fillRect(int x, int y, int w, int h, char ch, const std::string& color) {
    for (int j = y; j < y + h && j < height; j++)
        for (int i = x; i < x + w && i < width; i++)
            setCell(i, j, ch, color);
}

void Renderer::printW(int x, int y, const std::wstring& text, const std::string& color) {
    moveCursor(x, y);
    if (!color.empty()) printf("%s", color.c_str());
    HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
    DWORD written = 0;
    WriteConsoleW(hOut, text.c_str(), (DWORD)text.size(), &written, nullptr);
    if (!color.empty()) printf("%s", Color::RESET);
    fflush(stdout);
}

// ─────────────────────────────────────────────
//  flush: 행 단위로 출력 (cell-by-cell 보다 빠름)
// ─────────────────────────────────────────────
void Renderer::flush() {
    for (int y = 0; y < height; y++) {
        // 이 행에 dirty 셀이 있는지 확인
        bool rowDirty = false;
        for (int x = 0; x < width; x++) {
            if (curr_[idx(x, y)].dirty) { rowDirty = true; break; }
        }
        if (!rowDirty) continue;

        moveCursor(0, y);
        std::string lastColor = "";

        for (int x = 0; x < width; x++) {
            int i = idx(x, y);
            const std::string& col = curr_[i].color;
            if (col != lastColor) {
                printf("%s", col.empty() ? Color::RESET : col.c_str());
                lastColor = col;
            }
            putchar(curr_[i].ch);
            curr_[i].dirty = false;
            prev_[i] = curr_[i];
        }
    }
    printf("%s", Color::RESET);
    fflush(stdout);
}

void Renderer::clear() {
    for (auto& c : curr_) {
        c.ch    = ' ';
        c.color = "";
        c.dirty = true;
    }
}
