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
import tomllib
from pathlib import Path

from .config import TOOL_DIR, Config

FIRST = ['mf-warehouse', 'mf-Polar', 'mf-western', 'mf-sawmill', 'mf-waterfront', 'mf-airport', 'mf-Ghetto',
         'mf-dockyard', 'mf-Carpark', 'mf-Hydroworks', 'mf-Rail_Quarry', 'mf-The_Bank', 'mf-Toreford']
SKIP = {'mf-warehouse-merged'}   # merge work in progress, built unlit (Docs/WarehouseMerge.md)


def _maps(cfg: Config) -> list[str]:
    have = sorted((p.parent.name for p in (cfg.out_dir / 'Maps').glob('*/level.json')), key=str.casefold)
    have = [m for m in have if m not in SKIP]
    first = [m for f in FIRST for m in have if m.lower() == f.lower()]
    return first + [m for m in have if m not in first]


def run(cfg: Config, limit: int = 0, restart: bool = False) -> int:
    folder = cfg.work_dir / 'rebake'
    folder.mkdir(parents=True, exist_ok=True)
    checkpoint = folder / 'progress.json'
    state = {} if restart or not checkpoint.exists() else json.loads(checkpoint.read_text())
    vmf = set(tomllib.loads((TOOL_DIR / 'vmf_maps.toml').read_text()))
    for name in _maps(cfg):
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
    pending = [m for m, p in state.items() if not p['lit']]
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
        if step(f'{name}-light', 'light', name):
            state[name]['lit'] = True
        else:
            failed = True
        save()
    print(f"{sum(p['lit'] for p in state.values())}/{len(state)} maps rebaked. Checkpoint: {checkpoint}", flush=True)
    return int(failed)
