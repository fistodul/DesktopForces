"""map_edits.toml death_pits from the DFFindPits cheat's Saved/Pits/<map>.json.

    python3 Tools/play.py <map> dm 0 -RenderOffscreen -nosound "-DFExec=@5:DFFindPits" -DFQuitAfter=12
    python3 Tools/mfport/debug/deathpits.py <map>...      # prints the TOML to paste

A death pit: every way in is a fall that killed a 100-health soldier (MF FallDeathThreshold), the
pit not in a pain zone already nor under water (the water broke the fall, in MF too: the mine
shafts of mf-AbandonedMine), at least 5 m2 of floor. Pits with a gentler way in were traps for
everyone in MF too (stuck alive) and stay as they are. DFFindPits has joined the boxes already
(as few as reach no floor soldiers walk on and get back from).
"""
from __future__ import annotations

import json
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
# MF FallDeathThreshold -1200 uu/s = -2700 cm/s under -950 uu/s2 = -2137.5 cm/s2: 1705 cm
LETHAL_FALL = 2700.0 ** 2 / (2 * 2137.5)
MIN_AREA = 5.0


MAX_BOXES = 50


def is_death_pit(pit: dict) -> bool:
    """Left out for a look by hand: a pit whose boxes would reach floor soldiers walk on (some
    tiles clash: mf-dockyard's tanks) or that takes more than MAX_BOXES (sloped ground)."""
    return (pit['min_fall'] >= LETHAL_FALL and not pit['hurts'] and not pit.get('water') and pit['area'] >= MIN_AREA
            and 0 < len(pit['boxes']) <= MAX_BOXES and not pit['clashes'])


def death_pits(pits: list[dict]) -> list[list[float]]:
    return [b for p in pits if is_death_pit(p) for b in p['boxes']]


def toml(map_name: str, boxes: list[list[float]]) -> str:
    rows = ',\n'.join(f'  {{ min = [{b[0]:.0f}.0, {b[1]:.0f}.0, {b[2]:.0f}.0], max = [{b[3]:.0f}.0, {b[4]:.0f}.0, {b[5]:.0f}.0] }}' for b in boxes)
    return f'["{map_name}"]\ndeath_pits = [\n{rows},\n]\n'


def main(names: list[str]) -> None:
    for name in names:
        pits = json.loads((ROOT / 'Saved' / 'Pits' / f'{name}.json').read_text())
        boxes = death_pits(pits)
        left = [p for p in pits if p['min_fall'] >= LETHAL_FALL and not p['hurts'] and not p.get('water') and p['area'] >= MIN_AREA and not is_death_pit(p)]
        print(f'# {name}: {len(boxes)} boxes' + ''.join(f'\n# left out: floor at {p["min"]}, {len(p["boxes"])} boxes, {p["clashes"]} clashing tiles' for p in left))
        if boxes:
            print(toml(name, boxes))


if __name__ == '__main__':
    main(sys.argv[1:])
