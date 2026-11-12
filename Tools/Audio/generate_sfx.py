#!/usr/bin/env python3
"""
generate_sfx.py -- procedural sound design for ShoterAAA (Unreal Engine 5.8).

Every asset is synthesised in this file; no samples are used. Standard library
only (Python 3.11). Default output folder: ../../RawAssets/Audio relative to this file.

  SFX_*.wav  mono,   16-bit PCM, 44.1 kHz, peak -1 dBFS, 3 ms fade-in, 10 ms fade-out
  AMB_*.wav  stereo, 16-bit PCM, 44.1 kHz, peak -3 dBFS, seamless loop
  MUS_*.wav  stereo, 16-bit PCM, 44.1 kHz, peak -3 dBFS, seamless loop

Usage
  python generate_sfx.py                      render everything
  python generate_sfx.py --only Rail_Fire     render one sound (short or full name)
  python generate_sfx.py --list               print the sound names
  python generate_sfx.py --out FOLDER         write somewhere else

Conventions
  * Deterministic: every sound creates random.Random(<fixed seed>).
  * Oscillators keep their own phase; saw and pulse waves use PolyBLEP.
  * Loops are rendered from -1 s to (loop end + 1 s). The 1 s tail is crossfaded
    into the head: equal-power for noise beds, linear for periodic beds. Periodic
    beds are built on a 1/D Hz grid or as events copied one loop earlier and later,
    so the tail equals the head and the crossfade adds no level bump.
  * Layered weapons and impacts are tanh soft-clipped before normalisation.
"""
import argparse
import array
import math
import os
import random
import sys
import time
import wave

SR = 44100
TAU = 2.0 * math.pi
NYQ = SR / 2.0
PRE_S = 0.003        # transients start 3 ms in so the mandated fade-in does not eat them
LOOP_EXTRA_S = 1.0   # loop warm-up before t=0 and tail after the loop end


# ----------------------------------------------------------------- basics

def nsamp(seconds):
    return int(round(seconds * SR))


def zeros(n):
    return [0.0] * n


def mul(a, b):
    return [p * q for p, q in zip(a, b)]


def scale(a, g):
    return [p * g for p in a]


def fit(x, n):
    """Crop or zero-pad to exactly n samples."""
    if len(x) >= n:
        return x[:n]
    return x + [0.0] * (n - len(x))


def add_at(dst, src, start=0, gain=1.0):
    """dst[start + i] += gain * src[i], clipped to both buffers."""
    skip = max(0, -start)
    s = max(0, start)
    m = min(len(src) - skip, len(dst) - s)
    for i in range(m):
        dst[s + i] += gain * src[skip + i]
    return dst


def put(out, sig, t=0.0, gain=1.0):
    """Mix sig into out, starting t seconds in."""
    return add_at(out, sig, nsamp(t), gain)


def sweep_exp(f0, f1, n):
    """Exponential frequency glide f0 -> f1 across n samples."""
    if n < 2:
        return [float(f0)] * n
    r = (f1 / f0) ** (1.0 / (n - 1))
    out, f = [0.0] * n, float(f0)
    for i in range(n):
        out[i] = f
        f *= r
    return out


def sweep_lin(f0, f1, n):
    """Linear frequency glide f0 -> f1 across n samples."""
    if n < 2:
        return [float(f0)] * n
    step = (f1 - f0) / (n - 1)
    return [f0 + step * i for i in range(n)]


def decay_freq(f_start, f_end, tau, n):
    """Pitch drop: f_end + (f_start - f_end) * exp(-t / tau)."""
    g = math.exp(-1.0 / (tau * SR))
    out, d = [0.0] * n, 1.0
    for i in range(n):
        out[i] = f_end + (f_start - f_end) * d
        d *= g
    return out


# -------------------------------------------------------------- oscillators

def poly_blep(t, dt):
    """PolyBLEP residual at a waveform discontinuity (t = phase, dt = phase step)."""
    if t < dt:
        t /= dt
        return t + t - t * t - 1.0
    if t > 1.0 - dt:
        t = (t - 1.0) / dt
        return t * t + t + t + 1.0
    return 0.0


def osc(kind, freq, n, phase=0.0, width=0.5):
    """Phase-accumulating oscillator. kind: sine | saw | square | pulse (duty = width).
    freq: Hz (float) or a per-sample list. Returns (samples, end_phase)."""
    is_list = isinstance(freq, list)
    f_const = 0.0 if is_list else float(freq)
    out = [0.0] * n
    ph = phase % 1.0
    for i in range(n):
        dt = (freq[i] if is_list else f_const) / SR
        if dt < 0.0:
            dt = 0.0
        elif dt > 0.45:
            dt = 0.45
        if kind == 'sine':
            out[i] = math.sin(TAU * ph)
        elif kind == 'saw':
            out[i] = 2.0 * ph - 1.0 - poly_blep(ph, dt)
        else:
            w = 0.5 if kind == 'square' else width
            v = (1.0 if ph < w else -1.0) + poly_blep(ph, dt)
            out[i] = v - poly_blep((ph - w) % 1.0, dt)
        ph += dt
        if ph >= 1.0:
            ph -= 1.0
    return out, ph


# ------------------------------------------------- noise, envelopes, filters

def white(n, rng):
    u = rng.uniform
    return [u(-1.0, 1.0) for _ in range(n)]


def smooth_noise(n, rng, fc):
    """Slow random wander scaled to [-1, 1] (one-pole low-passed white noise)."""
    x = lp1(white(n, rng), fc)
    m = max(1e-9, max(abs(v) for v in x))
    return [v / m for v in x]


def envelope(n, attack=0.002, tau=None, release=0.005):
    """Linear attack, optional exponential decay (time constant tau, s), linear release."""
    e = [1.0] * n
    if tau:
        g = math.exp(-1.0 / (tau * SR))
        v = 1.0
        for i in range(n):
            e[i] = v
            v *= g
    a = int(round(attack * SR))
    for i in range(min(a, n)):
        e[i] *= (i + 1) / float(a)
    r = int(round(release * SR))
    for j in range(min(r, n)):
        e[n - 1 - j] *= j / float(r)
    return e


def lp1(x, fc):
    """One-pole low-pass. fc: Hz or per-sample list."""
    is_list = isinstance(fc, list)
    y, s = [0.0] * len(x), 0.0
    a = 0.0 if is_list else 1.0 - math.exp(-TAU * fc / SR)
    for i, v in enumerate(x):
        if is_list:
            a = 1.0 - math.exp(-TAU * fc[i] / SR)
        s += a * (v - s)
        y[i] = s
    return y


def hp1(x, fc):
    """One-pole high-pass: input minus its one-pole low-pass. fc: Hz or per-sample list."""
    lo = lp1(x, fc)
    return [v - l for v, l in zip(x, lo)]


def biquad(x, kind, fc, q=0.707, block=32):
    """RBJ biquad, kind 'lp' | 'hp' | 'bp' (0 dB peak). fc: Hz or per-sample list.
    Coefficients are refreshed every `block` samples, which is cheap and smooth enough."""
    n = len(x)
    y = [0.0] * n
    is_list = isinstance(fc, list)
    z1 = z2 = 0.0
    for s0 in range(0, n, block):
        f = fc[s0] if is_list else fc
        f = min(max(f, 10.0), NYQ * 0.95)
        w = TAU * f / SR
        cw = math.cos(w)
        al = math.sin(w) / (2.0 * q)
        if kind == 'lp':
            b0, b1 = (1.0 - cw) * 0.5, 1.0 - cw
        elif kind == 'hp':
            b0, b1 = (1.0 + cw) * 0.5, -(1.0 + cw)
        else:
            b0, b1 = al, 0.0
        b2 = -al if kind == 'bp' else b0
        a0 = 1.0 + al
        b0, b1, b2 = b0 / a0, b1 / a0, b2 / a0
        a1 = -2.0 * cw / a0
        a2 = (1.0 - al) / a0
        for i in range(s0, min(n, s0 + block)):
            xi = x[i]
            yi = b0 * xi + z1
            z1 = b1 * xi - a1 * yi + z2
            z2 = b2 * xi - a2 * yi
            y[i] = yi
    return y


def echo(x, delay_s, fb, damp=0.6):
    """Feedback delay line. Returns the wet (echo) signal only. damp is the one-pole
    low-pass coefficient inside the loop (1.0 = no damping)."""
    d = max(1, int(round(delay_s * SR)))
    buf = [0.0] * d
    y = [0.0] * len(x)
    idx, lp = 0, 0.0
    for i, v in enumerate(x):
        w = buf[idx]
        y[i] = w
        lp += damp * (w - lp)
        buf[idx] = v + lp * fb
        idx += 1
        if idx == d:
            idx = 0
    return y


def softclip(x, drive):
    """tanh soft clipper, unity gain at small levels."""
    k = 1.0 / math.tanh(drive)
    return [math.tanh(drive * v) * k for v in x]


# ------------------------------------------------- composition helpers

def tone(freq, dur=None, tau=None, amp=1.0, kind='sine', attack=0.002, release=0.005, width=0.5):
    """Enveloped oscillator. freq is Hz or a per-sample list (then dur is ignored)."""
    n = len(freq) if isinstance(freq, list) else nsamp(dur)
    x, _ = osc(kind, freq, n, 0.0, width)
    return scale(mul(x, envelope(n, attack, tau, release)), amp)


def noise_burst(rng, dur, filt=None, fc=None, q=0.707, tau=None, amp=1.0,
                attack=0.0002, release=0.002):
    """White noise, optionally filtered ('lp' | 'hp' | 'bp'), with an envelope."""
    n = nsamp(dur)
    x = white(n, rng)
    if filt:
        x = biquad(x, filt, fc, q)
    return scale(mul(x, envelope(n, attack, tau, release)), amp)


def fade_edges(x, fade_in_ms=3.0, fade_out_ms=10.0):
    """Linear fade-in and fade-out for one-shots (no clicks at the file edges)."""
    n = len(x)
    fi = min(n, int(round(fade_in_ms * SR / 1000.0)))
    fo = min(n, int(round(fade_out_ms * SR / 1000.0)))
    for i in range(fi):
        x[i] *= i / float(fi)
    for j in range(fo):
        x[n - 1 - j] *= j / float(fo)
    return x


def peak(x):
    return max((abs(v) for v in x), default=0.0)


def normalize(chans, dbfs):
    """Scale all channels together so the loudest sample sits at dbfs."""
    p = max(peak(c) for c in chans)
    if p <= 0.0:
        raise ValueError('silent render')
    g = (10.0 ** (dbfs / 20.0)) / p
    return [scale(c, g) for c in chans]


def write_wav(path, chans):
    """16-bit PCM WAV, little-endian, interleaved if stereo."""
    nch, n = len(chans), len(chans[0])
    inter = [0] * (n * nch)
    for c, data in enumerate(chans):
        inter[c::nch] = [int(round(max(-1.0, min(1.0, v)) * 32767.0)) for v in data]
    pcm = array.array('h', inter)
    if sys.byteorder == 'big':
        pcm.byteswap()
    with wave.open(path, 'wb') as w:
        w.setnchannels(nch)
        w.setsampwidth(2)
        w.setframerate(SR)
        w.writeframes(pcm.tobytes())


# ----------------------------------------------------------- loop helpers

def grid(f, loop_s):
    """Snap f to the nearest multiple of 1/loop_s Hz, so it repeats exactly every loop."""
    return round(f * loop_s) / loop_s


def place_periodic(bus, samples, t_start, loop_s, pre_n, gain=1.0):
    """Add an event starting at t_start (s) to a loop bus that begins at t = -pre_n samples.
    Copies one loop earlier and later keep the bus periodic across the loop seam."""
    for k in (-1, 0, 1):
        add_at(bus, samples, pre_n + nsamp(t_start + k * loop_s), gain)
    return bus


def loop_fold(sig, pre_n, d_n, x_n, equal_power):
    """sig covers t in [-pre, d + x). Returns d samples. The x-sample tail is crossfaded
    into the head, so the loop point is continuous."""
    out = sig[pre_n:pre_n + d_n]
    for i in range(x_n):
        a = i / float(x_n)
        if equal_power:
            hw = math.sin(0.5 * math.pi * a)
            tw = math.cos(0.5 * math.pi * a)
        else:
            hw, tw = a, 1.0 - a
        out[i] = hw * sig[pre_n + i] + tw * sig[pre_n + d_n + i]
    return out


def loop_mix(parts, d_n, pre_n, x_n):
    """parts: [(stereo_signal, equal_power), ...] where stereo_signal = [L, R] covers
    pre_n + d_n + x_n samples. Returns the summed, folded stereo loop of d_n samples."""
    out = [[0.0] * d_n, [0.0] * d_n]
    for stereo, eq in parts:
        for c in range(2):
            folded = loop_fold(stereo[c], pre_n, d_n, x_n, eq)
            o = out[c]
            for i in range(d_n):
                o[i] += folded[i]
    return out


# ----------------------------------------------------------------- weapons

def sfx_pulse_pistol_fire():
    rng = random.Random(1101)
    n = nsamp(0.30)
    m = n - nsamp(PRE_S)
    out = zeros(n)
    # saw + sine body, quick downward glide 1800 -> 300 Hz
    f = decay_freq(1800.0, 300.0, 0.03, m)
    saw, _ = osc('saw', f, m)
    sin_, _ = osc('sine', f, m)
    body = [0.6 * a + 0.4 * b for a, b in zip(saw, sin_)]
    body = biquad(body, 'lp', decay_freq(6000.0, 1500.0, 0.05, m), 0.8)
    put(out, mul(body, envelope(m, 0.0005, 0.06, 0.004)), PRE_S, 0.8)
    # transient click and short band-passed crack
    put(out, noise_burst(rng, 0.004, 'hp', 2500.0, 0.7, 0.0004, 0.9, 0.0001, 0.0005), PRE_S)
    put(out, noise_burst(rng, 0.05, 'bp', 2500.0, 1.2, 0.02, 0.5), PRE_S)
    out = softclip(out, 1.5)
    wet = echo(out, 0.045, 0.3, 0.6)           # tiny delay tail
    return [[a + 0.25 * b for a, b in zip(out, wet)]]


def sfx_pulse_rifle_fire():
    rng = random.Random(1102)
    n = nsamp(0.28)
    m = n - nsamp(PRE_S)
    out = zeros(n)
    # low thump: 80 Hz sine, fast decay
    put(out, tone(decay_freq(92.0, 80.0, 0.02, m), tau=0.035, amp=0.9, attack=0.0005), PRE_S)
    # mid zap: 1200 -> 250 Hz saw
    zap, _ = osc('saw', decay_freq(1200.0, 250.0, 0.035, m), m)
    zap = biquad(zap, 'lp', 3500.0, 0.7)
    put(out, mul(zap, envelope(m, 0.0005, 0.05, 0.004)), PRE_S, 0.55)
    # band-passed noise crack
    put(out, noise_burst(rng, 0.04, 'bp', 3200.0, 0.9, 0.008, 0.6), PRE_S)
    return [softclip(out, 1.3)]


def sfx_ripper_fire():
    rng = random.Random(1103)
    n = nsamp(0.16)
    m = n - nsamp(PRE_S)
    out = zeros(n)
    zap, _ = osc('saw', decay_freq(2200.0, 600.0, 0.02, m), m)
    zap = biquad(zap, 'lp', 6000.0, 0.7)
    put(out, mul(zap, envelope(m, 0.0003, 0.03, 0.003)), PRE_S, 0.5)
    put(out, tone(decay_freq(320.0, 260.0, 0.02, m), tau=0.02, amp=0.25, attack=0.0003), PRE_S)
    put(out, noise_burst(rng, 0.03, 'bp', 4500.0, 1.0, 0.006, 0.45), PRE_S)
    return [softclip(out, 1.2)]


def sfx_scatter_fire():
    rng = random.Random(1104)
    n = nsamp(0.70)
    m = n - nsamp(PRE_S)
    out = zeros(n)
    # heavy low boom plus a 2nd harmonic for punch
    put(out, tone(decay_freq(85.0, 55.0, 0.04, m), tau=0.16, attack=0.0005, release=0.01), PRE_S)
    put(out, tone(decay_freq(170.0, 110.0, 0.03, m), tau=0.08, amp=0.25, attack=0.0005), PRE_S)
    # wide noise blast, low-passed and closing as it decays
    blast = biquad(white(m, rng), 'lp', sweep_exp(6000.0, 700.0, m), 0.7)
    put(out, mul(blast, envelope(m, 0.001, 0.12, 0.01)), PRE_S, 0.8)
    # metallic ring: inharmonic partials with fast decay
    for f, a, t in ((900.0, 0.30, 0.07), (1370.0, 0.20, 0.05), (2210.0, 0.12, 0.03)):
        put(out, tone(f, 0.4, tau=t, amp=a, attack=0.001), PRE_S)
    return [softclip(out, 1.3)]


def sfx_rail_fire():
    rng = random.Random(1105)
    n = nsamp(1.30)
    out = zeros(n)
    # 50 ms rising charge whine (sine + octave, linear fade-in)
    c = nsamp(0.05)
    f = sweep_exp(600.0, 2400.0, c)
    s1, _ = osc('sine', f, c)
    s2, _ = osc('sine', [2.0 * v for v in f], c)
    charge = mul([a + 0.3 * b for a, b in zip(s1, s2)], [i / float(c) for i in range(c)])
    put(out, charge, 0.0, 0.35)
    # crack at 50 ms: sweep 4000 -> 100 Hz, wide noise burst, sub boom, click
    t_c = nsamp(0.05)
    m = n - t_c
    sw, _ = osc('saw', decay_freq(4000.0, 100.0, 0.04, m), m)
    add_at(out, mul(sw, envelope(m, 0.0002, 0.09, 0.01)), t_c, 0.6)
    add_at(out, noise_burst(rng, 0.4, 'bp', 3000.0, 0.6, 0.03, 1.0), t_c)
    add_at(out, tone(decay_freq(90.0, 45.0, 0.08, m), tau=0.25, amp=0.9, attack=0.0005, release=0.01), t_c)
    add_at(out, noise_burst(rng, 0.004, 'hp', 6000.0, 0.7, 0.0008, 0.7), t_c)
    # long shimmering metallic tail: 7 Hz tremolo, feedback delay
    sh = zeros(m)
    for f, a in ((1213.0, 0.25), (1733.0, 0.20), (2537.0, 0.15), (3117.0, 0.10), (4019.0, 0.07)):
        o, _ = osc('sine', f, m)
        sh = [s + a * v for s, v in zip(sh, o)]
    am = [0.5 + 0.5 * math.sin(TAU * 7.0 * i / SR) for i in range(m)]
    sh = mul(mul(sh, am), envelope(m, 0.004, 0.45, 0.02))
    wet = echo(sh, 0.13, 0.55, 0.5)
    add_at(out, [a + 0.6 * b for a, b in zip(sh, wet)], t_c, 0.12)
    return [softclip(out, 1.6)]


def sfx_plasma_fire():
    rng = random.Random(1106)
    n = nsamp(0.55)
    m = n - nsamp(PRE_S)
    out = zeros(n)
    # deep thoomp: sine glide 120 -> 40 Hz
    put(out, tone(decay_freq(120.0, 40.0, 0.06, m), tau=0.15, attack=0.002, release=0.01), PRE_S)
    # plasma whoosh: band-pass centre sweeping 400 -> 2000 Hz
    whoosh = biquad(white(m, rng), 'bp', sweep_exp(400.0, 2000.0, m), 2.2)
    put(out, mul(whoosh, envelope(m, 0.05, 0.25, 0.02)), PRE_S, 0.7)
    # bubbling: sparse random blips
    for _ in range(26):
        t = rng.uniform(0.03, 0.45)
        f = rng.uniform(260.0, 620.0)
        put(out, tone(f, 0.04, tau=0.012, amp=0.18 * rng.uniform(0.5, 1.0), attack=0.001), t)
    return [softclip(out, 1.4)]


def sfx_explosion():
    rng = random.Random(1107)
    n = nsamp(1.80)
    m = n - nsamp(PRE_S)
    out = zeros(n)
    put(out, noise_burst(rng, 0.05, 'hp', 3000.0, 0.7, 0.02, 0.7), PRE_S)
    put(out, tone(decay_freq(60.0, 45.0, 0.25, m), tau=0.5, attack=0.002, release=0.02), PRE_S)
    # rumble with a slowly closing filter (two cascaded low-passes)
    rumble = biquad(white(m, rng), 'lp', sweep_exp(1500.0, 120.0, m), 0.7)
    rumble = biquad(rumble, 'lp', sweep_exp(900.0, 90.0, m), 0.7)
    put(out, mul(rumble, envelope(m, 0.005, 0.7, 0.02)), PRE_S, 0.9)
    # crackle: sparse random impulses
    crackle = zeros(m)
    for _ in range(70):
        b = noise_burst(rng, 0.003, tau=0.0004, amp=rng.uniform(0.2, 0.8), attack=0.0001, release=0.001)
        add_at(crackle, b, nsamp(rng.uniform(0.02, 1.6)))
    put(out, biquad(crackle, 'hp', 1800.0, 0.7), PRE_S, 0.6)
    # electric sizzle tail
    siz = biquad(white(m, rng), 'bp', 7500.0, 2.5)
    am = [0.5 + 0.5 * math.sin(TAU * 110.0 * i / SR) for i in range(m)]
    put(out, mul(mul(siz, am), envelope(m, 0.12, 0.45, 0.02)), PRE_S, 0.25)
    return [softclip(out, 1.5)]


def sfx_reload():
    rng = random.Random(1108)
    out = zeros(nsamp(1.10))
    # 0.05 s magazine-release click
    put(out, noise_burst(rng, 0.01, 'hp', 2000.0, 0.7, 0.001, 0.8, 0.0001, 0.001), 0.05)
    put(out, tone(2200.0, 0.01, tau=0.003, amp=0.3, attack=0.0001), 0.05)
    # 0.15-0.35 s magazine slide: upward band-pass sweep
    k = nsamp(0.20)
    slide = biquad(white(k, rng), 'bp', sweep_exp(1000.0, 2500.0, k), 1.5)
    put(out, mul(slide, envelope(k, 0.02, None, 0.02)), 0.15, 0.35)
    # 0.55 s clunk
    put(out, tone(decay_freq(180.0, 90.0, 0.02, nsamp(0.25)), tau=0.05, amp=0.9, attack=0.0005), 0.55)
    k = nsamp(0.12)
    thud = biquad(white(k, rng), 'lp', 800.0, 0.7)
    put(out, mul(thud, envelope(k, 0.0005, 0.02, 0.01)), 0.55, 0.4)
    # 0.65-1.00 s power-up whine 300 -> 1200 Hz
    k = nsamp(0.35)
    f = sweep_exp(300.0, 1200.0, k)
    s1, _ = osc('sine', f, k)
    s2, _ = osc('saw', f, k)
    whine = biquad([0.6 * a + 0.4 * b for a, b in zip(s1, s2)], 'lp', 4000.0, 0.7)
    edge = nsamp(0.05)
    env = [0.6 * min(1.0, i / float(edge)) * min(1.0, (k - i) / float(edge)) for i in range(k)]
    put(out, mul(whine, env), 0.65, 0.25)
    # 1.02 s final click
    put(out, noise_burst(rng, 0.01, 'hp', 2500.0, 0.7, 0.0007, 0.7, 0.0001, 0.001), 1.02)
    put(out, tone(1500.0, 0.01, tau=0.003, amp=0.4, attack=0.0001), 1.02)
    return [softclip(out, 1.1)]


def sfx_mag_out():
    """Magazine release: latch click, short slide, a small energy-cell decouple."""
    rng = random.Random(1201)
    out = zeros(nsamp(0.38))
    put(out, noise_burst(rng, 0.01, 'hp', 2200.0, 0.7, 0.001, 0.9, 0.0001, 0.001), PRE_S)
    put(out, tone(2000.0, 0.012, tau=0.003, amp=0.35, attack=0.0001), PRE_S)
    k = nsamp(0.16)
    slide = biquad(white(k, rng), 'bp', sweep_exp(2600.0, 900.0, k), 1.4)
    put(out, mul(slide, envelope(k, 0.015, None, 0.03)), 0.04, 0.4)
    # decoupling thunk + falling electric chirp
    put(out, tone(decay_freq(160.0, 85.0, 0.03, nsamp(0.2)), tau=0.04, amp=0.7, attack=0.0005), 0.17)
    k = nsamp(0.15)
    chirp, _ = osc('sine', sweep_exp(1500.0, 420.0, k), k)
    put(out, mul(chirp, envelope(k, 0.005, 0.05, 0.03)), 0.17, 0.25)
    return [softclip(out, 1.1)]


def sfx_mag_in():
    """Fresh cell seated: slide, solid clunk, rising power-up and a latch click."""
    rng = random.Random(1202)
    out = zeros(nsamp(0.42))
    k = nsamp(0.14)
    slide = biquad(white(k, rng), 'bp', sweep_exp(900.0, 2600.0, k), 1.4)
    put(out, mul(slide, envelope(k, 0.02, None, 0.02)), PRE_S, 0.4)
    put(out, tone(decay_freq(190.0, 95.0, 0.02, nsamp(0.25)), tau=0.05, amp=0.95, attack=0.0005), 0.15)
    k = nsamp(0.1)
    thud = biquad(white(k, rng), 'lp', 900.0, 0.7)
    put(out, mul(thud, envelope(k, 0.0005, 0.02, 0.01)), 0.15, 0.45)
    k = nsamp(0.2)
    s1, _ = osc('sine', sweep_exp(500.0, 1700.0, k), k)
    put(out, mul(s1, envelope(k, 0.03, 0.08, 0.05)), 0.17, 0.22)
    put(out, noise_burst(rng, 0.01, 'hp', 2800.0, 0.7, 0.0007, 0.7, 0.0001, 0.001), 0.3)
    put(out, tone(1600.0, 0.012, tau=0.003, amp=0.4, attack=0.0001), 0.3)
    return [softclip(out, 1.1)]


def sfx_charge():
    """Charging handle / slide rack: scrape back, spring, sharp slap forward."""
    rng = random.Random(1203)
    out = zeros(nsamp(0.30))
    k = nsamp(0.11)
    scrape = biquad(white(k, rng), 'bp', sweep_exp(1200.0, 3200.0, k), 1.8)
    put(out, mul(scrape, envelope(k, 0.01, None, 0.02)), PRE_S, 0.45)
    # spring buzz
    k = nsamp(0.08)
    buzz = biquad(white(k, rng), 'bp', 4200.0, 4.0)
    am = [0.5 + 0.5 * math.sin(TAU * 90.0 * i / SR) for i in range(k)]
    put(out, mul(mul(buzz, am), envelope(k, 0.01, None, 0.02)), 0.07, 0.3)
    # forward slap
    put(out, noise_burst(rng, 0.012, 'hp', 1800.0, 0.7, 0.002, 1.0, 0.0001, 0.001), 0.17)
    put(out, tone(decay_freq(320.0, 130.0, 0.02, nsamp(0.12)), tau=0.03, amp=0.8, attack=0.0004), 0.17)
    put(out, tone(2400.0, 0.01, tau=0.003, amp=0.4, attack=0.0001), 0.175)
    return [softclip(out, 1.1)]


def sfx_dry_fire():
    rng = random.Random(1109)
    out = zeros(nsamp(0.12))
    put(out, noise_burst(rng, 0.01, 'bp', 2200.0, 0.8, 0.0008, 1.0, 0.0001, 0.001), PRE_S)
    put(out, noise_burst(rng, 0.004, 'hp', 4000.0, 0.7, 0.0005, 0.5, 0.0001, 0.0005), PRE_S)
    # tiny electronic blip
    put(out, tone(sweep_exp(1400.0, 900.0, nsamp(0.04)), tau=0.012, amp=0.3, attack=0.001), 0.03)
    return [out]


def sfx_weapon_equip():
    rng = random.Random(1110)
    n = nsamp(0.35)
    out = zeros(n)
    k = nsamp(0.22)
    env_sw = envelope(k, 0.07, 0.06, 0.03)
    sw = biquad(white(k, rng), 'bp', sweep_exp(500.0, 3000.0, k), 1.2)
    put(out, mul(sw, env_sw), 0.0, 0.6)
    saw, _ = osc('saw', sweep_exp(400.0, 800.0, k), k)
    saw = biquad(saw, 'lp', 2500.0, 0.7)
    put(out, mul(saw, env_sw), 0.0, 0.2)
    # latch click at 0.26 s
    put(out, noise_burst(rng, 0.01, 'hp', 3000.0, 0.7, 0.001, 0.9, 0.0001, 0.001), 0.26)
    put(out, tone(2600.0, 0.02, tau=0.003, amp=0.5, attack=0.0001), 0.26)
    put(out, tone(1800.0, 0.10, tau=0.03, amp=0.2, attack=0.0005), 0.27)
    return [softclip(out, 1.1)]


def sfx_grenade_throw():
    rng = random.Random(1111)
    n = nsamp(0.30)
    w = biquad(white(n, rng), 'bp', sweep_exp(400.0, 1800.0, n), 1.2)
    return [scale(mul(w, envelope(n, 0.05, 0.1, 0.03)), 0.9)]


def sfx_grenade_bounce():
    rng = random.Random(1112)
    n = nsamp(0.15)
    out = zeros(n)
    base = 1480.0
    for ratio, amp, tau in ((1.0, 1.0, 0.03), (1.58, 0.6, 0.025), (2.37, 0.35, 0.015), (3.11, 0.2, 0.01)):
        put(out, tone(base * ratio, 0.14, tau=tau, amp=amp, attack=0.0005), PRE_S)
    put(out, noise_burst(rng, 0.003, 'hp', 4000.0, 0.7, 0.0005, 0.4, 0.0001, 0.0005), PRE_S)
    return [softclip(out, 1.0)]


def sfx_impact_surface():
    rng = random.Random(1113)
    n = nsamp(0.18)
    out = zeros(n)
    put(out, noise_burst(rng, 0.01, 'hp', 2500.0, 0.7, 0.0015, 1.0, 0.0001, 0.001), PRE_S)
    put(out, noise_burst(rng, 0.03, 'bp', 3500.0, 1.0, 0.006, 0.7, 0.0001, 0.002), PRE_S)
    put(out, tone(1900.0, 0.05, tau=0.012, amp=0.25, attack=0.0003), PRE_S)
    # short sizzle
    k = nsamp(0.12)
    siz = biquad(white(k, rng), 'bp', 7000.0, 2.0)
    am = [0.5 + 0.5 * math.sin(TAU * 150.0 * i / SR) for i in range(k)]
    put(out, mul(mul(siz, am), envelope(k, 0.0005, 0.04, 0.01)), 0.005, 0.3)
    return [softclip(out, 1.3)]


def sfx_impact_flesh():
    rng = random.Random(1114)
    n = nsamp(0.18)
    m = n - nsamp(PRE_S)
    out = zeros(n)
    # dull thud: 140 -> 120 Hz sine
    put(out, tone(decay_freq(140.0, 120.0, 0.02, m), tau=0.05, attack=0.002, release=0.01), PRE_S)
    # wet body: double low-passed noise
    wet = biquad(white(m, rng), 'lp', 900.0, 0.7)
    wet = biquad(wet, 'lp', 900.0, 0.7)
    put(out, mul(wet, envelope(m, 0.002, 0.04, 0.01)), PRE_S, 0.7)
    put(out, noise_burst(rng, 0.03, 'bp', 1500.0, 1.5, 0.008, 0.25, 0.0001, 0.002), 0.01)
    return [softclip(out, 1.2)]


# ------------------------------------------------------ player feedback

def sfx_hit_marker():
    rng = random.Random(1201)
    out = zeros(nsamp(0.07))
    put(out, tone(3000.0, 0.05, tau=0.012, amp=0.8, attack=0.0003), PRE_S)
    put(out, tone(3600.0, 0.03, tau=0.006, amp=0.25, attack=0.0003), PRE_S)
    put(out, noise_burst(rng, 0.004, 'hp', 5000.0, 0.7, 0.0004, 0.5, 0.0001, 0.0005), PRE_S)
    return [out]


def sfx_headshot():
    rng = random.Random(1202)
    out = zeros(nsamp(0.30))
    # bell: 1760 Hz fundamental with 2640 and 4400 Hz partials, exponential decay
    for f, a, t in ((1760.0, 0.6, 0.14), (2640.0, 0.35, 0.09), (4400.0, 0.12, 0.05)):
        put(out, tone(f, 0.3, tau=t, amp=a, attack=0.001), PRE_S)
    # tick layered under the bell
    put(out, noise_burst(rng, 0.01, 'hp', 4000.0, 0.7, 0.0006, 0.6, 0.0001, 0.001), PRE_S)
    put(out, tone(2400.0, 0.02, tau=0.004, amp=0.3, attack=0.0002), PRE_S)
    return [out]


def sfx_kill_confirm():
    out = zeros(nsamp(0.40))
    # two rising tones (880 Hz, then 1320 Hz), each with an octave partial
    for t0, f, tau in ((0.0, 880.0, 0.12), (0.08, 1320.0, 0.18)):
        put(out, tone(f, 0.30, tau=tau, amp=0.7, attack=0.002, release=0.02), t0)
        put(out, tone(2.0 * f, 0.30, tau=tau * 0.6, amp=0.14, attack=0.002, release=0.02), t0)
    # subtle low thump
    put(out, tone(decay_freq(130.0, 70.0, 0.03, nsamp(0.2)), tau=0.06, amp=0.35, attack=0.001), PRE_S)
    return [out]


def sfx_shield_break():
    rng = random.Random(1203)
    n = nsamp(0.55)
    m = n - nsamp(PRE_S)
    out = zeros(n)
    shatter = biquad(white(m, rng), 'hp', 5000.0, 0.7)
    put(out, mul(shatter, envelope(m, 0.0005, 0.08, 0.02)), PRE_S, 1.0)
    # descending inharmonic partials
    base = decay_freq(3200.0, 1400.0, 0.15, m)
    for ratio, amp, tau in ((1.0, 0.4, 0.2), (1.46, 0.3, 0.15), (2.13, 0.2, 0.10), (2.9, 0.1, 0.08)):
        put(out, tone([b * ratio for b in base], tau=tau, amp=amp, attack=0.001), PRE_S)
    # glints: short high pings at random times
    for _ in range(5):
        f = rng.uniform(3000.0, 8000.0)
        t = rng.uniform(0.04, 0.35)
        put(out, tone(f, 0.04, tau=0.01, amp=0.15, attack=0.0003), t)
    return [softclip(out, 1.0)]


def sfx_shield_recharge():
    n = nsamp(1.00)
    out = zeros(n)
    k = nsamp(0.92)
    f = sweep_lin(150.0, 600.0, k)
    h1, _ = osc('sine', f, k)
    h2, _ = osc('sine', [2.0 * v for v in f], k)
    trem = [1.0 + 0.3 * math.sin(TAU * 9.0 * i / SR) for i in range(k)]
    hum = mul([a + 0.35 * b for a, b in zip(h1, h2)], mul(trem, envelope(k, 0.1, None, 0.08)))
    put(out, hum, 0.0, 0.5)
    # soft ping at the end
    put(out, tone(1760.0, 0.25, tau=0.07, amp=0.45, attack=0.002), 0.92)
    put(out, tone(2640.0, 0.20, tau=0.04, amp=0.12, attack=0.002), 0.92)
    return [out]


def sfx_respawn():
    rng = random.Random(1205)
    n = nsamp(0.90)
    out = zeros(n)
    base = sweep_exp(300.0, 1800.0, n)
    body = zeros(n)
    for det in (-0.012, 0.0, 0.012):                     # chorus-like detune
        s, _ = osc('saw', [b * (1.0 + det) for b in base], n)
        body = [a + 0.33 * b for a, b in zip(body, s)]
    body = biquad(body, 'lp', 3800.0, 0.8)
    shim = [1.0 + 0.4 * math.sin(TAU * 14.0 * i / SR) for i in range(n)]
    body = mul(mul(body, shim), envelope(n, 0.12, None, 0.25))
    put(out, body, 0.0, 0.25)
    # sparkles
    for _ in range(12):
        t = rng.uniform(0.1, 0.8)
        f = rng.uniform(3500.0, 7000.0)
        put(out, tone(f, 0.06, tau=0.02, amp=rng.uniform(0.05, 0.12), attack=0.001), t)
    return [softclip(out, 1.2)]


def sfx_purchase():
    out = zeros(nsamp(0.45))
    # three quick ascending notes: C6, E6, G6
    for t0, f, tau in ((0.0, 1046.5, 0.10), (0.09, 1318.5, 0.10), (0.18, 1568.0, 0.14)):
        put(out, tone(f, 0.27, tau=tau, amp=0.6, attack=0.002, release=0.02), t0)
        put(out, tone(2.0 * f, 0.20, tau=tau * 0.5, amp=0.25, attack=0.002, release=0.01), t0)
        put(out, tone(3.0 * f, 0.15, tau=tau * 0.3, amp=0.08, attack=0.002, release=0.01), t0)
    return [out]


def sfx_denied():
    out = zeros(nsamp(0.30))
    for t0 in (0.0, 0.15):                               # two buzzer pulses
        k = nsamp(0.12)
        sq, _ = osc('square', decay_freq(112.0, 104.0, 0.05, k), k)
        sq = biquad(sq, 'lp', 800.0, 0.7)
        put(out, mul(sq, envelope(k, 0.002, None, 0.008)), t0, 0.5)
    return [out]


def sfx_ui_click():
    rng = random.Random(1206)
    out = zeros(nsamp(0.05))
    put(out, tone(1800.0, 0.02, tau=0.003, amp=0.6, attack=0.0005), PRE_S)
    put(out, noise_burst(rng, 0.01, 'lp', 3000.0, 0.7, 0.0015, 0.3, 0.0002, 0.001), PRE_S)
    return [out]


def sfx_ui_hover():
    out = zeros(nsamp(0.04))
    put(out, tone(sweep_exp(2400.0, 2800.0, nsamp(0.03)), tau=0.012, amp=0.25, attack=0.003, release=0.005), PRE_S)
    return [out]


# ----------------------------------------------------------- movement

def _footstep(seed, f_thump, f_scuff, f_metal, gain):
    rng = random.Random(seed)
    n = nsamp(0.20)
    m = n - nsamp(PRE_S)
    out = zeros(n)
    put(out, tone(decay_freq(f_thump * 1.25, f_thump * 0.9, 0.02, m), tau=0.035, amp=gain,
                  attack=0.001, release=0.01), PRE_S)
    k = nsamp(0.15)
    scuff = biquad(white(k, rng), 'bp', f_scuff, 1.0)
    put(out, mul(scuff, envelope(k, 0.004, 0.05, 0.01)), 0.008, 0.35)
    put(out, tone(f_metal, 0.12, tau=0.03, amp=0.12, attack=0.0005), 0.008)          # metal ring
    put(out, tone(f_metal * 2.76, 0.12, tau=0.02, amp=0.06, attack=0.0005), 0.008)
    return [softclip(out, 1.2)]


def sfx_footstep_01():
    return _footstep(1301, 78.0, 1800.0, 620.0, 1.0)


def sfx_footstep_02():
    return _footstep(1302, 88.0, 2400.0, 760.0, 0.95)


def sfx_footstep_03():
    return _footstep(1303, 70.0, 1300.0, 480.0, 1.05)


def sfx_footstep_04():
    return _footstep(1304, 84.0, 2100.0, 540.0, 0.9)


def sfx_jump():
    rng = random.Random(1305)
    n = nsamp(0.20)
    e = envelope(n, 0.03, 0.06, 0.02)
    air = biquad(white(n, rng), 'bp', sweep_exp(500.0, 1500.0, n), 1.0)
    eff, _ = osc('sine', sweep_exp(260.0, 380.0, n), n)
    return [mul([0.6 * a + 0.08 * b for a, b in zip(air, eff)], e)]


def sfx_double_jump():
    rng = random.Random(1306)
    n = nsamp(0.35)
    thr = biquad(white(n, rng), 'bp', sweep_exp(300.0, 2500.0, n), 1.5)     # thruster sweep
    am = [1.0 + 0.3 * math.sin(TAU * 35.0 * i / SR) for i in range(n)]
    thr = mul(mul(thr, am), envelope(n, 0.02, None, 0.1))
    low, _ = osc('sine', 60.0, n)                                           # low rumble
    rumble = biquad(white(n, rng), 'lp', 200.0, 0.7)
    rumble = mul([0.7 * a + 0.5 * b for a, b in zip(low, rumble)], envelope(n, 0.02, 0.15, 0.05))
    return [softclip([0.7 * a + 0.4 * b for a, b in zip(thr, rumble)], 1.2)]


def sfx_land():
    rng = random.Random(1307)
    n = nsamp(0.22)
    m = n - nsamp(PRE_S)
    out = zeros(n)
    put(out, tone(decay_freq(65.0, 40.0, 0.03, m), tau=0.07, amp=1.0, attack=0.001, release=0.01), PRE_S)
    k = nsamp(0.15)
    clunk = biquad(white(k, rng), 'lp', 500.0, 0.7)
    put(out, mul(clunk, envelope(k, 0.0005, 0.04, 0.01)), PRE_S, 0.6)
    put(out, tone(780.0, 0.1, tau=0.04, amp=0.1, attack=0.0005), PRE_S)
    put(out, tone(1190.0, 0.1, tau=0.04, amp=0.06, attack=0.0005), PRE_S)
    return [softclip(out, 1.2)]


def sfx_slide():
    rng = random.Random(1308)
    n = nsamp(0.80)
    fc = [3000.0 + 800.0 * math.sin(TAU * 6.0 * i / SR) for i in range(n)]
    hiss = biquad(white(n, rng), 'bp', fc, 1.2)
    env = envelope(n, 0.04, 0.45, 0.25)
    rumble = biquad(white(n, rng), 'lp', 350.0, 0.7)
    out = [0.5 * a + 0.25 * b for a, b in zip(mul(hiss, env), mul(rumble, env))]
    return [out]


# ----------------------------------------------------------------- zombies

def _growl(seed, f0, dur, am_hz, depth, breath_gain):
    """Guttural growl: jittery saw/pulse near f0, vocal-fry amplitude modulation,
    band-pass formants near 400 and 900 Hz, breathy noise, tanh distortion."""
    rng = random.Random(seed)
    n = nsamp(dur)
    wander = smooth_noise(n, rng, 9.0)
    contour = [math.sin(math.pi * i / n) for i in range(n)]          # rises, then falls
    f = [f0 * (1.0 + 0.035 * w) * (1.0 + 0.12 * c) for w, c in zip(wander, contour)]
    pulse, _ = osc('pulse', f, n, 0.0, 0.3)
    saw, _ = osc('saw', f, n)
    src = [0.6 * p + 0.4 * s for p, s in zip(pulse, saw)]
    fo1 = biquad(src, 'bp', 400.0, 3.0)
    fo2 = biquad(src, 'bp', 900.0, 4.0)
    breath = biquad(white(n, rng), 'bp', 1200.0, 0.8)
    am = [1.0 - depth * 0.5 * (1.0 + math.sin(TAU * am_hz * i / SR)) for i in range(n)]
    body = [a + 0.6 * b + breath_gain * c for a, b, c in zip(fo1, fo2, breath)]
    body = mul(body, mul(am, envelope(n, 0.08, None, 0.2)))
    return [softclip(body, 2.2)]


def sfx_zombie_growl_01():
    return _growl(1401, 85.0, 1.0, 22.0, 0.6, 0.3)


def sfx_zombie_growl_02():
    return _growl(1402, 72.0, 1.2, 18.0, 0.8, 0.5)


def sfx_zombie_growl_03():
    return _growl(1403, 100.0, 1.4, 28.0, 0.5, 0.2)


def sfx_zombie_attack():
    rng = random.Random(1404)
    n = nsamp(0.55)
    out = zeros(n)
    # aggressive snarl: pitch rising 120 -> 260 Hz with jitter
    f = mul(sweep_exp(120.0, 260.0, n), [1.0 + 0.02 * w for w in smooth_noise(n, rng, 12.0)])
    pulse, _ = osc('pulse', f, n, 0.0, 0.35)
    saw, _ = osc('saw', f, n)
    src = [0.5 * p + 0.5 * s for p, s in zip(pulse, saw)]
    fo1 = biquad(src, 'bp', 700.0, 3.0)
    fo2 = biquad(src, 'bp', 1800.0, 5.0)
    am = [1.0 - 0.15 * (1.0 + math.sin(TAU * 30.0 * i / SR)) for i in range(n)]
    snarl = mul(mul([a + 0.5 * b for a, b in zip(fo1, fo2)], am), envelope(n, 0.02, None, 0.15))
    put(out, softclip(snarl, 3.0), 0.0, 0.9)
    # swipe whoosh
    k = nsamp(0.3)
    sw = biquad(white(k, rng), 'bp', sweep_exp(800.0, 4000.0, k), 1.2)
    put(out, mul(sw, envelope(k, 0.06, 0.1, 0.05)), 0.2, 0.5)
    return [softclip(out, 1.5)]


def sfx_zombie_death():
    rng = random.Random(1405)
    n = nsamp(0.90)
    out = zeros(n)
    # falling-pitch gurgle, bubbling amplitude, fading out
    jit = smooth_noise(n, rng, 14.0)
    f = [a * (1.0 + 0.06 * j) for a, j in zip(sweep_exp(220.0, 60.0, n), jit)]
    pulse, _ = osc('pulse', f, n, 0.0, 0.4)
    saw, _ = osc('saw', f, n)
    src = [0.4 * p + 0.6 * s for p, s in zip(pulse, saw)]
    fo1 = biquad(src, 'bp', 500.0, 2.5)
    fo2 = biquad(src, 'bp', 1100.0, 3.5)
    am = [1.0 - 0.35 * (1.0 + math.sin(TAU * 14.0 * i / SR)) for i in range(n)]
    gurgle = mul(mul([a + 0.6 * b for a, b in zip(fo1, fo2)], am), envelope(n, 0.004, 0.35, 0.05))
    put(out, softclip(gurgle, 2.0), 0.0, 0.9)
    for _ in range(9):                                     # bubbles
        t = rng.uniform(0.05, 0.7)
        put(out, tone(rng.uniform(200.0, 500.0), 0.04, tau=0.02, amp=0.2, attack=0.001), t)
    k = nsamp(0.5)
    breath = biquad(white(k, rng), 'bp', 900.0, 1.0)
    put(out, mul(breath, envelope(k, 0.02, 0.2, 0.05)), 0.05, 0.15)
    return [out]


# ------------------------------------------------------------- game flow

def sfx_wave_start():
    rng = random.Random(1501)
    n = nsamp(2.50)
    out = zeros(n)
    # ominous horn: two detuned saws (110 / 116 Hz) with 5 Hz vibrato, closing filter, slow swell
    vib = [1.0 + 0.004 * math.sin(TAU * 5.0 * i / SR) for i in range(n)]
    sa, _ = osc('saw', [110.0 * v for v in vib], n)
    sb, _ = osc('saw', [116.0 * v for v in vib], n)
    horn = biquad([0.4 * a + 0.4 * b for a, b in zip(sa, sb)], 'lp', sweep_lin(2200.0, 700.0, n), 0.9)
    put(out, mul(horn, envelope(n, 0.6, None, 0.9)), 0.0, 0.5)
    # deep sub hit at the start
    put(out, tone(decay_freq(55.0, 42.0, 0.2, nsamp(1.5)), tau=0.4, amp=1.0, attack=0.002, release=0.02), 0.0)
    put(out, noise_burst(rng, 0.05, 'hp', 2000.0, 0.7, 0.01, 0.5, 0.0005, 0.002), PRE_S)
    return [softclip(out, 1.3)]


def sfx_wave_complete():
    n = nsamp(1.50)
    out = zeros(n)
    # relieved ascending arpeggio, C major triad (C5 E5 G5 C6)
    for t0, f in ((0.0, 523.25), (0.14, 659.25), (0.28, 783.99), (0.42, 1046.5)):
        put(out, tone(f, 0.5, tau=0.22, amp=0.5, attack=0.003, release=0.03), t0)
        put(out, tone(2.0 * f, 0.5, tau=0.10, amp=0.10, attack=0.003, release=0.03), t0)
    # soft pad under it
    pad = zeros(n)
    for f in (261.63, 329.63, 392.0):
        put(pad, tone(f, 1.5, amp=0.12, attack=0.4, release=0.4), 0.0)
        put(pad, tone(2.0 * f, 1.5, amp=0.03, attack=0.4, release=0.4), 0.0)
    pad = biquad(pad, 'lp', 1500.0, 0.7)
    out = [a + b for a, b in zip(out, pad)]
    return [softclip(out, 1.0)]


def sfx_match_start():
    rng = random.Random(1502)
    n = nsamp(1.50)
    out = zeros(n)
    k = nsamp(1.10)
    riser = [(i / float(k)) ** 1.5 for i in range(k)]
    saw, _ = osc('saw', sweep_exp(150.0, 900.0, k), k)
    saw = biquad(saw, 'lp', sweep_exp(300.0, 5000.0, k), 0.8)
    put(out, mul(saw, riser), 0.0, 0.5)
    nz = biquad(white(k, rng), 'hp', sweep_exp(1000.0, 8000.0, k), 0.7)
    put(out, mul(nz, riser), 0.0, 0.3)
    # hit at 1.10 s: sub, noise crack, minor stab
    put(out, tone(decay_freq(70.0, 45.0, 0.2, nsamp(0.4)), tau=0.25, amp=1.0, attack=0.001, release=0.01), 1.10)
    put(out, noise_burst(rng, 0.08, 'hp', 2000.0, 0.7, 0.02, 0.6, 0.0002, 0.003), 1.10)
    k = nsamp(0.6)
    for f in (220.0, 330.0):
        s, _ = osc('saw', f, k)
        s = biquad(s, 'lp', 2200.0, 0.7)
        put(out, mul(s, envelope(k, 0.002, 0.18, 0.02)), 1.10, 0.2)
    return [softclip(out, 1.2)]


def sfx_match_victory():
    n = nsamp(2.50)
    env = envelope(n, 0.8, None, 0.6)
    chord = zeros(n)
    for f in (261.63, 329.63, 392.0, 523.25):              # C major, detuned saws
        for det in (-0.004, 0.004):
            s, _ = osc('saw', f * (1.0 + det), n)
            chord = [a + 0.1 * b for a, b in zip(chord, s)]
    out = mul(biquad(chord, 'lp', 2800.0, 0.7), env)
    # shimmer: high partials with 5 Hz flutter
    shimmer = zeros(n)
    for f in (1046.5, 1318.5, 1568.0):
        s, _ = osc('sine', f, n)
        shimmer = [a + 0.08 * b for a, b in zip(shimmer, s)]
    am = [0.5 + 0.5 * math.sin(TAU * 5.0 * i / SR) for i in range(n)]
    out = [a + b for a, b in zip(out, mul(mul(shimmer, am), env))]
    return [softclip(out, 1.0)]


def sfx_match_defeat():
    n = nsamp(2.50)
    env = envelope(n, 0.15, None, 0.8)
    body = zeros(n)
    for f in (220.0, 261.63, 329.63):                       # A minor, gliding down a fourth
        s, _ = osc('saw', sweep_exp(f, f * 0.7492, n), n)
        body = [a + 0.12 * b for a, b in zip(body, s)]
    body = mul(biquad(body, 'lp', sweep_exp(1800.0, 350.0, n), 0.8), env)
    sub = tone(sweep_exp(55.0, 46.0, n), tau=None, amp=0.6, attack=0.3, release=0.8)
    out = [a + b for a, b in zip(body, sub)]
    return [softclip(out, 1.1)]


# ------------------------------------------------- ambience and music loops

def amb_arena_loop():
    """30 s station ambience: periodic drone and creaks, plus random air-handling noise."""
    d_s = 30.0
    d, pre, x = nsamp(d_s), nsamp(LOOP_EXTRA_S), nsamp(LOOP_EXTRA_S)
    n = pre + d + x
    tt = [(i - pre) / float(SR) for i in range(n)]

    # 1. periodic bed: two detuned saws near 55 Hz plus a sub, low-passed with a slow sweep.
    #    All frequencies sit on the 1/30 Hz grid, so the bed repeats exactly every loop.
    drone = []
    for ph, det in ((0.0, 55.1), (math.pi, 54.9)):
        sa, _ = osc('saw', grid(55.0, d_s), n)
        sb, _ = osc('saw', grid(det, d_s), n)
        sub, _ = osc('sine', grid(27.5, d_s), n)
        fc = [215.0 + 55.0 * math.sin(TAU * t / d_s + ph) for t in tt]
        bed = biquad([0.25 * (a + b) for a, b in zip(sa, sb)], 'lp', fc, 0.7)
        drone.append([v + 0.3 * s for v, s in zip(bed, sub)])

    # 2. periodic events: three distant metallic creaks, each panned
    creak = [zeros(n), zeros(n)]
    for t0, f0, f1, pan in ((4.2, 260.0, 180.0, 0.35), (13.7, 300.0, 210.0, 0.7), (22.9, 220.0, 150.0, 0.5)):
        k = nsamp(1.3)
        sw, _ = osc('saw', sweep_exp(f0, f1, k), k)
        ev = scale(biquad(sw, 'lp', 1100.0, 0.8), 0.6)
        for ratio, a in ((1.0, 0.5), (2.32, 0.25), (3.9, 0.12)):
            p, _ = osc('sine', sweep_exp(f0 * ratio, f1 * ratio, k), k)
            ev = [e + a * v for e, v in zip(ev, p)]
        ev = mul(ev, envelope(k, 0.3, None, 0.5))
        place_periodic(creak[0], ev, t0, d_s, pre, math.cos(0.5 * math.pi * pan))
        place_periodic(creak[1], ev, t0, d_s, pre, math.sin(0.5 * math.pi * pan))

    # 3. random bed: band-limited air-conditioning hiss, one seed per channel
    air = []
    for ch in range(2):
        r = random.Random(4420 + ch)
        nz = hp1(biquad(white(n, r), 'lp', 520.0, 0.6), 90.0)
        mod = [1.0 + 0.2 * math.sin(TAU * 3.0 * t / d_s + ch) for t in tt]
        air.append(scale(mul(nz, mod), 0.6))

    return loop_mix([(drone, False), (creak, False), (air, True)], d, pre, x)


# Music: 120 BPM, 16 bars (32 s). Chords change every two bars: Am - F - C - G (i-VI-III-VII).
MUS_BPM = 120.0
MUS_CHORDS = (                       # (bass MIDI note, pad voicing MIDI notes)
    (33, (57, 60, 64)),              # Am: A1 bass, pad A3 C4 E4
    (29, (53, 57, 60)),              # F:  F1 bass, pad F3 A3 C4
    (36, (55, 60, 64)),              # C:  C2 bass, pad G3 C4 E4
    (31, (55, 59, 62)),              # G:  G1 bass, pad G3 B3 D4
)
ARP_ORDER = (0, 2, 1, 2, 0, 1, 2, 1)  # indices into the chord's upper tones


def mhz(midi):
    return 440.0 * 2.0 ** ((midi - 69) / 12.0)


def mus_menu_loop():
    """Moody synthwave menu loop: sub bass on the root, soft pad, 16th-note arpeggio."""
    beat = 60.0 / MUS_BPM                 # 0.5 s
    bar = 4.0 * beat                      # 2 s
    step = beat / 4.0                     # 16th note
    slot_len = 2.0 * bar                  # one chord = two bars = 4 s
    d_s = 16.0 * bar                      # 32 s
    d, pre, x = nsamp(d_s), nsamp(LOOP_EXTRA_S), nsamp(LOOP_EXTRA_S)
    n = pre + d + x
    pad_bus, bass_bus = zeros(n), zeros(n)
    arp_bus = [zeros(n), zeros(n)]
    pad_cache, bass_cache, arp_cache = {}, {}, {}
    for slot in range(int(round(d_s / slot_len))):
        t0 = slot * slot_len
        bass_midi, voicing = MUS_CHORDS[slot % len(MUS_CHORDS)]

        # soft pad: two detuned saws per voice, slow attack and release
        for midi in voicing:
            if midi not in pad_cache:
                ln = nsamp(slot_len + 0.35)
                s1, _ = osc('saw', mhz(midi) - 0.5, ln)
                s2, _ = osc('saw', mhz(midi) + 0.5, ln)
                pad_cache[midi] = mul([a + b for a, b in zip(s1, s2)], envelope(ln, 0.3, None, 0.35))
            place_periodic(pad_bus, pad_cache[midi], t0, d_s, pre, 0.09)

        # sub bass on the root, one pulse per beat
        if bass_midi not in bass_cache:
            ln = nsamp(beat)
            b1, _ = osc('sine', mhz(bass_midi), ln)
            b2, _ = osc('sine', 2.0 * mhz(bass_midi), ln)
            bass_cache[bass_midi] = mul([a + 0.15 * b for a, b in zip(b1, b2)],
                                        envelope(ln, 0.005, 0.3, 0.02))
        for bi in range(int(round(slot_len / beat))):
            place_periodic(bass_bus, bass_cache[bass_midi], t0 + bi * beat, d_s, pre, 0.9)

        # 16th-note arpeggio over the upper chord tones, alternating left and right
        upper = [m + 12 for m in voicing]
        for j in range(int(round(slot_len / step))):
            midi = upper[ARP_ORDER[j % len(ARP_ORDER)]]
            if midi not in arp_cache:
                ln = nsamp(0.2)
                s, _ = osc('saw', mhz(midi), ln)
                arp_cache[midi] = mul(s, envelope(ln, 0.002, 0.06, 0.02))
            place_periodic(arp_bus[j % 2], arp_cache[midi], t0 + j * step, d_s, pre, 0.22)

    # processing: pad filter sweep (1/16 Hz, periodic), Haas width, arp filter and ping-pong echo
    pad_fc = [1000.0 + 350.0 * math.sin(TAU * (2.0 / d_s) * (i - pre) / SR) for i in range(n)]
    pad = biquad(pad_bus, 'lp', pad_fc, 0.7)
    k12 = nsamp(0.012)
    pad_r = [0.0] * k12 + pad[:n - k12]
    arp_l = biquad(arp_bus[0], 'lp', 2400.0, 0.7)
    arp_r = biquad(arp_bus[1], 'lp', 2400.0, 0.7)
    echo_l = echo(arp_l, 0.1875, 0.4, 0.5)
    echo_r = echo(arp_r, 0.1875, 0.4, 0.5)
    left = [p + b + a + 0.5 * e for p, b, a, e in zip(pad, bass_bus, arp_l, echo_r)]
    right = [p + b + a + 0.5 * e for p, b, a, e in zip(pad_r, bass_bus, arp_r, echo_l)]
    return loop_mix([([left, right], False)], d, pre, x)


# ------------------------------------------------------------- registry

SOUNDS = [  # (file stem, duration in seconds, renderer)
    ("SFX_PulsePistol_Fire", 0.30, sfx_pulse_pistol_fire),
    ("SFX_PulseRifle_Fire", 0.28, sfx_pulse_rifle_fire),
    ("SFX_Ripper_Fire", 0.16, sfx_ripper_fire),
    ("SFX_Scatter_Fire", 0.70, sfx_scatter_fire),
    ("SFX_Rail_Fire", 1.30, sfx_rail_fire),
    ("SFX_Plasma_Fire", 0.55, sfx_plasma_fire),
    ("SFX_Explosion", 1.80, sfx_explosion),
    ("SFX_Reload", 1.10, sfx_reload),
    ("SFX_MagOut", 0.38, sfx_mag_out),
    ("SFX_MagIn", 0.42, sfx_mag_in),
    ("SFX_Charge", 0.30, sfx_charge),
    ("SFX_DryFire", 0.12, sfx_dry_fire),
    ("SFX_WeaponEquip", 0.35, sfx_weapon_equip),
    ("SFX_Grenade_Throw", 0.30, sfx_grenade_throw),
    ("SFX_Grenade_Bounce", 0.15, sfx_grenade_bounce),
    ("SFX_Impact_Surface", 0.18, sfx_impact_surface),
    ("SFX_Impact_Flesh", 0.18, sfx_impact_flesh),
    ("SFX_HitMarker", 0.07, sfx_hit_marker),
    ("SFX_Headshot", 0.30, sfx_headshot),
    ("SFX_KillConfirm", 0.40, sfx_kill_confirm),
    ("SFX_ShieldBreak", 0.55, sfx_shield_break),
    ("SFX_ShieldRecharge", 1.00, sfx_shield_recharge),
    ("SFX_Respawn", 0.90, sfx_respawn),
    ("SFX_Purchase", 0.45, sfx_purchase),
    ("SFX_Denied", 0.30, sfx_denied),
    ("SFX_UI_Click", 0.05, sfx_ui_click),
    ("SFX_UI_Hover", 0.04, sfx_ui_hover),
    ("SFX_Footstep_01", 0.20, sfx_footstep_01),
    ("SFX_Footstep_02", 0.20, sfx_footstep_02),
    ("SFX_Footstep_03", 0.20, sfx_footstep_03),
    ("SFX_Footstep_04", 0.20, sfx_footstep_04),
    ("SFX_Jump", 0.20, sfx_jump),
    ("SFX_DoubleJump", 0.35, sfx_double_jump),
    ("SFX_Land", 0.22, sfx_land),
    ("SFX_Slide", 0.80, sfx_slide),
    ("SFX_Zombie_Growl_01", 1.00, sfx_zombie_growl_01),
    ("SFX_Zombie_Growl_02", 1.20, sfx_zombie_growl_02),
    ("SFX_Zombie_Growl_03", 1.40, sfx_zombie_growl_03),
    ("SFX_Zombie_Attack", 0.55, sfx_zombie_attack),
    ("SFX_Zombie_Death", 0.90, sfx_zombie_death),
    ("SFX_Wave_Start", 2.50, sfx_wave_start),
    ("SFX_Wave_Complete", 1.50, sfx_wave_complete),
    ("SFX_Match_Start", 1.50, sfx_match_start),
    ("SFX_Match_Victory", 2.50, sfx_match_victory),
    ("SFX_Match_Defeat", 2.50, sfx_match_defeat),
    ("AMB_Arena_Loop", 30.0, amb_arena_loop),
    ("MUS_Menu_Loop", 32.0, mus_menu_loop),
]


# ------------------------------------------------------------------ output

def render_one(stem, dur, fn):
    """Render one sound to its final PCM-ready channels."""
    chans = [fit(c, nsamp(dur)) for c in fn()]
    if stem.startswith('SFX_'):
        return normalize([fade_edges(c) for c in chans], -1.0)
    return normalize(chans, -3.0)


def describe(path):
    """Read a written file back and report (name, seconds, channels, bits, rate, peak dBFS)."""
    with wave.open(path, 'rb') as w:
        nch, sr, nf = w.getnchannels(), w.getframerate(), w.getnframes()
        bits = 8 * w.getsampwidth()
        raw = w.readframes(nf)
    pcm = array.array('h')
    pcm.frombytes(raw)
    if sys.byteorder == 'big':
        pcm.byteswap()
    pk = max((abs(v) for v in pcm), default=0)
    db = 20.0 * math.log10(pk / 32767.0) if pk else float('-inf')
    return os.path.basename(path), nf / float(sr), nch, bits, sr, db


def main(argv=None):
    here = os.path.dirname(os.path.abspath(__file__))
    default_out = os.path.normpath(os.path.join(here, '..', '..', 'RawAssets', 'Audio'))
    ap = argparse.ArgumentParser(description='Synthesize the ShoterAAA sound effects and loops.')
    ap.add_argument('--only', action='append', metavar='NAME',
                    help='render only this sound, e.g. Rail_Fire or SFX_Rail_Fire.wav (repeatable)')
    ap.add_argument('--out', default=default_out, help='output folder (default: %(default)s)')
    ap.add_argument('--list', action='store_true', help='print sound names and exit')
    args = ap.parse_args(argv)

    if args.list:
        for stem, _, _ in SOUNDS:
            print(stem)
        return 0

    wanted = None
    if args.only:
        wanted = []
        for name in args.only:
            low = name.strip().lower()
            wanted.append(low[:-4] if low.endswith('.wav') else low)
        valid = set()
        for stem, _, _ in SOUNDS:
            low = stem.lower()
            valid.update((low, low.split('_', 1)[1]))
        bad = [w for w in wanted if w not in valid]
        if bad:
            print('Unknown sound(s): ' + ', '.join(bad) + '  (see --list)', file=sys.stderr)
            return 2

    def selected(stem):
        if not wanted:
            return True
        low = stem.lower()
        return any(w in (low, low.split('_', 1)[1]) for w in wanted)

    os.makedirs(args.out, exist_ok=True)
    rows = []
    for stem, dur, fn in SOUNDS:
        if not selected(stem):
            continue
        t0 = time.time()
        chans = render_one(stem, dur, fn)
        path = os.path.join(args.out, stem + '.wav')
        write_wav(path, chans)
        rows.append(describe(path))
        print('rendered %-24s %6.1f s' % (stem, time.time() - t0), flush=True)

    print()
    print('%-28s %9s %3s %5s %6s %10s' % ('File', 'Dur (s)', 'Ch', 'Bits', 'Rate', 'Peak dBFS'))
    for name, secs, nch, bits, sr, db in rows:
        print('%-28s %9.3f %3d %5d %6d %10.2f' % (name, secs, nch, bits, sr, db))
    print('\nWrote %d file(s) to %s' % (len(rows), args.out))
    return 0


if __name__ == '__main__':
    sys.exit(main())
