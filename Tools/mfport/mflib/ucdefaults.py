"""A class's effective default properties, read from the exported UnrealScript (.uc) sources.

Exported `defaultproperties` list only what differs from the parent class, struct fields too:
Pistol's `PlayerViewOffset=(X=3,Y=-1)` under Weapon's `(X=30,Z=-5)` means Z = -5. This walks the
`extends` chain from the root down and merges every value (struct fields one by one), keeping
which class set each, so a port takes the value the game really used.

    python3 Tools/mfport/mfport.py defaults Pistol [PlayerViewOffset ...]
"""
from __future__ import annotations

import re
from pathlib import Path

_CLASS = re.compile(r'^\s*class\s+(\w+)\s+(?:extends|expands)\s+(?:\w+\.)?(\w+)', re.I | re.M)
_ROOT = re.compile(r'^\s*class\s+(\w+)\b', re.I | re.M)
_PROP = re.compile(r'^\s*(\w+(?:\(\d+\))?)\s*=\s*(.*?)\s*$')


def _struct(value: str) -> dict[str, str] | None:
    """'(X=3.000000,Y=-1.000000)' -> {'X': '3.000000', 'Y': '-1.000000'}; None if not a flat struct."""
    if not (value.startswith('(') and value.endswith(')')) or '(' in value[1:-1]:
        return None
    fields = {}
    for part in value[1:-1].split(','):
        if '=' not in part:
            return None
        k, v = part.split('=', 1)
        fields[k.strip()] = v.strip()
    return fields


def read_classes(game_dir: Path) -> dict[str, dict]:
    """Every exported class: lower name -> {name, parent, package, defaults: {key: value}}."""
    classes = {}
    for uc in sorted(game_dir.glob('*/Classes/*.uc')):
        text = uc.read_text(errors='replace')
        m = _CLASS.search(text)
        root = None if m else _ROOT.search(text)
        if not m and not root:
            continue
        name, parent = (m.group(1), m.group(2)) if m else (root.group(1), None)
        defaults = {}
        block = re.search(r'^\s*defaultproperties\s*\{(.*?)^\s*\}', text, re.I | re.M | re.S)
        for line in (block.group(1).splitlines() if block else []):
            p = _PROP.match(line)
            if p:
                defaults[p.group(1)] = p.group(2)
        classes.setdefault(name.lower(), {'name': name, 'parent': parent, 'package': uc.parent.parent.name,
                                          'defaults': defaults})
    return classes


def effective(classes: dict[str, dict], name: str) -> tuple[list[str], dict[str, tuple]]:
    """(chain root -> class, {key: (value, source)}): a struct's value is {field: (value, source)}."""
    chain, c = [], classes.get(name.lower())
    if not c:
        raise KeyError(f'no exported class {name}')
    while c:
        chain.append(c['name'])
        c = classes.get((c['parent'] or '').lower())
    chain.reverse()
    out: dict[str, tuple] = {}
    for cls in chain:
        for key, value in classes[cls.lower()]['defaults'].items():
            fields = _struct(value)
            old = out.get(key)
            if fields is not None and old and isinstance(old[0], dict):
                merged = dict(old[0])
                merged.update({f: (v, cls) for f, v in fields.items()})
                out[key] = (merged, cls)
            elif fields is not None:
                out[key] = ({f: (v, cls) for f, v in fields.items()}, cls)
            else:
                out[key] = (value, cls)
    return chain, out


def show(game_dir: Path, name: str, keys: list[str]) -> int:
    classes = read_classes(game_dir)
    chain, values = effective(classes, name)
    print(' > '.join(chain))
    wanted = {k.lower() for k in keys}
    for key in sorted(values, key=str.lower):
        if wanted and key.lower().split('(')[0] not in wanted and key.lower() not in wanted:
            continue
        value, source = values[key]
        if isinstance(value, dict):
            parts = ', '.join(f'{f}={v}' + ('' if s == chain[-1] else f' [{s}]') for f, (v, s) in value.items())
            print(f'{key} = ({parts})')
        else:
            print(f'{key} = {value}' + ('' if source == chain[-1] else f'   [{source}]'))
    return 0
