#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Compare a reference recording of one note with a AURORA render of the same note:
       tools/compare_wav.py reference.wav build/host/blade.wav [--note 48]
   Both files are aligned on their onsets and the script reports, side by side: the level envelope over time, the
   spectral centroid (brightness) over time, the harmonic levels in the sustain, the spectrum in octave bands, the
   stereo width, the inharmonic / noise floor, and the release time. The numbers say what to change; ears say when
   it is right. Reads 16 / 24 / 32-bit PCM and float WAV, mono or stereo, any rate (resampled to 44.1 kHz)."""
import struct
import sys
import numpy as np

FS = 44100


def read_wav(path):
    raw = open(path, "rb").read()
    assert raw[:4] == b"RIFF" and raw[8:12] == b"WAVE", path
    pos, fmt, data = 12, None, None
    while pos + 8 <= len(raw):
        cid, size = raw[pos:pos + 4], struct.unpack("<I", raw[pos + 4:pos + 8])[0]
        body = raw[pos + 8:pos + 8 + size]
        if cid == b"fmt ":
            fmt = struct.unpack("<HHIIHH", body[:16])
        elif cid == b"data":
            data = body
        pos += 8 + size + (size & 1)
    tag, ch, rate, _, _, bits = fmt
    if tag == 0xFFFE:                                   # extensible: the real tag is in the sub-format
        tag = struct.unpack("<H", raw[raw.find(b"fmt ") + 8 + 24:][:2])[0]
    if tag == 3 and bits == 32:
        x = np.frombuffer(data, dtype="<f4").astype(float)
    elif bits == 16:
        x = np.frombuffer(data, dtype="<i2").astype(float) / 32768
    elif bits == 24:
        b = np.frombuffer(data, dtype=np.uint8).reshape(-1, 3).astype(np.int32)
        x = ((b[:, 0] | (b[:, 1] << 8) | (b[:, 2] << 16)) << 8 >> 8).astype(float) / 8388608
    elif bits == 32:
        x = np.frombuffer(data, dtype="<i4").astype(float) / 2147483648
    else:
        raise SystemExit(f"{path}: {bits}-bit PCM not handled")
    x = x.reshape(-1, ch)
    if rate != FS:
        n = int(len(x) * FS / rate)
        t_old, t_new = np.arange(len(x)) / rate, np.arange(n) / FS
        x = np.stack([np.interp(t_new, t_old, x[:, c]) for c in range(ch)], axis=1)
    return x, rate, ch, bits


def onset(m, thresh_db=-45):
    env = np.abs(m)
    i = np.argmax(env > 10 ** (thresh_db / 20))
    return int(i)


def rms_db(seg):
    return 20 * np.log10(np.sqrt((seg ** 2).mean()) + 1e-9)


def spectrum(seg):
    w = seg * np.hanning(len(seg))
    sp = np.abs(np.fft.rfft(w, 1 << 16))
    f = np.fft.rfftfreq(1 << 16, 1 / FS)
    return f, sp


def centroid(seg):
    f, sp = spectrum(seg)
    p = sp ** 2
    return (f * p).sum() / (p.sum() + 1e-12)


def harmonics(seg, f0, n=12):
    f, sp = spectrum(seg)
    df = FS / (1 << 16)
    out = []
    for h in range(1, n + 1):
        if f0 * h >= FS / 2: break
        i = int(round(f0 * h / df))
        out.append(20 * np.log10(sp[max(0, i - 4):i + 5].max() / (sp.max() + 1e-12) + 1e-12))
    return out


def inharmonic_floor(seg, f0):
    """how much energy sits between the harmonics (noise, detuning spread, chorus smear): dB below the harmonic peaks"""
    f, sp = spectrum(seg)
    mask = np.ones(len(f), bool)
    for h in range(1, 60):
        mask &= np.abs(f - f0 * h) > f0 * 0.12
    mask &= (f > f0 * 0.8) & (f < 8000)
    between = np.sqrt((sp[mask] ** 2).mean())
    peaks = sp.max()
    return 20 * np.log10(between / peaks + 1e-12)


def bands(seg):
    f, sp = spectrum(seg)
    p = sp ** 2
    tot = p.sum() + 1e-12
    edges = [50, 100, 200, 400, 800, 1600, 3200, 6400, 12800, 20000]
    return [10 * np.log10(p[(f >= a) & (f < b)].sum() / tot + 1e-12) for a, b in zip(edges[:-1], edges[1:])]


def f0_of(seg, hint):
    f, sp = spectrum(seg)
    lo, hi = hint / 1.06, hint * 1.06
    sel = (f > lo) & (f < hi)
    return f[sel][np.argmax(sp[sel])]


def release_time(m, off, db=-40):
    """ms from note-off until the envelope sits 'db' below the level just before note-off"""
    ref = rms_db(m[off - FS // 10:off])
    hop = FS // 100
    for i in range(off, len(m) - hop, hop):
        if rms_db(m[i:i + hop]) < ref + db:
            return (i - off) * 1000 // FS
    return None


def analyse(path, note):
    x, rate, ch, bits = read_wav(path)
    m = x.mean(axis=1)
    on = onset(m)
    m = m[on:]
    x = x[on:]
    hint = 440 * 2 ** ((note - 69) / 12)
    sus = m[int(1.5 * FS):int(2.5 * FS)] if len(m) > 2.5 * FS else m[len(m) // 3:2 * len(m) // 3]
    f0 = f0_of(sus, hint)
    env = [(t, rms_db(m[int(t * FS):int((t + 0.1) * FS)])) for t in np.arange(0, min(len(m) / FS - 0.1, 8), 0.25)]
    cen = [(t, centroid(m[int(t * FS):int((t + 0.2) * FS)])) for t in [0.05, 0.2, 0.5, 1, 2, 3, 4]
           if (t + 0.2) * FS < len(m)]
    # note-off: the last point where the envelope was within 6 dB of its sustain, scanning backwards for the drop
    e = np.array([rms_db(m[i:i + FS // 20]) for i in range(0, len(m) - FS // 20, FS // 20)])
    peak_i = int(np.argmax(e))
    drop = np.where(e[peak_i:] < e[peak_i] - 12)[0]
    off = (peak_i + int(drop[0])) * FS // 20 if len(drop) else len(m) - 1
    width = None
    if ch == 2:
        s = x[int(1.5 * FS):int(2.5 * FS)]
        mid, side = (s[:, 0] + s[:, 1]) / 2, (s[:, 0] - s[:, 1]) / 2
        width = 20 * np.log10(np.sqrt((side ** 2).mean()) / (np.sqrt((mid ** 2).mean()) + 1e-12) + 1e-12)
    return dict(path=path, rate=rate, ch=ch, bits=bits, dur=len(m) / FS, f0=f0, env=env, cen=cen,
                harm=harmonics(sus, f0), floor=inharmonic_floor(sus, f0), bands=bands(sus), width=width,
                rel=release_time(m, off), peak=20 * np.log10(np.abs(m).max() + 1e-9))


def main():
    args = sys.argv[1:]
    note = 48
    if "--note" in args:
        i = args.index("--note"); note = int(args[i + 1]); del args[i:i + 2]
    a, b = analyse(args[0], note), analyse(args[1], note)
    name = lambda r: r["path"].split("/")[-1][:18]
    print(f"{'':24}{name(a):>18}{name(b):>18}")
    print(f"{'file':24}{a['rate']} Hz {a['ch']}ch {a['bits']}b{'':>3}{b['rate']} Hz {b['ch']}ch {b['bits']}b")
    print(f"{'length after onset':24}{a['dur']:15.2f} s {b['dur']:15.2f} s")
    print(f"{'peak':24}{a['peak']:13.1f} dBFS {b['peak']:13.1f} dBFS")
    print(f"{'fundamental':24}{a['f0']:14.1f} Hz {b['f0']:14.1f} Hz")
    print(f"{'stereo width (S/M)':24}{'mono' if a['width'] is None else f'{a[chr(119)+chr(105)+chr(100)+chr(116)+chr(104)]:.1f} dB':>18}"
          f"{'mono' if b['width'] is None else f'{b[chr(119)+chr(105)+chr(100)+chr(116)+chr(104)]:.1f} dB':>18}")
    print(f"{'release to -40 dB':24}{str(a['rel']) + ' ms':>18}{str(b['rel']) + ' ms':>18}")
    print(f"{'between-harmonic floor':24}{a['floor']:15.1f} dB {b['floor']:15.1f} dB   (noise, detune smear; higher = thicker)")
    print("level over time (dBFS, 100 ms RMS):")
    for (t, u), (_, v) in zip(a["env"], b["env"]):
        print(f"  {t:5.2f} s {u:15.1f} {v:18.1f}   {'#' * int(max(0, u + 60) / 2):30} | {'#' * int(max(0, v + 60) / 2)}")
    print("brightness over time (spectral centroid, Hz):")
    for (t, u), (_, v) in zip(a["cen"], b["cen"]):
        print(f"  {t:5.2f} s {u:15.0f} {v:18.0f}")
    print("harmonics in the sustain (dB below the strongest):")
    n = min(len(a["harm"]), len(b["harm"]))
    print("  h  " + " ".join(f"{h:5d}" for h in range(1, n + 1)))
    print("  A  " + " ".join(f"{v:5.1f}" for v in a["harm"][:n]))
    print("  B  " + " ".join(f"{v:5.1f}" for v in b["harm"][:n]))
    print("octave bands in the sustain (dB of total): 50 100 200 400 800 1.6k 3.2k 6.4k 12.8k")
    print("  A  " + " ".join(f"{v:5.1f}" for v in a["bands"]))
    print("  B  " + " ".join(f"{v:5.1f}" for v in b["bands"]))


if __name__ == "__main__":
    main()
