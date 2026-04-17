#pragma once
#include <windows.h>
#include <string>
#include <vector>

// ANSI 색상 코드
namespace Color {
    constexpr const char* RESET          = "\033[0m";
    constexpr const char* BLACK          = "\033[30m";
    constexpr const char* RED            = "\033[31m";
    constexpr const char* GREEN          = "\033[32m";
    constexpr const char* YELLOW         = "\033[33m";
    constexpr const char* BLUE           = "\033[34m";
    constexpr const char* MAGENTA        = "\033[35m";
    constexpr const char* CYAN           = "\033[36m";
    constexpr const char* WHITE          = "\033[37m";
    constexpr const char* BRIGHT_BLACK   = "\033[90m";
    constexpr const char* BRIGHT_RED     = "\033[91m";
    constexpr const char* BRIGHT_GREEN   = "\033[92m";
    constexpr const char* BRIGHT_YELLOW  = "\033[93m";
    constexpr const char* BRIGHT_BLUE    = "\033[94m";
    constexpr const char* BRIGHT_CYAN    = "\033[96m";
    constexpr const char* BRIGHT_WHITE   = "\033[97m";
    constexpr const char* BRIGHT_MAGENTA = "\033[95m";
    constexpr const char* BG_BLACK       = "\033[40m";
    constexpr const char* BG_WHITE       = "\033[47m";
    constexpr const char* BG_BLUE        = "\033[44m";
    constexpr const char* BG_RED         = "\033[41m";
}

// 버퍼 셀: ASCII 문자 1개 + 색상
struct Cell {
    char        ch    = ' ';
    std::string color = "";
    bool        dirty = true;
};

class Renderer {
public:
    int width  = 0;
    int height = 0;

    void init();

    // ASCII 문자 1개 셀에 쓰기
    void setCell(int x, int y, char ch, const std::string& color = "");

    // ASCII 문자열 한 줄 쓰기 (1바이트 문자만)
    void print(int x, int y, const std::string& text, const std::string& color = "");

    // 한국어 등 멀티바이트 문자열 직접 출력 (버퍼 우회, flush 후 호출)
    void printW(int x, int y, const std::wstring& text, const std::string& color = "");

    // ANSI + UTF-8 포함 raw 문자열 직접 출력 (스프라이트용, flush 후 호출)
    void printRaw(int x, int y, const char* utf8_ansi);

    // 박스 그리기 (ASCII +, -, |)
    void drawBox(int x, int y, int w, int h, const std::string& color = "");

    // 영역 채우기
    void fillRect(int x, int y, int w, int h, char ch, const std::string& color = "");

    // 변경된 셀만 출력 (더블버퍼링)
    void flush();

    // 버퍼 전체 공백으로 초기화 (dirty 표시)
    void clear();

private:
    std::vector<Cell> curr_;
    std::vector<Cell> prev_;

    void moveCursor(int x, int y);
    void enableAnsi();
    void setupInput();
    void maximizeConsole();
    void hideCursor();
    int  idx(int x, int y) const { return y * width + x; }
};
