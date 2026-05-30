#include "audio.h"
#include <windows.h>
#include <mmsystem.h>
// winmm 링크는 build.bat / CMakeLists 의 -lwinmm 으로 처리

static std::string g_soundDir;          // exe 폴더의 sounds 디렉토리 경로
static bool        g_bgmOpen   = false;
static bool        g_bgmLoop   = false; // 루프 의도 여부
static std::string g_curBGM    = "";    // 현재 BGM 이름 (재시작 방지)
static int         g_volPct    = 33;    // 출력 볼륨 % (기본 약 1/3)

static void mci(const std::string& cmd) {
    mciSendStringA(cmd.c_str(), nullptr, 0, nullptr);
}

// 퍼센트 볼륨을 파형 출력 장치에 적용 (좌/우 동일).
// waveaudio MCI 는 자체 볼륨 명령이 없어 장치 볼륨으로 조절한다.
static void applyVol() {
    DWORD v = (DWORD)(g_volPct * 655);   // 0~100% -> 0~65500
    if (v > 0xFFFF) v = 0xFFFF;
    waveOutSetVolume((HWAVEOUT)0, (v & 0xFFFF) | (v << 16));
}

static std::string wavPath(const std::string& name) {
    return g_soundDir + name + ".wav";
}

void Audio::init() {
    char buf[MAX_PATH] = {0};
    GetModuleFileNameA(nullptr, buf, MAX_PATH);
    std::string p(buf);
    size_t pos = p.find_last_of("\\/");
    g_soundDir = (pos == std::string::npos) ? "" : p.substr(0, pos + 1);
    g_soundDir += "sounds\\";
    applyVol();   // 시작 시 기본 볼륨(약 1/3) 적용
}

void Audio::volumeUp() {
    g_volPct += 10;
    if (g_volPct > 100) g_volPct = 100;
    applyVol();
    playSE("se_cursor");   // 바뀐 볼륨으로 들려 즉시 확인 가능
}

void Audio::volumeDown() {
    g_volPct -= 10;
    if (g_volPct < 0) g_volPct = 0;
    applyVol();
    playSE("se_cursor");
}

int Audio::volumePercent() { return g_volPct; }

void Audio::playBGM(const std::string& name, bool loop) {
    // 같은 곡이 이미 재생 중이면 그대로 둔다 (맵 이동 시 끊김 방지)
    if (g_bgmOpen && g_curBGM == name) return;

    if (g_bgmOpen) {
        mci("stop bgm");
        mci("close bgm");
        g_bgmOpen = false;
    }

    std::string openCmd = "open \"" + wavPath(name) + "\" type waveaudio alias bgm";
    if (mciSendStringA(openCmd.c_str(), nullptr, 0, nullptr) != 0) {
        // 파일 없음 / 장치 실패 — 조용히 무시 (게임 진행엔 영향 없음)
        g_bgmOpen = false;
        g_curBGM  = "";
        return;
    }
    // 주의: waveaudio 의 'play' 는 repeat 플래그를 지원하지 않는다(에러→무음).
    // 루프는 아래 update() 의 상태 폴링으로 직접 처리한다.
    mci("play bgm");
    g_bgmOpen = true;
    g_bgmLoop = loop;
    g_curBGM  = name;
}

void Audio::stopBGM() {
    if (!g_bgmOpen) return;
    mci("stop bgm");
    mci("close bgm");
    g_bgmOpen = false;
    g_bgmLoop = false;
    g_curBGM  = "";
}

void Audio::pauseBGM() {
    if (g_bgmOpen) mci("pause bgm");
}

void Audio::resumeBGM() {
    if (g_bgmOpen) mci("resume bgm");
}

// MCI waveaudio 의 'repeat' 는 환경에 따라 곡 끝에서 멈춘다.
// 매 프레임 상태를 확인해 'stopped' 면 처음부터 다시 재생 → 루프 보장.
void Audio::update() {
    if (!g_bgmOpen || !g_bgmLoop) return;
    char mode[64] = {0};
    mciSendStringA("status bgm mode", mode, sizeof(mode), nullptr);
    if (mode[0] && lstrcmpiA(mode, "stopped") == 0) {
        mci("seek bgm to start");
        mci("play bgm");
    }
}

// 현재 BGM 모드 질의 헬퍼 ("playing"/"stopped"/"paused"/...).
static bool bgmModeIs(const char* want) {
    if (!g_bgmOpen) return false;
    char mode[64] = {0};
    mciSendStringA("status bgm mode", mode, sizeof(mode), nullptr);
    return mode[0] && lstrcmpiA(mode, want) == 0;
}

bool Audio::bgmPlaying() { return bgmModeIs("playing"); }

// 비루프(loop=false) BGM 이 끝까지 재생되어 멈췄으면 true.
// (루프 BGM 은 update() 가 곧바로 재시작하므로 항상 false)
bool Audio::bgmFinished() {
    if (!g_bgmOpen || g_bgmLoop) return false;
    return bgmModeIs("stopped");
}

void Audio::playSE(const std::string& name) {
    std::string p = wavPath(name);
    // SND_ASYNC: 논블로킹, SND_NODEFAULT: 파일 없어도 기본 삑소리 금지
    PlaySoundA(p.c_str(), nullptr, SND_FILENAME | SND_ASYNC | SND_NODEFAULT);
}
