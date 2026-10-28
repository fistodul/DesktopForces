"""Resume imports of playable maps missing from an initialized project.

Shared gameplay props/soldiers must already exist (run `mfport all <map>` once).
Each batch converts its maps, refreshes shared dependencies once, then imports and
lights maps individually. Logs and checkpoints stay in the ignored work directory.
"""
from __future__ import annotations

import json
import subprocess
import sys
from pathlib import Path

from . import ue
from .config import TOOL_DIR, Config


def run(cfg: Config, limit: int = 0) -> int:
    folder = cfg.work_dir / 'remaining'
    folder.mkdir(parents=True, exist_ok=True)
    checkpoint = folder / 'progress.json'
    state = json.loads(checkpoint.read_text()) if checkpoint.exists() else {}
    for source in sorted((cfg.game_dir / 'Maps').glob('*.umf'), key=lambda p: p.name.casefold()):
        if source.stem.lower() not in {'entry', 'intro'} and not ue.map_package(cfg, source.stem)[1].exists():
            state.setdefault(source.stem, {'converted': False, 'imported': False, 'lit': False})

    def save():
        temporary = checkpoint.with_suffix('.tmp')
        temporary.write_text(json.dumps(state, indent=2) + '\n')
        temporary.replace(checkpoint)

    def step(label: str, *args: str) -> bool:
        log = folder / f'{label}.log'
        print(f'{label}: {log}', flush=True)
        with log.open('w') as output:
            result = subprocess.run([sys.executable, str(TOOL_DIR / 'mfport.py'), *args],
                                    stdout=output, stderr=subprocess.STDOUT)
        if result.returncode:
            print(f'FAILED ({result.returncode}): {log}', flush=True)
        return result.returncode == 0

    save()
    pending = [name for name, progress in state.items() if not progress['lit']]
    selected = pending[:limit] if limit > 0 else pending
    print(f'{len(pending)} maps pending; processing {len(selected)}. Menu maps entry/intro excluded.', flush=True)
    converted = []
    failed = False
    for name in selected:
        if state[name]['converted'] or step(f'{name}-convert', 'map', name):
            state[name]['converted'] = True
            converted.append(name)
        else:
            failed = True
        save()
    if not converted:
        return int(failed)
    # Repeated dependency conversion is cached by the normal pipeline. Refresh only once per batch.
    if any(not state[name]['imported'] for name in converted):
        for command in ('textures', 'effects', 'sounds', 'music'):
            if not step(f'shared-{command}', command):
                return 1
        if not step('shared-import', 'import', '--steps', 'textures,effects,sounds,music'):
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
            save()
        else:
            failed = True
    print(f"{sum(p['lit'] for p in state.values())}/{len(state)} maps imported and lit. Checkpoint: {checkpoint}", flush=True)
    return int(failed)
