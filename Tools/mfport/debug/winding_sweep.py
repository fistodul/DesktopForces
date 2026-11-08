"""Which converted static meshes `t3d.orient_triangles` would wind differently now: reruns it over
work/out/Maps/<map>/meshes/*.mfmesh.json (the winding they were converted with) and lists, per
map, the meshes whose triangles it turns. Those maps need `rebake` to get the change.

With --check it also counts, on the most changed meshes, the faces the ray test finds facing
into their solid before and after (fewer after = the change fixes them).

Usage: python3 debug/winding_sweep.py [map ...] [--check N]
"""
import argparse
import collections
import random
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from mflib import config, mfmesh, t3d  # noqa: E402


def triangles(path: Path) -> list[t3d.SMTriangle]:
    mesh = mfmesh.read(path)
    return [t3d.SMTriangle('', 0, 0, [tuple(v) for v in tri]) for tris in mesh.tris.values() for tri in tris]


def rewound(tris: list[t3d.SMTriangle]) -> list[t3d.SMTriangle]:
    return t3d.orient_triangles([t3d.SMTriangle('', 0, 0, list(t.verts)) for t in tris])


def inward(tris: list[t3d.SMTriangle], samples: int = 400) -> int:
    cache: dict = {}
    picks = range(len(tris)) if len(tris) <= samples else random.Random(1).sample(range(len(tris)), samples)
    return sum(t3d._parity_side(tris, {i: False}, cache, samples=1) < 0 for i in picks)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('maps', nargs='*')
    ap.add_argument('--check', type=int, default=0, help='ray-test the N most changed meshes')
    ap.add_argument('--max-triangles', type=int, default=20000)
    a = ap.parse_args()
    root = config.load().out_dir / 'Maps'
    dirs = [root / m for m in a.maps] if a.maps else sorted(p for p in root.iterdir() if p.is_dir())
    changed = collections.defaultdict(list)
    for d in dirs:
        for path in sorted((d / 'meshes').glob('*.mfmesh.json')):
            tris = triangles(path)
            if len(tris) > a.max_triangles:
                continue
            turned = sum(before.verts != after.verts for before, after in zip(tris, rewound(tris)))
            if turned:
                changed[d.name].append((path.name.split('.')[0], turned, len(tris)))
    for m, meshes in sorted(changed.items(), key=lambda kv: -sum(n for _, n, _ in kv[1])):
        print(f'{m}: {sum(n for _, n, _ in meshes)} triangles in {len(meshes)} meshes:',
              ', '.join(f'{name} {n}/{total}' for name, n, total in sorted(meshes, key=lambda x: -x[1])[:8]))
    print(f'{len(changed)} maps, {sum(len(v) for v in changed.values())} meshes')
    if a.check:
        rows = sorted(((n, m, name) for m, v in changed.items() for name, n, _ in v), reverse=True)[:a.check]
        total_before = total_after = 0
        for _, m, name in rows:
            tris = triangles(root / m / 'meshes' / f'{name}.mfmesh.json')
            before, after = inward(tris), inward(rewound(tris))
            total_before += before
            total_after += after
            print(f'  {m}/{name}: inward {before} -> {after}{"  WORSE" if after > before else ""}')
        print(f'  sampled inward faces {total_before} -> {total_after}')


if __name__ == '__main__':
    main()
