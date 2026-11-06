"""Rebuild and rebake every converted map after a pipeline change that touches them all.

Converts each map again (UE1 `map`, Source `vmf`), imports the textures that changed once (the
minimaps), then imports and lights the maps one at a time. Logs and a checkpoint stay in
work/rebake/, so an interrupted run resumes where it stopped; `--restart` begins again.
Maps the team plays most go first (the community servers' rotation in Docs/Hosting.md).
"""
from __future__ import annotations

import json
import subprocess
import sys
import time
import tomllib
from pathlib import Path

from .config import TOOL_DIR, Config

FIRST = ['mf-warehouse', 'mf-Polar', 'mf-western', 'mf-sawmill', 'mf-waterfront', 'mf-airport', 'mf-Ghetto',
         'mf-dockyard', 'mf-Carpark', 'mf-Hydroworks', 'mf-Rail_Quarry', 'mf-The_Bank', 'mf-Toreford']
SKIP = {'mf-warehouse-merged'}   # merge work in progress, built unlit (Docs/WarehouseMerge.md)


def _maps(cfg: Config, only: list[str] | None = None) -> list[str]:
    have = sorted((p.parent.name for p in (cfg.out_dir / 'Maps').glob('*/level.json')), key=str.casefold)
    wanted = {m.lower() for m in only or []}
    have = [m for m in have if m not in SKIP and (not wanted or m.lower() in wanted)]
    first = [m for f in FIRST for m in have if m.lower() == f.lower()]
    return first + [m for m in have if m not in first]


def run(cfg: Config, limit: int = 0, restart: bool = False, only: list[str] | None = None) -> int:
    """`only`: just these maps (`--restart` the first time, as the checkpoint has them lit)."""
    folder = cfg.work_dir / 'rebake'
    folder.mkdir(parents=True, exist_ok=True)
    checkpoint = folder / 'progress.json'
    state = {} if restart or not checkpoint.exists() else json.loads(checkpoint.read_text())
    vmf = set(tomllib.loads((TOOL_DIR / 'vmf_maps.toml').read_text()))
    maps = _maps(cfg, only)
    if only and len(maps) != len(set(m.lower() for m in only)):
        print(f'not converted: {sorted(set(m.lower() for m in only) - {m.lower() for m in maps})}', flush=True)
        return 1
    for name in maps:
        state.setdefault(name, {'converted': False, 'imported': False, 'lit': False})

    def save():
        tmp = checkpoint.with_suffix('.tmp')
        tmp.write_text(json.dumps(state, indent=2) + '\n')
        tmp.replace(checkpoint)

    def step(label: str, *args: str) -> bool:
        log = folder / f'{label}.log'
        print(f'{label}: {log}', flush=True)
        with log.open('w') as out:
            rc = subprocess.run([sys.executable, str(TOOL_DIR / 'mfport.py'), *args], stdout=out, stderr=subprocess.STDOUT).returncode
        if rc:
            print(f'FAILED ({rc}): {log}', flush=True)
        return rc == 0

    save()
    pending = [m for m in maps if not state[m]['lit']]
    selected = pending[:limit] if limit > 0 else pending
    print(f'{len(pending)} maps to rebake, {len(selected)} in this run', flush=True)
    failed = False
    converted = []
    for name in selected:
        if state[name]['converted'] or step(f'{name}-convert', 'vmf' if name in vmf else 'map', name):
            state[name]['converted'] = True
            converted.append(name)
        else:
            failed = True
        save()
    if converted and any(not state[m]['imported'] for m in converted):
        if not step('textures-import', 'import', '--steps', 'textures'):
            return 1
    for name in converted:
        if not state[name]['imported']:
            if not step(f'{name}-import', 'import', name, '--steps', 'maps'):
                failed = True
                continue
            state[name]['imported'] = True
            save()
        # Lightmass talks to the editor over Swarm (UDP messaging): after a long bake it can
        # fail to connect, or lose the reports of finished tasks. A second try has always worked
        for attempt in range(3):
            if attempt:
                time.sleep(30)
            if step(f'{name}-light', 'light', name):
                state[name]['lit'] = True
                break
        failed |= not state[name]['lit']
        save()
    print(f"{sum(p['lit'] for p in state.values())}/{len(state)} maps rebaked. Checkpoint: {checkpoint}", flush=True)
    return int(failed)
