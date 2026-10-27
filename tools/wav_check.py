#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Look at a host render: tools/wav_check.py build/host/aurora.wav T0 T1 [T0 T1 ...]  (windows in ms)
   For each window: the peak, the RMS, the fundamental (the strongest low peak of the spectrum) and the
   levels of its first harmonics, so a change to the voice can be checked without ears."""
import struct
import sys
import numpy as np

path = sys.argv[1]
raw = open(path, "rb").read()
n = struct.unpack("<I", raw[40:44])[0] // 4
x = np.frombuffer(raw[44:44 + n * 4], dtype="<i2").reshape(-1, 2)[:, 0].astype(float) / 32768
fs = 44100
args = [float(a) for a in sys.argv[2:]] or [200, 700]
for t0, t1 in zip(args[::2], args[1::2]):
    seg = x[int(t0 * fs / 1000):int(t1 * fs / 1000)]
    if len(seg) < 2048:
        print(f"{t0:.0f}-{t1:.0f} ms: too short"); continue
    peak, rms = np.abs(seg).max(), np.sqrt((seg ** 2).mean())
    w = seg * np.hanning(len(seg))
    sp = np.abs(np.fft.rfft(w, 1 << 16))
    f = np.fft.rfftfreq(1 << 16, 1 / fs)
    lo = sp.copy(); lo[f < 25] = 0
    peaks = [i for i in range(1, len(sp) - 1) if lo[i] > lo[i - 1] and lo[i] >= lo[i + 1] and lo[i] > lo.max() / 30]
    f0 = 0
    if peaks:                                   # the lowest strong peak, then the strongest within 4 % of it
        lowest = f[peaks[0]]
        near = [i for i in peaks if f[i] < lowest * 1.04]
        f0 = f[max(near, key=lambda i: sp[i])]
    harm = []
    for h in range(1, 9):
        if f0 and f0 * h < fs / 2:
            i = int(round(f0 * h / (fs / (1 << 16))))
            band = sp[max(0, i - 3):i + 4].max()
            harm.append(f"{20 * np.log10(band / sp.max() + 1e-12):5.1f}")
    db = lambda v: 20 * np.log10(v + 1e-9)
    print(f"{t0:.0f}-{t1:.0f} ms: peak {db(peak):6.1f} dBFS  rms {db(rms):6.1f} dBFS  f0 {f0:7.1f} Hz  harmonics dB: {' '.join(harm)}")
