#!/usr/bin/env python3
"""Replacement sounds: ambience synthesized from noise and tones, chosen by the original's name.

sounds_table.toml lists each sound with the only things measured from the original: length and
loudness (--table refreshes them from work/out/Sounds, so it regenerates without the game).
Output: Tools/mfport/work/generated/sounds/<package>/<name>.wav, imported by
`mfport.py import --steps ambience`. Deterministic: the seed is the sound's name.

    python3 Tools/artgen/ambience.py --table <list of /…/Sounds/<package>/<name> references>
    python3 Tools/artgen/ambience.py
"""
from __future__ import annotations

import re
import sys
import tomllib
import wave
import zlib
from pathlib import Path

import numpy as np

from lookalike import MFPORT

HERE = Path(__file__).resolve().parent
TABLE = HERE / 'sounds_table.toml'
OUT = MFPORT / 'work' / 'generated' / 'sounds'
RATE = 22050


def band(rng, n: int, lo: float, hi: float) -> np.ndarray:
    """White noise kept between two frequencies, peak 1; wraps around, so it loops."""
    spec = np.fft.rfft(rng.standard_normal(n))
    f = np.fft.rfftfreq(n, 1 / RATE)
    spec *= np.exp(-((np.maximum(lo - f, 0) / max(lo * 0.3, 1)) ** 2) - ((np.maximum(f - hi, 0) / (hi * 0.3)) ** 2))
    out = np.fft.irfft(spec, n)
    return out / max(np.abs(out).max(), 1e-6)


def slow(rng, n: int, rate: float, lo: float = 0.3) -> np.ndarray:
    """A random level wandering about `rate` times a second, lo..1."""
    v = band(rng, n, 0.01, rate)
    return lo + (1 - lo) * (v - v.min()) / max(np.ptp(v), 1e-6)


def tone(n: int, freq, shape: str = 'sine') -> np.ndarray:
    phase = 2 * np.pi * np.cumsum(np.full(n, freq) if np.isscalar(freq) else freq) / RATE
    return np.sin(phase) if shape == 'sine' else 2 * ((phase / (2 * np.pi)) % 1) - 1


def hits(rng, n: int, per_second: float, length: float, freq: float) -> np.ndarray:
    """Short decaying tones at random times: drips, sparks, knocks."""
    out = np.zeros(n)
    m = int(length * RATE)
    env = np.exp(-np.linspace(0, 7, m))
    for _ in range(max(1, int(per_second * n / RATE))):
        at = rng.integers(0, max(n - m, 1))
        f = freq * rng.uniform(0.7, 1.4)
        out[at:at + m] += env[:n - at] * np.sin(2 * np.pi * f * np.arange(m)[:n - at] / RATE) * rng.uniform(0.4, 1)
    return out


def wind(rng, n):
    return band(rng, n, 120, 900) * slow(rng, n, 0.5) + 0.25 * band(rng, n, 700, 1500) * slow(rng, n, 0.8, 0)


def water(rng, n):
    return band(rng, n, 300, 4500) * slow(rng, n, 2, 0.5) + 0.5 * band(rng, n, 80, 400) * slow(rng, n, 0.6)


def lapping(rng, n):
    return band(rng, n, 200, 1800) * slow(rng, n, 0.7, 0.05) ** 2


def rumble(rng, n):
    return band(rng, n, 30, 220) * slow(rng, n, 0.4, 0.6)


def room(rng, n):
    return band(rng, n, 40, 500) * slow(rng, n, 0.3, 0.6) + 0.3 * band(rng, n, 400, 1600) * slow(rng, n, 0.5, 0.2)


def hum(rng, n):
    return sum(tone(n, 50.0 * h) / h for h in (1, 2, 3, 4, 6)) * 0.5 + 0.04 * band(rng, n, 500, 4000)


def computers(rng, n):
    beeps = np.zeros(n)
    for _ in range(max(2, n // RATE)):
        at, m = rng.integers(0, n - 2000), 1500
        beeps[at:at + m] += tone(m, rng.choice((880.0, 1175.0, 1568.0))) * 0.3
    return hum(rng, n) * 0.5 + beeps


def fans(rng, n):
    return band(rng, n, 90, 900) * (0.65 + 0.35 * tone(n, 11.0)) + 0.3 * tone(n, 55.0)


def engine(rng, n):
    return tone(n, 48.0, 'saw') * 0.5 + tone(n, 72.5, 'saw') * 0.3 + band(rng, n, 60, 700) * (0.6 + 0.4 * tone(n, 8.0))


def machinery(rng, n):
    return engine(rng, n) * 0.6 + 0.6 * hits(rng, n, 4, 0.08, 420)


def fire(rng, n):
    return band(rng, n, 80, 1800) * slow(rng, n, 3, 0.4) + 0.8 * hits(rng, n, 9, 0.02, 2600)


def creak(rng, n):
    out = np.zeros(n)
    for _ in range(max(2, n // (3 * RATE))):
        m, at = int(RATE * rng.uniform(0.3, 0.9)), rng.integers(0, max(n - RATE, 1))
        pitch = np.linspace(rng.uniform(180, 320), rng.uniform(300, 520), m)
        grain = (np.sin(2 * np.pi * np.cumsum(np.full(m, 30.0)) / RATE) > 0.4) * 1.0
        piece = tone(m, pitch, 'saw') * grain * np.hanning(m)
        out[at:at + m] += piece[:n - at]
    return out


def seagulls(rng, n):
    out = np.zeros(n)
    for _ in range(max(3, n // RATE)):
        m, at = int(RATE * rng.uniform(0.25, 0.5)), rng.integers(0, max(n - RATE, 1))
        f = np.linspace(1500, 1000, m) * rng.uniform(0.8, 1.2)
        piece = (tone(m, f) + 0.4 * tone(m, f * 2) + 0.2 * tone(m, f * 3)) * np.hanning(m)
        out[at:at + m] += piece[:n - at]
    return out + 0.05 * band(rng, n, 300, 2000)


def bat(rng, n):
    return hits(rng, n, 6, 0.03, 5000)


def crickets(rng, n):
    return tone(n, 4600.0) * (tone(n, 28.0) > 0.2) * (slow(rng, n, 1.5, 0) > 0.45) + 0.05 * band(rng, n, 200, 900)


def drips(rng, n):
    return hits(rng, n, 1.5, 0.12, 900)


def horn(rng, n):
    env = np.minimum(np.linspace(0, 12, n), 1) * np.minimum(np.linspace(8, 0, n), 1)
    return sum(tone(n, f, 'saw') for f in (311.0, 370.0, 466.0)) * env * 0.4


def foghorn(rng, n):
    env = np.minimum(np.linspace(0, 6, n), 1) * np.minimum(np.linspace(5, 0, n), 1)
    return (tone(n, 110.0, 'saw') + tone(n, 165.0, 'saw') * 0.5) * env


def siren(rng, n):
    f = 700 + 300 * np.sin(2 * np.pi * np.arange(n) * 0.5 / RATE)
    return tone(n, f) * 0.6 + 0.3 * band(rng, n, 100, 800)


def hiss(rng, n):
    return band(rng, n, 2500, 9000) * slow(rng, n, 1.2, 0.7)


def sparks(rng, n):
    return band(rng, n, 2000, 8000) * (slow(rng, n, 14, 0) > 0.6) + hits(rng, n, 10, 0.015, 3500)


def static(rng, n):
    return band(rng, n, 500, 7000) * slow(rng, n, 6, 0.6)


def music(rng, n):
    """A soft four-chord loop on a plucked sine."""
    out, step = np.zeros(n), max(n // 16, 1)
    chords = ((261.6, 329.6, 392.0), (220.0, 261.6, 329.6), (174.6, 220.0, 261.6), (196.0, 246.9, 293.7))
    env = np.exp(-np.linspace(0, 5, step))
    for i in range(16):
        chord = chords[i // 4 % 4]
        piece = (tone(step, chord[i % 3]) + 0.5 * tone(step, chord[0] / 2)) * env
        out[i * step:(i + 1) * step] = piece[:len(out[i * step:(i + 1) * step])]
    return out


def city(rng, n):
    return band(rng, n, 50, 800) * slow(rng, n, 0.4, 0.5) + 0.2 * band(rng, n, 400, 2200) * slow(rng, n, 1, 0.1)


def flyby(rng, n):
    env = np.hanning(n)
    return (band(rng, n, 100, 2200) + 0.3 * tone(n, np.linspace(420, 260, n))) * env


def hammer(rng, n):
    return hits(rng, n, 2.5, 0.09, 520) + 0.3 * rumble(rng, n)


def saw(rng, n):
    env = np.minimum(np.linspace(0, 10, n), 1) * np.minimum(np.linspace(6, 0, n), 1)
    return (tone(n, np.linspace(900, 1300, n), 'saw') * 0.5 + band(rng, n, 1000, 5000) * 0.5) * env


def gravel(rng, n):
    return band(rng, n, 500, 4000) * (slow(rng, n, 12, 0) ** 3) * np.hanning(n)


def gasp(rng, n):
    f = np.linspace(190, 90, n)
    return (tone(n, f, 'saw') * 0.5 + band(rng, n, 500, 2400) * 0.5) * np.hanning(n)


def underwater(rng, n):
    return band(rng, n, 40, 350) * slow(rng, n, 0.6, 0.5) + 0.5 * hits(rng, n, 5, 0.06, 500)


# first match on the sound's name, lower case
RECIPES = [
    ('wind|polar', wind), ('underwater', underwater), ('lapping', lapping),
    ('crashing|heavyflow|pipeflow|sewer', water), ('dripping', drips), ('seagull', seagulls), ('bat$', bat),
    ('cricket', crickets), ('foghorn', foghorn), ('trainhorn', horn), ('police', siren),
    ('gas|hiss', hiss), ('spark|welding|flickering', sparks), ('static', static),
    ('music', music), ('flyby|flyover|passby', flyby), ('hammering', hammer), ('circularsaw', saw),
    ('gravel', gravel), ('death|gasp', gasp), ('creak|crane', creak), ('flame|fire|burn', fire),
    ('computer|technology', computers), ('hum', hum), ('fan|vent|duct', fans),
    ('machinery|mechanical|turbine|industrial', machinery), ('engine|train|boat|aircraft', engine),
    ('city|street|subway|sunny|tower|gym', city), ('rumble', rumble),
]
ONE_SHOTS = 'creak|flyby|flyover|passby|horn|circularsaw|gravel|death|gasp|hammering'


def make(name: str, seconds: float, rms: float) -> np.ndarray:
    n = int(seconds * RATE)
    rng = np.random.default_rng(zlib.crc32(name.lower().encode()))
    lower = name.lower()
    paint = next((fn for pattern, fn in RECIPES if re.search(pattern, lower)), room)
    out = paint(rng, n).astype(np.float64)
    if not re.search(ONE_SHOTS, lower):       # loops: the tail fades into the head
        m = min(n // 8, RATE // 2)
        fade = np.linspace(0, 1, m)
        out[:m] = out[:m] * fade + out[-m:] * (1 - fade)
        out = out[:-m]
    out *= rms / max(np.sqrt(np.mean(out ** 2)), 1e-6)
    return np.clip(out, -1, 1)


def write_table(refs: Path) -> None:
    lines = ['# Replacement sounds (ambience.py): length in seconds and loudness (RMS 0..1) of each original.', '']
    for ref in sorted(set(refs.read_text().split())):
        m = re.search(r'/Sounds/([^/]+)/([^/]+)$', ref)
        if not m:
            continue
        f = MFPORT / 'work' / 'out' / 'Sounds' / m[1] / f'{m[2]}.wav'
        if not f.exists():
            print(f'  ! no original for {ref}')
            continue
        with wave.open(str(f)) as w:
            frames, rate, width, chans = w.getnframes(), w.getframerate(), w.getsampwidth(), w.getnchannels()
            raw = np.frombuffer(w.readframes(frames), {1: np.uint8, 2: np.int16}[width]).astype(np.float64)
        pcm = (raw - 128) / 128 if width == 1 else raw / 32768
        lines += [f'["{m[1]}.{m[2]}"]', f'seconds = {frames / rate:.2f}', f'rms = {np.sqrt(np.mean(pcm ** 2)):.4f}', '']
    TABLE.write_text('\n'.join(lines))
    print(f'[ambience] {sum(line.startswith("[") for line in lines)} sounds -> {TABLE.name}')


def main(argv: list[str]) -> None:
    if '--table' in argv:
        write_table(Path(argv[argv.index('--table') + 1]))
        return
    table = tomllib.loads(TABLE.read_text())
    for key, spec in table.items():
        package, name = key.split('.', 1)
        pcm = make(name, max(spec['seconds'], 0.3), max(spec['rms'], 0.01))
        dst = OUT / package / f'{name}.wav'
        dst.parent.mkdir(parents=True, exist_ok=True)
        with wave.open(str(dst), 'wb') as w:
            w.setnchannels(1)
            w.setsampwidth(2)
            w.setframerate(RATE)
            w.writeframes((pcm * 32767).astype(np.int16).tobytes())
    print(f'[ambience] {len(table)} sounds -> {OUT}')


if __name__ == '__main__':
    main(sys.argv[1:])
