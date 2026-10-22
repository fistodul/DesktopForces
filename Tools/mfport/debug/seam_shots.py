"""Screenshots of the worst seams found by seams.py, for review (Docs/WarehouseMerge.md).

Each seam cluster gets a perspective shot from 8.5 m away and 5 m up, looking at it.
Writes <out>/seam_<n>.png and a contact sheet <out>/sheet.png.

Usage: python3 seam_shots.py mf-warehouse-merged [--top 12] [--seams work/merge/seams]
"""
import argparse
import json
import math
import subprocess
import sys
from pathlib import Path

from PIL import Image, ImageDraw

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from mflib import config  # noqa: E402


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('map')
    ap.add_argument('--top', type=int, default=12)
    ap.add_argument('--seams', default='work/merge/seams')
    ap.add_argument('--zmax', type=float, default=900.0)
    a = ap.parse_args()
    cfg = config.load()
    out = Path(a.seams)
    clusters = [c for c in json.loads((out / 'seams.json').read_text()) if c['z'] < a.zmax][:a.top]
    cmds, t = ['@2:viewmode unlit', '@2.1:ShowHUD'], 3.0
    for n, c in enumerate(clusters):
        cx, cy, cz = c['x'] - 600, c['y'] - 600, c['z'] + 500
        yaw = math.degrees(math.atan2(c['y'] - cy, c['x'] - cx))
        pitch = -math.degrees(math.atan2(500, math.hypot(600, 600)))
        cmds += [f'@{t:.1f}:DFCam {cx:.0f} {cy:.0f} {cz:.0f} {yaw:.1f} {pitch:.1f}', f'@{t + 0.7:.1f}:DFShot seam_{n}']
        t += 1.0
    exe = cfg.ue_root / 'Engine/Binaries/Linux/UnrealEditor'
    subprocess.run([str(exe), str(cfg.uproject), f'/MobileForcesClassic/Maps/{a.map}?game=/Script/DesktopForces.DeathMatch?Bots=0',
                    '-game', '-RenderOffscreen', '-ResX=800', '-ResY=500', '-nosplash',
                    f'-DFExec={"|".join(cmds)}', f'-DFQuitAfter={t + 2:.0f}'],
                   stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, timeout=600)
    shots = cfg.uproject.parent / 'Saved/Screenshots/LinuxEditor'
    tiles = []
    for n, c in enumerate(clusters):
        p = shots / f'seam_{n}.png'
        if p.exists():
            im = Image.open(p).convert('RGB')
            im.save(out / f'seam_{n}.png')
            im.thumbnail((400, 250))
            d = ImageDraw.Draw(im)
            d.rectangle([0, 0, 400, 14], fill=(0, 0, 0))
            d.text((3, 1), f"#{n} {c['kind']} n={c['n']} ({c['x']:.0f},{c['y']:.0f},{c['z']:.0f})", fill=(255, 255, 0))
            tiles.append(im)
    cols = 3
    sheet = Image.new('RGB', (cols * 402, ((len(tiles) + cols - 1) // cols) * 252), (0, 0, 0))
    for i, im in enumerate(tiles):
        sheet.paste(im, ((i % cols) * 402, (i // cols) * 252))
    sheet.save(out / 'sheet.png')
    print(f'{len(tiles)} seam shots -> {out}/sheet.png')


if __name__ == '__main__':
    main()
