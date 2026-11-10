#!/usr/bin/env python3
"""Synthesises a small demo kit (WAV, 44.1 kHz, 16-bit stereo) for testing OKTAKT.

Usage: python3 scripts/make-demo-kit.py [output-folder]
Default output: resources/demo-kit
"""
import math
import os
import random
import struct
import sys
import wave

SR = 44100
random.seed(7)


def write(path, left, right=None):
    right = right if right is not None else left
    os.makedirs(os.path.dirname(path), exist_ok=True)
    peak = max(1e-9, max(max(abs(x) for x in left), max(abs(x) for x in right)))
    g = 0.89 / peak
    with wave.open(path, "wb") as w:
        w.setnchannels(2)
        w.setsampwidth(2)
        w.setframerate(SR)
        frames = bytearray()
        for a, b in zip(left, right):
            frames += struct.pack("<hh", int(max(-1, min(1, a * g)) * 32767), int(max(-1, min(1, b * g)) * 32767))
        w.writeframes(bytes(frames))


def env(i, decay):
    return math.exp(-i / (decay * SR))


def kick(length=0.45):
    out, phase = [], 0.0
    for i in range(int(length * SR)):
        t = i / SR
        f = 45 + 110 * math.exp(-t / 0.035)
        phase += 2 * math.pi * f / SR
        click = (random.uniform(-1, 1) * 0.3) if i < 60 else 0.0
        out.append((math.sin(phase) + click) * env(i, 0.16))
    return out


def snare(length=0.32):
    out, phase, lp = [], 0.0, 0.0
    for i in range(int(length * SR)):
        phase += 2 * math.pi * 185 / SR
        n = random.uniform(-1, 1)
        lp += 0.55 * (n - lp)
        out.append(0.55 * math.sin(phase) * env(i, 0.05) + 0.8 * (n - lp * 0.5) * env(i, 0.09))
    return out


def hat(length=0.12, decay=0.025):
    out, prev = [], 0.0
    for i in range(int(length * SR)):
        n = random.uniform(-1, 1)
        hp = n - prev
        prev = n
        out.append(hp * env(i, decay))
    return out


def clap(length=0.35):
    out, lp = [], 0.0
    for i in range(int(length * SR)):
        t = i / SR
        n = random.uniform(-1, 1)
        lp += 0.35 * (n - lp)
        bp = n - lp
        burst = sum(math.exp(-max(0.0, t - d) / 0.008) * (1 if t >= d else 0) for d in (0.0, 0.011, 0.022))
        tail = math.exp(-max(0.0, t - 0.03) / 0.11) if t >= 0.03 else 0.0
        out.append(bp * (0.6 * burst + 0.5 * tail))
    return out


def perc(length=0.2):
    out, phase = [], 0.0
    for i in range(int(length * SR)):
        t = i / SR
        f = 820 * (1 + 0.3 * math.exp(-t / 0.01))
        phase += 2 * math.pi * f / SR
        out.append(math.sin(phase) * env(i, 0.04) + random.uniform(-0.2, 0.2) * env(i, 0.006))
    return out


def mix_into(buf, sample, start, gain=1.0):
    for k, x in enumerate(sample):
        if start + k < len(buf):
            buf[start + k] += x * gain


def break_loop(bpm=100.0, bars=2):
    step = 60.0 / bpm / 4
    n = int(bars * 16 * step * SR)
    buf = [0.0] * n
    k, s, h, oh = kick(), snare(), hat(), hat(0.4, 0.12)
    pattern_k = [0, 6, 10, 16, 22, 26, 27]
    pattern_s = [4, 12, 15, 20, 28]
    for st in range(bars * 16):
        pos = int(st * step * SR)
        if st in pattern_k:
            mix_into(buf, k, pos, 0.9)
        if st in pattern_s:
            mix_into(buf, s, pos, 0.8 if st not in (15,) else 0.4)
        if st % 2 == 0:
            mix_into(buf, h, pos, 0.35)
        if st in (14, 30):
            mix_into(buf, oh, pos, 0.3)
    return buf


def bass_loop(bpm=120.0):
    beat = 60.0 / bpm
    notes = [(0, 0.75, 41.2), (0.75, 0.25, 41.2), (1.5, 0.5, 55.0), (2.0, 1.0, 49.0), (3.25, 0.5, 61.7)]
    n = int(4 * beat * SR)
    buf = [0.0] * n
    for start, dur, f in notes:
        a = int(start * beat * SR)
        phase, lp = 0.0, 0.0
        for i in range(int(dur * beat * SR)):
            phase = (phase + f / SR) % 1.0
            saw = 2 * phase - 1
            lp += 0.08 * (saw - lp)
            g = min(1.0, i / 200) * math.exp(-i / (0.4 * SR))
            if a + i < n:
                buf[a + i] += lp * g
    return buf


def pad(length=4.0):
    freqs = [220.0, 261.6, 329.6, 392.0]
    left, right = [], []
    phases = [[random.random() for _ in range(2)] for _ in freqs]
    lpl = lpr = 0.0
    total = int(length * SR)
    for i in range(total):
        sl = sr = 0.0
        for k, f in enumerate(freqs):
            for d, det in enumerate((0.997, 1.003)):
                phases[k][d] = (phases[k][d] + f * det / SR) % 1.0
                v = 2 * phases[k][d] - 1
                if d == 0:
                    sl += v
                else:
                    sr += v
        lpl += 0.03 * (sl - lpl)
        lpr += 0.03 * (sr - lpr)
        g = min(1.0, i / (0.6 * SR)) * min(1.0, (total - i) / (0.8 * SR))
        left.append(lpl * g)
        right.append(lpr * g)
    return left, right


def main():
    root = sys.argv[1] if len(sys.argv) > 1 else os.path.join(os.path.dirname(__file__), "..", "resources", "demo-kit")
    root = os.path.abspath(root)
    write(os.path.join(root, "Drums", "Kicks", "kick_round.wav"), kick())
    write(os.path.join(root, "Drums", "Snares & Claps", "snare_tight.wav"), snare())
    write(os.path.join(root, "Drums", "Snares & Claps", "clap_stack.wav"), clap())
    write(os.path.join(root, "Drums", "Hats", "hat_closed.wav"), hat())
    write(os.path.join(root, "Drums", "Hats", "hat_open.wav"), hat(0.45, 0.13))
    write(os.path.join(root, "Drums", "Perc", "perc_rim.wav"), perc())
    write(os.path.join(root, "Breaks", "break_100.wav"), break_loop())
    write(os.path.join(root, "Loops", "Bass", "bass_loop_120.wav"), bass_loop())
    l, r = pad()
    write(os.path.join(root, "Loops", "Pads", "pad_wide.wav"), l, r)
    print("Demo kit written to", root)


if __name__ == "__main__":
    main()
