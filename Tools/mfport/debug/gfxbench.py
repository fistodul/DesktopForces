#!/usr/bin/env python3
"""Graphics options: frame time and picture per setting at fixed cameras.

    python3 Tools/mfport/debug/gfxbench.py <out dir> [scene ...]     # run (4.5 min a scene)
    python3 Tools/mfport/debug/gfxbench.py <out dir> --report        # table of what was run

One game process per scene: every 9 game seconds it sets the scalability groups to Medium plus
one change, takes a screenshot (`DFShot`) and 300 frames of `csvprofile`, uncapped
(`t.MaxFPS 0`), rendering off screen at 1920 x 1080. Medium runs first and last: the two
differ by the run's own drift. The table gives median frame and GPU ms, the change against
Medium and how far the picture moves from Medium's (mean grey levels of 255, HUD rows cut).
Run it on mains power with nothing else busy: on battery the GPU is throttled."""
import csv
import glob
import json
import os
import shutil
import statistics
import subprocess
import sys
from pathlib import Path

import numpy as np
from PIL import Image

REPO = Path(__file__).resolve().parents[3]
OUT = Path.cwd()
BASE = {'sg.AntiAliasingQuality': 1, 'sg.TextureQuality': 1, 'sg.ViewDistanceQuality': 1, 'sg.EffectsQuality': 1,
        'sg.PostProcessQuality': 1, 'sg.ShadowQuality': 1, 'sg.GlobalIlluminationQuality': 1, 'sg.ReflectionQuality': 1,
        'sg.ShadingQuality': 1, 'sg.FoliageQuality': 1, 'r.ScreenPercentage': 100}
CONFIGS = [('medium', {})]
for group, levels in (('AntiAliasing', (0, 2)), ('Texture', (0, 2, 3)), ('ViewDistance', (0, 2, 3)), ('Effects', (0, 3)),
                      ('PostProcess', (0, 3)), ('Shadow', (0, 3)), ('GlobalIllumination', (0, 3)), ('Reflection', (0, 3)),
                      ('Shading', (0, 3))):
    for lv in levels:
        CONFIGS.append((f'{group}{lv}', {f'sg.{group}Quality': lv}))
for pct in (50, 75, 150):
    CONFIGS.append((f'scale{pct}', {'r.ScreenPercentage': pct}))
CONFIGS.append(('low', {k: 0 for k in BASE if k.startswith('sg.')}))
CONFIGS.append(('high', {k: 2 for k in BASE if k.startswith('sg.')}))
CONFIGS.append(('epic', {k: 3 for k in BASE if k.startswith('sg.')}))
CONFIGS.append(('medium_again', {}))
SCENES = {
    'sardine_roof': ('mf-Sardine_City', 'DFCam 20000 9000 1500 200 -35'),
    'sardine_street': ('mf-Sardine_City', 'DFCam 18423 7400 -380 90 8'),
    'warehouse': ('mf-warehouse', 'DFCam 9394 3300 -110 60 0'),
}
STEP = 9.0   # game seconds per setting
FRAMES = 300


def run(scene: str, res=(1920, 1080)):
    global OUT
    game_map, cam = SCENES[scene]
    csv_dir = REPO / 'Saved' / 'Profiling' / 'CSV'
    shots = REPO / 'Saved' / 'Screenshots' / 'LinuxEditor'
    for f in glob.glob(str(csv_dir / '*.csv')):
        os.remove(f)
    cmds = ['@3:DFLoadout', '@4:t.MaxFPS 0', '@4:r.VSync 0', f'@5:{cam}']
    t = 8.0
    for name, change in CONFIGS:
        for k, v in {**BASE, **change}.items():
            cmds.append(f'@{t:.1f}:{k} {v}')
        cmds.append(f'@{t + 2.5:.1f}:DFShot bench_{scene}_{name}')
        cmds.append(f'@{t + 3.5:.1f}:csvprofile frames={FRAMES}')
        t += STEP
    log = OUT / f'{scene}.log'
    args = ['python3', str(REPO / 'Tools' / 'play.py'), game_map, 'tdm', '0', '-RenderOffscreen', '-windowed',
            f'-ResX={res[0]}', f'-ResY={res[1]}', '-nosound', '-unattended', '-DFExec=' + '|'.join(cmds),
            f'-DFQuitAfter={t + 2:.0f}', f'-abslog={log}']
    subprocess.run(args, cwd=REPO, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, timeout=900)
    files = sorted(glob.glob(str(csv_dir / '*.csv')), key=os.path.getmtime)
    rows = {}
    for (name, _), f in zip(CONFIGS, files):
        with open(f, newline='') as fh:
            data = list(csv.reader(fh))
        head = data[0]
        cols = {c: i for i, c in enumerate(head)}
        vals = {c: [] for c in ('FrameTime', 'GameThreadTime', 'RenderThreadTime', 'GPUTime', 'RHIThreadTime') if c in cols}
        for r in data[1:]:
            if len(r) < len(head) or r[0] == head[0]:
                continue
            try:
                for c in vals:
                    vals[c].append(float(r[cols[c]]))
            except ValueError:
                continue
        rows[name] = {c: round(statistics.median(v), 3) for c, v in vals.items() if v}
        rows[name]['frames'] = len(next(iter(vals.values()), []))
    dst = OUT / scene
    dst.mkdir(exist_ok=True)
    for name, _ in CONFIGS:
        s = shots / f'bench_{scene}_{name}.png'
        if s.exists():
            shutil.move(str(s), dst / f'{name}.png')
    (OUT / f'{scene}.json').write_text(json.dumps({'files': len(files), 'configs': len(CONFIGS), 'rows': rows}, indent=1))
    print(scene, 'csv files', len(files), 'of', len(CONFIGS))
    for name, _ in CONFIGS:
        print(f'  {name:22s}', rows.get(name))


def diff(a: Path, b: Path) -> float:
    if not (a.exists() and b.exists()):
        return float('nan')
    x = np.asarray(Image.open(a).convert('RGB'), np.float32)[80:-100]   # without the HUD rows
    y = np.asarray(Image.open(b).convert('RGB'), np.float32)[80:-100]
    return float(np.abs(x - y).mean())


def report():
    scenes = [p.stem for p in sorted(OUT.glob('*.json'))]
    data = {s: json.loads((OUT / f'{s}.json').read_text())['rows'] for s in scenes}
    names = list(next(iter(data.values())))
    print('setting'.ljust(22) + ''.join(f'{s[:16]:>34s}' for s in scenes))
    print(' ' * 22 + ''.join(f'{"frame ms":>9s}{"gpu ms":>8s}{"fps":>6s}{"vs med":>6s}{"pic":>5s}' for _ in scenes))
    for n in names:
        line = n.ljust(22)
        for s in scenes:
            r, m = data[s].get(n, {}), data[s]['medium']
            if not r:
                line += ' ' * 34
                continue
            ft, gpu = r.get('FrameTime', 0), r.get('GPUTime', 0)
            line += f'{ft:9.2f}{gpu:8.2f}{1000 / ft if ft else 0:6.0f}{(ft / m["FrameTime"] - 1) * 100:+6.0f}{diff(OUT / s / f"{n}.png", OUT / s / "medium.png"):5.1f}'
        print(line)


if __name__ == '__main__':
    if len(sys.argv) < 2:
        sys.exit(__doc__)
    OUT = Path(sys.argv[1]).resolve()
    OUT.mkdir(parents=True, exist_ok=True)
    if '--report' in sys.argv:
        report()
    else:
        for s in [a for a in sys.argv[2:] if not a.startswith('--')] or SCENES:
            run(s)
        report()
