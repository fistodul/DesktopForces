#!/usr/bin/env python3
"""Split a staged build's content packs into downloads (Tools/package.sh --downloads, package.ps1 -Downloads).

    python3 Tools/downloads.py <staged Paks dir> <cooker chunk lists dir> <out dir> <build id> <platform>

Moves every pack but core (chunk 0) out of the staged build: one whose cooker list
(pakchunkN.txt) names no classic package to <out>/Downloads/<platform>/<build id>/ with
ChunkDownloader's BuildManifest-<platform>.txt (the game asks <server>/<platform>/<build id>/);
one with classic content - converted from the player's own Mobile Forces, never on a public
server - to <out>/Sideload/. No chunk ids are listed here: a new pack needs no change.
"""
import hashlib
import re
import shutil
import sys
from pathlib import Path

CLASSIC_MARK = 'mobileforcesclassic'


def chunk_of(name: str) -> int | None:
    m = re.match(r'pakchunk(\d+)', name)
    return int(m.group(1)) if m else None


def sha1(path: Path) -> str:
    h = hashlib.sha1()
    with path.open('rb') as f:
        for block in iter(lambda: f.read(1 << 20), b''):
            h.update(block)
    return h.hexdigest().upper()


def split(paks: Path, chunk_lists: Path, out: Path, build_id: str, platform: str) -> Path:
    files = sorted(p for p in paks.glob('pakchunk*') if chunk_of(p.name) not in (None, 0))  # 0: core stays
    # public or not, decided before moving anything: by what the cooker put in each chunk
    public = {}
    for n in sorted({chunk_of(p.name) for p in files}):
        listing = chunk_lists / f'pakchunk{n}.txt'
        if not listing.is_file():
            sys.exit(f'no cooker list for pakchunk{n}: {listing}')
        public[n] = CLASSIC_MARK not in listing.read_text(errors='replace').lower()
    upload = out / 'Downloads' / platform / build_id
    sideload = out / 'Sideload'
    for d in (out / 'Downloads', sideload):
        shutil.rmtree(d, ignore_errors=True)
    upload.mkdir(parents=True)
    sideload.mkdir(parents=True)
    lines = []
    for p in files:
        n = chunk_of(p.name)
        if public[n]:
            dest = upload / p.name
            shutil.move(p, dest)
            # name <tab> size <tab> version (SHA1:, checked after download) <tab> chunk <tab> url
            lines.append(f'{p.name}\t{dest.stat().st_size}\tSHA1:{sha1(dest)}\t{n}\t{p.name}')
        else:
            shutil.move(p, sideload / p.name)
    manifest = [f'$NUM_ENTRIES = {len(lines)}', f'$BUILD_ID = {build_id}', *lines]
    (upload / f'BuildManifest-{platform}.txt').write_text('\n'.join(manifest) + '\n', newline='\n')
    return upload


def main(argv: list[str]) -> None:
    if len(argv) != 5:
        sys.exit(__doc__)
    paks, lists, out, build_id, platform = argv
    if not all(argv):
        sys.exit(f'empty argument: {argv}')
    upload = split(Path(paks), Path(lists), Path(out), build_id, platform)
    print(f'Downloads: {upload}')


if __name__ == '__main__':
    main(sys.argv[1:])
