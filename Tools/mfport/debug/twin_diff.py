"""What world geometry one release of a map has that the other lacks (mflib/twins.py).

Both maps must share coordinates (twin releases: MF-BattleBayDarkV1 / mf-battlebaynoonv1).
Reads the original .umf BSPs. For each direction it prints the polygons only that release
has, split into pure additions (nothing changed nearby in the other) and reworked spots
(both changed there), grouped by texture and brush. Pure additions are what a merge could
add; reworked spots need a choice (map_edits.toml layout_from takes one release's layout).

Usage: python3 debug/twin_diff.py MF-BattleBayDarkV1 mf-battlebaynoonv1
"""
import collections
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from mflib import config, twins  # noqa: E402
from mflib.bsp import LevelModel  # noqa: E402
from mflib.upkg import Package  # noqa: E402


def main(a: str, b: str):
    cfg = config.load()
    model = {m: LevelModel(Package(str(cfg.game_dir / 'Maps' / f'{m}.umf'))) for m in (a, b)}
    polys = {m: model[m].polygons() for m in (a, b)}
    extra = {a: twins.extra_polygons(model[a], polys[a], polys[b]), b: twins.extra_polygons(model[b], polys[b], polys[a])}
    for base, other in ((a, b), (b, a)):
        pure = twins.pure_additions(extra[base], extra[other])
        print(f'{other} has {len(extra[base])} polygon pieces {base} lacks: {len(pure)} pure additions, {len(extra[base]) - len(pure)} in reworked spots')
        groups = collections.defaultdict(list)
        for p in pure:
            groups[((p.texture or 'None').split('.')[-1], hex(p.poly_flags), p.brush)].append(p)
        for (tex, flags, brush), ps in sorted(groups.items(), key=lambda kv: -len(kv[1])):
            lo, hi = twins._bounds([v for p in ps for v in p.vertices])
            print(f'  {len(ps):4}  {tex:16} flags {flags:10} {brush:10} uu {[round(c) for c in lo]} .. {[round(c) for c in hi]}')


if __name__ == '__main__':
    if len(sys.argv) != 3:
        sys.exit(__doc__)
    main(sys.argv[1], sys.argv[2])
