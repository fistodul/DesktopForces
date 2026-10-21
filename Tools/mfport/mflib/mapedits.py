"""Changes made to MF maps when converting them: map_edits.toml (vehicle generators the
servers' mods added or swapped at run time, now part of the maps)."""
from __future__ import annotations

import tomllib
from pathlib import Path

EDITS_FILE = Path(__file__).resolve().parent.parent / 'map_edits.toml'


def _edits_for(map_name: str) -> dict:
    if not EDITS_FILE.exists():
        return {}
    data = tomllib.loads(EDITS_FILE.read_text())
    return next((v for k, v in data.items() if k.lower() == map_name.lower()), {})


def apply(map_name: str, actors: list[dict]) -> None:
    """Edit the map's converted actor records (level.json 'actors') in place."""
    edits = _edits_for(map_name)
    for swap in edits.get('generators', []):
        n = 0
        for a in actors:
            if a['class'].lower() == swap['class'].lower():
                a['class'] = 'VehicleGenerator'
                a['props']['VehicleClass'] = f"Class'{swap['vehicle']}'"
                a['props']['Tag'] = 'VehicleGenerator'
                n += 1
        print(f'  map edit: {n} {swap["class"]} -> {swap["vehicle"]}')
    for i, add in enumerate(edits.get('add_vehicles', [])):
        actors.append({
            'class': 'VehicleGenerator',
            'name': f'AddedVehicleGenerator{i}',
            'location': [float(c) for c in add['location']],
            'rotation': {'pitch': 0.0, 'yaw': float(add.get('yaw', 0.0)), 'roll': 0.0},
            'zone': 0,
            'props': {'VehicleClass': f"Class'{add['vehicle']}'", 'Tag': 'VehicleGenerator'},
        })
    if edits.get('add_vehicles'):
        print(f'  map edit: {len(edits["add_vehicles"])} vehicle generators added')
