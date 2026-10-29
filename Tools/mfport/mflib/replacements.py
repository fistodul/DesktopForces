"""Stage selected, locally acquired textures without importing a whole asset pack."""
from __future__ import annotations

import hashlib
import io
import json
import re
from pathlib import Path

from PIL import Image

from . import textures
from .config import Config


def stage(cfg: Config, manifest_file: Path) -> list[dict]:
    """Manifest entries name a converted target, local source image, source URL and license.

    Raw licensed images stay in an ignored directory. The normal texture conversion
    routes the resulting assets into the existing community replacement pack.
    """
    manifest_file = manifest_file.resolve()
    entries = json.loads(manifest_file.read_text(encoding='utf-8'))
    converted = json.loads((cfg.out_dir / 'textures.json').read_text(encoding='utf-8'))
    names = {key.casefold(): key for key in converted}
    prepared = []
    seen = set()
    for entry in entries:
        target = entry['target']
        if not re.fullmatch(r'[A-Za-z0-9_-]+(?:\.[A-Za-z0-9_-]+)+', target):
            raise ValueError(f'Invalid texture target: {target}')
        if target.casefold() not in names:
            raise ValueError(f'Target is not in the converted texture manifest: {target}')
        if not entry.get('source_url') or not entry.get('license'):
            raise ValueError(f'Source URL and license are required for {target}')
        target = names[target.casefold()]
        if target in seen:
            raise ValueError(f'Duplicate texture target: {target}')
        seen.add(target)
        source = (manifest_file.parent / entry['source']).resolve()
        with Image.open(source) as image:
            has_alpha = 'A' in image.getbands() or 'transparency' in image.info
            image = image.convert('RGBA' if has_alpha else 'RGB')
            info = converted[target]
            if info['alpha']:
                with Image.open(cfg.out_dir / info['file']) as original:
                    has_holes = 'A' in original.getbands() and original.getchannel('A').getextrema()[0] < 255
                if has_holes and ('A' not in image.getbands() or image.getchannel('A').getextrema()[0] == 255):
                    raise ValueError(f'{target} needs transparency; an opaque replacement would close its holes')
            image = textures._limit_size(image, min(512, cfg.max_texture_size))
            buffer = io.BytesIO()
            image.save(buffer, format='PNG', optimize=True)
            pixels = buffer.getvalue()
            relative = Path(*target.split('.')).with_suffix('.png')
            record = {
                'target': target,
                'file': relative.as_posix(),
                'source': str(source),
                'source_url': entry['source_url'],
                'license': entry['license'],
                'source_sha256': hashlib.sha256(source.read_bytes()).hexdigest(),
                'width': image.width,
                'height': image.height,
            }
            prepared.append((relative, pixels, record))
    # Validate the whole selection before writing any replacements.
    provenance_file = textures.LICENSED_DIR / 'provenance.json'
    provenance = json.loads(provenance_file.read_text(encoding='utf-8')) if provenance_file.exists() else {}
    for relative, pixels, record in prepared:
        destination = textures.LICENSED_DIR / relative
        destination.parent.mkdir(parents=True, exist_ok=True)
        if not destination.exists() or destination.read_bytes() != pixels:
            temporary = destination.with_suffix('.tmp')
            temporary.write_bytes(pixels)
            temporary.replace(destination)
        provenance[record['target']] = record
        print(f"{record['target']}: {record['width']}x{record['height']} -> {destination}")
    if prepared:
        temporary = provenance_file.with_suffix('.tmp')
        temporary.write_text(json.dumps(provenance, indent=2) + '\n', encoding='utf-8')
        temporary.replace(provenance_file)
    return [record for _, _, record in prepared]
