#!/usr/bin/env python3
"""按 timeline.json 合成原创配乐与音效（无采样、无版权素材），输出 public/gen/audio/music.wav。

声音设计：
  开场   太鼓重击 + 低频冲击 → 闪切咔哒（与每个剪辑点逐帧对齐）+ 上扬音 → 拉远 whoosh
         → 落屏冲击 → 拓本/墨迹/描红三连鼓 → 「顏真卿」三记重鼓 + 印章声
  功能段 120 BPM：Karplus-Strong 古琴拨弦（D 宫五声音阶）+ 低音持续音 + 轻打击，换镜处加墨滴音
  收尾   锣声 + 主旋律回落；最后一拍用反向镲把能量推回第 0 帧的太鼓，循环播放时无缝衔接
旁白所在的时段由旁白音频的包络自动压低配乐（ducking）。

用法：.venv/bin/python scripts/synth_audio.py
依赖：numpy、scipy
"""

from __future__ import annotations

import json
from pathlib import Path

import numpy as np
from scipy import signal
from scipy.io import wavfile

HERE = Path(__file__).resolve().parents[1]
TL = json.loads((HERE / "timeline.json").read_text())
SR = 48000
FPS = TL["fps"]
N = int(TL["durationInFrames"] / FPS * SR)
RNG = np.random.default_rng(1274)


def at(frame: float) -> int:
    return int(round(frame / FPS * SR))


def t_axis(sec: float) -> np.ndarray:
    return np.arange(int(sec * SR)) / SR


def noise(sec: float) -> np.ndarray:
    return RNG.standard_normal(int(sec * SR))


def bandpass(x: np.ndarray, lo: float, hi: float, order: int = 2) -> np.ndarray:
    sos = signal.butter(order, [lo, hi], btype="band", fs=SR, output="sos")
    return signal.sosfilt(sos, x)


def lowpass(x: np.ndarray, f: float, order: int = 2) -> np.ndarray:
    return signal.sosfilt(signal.butter(order, f, btype="low", fs=SR, output="sos"), x)


def highpass(x: np.ndarray, f: float, order: int = 2) -> np.ndarray:
    return signal.sosfilt(signal.butter(order, f, btype="high", fs=SR, output="sos"), x)


class Bus:
    def __init__(self) -> None:
        self.l = np.zeros(N)
        self.r = np.zeros(N)

    def add(self, sig: np.ndarray, start: int, gain: float = 1.0, pan: float = 0.0) -> None:
        if start >= N:
            return
        if start < 0:
            sig, start = sig[-start:], 0
        sig = sig[: N - start] * gain
        self.l[start:start + len(sig)] += sig * np.sqrt(0.5 * (1 - pan))
        self.r[start:start + len(sig)] += sig * np.sqrt(0.5 * (1 + pan))


# ---------------------------------------------------------------- 乐器

def taiko(base: float = 55.0, big: bool = False) -> np.ndarray:
    dur = 2.6 if big else 1.3
    t = t_axis(dur)
    freq = base + base * 1.6 * np.exp(-t * 16)
    phase = 2 * np.pi * np.cumsum(freq) / SR
    body = np.sin(phase) * np.exp(-t * (2.6 if big else 5.0))
    body += 0.35 * np.sin(phase * 1.52) * np.exp(-t * 9)
    skin = bandpass(noise(dur), 150, 900) * np.exp(-t * 28) * 0.5
    click = highpass(noise(dur), 2500) * np.exp(-t * 300) * 0.35
    out = body + skin + click
    if big:
        out += 0.8 * np.sin(2 * np.pi * 36 * t) * np.exp(-t * 1.5) * (1 - np.exp(-t * 60))
        out += lowpass(noise(dur), 300) * np.exp(-t * 3.5) * 0.25
    return out


def tick(freq: float = 2400.0) -> np.ndarray:
    t = t_axis(0.06)
    return (np.sin(2 * np.pi * freq * t) * np.exp(-t * 160) * 0.6
            + highpass(noise(0.06), 3500) * np.exp(-t * 500) * 0.5)


def crack() -> np.ndarray:
    t = t_axis(0.35)
    return highpass(noise(0.35), 1200) * np.exp(-t * 22) * 0.7


def tok(freq: float = 880.0) -> np.ndarray:
    t = t_axis(0.4)
    s = np.sin(2 * np.pi * freq * t) * np.exp(-t * 45) + 0.5 * np.sin(2 * np.pi * freq * 1.51 * t) * np.exp(-t * 70)
    return s + lowpass(noise(0.4), 600) * np.exp(-t * 40) * 0.6


def drop() -> np.ndarray:
    """墨滴：短促上滑的水滴音。"""
    t = t_axis(0.25)
    f = 500 + 1400 * (1 - np.exp(-t * 60))
    return np.sin(2 * np.pi * np.cumsum(f) / SR) * np.exp(-t * 28) * 0.5


def gong(base: float = 98.0) -> np.ndarray:
    t = t_axis(4.5)
    out = np.zeros_like(t)
    for k, (ratio, amp, dec) in enumerate(((1, 1, 0.9), (1.48, 0.6, 1.2), (1.98, 0.45, 1.5),
                                           (2.44, 0.35, 1.9), (2.94, 0.25, 2.4), (3.6, 0.15, 3.0))):
        out += amp * np.sin(2 * np.pi * base * ratio * t * (1 + 0.002 * np.sin(2 * np.pi * (0.7 + k) * t)))\
            * np.exp(-t * dec)
    return out * (1 - np.exp(-t * 80)) * 0.5


def pluck(freq: float, dur: float = 2.2, bright: float = 0.5) -> np.ndarray:
    """Karplus-Strong 拨弦（用 lfilter 实现延迟反馈），叠一点低通的琴身共鸣。"""
    n = int(dur * SR)
    # 反馈环 y[n] = x[n] + d·(y[n−p] + y[n−p−1])/2，环路总延迟 p + 0.5 个样本。
    p = max(2, int(round(SR / freq - 0.5)))
    excite = np.zeros(n)
    burst = lowpass(RNG.uniform(-1, 1, p), 1500 + 5000 * bright)
    excite[:p] = burst
    decay = 0.996
    a = np.zeros(p + 2)
    a[0] = 1.0
    a[p] = a[p + 1] = -0.5 * decay
    out = signal.lfilter([1.0], a, excite)
    t = np.arange(n) / SR
    out *= np.exp(-t * 1.4)
    out = out / (np.max(np.abs(out)) + 1e-9)
    return out + 0.3 * lowpass(out, 400)


def reverse_swell(sec: float) -> np.ndarray:
    t = t_axis(sec)
    s = bandpass(noise(sec), 800, 9000) * (t / sec) ** 3
    return s * 0.6


def riser(sec: float) -> np.ndarray:
    t = t_axis(sec)
    k = t / sec
    f = 110 * (8 ** k)
    tone = sum(np.sin(2 * np.pi * np.cumsum(f * m) / SR) / m for m in (1, 1.5, 2))
    return (tone * 0.25 + highpass(noise(sec), 2000) * 0.35) * k ** 2.2


def whoosh(sec: float) -> np.ndarray:
    n = int(sec * SR)
    x = noise(sec)
    y = np.zeros(n)
    t = np.arange(n) / SR
    cutoff = 300 + 7000 * np.exp(-t * 3.2)
    coef = 1 - np.exp(-2 * np.pi * cutoff / SR)
    acc = 0.0
    for i in range(n):  # 时变一阶低通：截止频率从高往低扫
        acc += coef[i] * (x[i] - acc)
        y[i] = acc
    env = np.sin(np.pi * np.clip(t / sec, 0, 1)) ** 1.5
    return y * env * 0.9


def drone(sec: float, freqs: tuple[float, ...]) -> np.ndarray:
    t = t_axis(sec)
    out = np.zeros_like(t)
    for f in freqs:
        for det in (-0.6, 0.6):
            out += np.sin(2 * np.pi * (f + det) * t + RNG.uniform(0, 6.28))
    out = lowpass(out, 900) / (2 * len(freqs))
    fade = np.minimum(1, np.minimum(t / 1.5, (sec - t) / 1.0))
    return out * fade * (0.85 + 0.15 * np.sin(2 * np.pi * 0.25 * t))


def reverb(x: np.ndarray, sec: float = 1.8, seed: int = 0) -> np.ndarray:
    r = np.random.default_rng(seed)
    t = t_axis(sec)
    ir = r.standard_normal(len(t)) * np.exp(-t * 3.2)
    ir = lowpass(ir, 5000)
    ir[0] = 0
    return signal.fftconvolve(x, ir / np.sqrt(np.sum(ir ** 2)), mode="full")[: len(x)]


# ---------------------------------------------------------------- 编排

def build() -> tuple[np.ndarray, np.ndarray]:
    hits, bed = Bus(), Bus()
    h = TL["hook"]

    # 第 0 帧：太鼓重击 + 石裂声。循环时由收尾的反向镲推到这里。
    hits.add(taiko(48, big=True), at(h["slam"]), 1.0)
    hits.add(crack(), at(h["slam"]), 0.5)
    bed.add(drone(10.2, (36.7, 73.4, 110)), 0, 0.9)

    # 闪切：每个剪辑点一声咔哒，音高随加速上行；上扬音推到拉远。
    cuts = h["flashCuts"]
    for k, f in enumerate(cuts):
        hits.add(tick(1800 + 40 * k), at(f), 0.65 + 0.45 * k / len(cuts), pan=(-0.4 if k % 2 else 0.4))
    hits.add(riser((h["flashEnd"] - h["flashStart"]) / FPS), at(h["flashStart"]), 1.1)
    hits.add(taiko(70), at(h["flashStart"]), 0.5)

    # 拉远：whoosh 自左向右；落屏前反向镲 → 冲击。
    hits.add(whoosh(1.6), at(h["wallStart"]) - int(0.1 * SR), 1.3, pan=-0.3)
    hits.add(taiko(60), at(h["wallStart"]), 0.55)
    hits.add(reverse_swell((h["land"] - h["landStart"]) / FPS), at(h["landStart"]), 1.0)
    hits.add(taiko(45, big=True), at(h["land"]), 0.95)

    # 三连鼓（拓本 / 墨迹 / 描红），音高递升；纸张拍击。
    for k, f in enumerate(h["triple"]):
        hits.add(taiko(68 + 12 * k), at(f), 0.75)
        hits.add(bandpass(noise(0.12), 900, 4000) * np.exp(-t_axis(0.12) * 40), at(f), 0.35)

    # 「顏」「真」「卿」三记重鼓 + 石裂；印章「鲁公」一声木击。
    for k, f in enumerate(h["title"]):
        hits.add(taiko(52 - 3 * k, big=True), at(f), 0.5)
        hits.add(crack(), at(f), 0.35)
    hits.add(tok(760), at(h["seal"]), 0.55)

    # 功能段与收尾：120 BPM 古琴。
    beat = SR // 2
    start, end = at(300), at(TL["loopZoom"])
    scale = [146.83, 164.81, 185.0, 220.0, 246.94, 293.66, 329.63, 369.99, 440.0, 493.88, 587.33]
    # 8 拍一句，(拍内偏移, 音阶序号, 力度)；四句循环，第四句回到宫音。
    phrases = [
        [(0, 5, 1.0), (1, 7, 0.7), (1.5, 8, 0.6), (2, 7, 0.8), (3, 5, 0.6), (4, 3, 0.9), (5, 4, 0.6), (6, 5, 0.8)],
        [(0, 8, 1.0), (1, 9, 0.7), (2, 8, 0.7), (2.5, 7, 0.5), (3, 5, 0.7), (4, 7, 0.9), (6, 8, 0.7), (7, 9, 0.5)],
        [(0, 10, 1.0), (1, 9, 0.7), (2, 8, 0.8), (3, 7, 0.6), (4, 5, 0.9), (5, 7, 0.6), (6, 4, 0.7), (7, 3, 0.5)],
        [(0, 5, 1.0), (1, 4, 0.6), (2, 3, 0.7), (3, 2, 0.5), (4, 0, 1.0), (6, 5, 0.6)],
    ]
    plucks = Bus()
    n_beats = (end - start) // beat
    for b0 in range(0, n_beats, 8):
        for off, deg, vel in phrases[(b0 // 8) % 4]:
            pos = start + int((b0 + off) * beat)
            if pos < end:
                plucks.add(pluck(scale[deg], 2.4, 0.3 + 0.4 * vel), pos, 0.75 * vel, pan=0.25 if deg % 2 else -0.15)
        root = 73.42 if (b0 // 8) % 2 == 0 else 110.0
        plucks.add(pluck(root, 3.0, 0.2), start + b0 * beat, 0.8)
    bed.add(drone((end - start) / SR + 1.0, (73.4, 110.0, 146.8)), start, 0.7)

    # 轻打击：每小节 1、3 拍小太鼓，八分音符沙锤（收尾前两拍停）。
    for b in range(n_beats):
        pos = start + b * beat
        if b % 4 in (0, 2):
            bed.add(taiko(90) * 0.5, pos, 0.75)
        for half in (0, 1):
            shk = highpass(noise(0.05), 6000) * np.exp(-t_axis(0.05) * 90)
            bed.add(shk, pos + half * beat // 2, 0.14 if half else 0.2, pan=0.5)

    # 换镜：墨滴音；收尾：锣 + 重鼓。
    for sc in TL["scenes"]:
        if sc["id"] != "outro":
            hits.add(drop(), at(sc["from"]), 0.45)
        for f in sc["beats"][1:]:
            hits.add(drop() * 0.6, at(f), 0.3)
    outro = next(s for s in TL["scenes"] if s["id"] == "outro")
    hits.add(taiko(50, big=True), at(outro["from"]), 0.75)
    hits.add(gong(98), at(outro["from"]), 0.55)
    hits.add(tok(980), at(outro["beats"][2]), 0.4)

    # 循环接缝：最后 1 秒反向镲 + 上扬，第 900 帧正好落在第 0 帧的太鼓上。
    tail = (TL["durationInFrames"] - TL["loopZoom"]) / FPS
    hits.add(reverse_swell(tail), at(TL["loopZoom"]), 1.4)
    hits.add(riser(tail) * 0.6, at(TL["loopZoom"]), 1.0)

    # 混音：拨弦与床音过混响；旁白时段压低床音。
    wet_l = reverb(plucks.l, 2.2, 1)
    wet_r = reverb(plucks.r, 2.2, 2)
    bed.l += plucks.l * 0.8 + wet_l * 0.45
    bed.r += plucks.r * 0.8 + wet_r * 0.45
    duck = ducking_gain()
    hl = hits.l + reverb(hits.l, 1.4, 3) * 0.25
    hr = hits.r + reverb(hits.r, 1.4, 4) * 0.25
    left = hl * (0.75 + 0.25 * duck) + bed.l * duck
    right = hr * (0.75 + 0.25 * duck) + bed.r * duck
    return left, right


def ducking_gain() -> np.ndarray:
    """旁白包络 → 床音增益（旁白处约 −7 dB，起 30 ms、释 350 ms）。"""
    env = np.zeros(N)
    vo_dir = HERE / "public" / "gen" / "vo"
    for v in TL["vo"]:
        path = vo_dir / f"{v['id']}.wav"
        if not path.exists():
            continue
        sr, x = wavfile.read(path)
        x = x.astype(np.float64)
        if x.ndim > 1:
            x = x.mean(axis=1)
        x /= 32768.0
        x = signal.resample_poly(x, SR, sr)
        e = np.sqrt(lowpass(x ** 2, 20, order=1).clip(0))
        s = at(v["at"])
        e = e[: N - s]
        env[s:s + len(e)] = np.maximum(env[s:s + len(e)], e)
    active = np.clip(env / 0.02, 0, 1)
    # 包络跟随：先降采样到 1 kHz（避免逐样本 Python 循环），起 30 ms、释 350 ms，再插值回去。
    step = SR // 1000
    coarse = active[::step]
    gc = np.zeros_like(coarse)
    g = 0.0
    a_att, a_rel = 1 - np.exp(-1 / 30), 1 - np.exp(-1 / 350)
    for i, v in enumerate(coarse):
        g += (a_att if v > g else a_rel) * (v - g)
        gc[i] = g
    out = np.interp(np.arange(N), np.arange(len(gc)) * step, gc)
    return 1.0 - 0.55 * out


def main() -> int:
    left, right = build()
    st = np.stack([left, right], axis=1)
    peak = np.max(np.abs(st))
    st = np.tanh(st / peak * 1.6) / np.tanh(1.6) * 0.89  # 软限幅，留 1 dB 余量
    out = HERE / "public" / "gen" / "audio"
    out.mkdir(parents=True, exist_ok=True)
    wavfile.write(out / "music.wav", SR, (st * 32767).astype(np.int16))
    print(f"music.wav {len(st) / SR:.2f}s peak_in={peak:.2f}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
