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


def hull_collision(map_name: str) -> list[str]:
    """Static meshes that collide as one smooth shape, by name (a ladder lying on a slope)."""
    return _edits_for(map_name).get('hull_collision', [])


def nav_decks(map_name: str) -> list[str]:
    """Static meshes walked on across gaps (rope bridge planks): bots get a navmesh over them."""
    return _edits_for(map_name).get('nav_decks', [])


def drop_faces(map_name: str) -> dict[str, set[str]]:
    """Static mesh faces left out, by mesh and texture (lower case): hidden ones that break the bake."""
    return {m.lower(): {t.lower() for t in texs} for m, texs in _edits_for(map_name).get('drop_faces', {}).items()}


def bounces(map_name: str, default: int) -> int:
    """Lightmass indirect bounces: more light in rooms the sun doesn't reach, for bake time only."""
    return _edits_for(map_name).get('bounces', default)


def instance_props(map_name: str) -> int:
    """Meshes without collision placed this often in a cell become one instanced component (0: never)."""
    return _edits_for(map_name).get('instance_props', 8)

