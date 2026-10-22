"""Side-by-side in-engine shots of two aligned maps, zone by zone (Docs/WarehouseMerge.md).

For every zone of the first map (from its level.json) an orthographic camera looks straight
down from under the roofs, which are cut away, so each shot is a textured floor plan
(UE 5.8's ortho clipping doesn't slice exactly at the camera height, so storeys overlap).
The same cameras run in both maps (one offscreen game per map), then each pair is put
side by side in <out>/<zone>_<slice>.png.

Usage: python3 merge_shots.py mf-warehouse mfs-warehouse work/merge/shots [--slices 1000]
Needs both maps imported (`mfport.py import <map> --steps maps`) and the editor built.
"""
import argparse
import json
import subprocess
import sys
from pathlib import Path

from PIL import Image, ImageDraw

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from mflib import config  # noqa: E402

MIN_SIZE = 600.0


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('a')
    ap.add_argument('b')
    ap.add_argument('out')
    ap.add_argument('--slices', default='1000', help='camera heights in cm')
    ap.add_argument('--res', type=int, default=900)
    a = ap.parse_args()
    cfg = config.load()
    lvl = json.loads((cfg.out_dir / 'Maps' / a.a / 'level.json').read_text())
    slices = [float(s) for s in a.slices.split(',')]
    shots = []   # (name, x, y, width, z)
    for z, (lo, hi) in sorted(lvl['zone_bounds'].items(), key=lambda kv: int(kv[0])):
        name = lvl['zones'][int(z)] or f'zone{z}'
        if hi[0] - lo[0] < MIN_SIZE or hi[1] - lo[1] < MIN_SIZE or name.startswith('SkyZone'):
            continue
        for s in slices:
            if lo[2] < s < hi[2] + 200:
                width = max(hi[0] - lo[0], hi[1] - lo[1]) * 1.05
                shots.append((f'{name}_{int(s)}', (lo[0] + hi[0]) / 2, (lo[1] + hi[1]) / 2, width, s))

    exe = cfg.ue_root / 'Engine/Binaries/Linux/UnrealEditor'
    shot_dir = cfg.uproject.parent / 'Saved/Screenshots/LinuxEditor'
    for m in (a.a, a.b):
        cmds = ['@2:viewmode unlit', '@2.1:ShowHUD']
        t = 3.0
        for name, x, y, w, z in shots:
            cmds.append(f'@{t:.1f}:DFTopView {x:.0f} {y:.0f} {w:.0f} {z:.0f}')
            cmds.append(f'@{t + 0.7:.1f}:DFShot merge_{m}_{name}')
            t += 1.0
        args = [str(exe), str(cfg.uproject), f'/MobileForcesClassic/Maps/{m}?game=/Script/DesktopForces.DeathMatch?Bots=0',
                '-game', '-RenderOffscreen', f'-ResX={a.res}', f'-ResY={a.res}', '-nosplash',
                f'-DFExec={"|".join(cmds)}', f'-DFQuitAfter={t + 2:.0f}']
        print(f'[{m}] {len(shots)} shots ...')
        subprocess.run(args, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, timeout=900)

    out = Path(a.out)
    out.mkdir(parents=True, exist_ok=True)
    for name, *_ in shots:
        imgs = [shot_dir / f'merge_{m}_{name}.png' for m in (a.a, a.b)]
        if not all(p.exists() for p in imgs):
            print(f'  ! missing shot {name}')
            continue
        ia, ib = (Image.open(p).convert('RGB') for p in imgs)
        sheet = Image.new('RGB', (ia.width + ib.width + 12, ia.height + 26), (12, 12, 16))
        sheet.paste(ia, (0, 26))
        sheet.paste(ib, (ia.width + 12, 26))
        d = ImageDraw.Draw(sheet)
        d.text((6, 6), f'{name}  -  {a.a} (original)', fill=(200, 210, 255))
        d.text((ia.width + 18, 6), f'{a.b} (remake; Valve props left out)', fill=(255, 210, 160))
        sheet.save(out / f'{name}.png')
    print(f'{len(shots)} pairs -> {out}')


if __name__ == '__main__':
    main()
