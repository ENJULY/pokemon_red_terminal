#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
synth_audio.py — pokered(.asm) 게임보이 사운드엔진 악보를 읽어 WAV로 합성.

원본 Pokemon Red 의 BGM 은 mp3/wav 가 아니라 GB 사운드엔진 '악보'(note/octave/tempo)
형태로 audio/music/*.asm 에 들어있다. 이 스크립트가 그 악보를 파싱해서
사각파(square, 채널1/2) · 삼각파(wave, 채널3) · 노이즈(noise, 채널4) 로 직접 합성한다.

- 타이밍: frames = note_length * note_speed * tempo / 256  (V-blank 59.7275Hz)
- 음정:   equal temperament, midi = 12*(octave+1) + semitone  (OCTAVE_OFFSET 로 보정)
- 멜로디/박자는 원곡 그대로, 음색은 GB 근사 (vibrato/pitch_slide 는 v1 에서 생략).

사용:
    python3 synth_audio.py <pokered_root> <out_dir>
numpy 필요 (한 번 생성해 두면 게임은 WAV 만 읽으므로 재생성은 선택사항).
"""
import sys, os, re, wave, struct
import numpy as np

SR        = 44100          # 샘플레이트
FRAME_HZ  = 59.7275         # GB V-blank
OCTAVE_OFFSET = 0           # 음이 한 옥타브 어긋나면 ±1 조정
MAX_SONG_SEC  = 120.0       # 안전 상한 (루프 경계에서 자르도록 넉넉히)
MIN_SONG_SEC  = 3.0

SEMITONE = {  # pokered note 토큰 -> 반음(0..11)
    'C_':0, 'C#':1, 'D_':2, 'D#':3, 'E_':4, 'F_':5,
    'F#':6, 'G_':7, 'G#':8, 'A_':9, 'A#':10, 'B_':11,
}

# 게임에서 실제로 쓰이는 곡만: pokered Music_라벨 -> 출력 파일명
SONGS = {
    'Music_IntroBattle':       'introbattle',
    'Music_TitleScreen':       'title',
    'Music_OaksLab':           'oakslab',
    'Music_PalletTown':        'pallettown',
    'Music_Routes1':           'route',
    'Music_Cities1':           'city',
    'Music_Pokecenter':        'pokecenter',
    'Music_Gym':               'gym',
    'Music_WildBattle':        'wildbattle',
    'Music_TrainerBattle':     'trainerbattle',
    'Music_GymLeaderBattle':   'gymleader',
    'Music_HallOfFame':        'ending',
}


# ───────────────────────── 헤더 파싱 ─────────────────────────
def parse_headers(root):
    """musicheaders*.asm -> {songLabel: [(hwchan, chLabel), ...]}"""
    songs = {}
    cur = None
    hdr_re = re.compile(r'^(Music_\w+)::')
    ch_re  = re.compile(r'channel\s+(\d+)\s*,\s*(Music_\w+)')
    for i in (1, 2, 3):
        path = os.path.join(root, 'audio', 'headers', f'musicheaders{i}.asm')
        with open(path, encoding='utf-8') as f:
            for line in f:
                line = line.split(';')[0]
                m = hdr_re.match(line.strip())
                if m:
                    cur = m.group(1); songs[cur] = []; continue
                mc = ch_re.search(line)
                if mc and cur:
                    songs[cur].append((int(mc.group(1)), mc.group(2)))
    return songs


# ───────────────────────── 채널 파싱 ─────────────────────────
class Cmd:
    __slots__ = ('op', 'args')
    def __init__(self, op, args): self.op = op; self.args = args

def load_music_file(root, song):
    """song(예 Music_PalletTown)에 해당하는 .asm 파일 경로 추정."""
    # 헤더 라벨 Music_PalletTown -> 파일 pallettown.asm (CamelCase 제거)
    base = song[len('Music_'):]
    fname = base.lower() + '.asm'
    return os.path.join(root, 'audio', 'music', fname)

def parse_channel(lines, start_label):
    """
    전체 파일 lines 에서 start_label(글로벌)부터 다음 글로벌 라벨 직전까지를 읽어
    (cmds[], local_labels{name:index}) 반환. 로컬 라벨('.x')은 이 채널 범위로 스코프.
    """
    glob_re  = re.compile(r'^(Music_\w+)::')
    local_re = re.compile(r'^\.(\w+):')
    # 시작/끝 줄 찾기
    start = None
    for i, ln in enumerate(lines):
        m = glob_re.match(ln.strip())
        if m and m.group(1) == start_label:
            start = i; break
    if start is None:
        raise KeyError(start_label)
    end = len(lines)
    for i in range(start + 1, len(lines)):
        if glob_re.match(lines[i].strip()):
            end = i; break

    cmds = []
    labels = {}
    for i in range(start, end):
        raw = lines[i].split(';')[0].strip()
        if not raw:
            continue
        if glob_re.match(raw):
            continue
        ml = local_re.match(raw)
        if ml:
            labels[ml.group(1)] = len(cmds)
            continue
        # 명령 파싱
        parts = raw.replace(',', ' ').split()
        op = parts[0]
        args = parts[1:]
        cmds.append(Cmd(op, args))
    return cmds, labels


# ───────────────────────── VM: 악보 -> 노트 이벤트 ─────────────────────────
def run_channel(cmds, labels, hwchan, t_limit, init_tempo=0x100):
    """
    명령 스트림을 실행하여 (start_sec, dur_sec, freq_or_None, vol, duty, is_noise) 리스트 반환.
    무한루프(sound_loop 0)는 t_limit 까지 반복. 반환된 total_sec 도 함께.
    tempo 는 엔진상 전역이므로 init_tempo 로 곡 전체 공통값을 받는다.
    """
    events = []
    pc = 0
    t = 0.0
    octave = 4
    speed = 8
    note_vol = 12
    fade = 0
    tempo = init_tempo
    duty = 2
    callstack = []
    loopstate = {}     # cmd_index -> remaining iterations
    is_square = hwchan in (1, 2)
    is_wave   = hwchan == 3
    is_noise  = hwchan == 4
    guard = 0
    first_inf_loop_t = None

    def dur_of(length):
        frames = length * speed * tempo / 256.0
        return frames / FRAME_HZ

    while pc < len(cmds):
        guard += 1
        if guard > 2_000_000:      # 폭주 방지
            break
        if t >= t_limit:
            break
        c = cmds[pc]
        op = c.op; a = c.args

        if op == 'note':
            # note pitch, length
            length = int(a[1])
            d = dur_of(length)
            if is_noise:
                events.append((t, d, None, note_vol, duty, True, fade))
            else:
                midi = 12 * (octave + 1) + SEMITONE[a[0]] + 12 * OCTAVE_OFFSET
                freq = 440.0 * (2.0 ** ((midi - 69) / 12.0))
                events.append((t, d, freq, note_vol, duty, False, fade))
            t += d
            pc += 1
        elif op == 'drum_note':
            # drum_note instrument, length
            length = int(a[1])
            d = dur_of(length)
            events.append((t, d, None, note_vol, int(a[0]), True, 0))
            t += d
            pc += 1
        elif op == 'rest':
            d = dur_of(int(a[0]))
            t += d
            pc += 1
        elif op == 'octave':
            octave = int(a[0]); pc += 1
        elif op == 'note_type':
            speed = int(a[0])
            if len(a) >= 2: note_vol = int(a[1])
            if len(a) >= 3: fade = int(a[2])
            pc += 1
        elif op == 'drum_speed':
            speed = int(a[0]); pc += 1
        elif op == 'tempo':
            tempo = parse_int(a[0]); pc += 1
        elif op == 'duty_cycle':
            duty = int(a[0]); pc += 1
        elif op == 'volume':
            pc += 1   # 마스터 출력 — note_vol 로 충분히 근사
        elif op == 'sound_call':
            callstack.append(pc + 1)
            pc = resolve(labels, a[-1])
        elif op == 'sound_ret':
            if callstack:
                pc = callstack.pop()
            else:
                break   # 채널 끝
        elif op == 'sound_loop':
            count = int(a[0])
            target = resolve(labels, a[-1])
            if count == 0:
                # 무한 루프 = 곡 루프 지점
                if first_inf_loop_t is None:
                    first_inf_loop_t = t
                pc = target           # 계속 반복 (t_limit 에서 종료)
            else:
                rem = loopstate.get(pc)
                if rem is None:
                    rem = count - 1
                if rem > 0:
                    loopstate[pc] = rem - 1
                    pc = target
                else:
                    loopstate.pop(pc, None)
                    pc += 1
        else:
            # vibrato, pitch_slide, toggle_perfect_pitch, stereo_panning,
            # duty_cycle_pattern, execute_music 등 — v1 생략
            pc += 1

    return events, t, first_inf_loop_t


def resolve(labels, token):
    token = token.lstrip('.')
    if token in labels:
        return labels[token]
    raise KeyError(token)

def parse_int(s):
    s = s.strip()
    if s.startswith('$'): return int(s[1:], 16)
    if s.startswith('%'): return int(s[1:], 2)
    return int(s)


# ───────────────────────── 합성 ─────────────────────────
def env(n, attack=0.004, release=0.02):
    """클릭 방지용 attack/release 엔벨로프 (샘플 n개)."""
    e = np.ones(n)
    a = min(int(SR * attack), n // 2)
    r = min(int(SR * release), n // 2)
    if a > 0: e[:a] = np.linspace(0, 1, a)
    if r > 0: e[-r:] = np.linspace(1, 0, r)
    return e

def square_wave(freq, n, duty):
    duty_frac = [0.125, 0.25, 0.5, 0.75][duty & 3]
    t = np.arange(n) / SR
    phase = (t * freq) % 1.0
    return np.where(phase < duty_frac, 0.6, -0.6)

def triangle_wave(freq, n):
    t = np.arange(n) / SR
    phase = (t * freq) % 1.0
    return (2.0 * np.abs(2.0 * phase - 1.0) - 1.0) * 0.7

_noise_cache = None
def noise_burst(n, inst):
    global _noise_cache
    if _noise_cache is None:
        rng = np.random.default_rng(1234)
        _noise_cache = rng.uniform(-1, 1, SR * 2)
    src = _noise_cache[:n] if n <= len(_noise_cache) else np.resize(_noise_cache, n)
    # instrument 에 따라 감쇠 속도 약간 변화 (드럼 느낌)
    decay = np.exp(-np.linspace(0, 6 + (inst % 5), n))
    return src * decay * 0.5

def fade_env(n, vol, fade):
    """GB 볼륨 엔벨로프: fade>0 면 |fade| 프레임마다 1단계 감쇠, <0 면 증가, 0 이면 일정.
    음이 오르간처럼 지속되지 않고 GB 처럼 통통 튀며 감쇠하게 한다."""
    if fade == 0 or vol <= 0:
        return np.ones(n)
    spf = SR / FRAME_HZ                      # 프레임당 샘플 수
    frames = np.arange(n) / spf
    if fade > 0:
        units = np.maximum(0.0, vol - np.floor(frames / fade))
    else:
        units = np.minimum(15.0, vol + np.floor(frames / (-fade)))
    return units / float(vol)

def render_channel(events, total_n, hwchan):
    buf = np.zeros(total_n, dtype=np.float32)
    is_wave   = hwchan == 3
    for (t0, dur, freq, vol, duty, noise, fade) in events:
        start = int(t0 * SR)
        n = int(dur * SR)
        if n <= 1 or start >= total_n:
            continue
        n = min(n, total_n - start)
        amp = (vol / 15.0)
        if noise:
            seg = noise_burst(n, duty) * amp
        elif is_wave:
            seg = triangle_wave(freq, n) * amp * env(n)
        else:
            seg = square_wave(freq, n, duty) * amp * env(n) * fade_env(n, vol, fade)
        buf[start:start + n] += seg[:n]
    return buf


def synth_song(root, song, headers):
    chans = headers[song]
    path = load_music_file(root, song)
    with open(path, encoding='utf-8') as f:
        lines = f.readlines()

    parsed = []
    for hw, label in chans:
        cmds, labels = parse_channel(lines, label)
        parsed.append((hw, cmds, labels))

    # tempo 는 엔진상 전역 — ch1(hw1)이 설정하는 값을 곡 전체 공통으로 사용.
    # (파일 첫 줄 스캔은 미사용 _AlternateTempo 채널을 잘못 잡으므로 ch1 한정)
    g_tempo = 0x100
    for hw, cmds, _ in parsed:
        if hw == 1:
            for c in cmds:
                if c.op == 'tempo':
                    g_tempo = parse_int(c.args[0]); break
            break

    # 1) lead 채널(hw1)로 한 바퀴 길이(T_target) 결정
    t_target = None
    for hw, cmds, labels in parsed:
        if hw == 1:
            ev, tend, inf2 = run_channel(cmds, labels, hw, MAX_SONG_SEC, g_tempo)
            t_target = inf2 if inf2 else tend
            break
    if not t_target or t_target < MIN_SONG_SEC:
        longest = 0.0
        for hw, cmds, labels in parsed:
            _, tend, inf = run_channel(cmds, labels, hw, MAX_SONG_SEC, g_tempo)
            longest = max(longest, inf or tend)
        t_target = max(longest, MIN_SONG_SEC)
    t_target = min(t_target, MAX_SONG_SEC)

    total_n = int(t_target * SR)
    mix = np.zeros(total_n, dtype=np.float32)
    nactive = 0
    for hw, cmds, labels in parsed:
        ev, _, _ = run_channel(cmds, labels, hw, t_target + 0.5, g_tempo)
        if not ev:
            continue
        mix += render_channel(ev, total_n, hw)
        nactive += 1

    # 정규화 (클리핑 방지)
    peak = np.max(np.abs(mix)) if total_n else 1.0
    if peak > 0:
        mix = mix / peak * 0.85
    return mix, t_target


# ───────────────────────── 효과음(SE) 절차 생성 ─────────────────────────
def synth_se():
    """간단한 GB풍 블립 효과음 — BGM 보조용."""
    out = {}
    def tone(freq, dur, duty=2, vol=0.5):
        n = int(dur * SR)
        return square_wave(freq, n, duty) * vol * env(n, 0.002, 0.01)
    # 커서 이동
    out['se_cursor'] = tone(990, 0.04)
    # 결정/확인 (두 음 상행)
    out['se_select'] = np.concatenate([tone(700, 0.04), tone(1050, 0.06)])
    # 취소/뒤로 (하행)
    out['se_back']   = np.concatenate([tone(700, 0.04), tone(450, 0.06)])
    # 메뉴 열기
    out['se_menu']   = np.concatenate([tone(520, 0.03), tone(820, 0.05)])
    # 벽 부딪힘 (낮은 버즈)
    out['se_bump']   = tone(160, 0.08, duty=0, vol=0.4)
    return out


# ───────────────────────── WAV 쓰기 ─────────────────────────
def write_wav(path, data):
    data = np.clip(data, -1.0, 1.0)
    pcm = (data * 32767.0).astype('<i2')
    with wave.open(path, 'wb') as w:
        w.setnchannels(1)
        w.setsampwidth(2)
        w.setframerate(SR)
        w.writeframes(pcm.tobytes())


def main():
    if len(sys.argv) < 3:
        print("usage: synth_audio.py <pokered_root> <out_dir>")
        sys.exit(1)
    root, out = sys.argv[1], sys.argv[2]
    os.makedirs(out, exist_ok=True)
    headers = parse_headers(root)

    for song, name in SONGS.items():
        if song not in headers:
            print(f"  [skip] {song} (헤더 없음)")
            continue
        try:
            mix, dur = synth_song(root, song, headers)
            path = os.path.join(out, name + '.wav')
            write_wav(path, mix)
            print(f"  [ok] {name:14s} {dur:5.1f}s  <- {song}")
        except Exception as e:
            print(f"  [FAIL] {song}: {e!r}")

    for name, data in synth_se().items():
        path = os.path.join(out, name + '.wav')
        write_wav(path, data)
        print(f"  [ok] {name:14s} {len(data)/SR:5.2f}s  (SE)")


if __name__ == '__main__':
    main()
