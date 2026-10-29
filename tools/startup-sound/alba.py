#!/usr/bin/env python3
"""Alba, Starlight's startup sound, synthesised from scratch (no samples).

    python3 alba.py ../../resources/start.wav

A warm pad on E, a vibraphone climbing E4-B4-E5-G#5, then resting on E major
add9 with F#6 on top; 4.6 s, 48 kHz stereo 16-bit (the mixer's own rate).
Pure Python, a few seconds to run; the same file every time (fixed seed).
resources/start.wav is the take chosen by ear: same notes, timbre and
timing, only the partials' random starting phases differ from this run.
"""
import math, random, struct, sys, wave

SR = 48000

def note(name):
    """'E4' -> Hz (A4 = 440)."""
    names = {"C": -9, "C#": -8, "D": -7, "D#": -6, "E": -5, "F": -4, "F#": -3,
             "G": -2, "G#": -1, "A": 0, "A#": 1, "B": 2}
    pitch, octave = name[:-1], int(name[-1])
    return 440.0 * 2 ** ((names[pitch] + 12 * (octave - 4)) / 12)

class Mix:
    def __init__(self, seconds):
        self.n = int(seconds * SR)
        self.l = [0.0] * self.n
        self.r = [0.0] * self.n

    def add(self, start, samples, pan):
        """pan -1 (left) .. 1 (right), equal power."""
        a = (pan + 1) * math.pi / 4
        gl, gr = math.cos(a), math.sin(a)
        i0 = int(start * SR)
        for k, v in enumerate(samples):
            i = i0 + k
            if i >= self.n:
                break
            self.l[i] += v * gl
            self.r[i] += v * gr

def env(i, n, attack, release_curve):
    t = i / SR
    a = min(1.0, t / attack) if attack > 0 else 1.0
    a = a * a * (3 - 2 * a)                      # smooth attack
    return a * math.exp(-t * release_curve)

def sub(freq, dur, amp, attack=0.25, decay=0.9):
    n = int(dur * SR)
    w = 2 * math.pi * freq / SR
    return [amp * env(i, n, attack, decay) * math.sin(w * i) for i in range(n)]

def reverb(mix, wet=0.28):
    """A small Schroeder reverb: four combs, two all-passes, per channel."""
    def chan(x, spread):
        combs = [(int(SR * d) + spread, g) for d, g in
                 ((0.0297, 0.80), (0.0371, 0.79), (0.0411, 0.77), (0.0437, 0.76))]
        y = [0.0] * len(x)
        for length, g in combs:
            buf = [0.0] * length
            idx = 0
            lp = 0.0
            for i, v in enumerate(x):
                o = buf[idx]
                lp = o * 0.7 + lp * 0.3        # damping
                buf[idx] = v + lp * g
                idx = (idx + 1) % length
                y[i] += o
        for d, g in ((0.005, 0.7), (0.0017, 0.7)):
            length = int(SR * d) + spread
            buf = [0.0] * length
            idx = 0
            z = [0.0] * len(y)
            for i, v in enumerate(y):
                b = buf[idx]
                o = -g * v + b
                buf[idx] = v + g * o
                idx = (idx + 1) % length
                z[i] = o
            y = z
        return [a * (1 - wet) + b * wet * 0.25 for a, b in zip(x, y)]
    mix.l = chan(mix.l, 0)
    mix.r = chan(mix.r, 23)

def write(mix, path, fade_out=0.25):
    peak = max(max(abs(v) for v in mix.l), max(abs(v) for v in mix.r)) or 1.0
    gain = 0.89 / peak                          # about -1 dBFS
    nfade = int(fade_out * SR)
    frames = bytearray()
    for i in range(mix.n):
        f = 1.0
        if i > mix.n - nfade:
            f = (mix.n - i) / nfade
        frames += struct.pack("<hh", int(max(-1, min(1, mix.l[i] * gain * f)) * 32767),
                              int(max(-1, min(1, mix.r[i] * gain * f)) * 32767))
    with wave.open(str(path), "wb") as w:
        w.setnchannels(2)
        w.setsampwidth(2)
        w.setframerate(SR)
        w.writeframes(bytes(frames))

def lowpass_gain(f, cutoff):
    return 1.0 / (1.0 + (f / cutoff) ** 2)

def warm_pad(freqs, dur, amp, attack=1.0, release=1.6, cutoff=900, harmonics=10, detune=0.004, vibrato=0.0):
    """Horn and cello like: many harmonics through a low cutoff, slow swell."""
    n = int(dur * SR)
    out = [0.0] * n
    vw = 2 * math.pi * 5.0 / SR
    for f0 in freqs:
        for d in (-detune, 0.0, detune):
            f = f0 * (1 + d)
            for h in range(1, harmonics + 1):
                fh = f * h
                if fh > 6000:
                    break
                g = lowpass_gain(fh, cutoff) / (h ** 1.1)
                w = 2 * math.pi * fh / SR
                ph = random.random() * 6.28
                if vibrato:
                    depth = vibrato * fh / SR * 2 * math.pi
                    acc = ph
                    for i in range(n):
                        acc += w + depth * math.sin(vw * i)
                        out[i] += g * math.sin(acc)
                else:
                    for i in range(n):
                        out[i] += g * math.sin(w * i + ph)
    norm = amp / (len(freqs) * 3)
    rel_start = dur - release
    for i in range(n):
        t = i / SR
        a = min(1.0, t / attack)
        a = a * a * (3 - 2 * a)
        if t > rel_start:
            r = max(0.0, 1 - (t - rel_start) / release)
            a *= r * r
        out[i] *= a * norm
    return out

CLIMB = [("E4", 0.30, -0.5), ("B4", 0.48, 0.4), ("E5", 0.66, -0.3), ("G#5", 0.84, 0.5)]
ARRIVAL = [("E4", 1.05, -0.25), ("G#4", 1.07, 0.2), ("B4", 1.09, -0.05), ("F#5", 1.12, 0.3),
           ("B5", 1.16, -0.2), ("F#6", 1.22, 0.35)]

def vibraphone(freq, dur, amp, decay=0.9, motor=5.5):
    """Soft mallets on metal bars: the fundamental, the bar's 4th partial
    faint and short, and the motor's tremolo."""
    n = int(dur * SR)
    out = [0.0] * n
    for ratio, pa, dk in ((1.0, 1.0, 1.0), (3.98, 0.14, 3.5), (9.9, 0.03, 9.0)):
        f = freq * ratio
        if f > SR / 2.4:
            continue
        w = 2 * math.pi * f / SR
        ph = random.random() * 6.28
        for i in range(n):
            t = i / SR
            a = min(1.0, t / 0.008)
            out[i] += pa * a * math.exp(-t * decay * dk) * math.sin(w * i + ph)
    mw = 2 * math.pi * motor / SR
    return [amp * v * (1 - 0.22 * (0.5 + 0.5 * math.sin(mw * i))) for i, v in enumerate(out)]

def make(instrument, vol_climb, vol_arrival, tail):
    m = Mix(4.6)
    m.add(0.0, warm_pad([note("E2"), note("B2"), note("E3")], 4.4, 0.75, attack=1.1, release=1.7, cutoff=650), 0.0)
    m.add(0.05, sub(note("E1"), 3.9, 0.3, attack=0.6, decay=0.6), 0.0)
    for k, (n, t, pan) in enumerate(CLIMB):
        m.add(t, instrument(note(n), tail, vol_climb - 0.03 * k), pan)
    for n, t, pan in ARRIVAL:
        m.add(t, instrument(note(n), tail, vol_arrival), pan)
    m.add(1.05, warm_pad([note("E3"), note("G#3"), note("B3")], 3.4, 0.45, attack=0.5, release=1.9, cutoff=900), 0.0)
    reverb(m, wet=0.33)
    return m

if __name__ == "__main__":
    out = sys.argv[1] if len(sys.argv) > 1 else "start.wav"
    random.seed(23)
    write(make(lambda f, d, a: vibraphone(f, d, a, 0.75), 0.5, 0.36, 3.4), out)
    print("wrote", out)
