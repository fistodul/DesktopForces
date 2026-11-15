#!/usr/bin/env python3
"""Does a higher Lightmass quality show? Same cameras before and after a bake (Roadmap #16, #24).

    python3 Tools/mfport/debug/bakequality.py <out dir> run <map> [quality=High] [cameras=10]
    python3 Tools/mfport/debug/bakequality.py <out dir> restore <map>
    python3 Tools/mfport/debug/bakequality.py <out dir> report

`run` keeps a copy of the map's baked files, takes screenshots at up to `cameras` player starts
(the ones furthest apart, looking ahead and back, no HUD, no bots) twice with the bake it has
(the second set against the first is the noise two runs differ by anyway: particles, flags,
texture streaming), bakes at `quality`, and takes them again. `report` prints, per map, how far
the new bake's pictures are from the old one's next to that noise, in grey levels of 255: the
mean, the 99th percentile and the worst 32 px tile, as the median camera and the worst camera.
`restore` puts the copied bake back. The map stays at the new bake until then: set `quality` in
map_edits.toml for the ones worth it, restore the rest.

Out dir: outside the project (absolute). Bake times only count on mains power; the conditions
are in each map's result.json.
"""
import json
import shutil
import subprocess
import sys
import time
from pathlib import Path

import numpy as np
from PIL import Image

REPO = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(REPO / 'Tools' / 'mfport'))
SHOTS = REPO / 'Saved' / 'Screenshots' / 'LinuxEditor'
TILE = 32


def power() -> dict:
    def read(pattern):
        files = sorted(Path('/sys/class/power_supply').glob(pattern))
        return files[0].read_text().strip() if files else None
    return {'mains': read('AC*/online') == '1', 'battery': read('BAT*/capacity')}


def cameras(game_map: str, count: int) -> list[tuple[float, float, float, float]]:
    """x, y, z, yaw of the starts furthest apart, each looking ahead and back."""
    level = json.loads((REPO / 'Tools/mfport/work/out/Maps' / game_map / 'level.json').read_text())
    starts = [s for s in level['spawns'] if str(s.get('class', '')).endswith('DFPlayerStart')]
    if not starts:
        raise SystemExit(f'{game_map}: no player starts in level.json')
    chosen = [starts[0]]
    while len(chosen) < min(count, len(starts)):   # furthest from those taken already
        chosen.append(max(starts, key=lambda s: min(sum((a - b) ** 2 for a, b in zip(s['location'], c['location'])) for c in chosen)))
    out = []
    for s in chosen:
        x, y, z = s['location']
        for turn in (0.0, 180.0):
            out.append((x, y, z + 60.0, (s['rotation']['yaw'] + turn) % 360.0))
    return out


def shoot(out: Path, game_map: str, tag: str, cams) -> None:
    """Screenshots bq_<tag>_NN at the cameras into out/tag."""
    first = cams[0]
    cmds = ['@5:ShowHUD', f'@5.5:DFTeleport {first[0]:.0f} {first[1]:.0f} {first[2]:.0f}']   # the soldier in one place every run
    t = 7.0
    for i, (x, y, z, yaw) in enumerate(cams):
        cmds.append(f'@{t:.1f}:DFCam {x:.0f} {y:.0f} {z:.0f} {yaw:.0f} -4')
        cmds.append(f'@{t + 2.2:.1f}:DFShot bq_{tag}_{i:02d}')   # 2 s: textures stream in after the jump
        t += 2.8
    args = ['python3', str(REPO / 'Tools' / 'play.py'), game_map, 'dm', '0', '-RenderOffscreen', '-windowed', '-ResX=1280', '-ResY=720',
            '-nosound', '-unattended', '-DFExec=' + '|'.join(cmds), f'-DFQuitAfter={t + 2:.0f}', f'-abslog={out / (tag + ".log")}']
    subprocess.run(args, cwd=REPO, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, timeout=1500)
    (out / tag).mkdir(parents=True, exist_ok=True)
    got = 0
    for i in range(len(cams)):
        src = SHOTS / f'bq_{tag}_{i:02d}.png'
        if src.exists():
            shutil.move(str(src), str(out / tag / f'{i:02d}.png'))
            got += 1
    if got != len(cams):
        raise SystemExit(f'{game_map} {tag}: {got} of {len(cams)} screenshots (see {out / (tag + ".log")})')


def baked_files(game_map: str) -> list[Path]:
    maps = REPO / 'Content' / 'Maps'
    return [p for p in (maps / f'{game_map}.umap', maps / f'{game_map}_BuiltData.uasset') if p.exists()]


def compare(a: Path, b: Path) -> list[dict]:
    """Per camera: mean, 99th percentile and worst-tile mean of the grey difference (of 255)."""
    rows = []
    for fa in sorted(a.glob('*.png')):
        fb = b / fa.name
        if not fb.exists():
            continue
        ga = np.asarray(Image.open(fa).convert('RGB'), dtype=np.float32).mean(axis=2)
        gb = np.asarray(Image.open(fb).convert('RGB'), dtype=np.float32).mean(axis=2)
        d = np.abs(ga - gb)
        h, w = (d.shape[0] // TILE) * TILE, (d.shape[1] // TILE) * TILE
        tiles = d[:h, :w].reshape(h // TILE, TILE, w // TILE, TILE).mean(axis=(1, 3))
        ty, tx = np.unravel_index(int(tiles.argmax()), tiles.shape)
        rows.append({'camera': fa.stem, 'mean': float(d.mean()), 'p99': float(np.percentile(d, 99)), 'tile': float(tiles.max()),
                     'tile_at': [int(tx * TILE), int(ty * TILE)]})
    return rows


def summary(rows: list[dict]) -> dict:
    return {k: {'median': float(np.median([r[k] for r in rows])), 'worst': float(max(r[k] for r in rows))} for k in ('mean', 'p99', 'tile')}


def run(out: Path, game_map: str, quality: str, count: int) -> None:
    from mflib import config, ue
    out = out / game_map
    out.mkdir(parents=True, exist_ok=True)
    backup = out / 'backup'
    if not backup.exists():   # the bake the map came with: kept once, whatever is tried after
        backup.mkdir()
        for f in baked_files(game_map):
            shutil.copy2(f, backup / f.name)
    cams = cameras(game_map, count)
    shoot(out, game_map, 'old', cams)
    shoot(out, game_map, 'old_again', cams)
    before = power()
    start = time.time()
    rc = ue.build_lighting(config.load(), [game_map], quality)
    minutes = (time.time() - start) / 60.0
    if rc:
        raise SystemExit(f'{game_map}: the {quality} bake failed (rc {rc}); `restore` puts the old one back')
    shoot(out, game_map, quality, cams)
    noise, change = compare(out / 'old', out / 'old_again'), compare(out / 'old', out / quality)
    result = {'map': game_map, 'quality': quality, 'bake_minutes': round(minutes, 1), 'power_before': before, 'power_after': power(),
              'cameras': len(cams), 'noise': summary(noise), 'change': summary(change), 'per_camera': {'noise': noise, 'change': change}}
    (out / 'result.json').write_text(json.dumps(result, indent=1))
    print(line(result))


def line(r: dict) -> str:
    def part(s):
        return ' '.join(f"{k} {s[k]['median']:.2f}/{s[k]['worst']:.2f}" for k in ('mean', 'p99', 'tile'))
    mains = 'mains' if r['power_before']['mains'] and r['power_after']['mains'] else 'BATTERY'
    return f"{r['map']:24} {r['quality']:10} {r['bake_minutes']:6.1f} min ({mains})  noise: {part(r['noise'])}  change: {part(r['change'])}"


def restore(out: Path, game_map: str) -> None:
    backup = out / game_map / 'backup'
    files = sorted(backup.glob('*'))
    if not files:
        raise SystemExit(f'{game_map}: nothing kept in {backup}')
    for f in files:
        shutil.copy2(f, REPO / 'Content' / 'Maps' / f.name)
    print(f'{game_map}: {len(files)} files back from {backup}')


def main() -> None:
    if len(sys.argv) < 3:
        raise SystemExit(__doc__)
    out, cmd, rest = Path(sys.argv[1]).resolve(), sys.argv[2], sys.argv[3:]
    if cmd == 'run' and rest:
        run(out, rest[0], rest[1] if len(rest) > 1 else 'High', int(rest[2]) if len(rest) > 2 else 10)
    elif cmd == 'restore' and rest:
        restore(out, rest[0])
    elif cmd == 'report':
        print('median camera / worst camera, grey levels of 255')
        for f in sorted(out.glob('*/result.json')):
            print(line(json.loads(f.read_text())))
    else:
        raise SystemExit(__doc__)


if __name__ == '__main__':
    main()
