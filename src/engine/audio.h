#pragma once
#include <string>

// BGM / SE 재생 (Windows MCI 사용 - MP3, WAV 지원)
class Audio {
public:
    // BGM: 반복 재생
    static void playBGM(const std::string& filepath);
    static void stopBGM();
    static void pauseBGM();
    static void resumeBGM();

    // SE: 효과음 (비동기)
    static void playSE(const std::string& filepath);
};
