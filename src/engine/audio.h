#pragma once
#include <string>

// BGM / SE 재생 (Windows MCI: WAV 반복재생, PlaySound: 효과음)
//
// 이름(name)은 sounds/ 폴더의 확장자 없는 파일명. 예) "pallettown", "se_cursor".
// 경로는 실행파일(exe) 기준으로 해석 → 작업 디렉토리(CWD)와 무관하게 동작.
class Audio {
public:
    // exe 위치 기준 sounds/ 경로 캐싱. main 시작 시 1회 호출.
    static void init();

    // BGM: 반복 재생. 이미 같은 곡이 재생 중이면 무시(끊김 방지).
    static void playBGM(const std::string& name);
    static void stopBGM();
    static void pauseBGM();
    static void resumeBGM();

    // 매 프레임 호출 — MCI repeat 가 안 먹는 환경에서도 곡이 끝나면 재시작(루프 보장).
    static void update();

    // 출력 볼륨 조절 (0~100%). +/- 키로 실행 중 조절. 기본 ~33%.
    static void volumeUp();
    static void volumeDown();
    static int  volumePercent();

    // SE: 효과음 (비동기, BGM 과 별도 채널)
    static void playSE(const std::string& name);
};
