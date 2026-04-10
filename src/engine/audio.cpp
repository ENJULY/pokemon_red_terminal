#include "audio.h"
#include <windows.h>
#include <mmsystem.h>

// winmm.lib 링크는 build.bat의 -lwinmm 으로 처리

static bool   bgmOpen    = false;
static std::string currentBGM = "";

static void mciCmd(const std::string& cmd) {
    mciSendStringA(cmd.c_str(), nullptr, 0, nullptr);
}

void Audio::playBGM(const std::string& filepath) {
    if (bgmOpen) {
        mciCmd("stop bgm");
        mciCmd("close bgm");
        bgmOpen = false;
    }
    std::string openCmd = "open \"" + filepath + "\" type mpegvideo alias bgm";
    mciSendStringA(openCmd.c_str(), nullptr, 0, nullptr);
    mciCmd("play bgm repeat");
    bgmOpen    = true;
    currentBGM = filepath;
}

void Audio::stopBGM() {
    if (!bgmOpen) return;
    mciCmd("stop bgm");
    mciCmd("close bgm");
    bgmOpen    = false;
    currentBGM = "";
}

void Audio::pauseBGM() {
    if (bgmOpen) mciCmd("pause bgm");
}

void Audio::resumeBGM() {
    if (bgmOpen) mciCmd("resume bgm");
}

void Audio::playSE(const std::string& filepath) {
    PlaySoundA(filepath.c_str(), nullptr, SND_FILENAME | SND_ASYNC | SND_NODEFAULT);
}
