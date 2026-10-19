"""Read compiled Source engine models (.mdl + .vvd + .dx90.vtx, versions 44-49) into
`.mfmesh` builders. Only static geometry is read: LOD 0 of every body part, first skin,
no bones/animation - enough for props (Mobile Forces Source used MF meshes recompiled as
Source props).

Formats (Source SDK 2013, studio.h / optimize.h):
  MDL  studiohdr_t: textures, cd texture dirs, body parts -> models -> meshes (material, vertex offsets)
  VVD  vertexFileHeader_t: 48-byte vertices (bone weights, position, normal, uv) + LOD fixups
  VTX  FileHeader_t: body parts -> models -> LODs -> meshes -> strip groups (vertex refs + index lists)
"""
from __future__ import annotations

import struct
from dataclasses import dataclass
from pathlib import Path


@dataclass
class SourceMesh:
    name: str
    textures: list[str]      # material names (no dir), by MDL texture index
    tex_dirs: list[str]      # cdtextures
    # (texture index, ((pos, normal, uv) x3)) in Source units, right-handed
    triangles: list[tuple[int, tuple]]


def _cstr(b: bytes, off: int) -> str:
    end = b.index(b'\0', off)
    return b[off:end].decode('latin-1')


def read_model(mdl_path: Path) -> SourceMesh:
    mdl = mdl_path.read_bytes()
    ident, version = struct.unpack_from('<4si', mdl, 0)
    if ident != b'IDST' or not 44 <= version <= 49:
        raise ValueError(f'{mdl_path.name}: not a Source model (v{version})')
    stem = mdl_path.with_suffix('')
    vvd = _find(stem, '.vvd').read_bytes()
    vtx = _find(stem, '.dx90.vtx').read_bytes()

    numtex, texidx, numcd, cdidx = struct.unpack_from('<4i', mdl, 204)
    textures = [_cstr(mdl, texidx + 64 * i + struct.unpack_from('<i', mdl, texidx + 64 * i)[0]) for i in range(numtex)]
    tex_dirs = [_cstr(mdl, struct.unpack_from('<i', mdl, cdidx + 4 * i)[0]).replace('\\', '/') for i in range(numcd)]
    numskinref, numskinfam, skinidx = struct.unpack_from('<3i', mdl, 220)
    skin = list(struct.unpack_from(f'<{numskinref}h', mdl, skinidx)) if numskinref else []
    nbp, bpidx = struct.unpack_from('<2i', mdl, 232)

    verts = _vvd_vertices(vvd)

    # VTX header: version, vertCacheSize, maxBonesPerStrip(u16), maxBonesPerFace(u16), maxBonesPerVert,
    # checksum, numLODs, materialReplacementListOffset, numBodyParts, bodyPartOffset
    vtx_nbp, vtx_bpoff = struct.unpack_from('<2i', vtx, 28)
    tris = []
    for bp in range(nbp):
        bp_off = bpidx + 16 * bp
        _, nmodels, _, modelidx = struct.unpack_from('<4i', mdl, bp_off)
        vbp = vtx_bpoff + 8 * bp
        vnm, vmodoff = struct.unpack_from('<2i', vtx, vbp)
        for m in range(nmodels):
            mo = bp_off + modelidx + 148 * m                       # mstudiomodel_t (148 bytes)
            nmeshes, meshidx, _, vertexindex = struct.unpack_from('<4i', mdl, mo + 72)
            model_base = vertexindex // 48
            vmo = vbp + vmodoff + 8 * m                            # ModelHeader_t
            nlods, lodoff = struct.unpack_from('<2i', vtx, vmo)
            vlod = vmo + lodoff                                    # LOD 0
            vnmesh, vmeshoff = struct.unpack_from('<2i', vtx, vlod)
            for me in range(nmeshes):
                mso = mo + meshidx + 116 * me                      # mstudiomesh_t (116 bytes)
                material, _, _, vertexoffset = struct.unpack_from('<4i', mdl, mso)
                tex = skin[material] if material < len(skin) else material
                vm = vlod + vmeshoff + 9 * me                      # MeshHeader_t (9 bytes, packed)
                nsg, sgoff = struct.unpack_from('<2i', vtx, vm)
                for g in range(nsg):
                    sg = vm + sgoff + 25 * g                       # StripGroupHeader_t (25 bytes)
                    nv, voff, ni, ioff = struct.unpack_from('<4i', vtx, sg)
                    refs = [struct.unpack_from('<H', vtx, sg + voff + 9 * k + 4)[0] for k in range(nv)]
                    idx = struct.unpack_from(f'<{ni}H', vtx, sg + ioff)
                    for t in range(0, ni - 2, 3):
                        tri = tuple(verts[model_base + vertexoffset + refs[idx[t + k]]] for k in range(3))
                        tris.append((tex, tri))
    return SourceMesh(mdl_path.stem, textures, tex_dirs, tris)


def _find(stem: Path, ext: str) -> Path:
    for p in stem.parent.iterdir():
        if p.name.lower() == (stem.name + ext).lower():
            return p
    raise FileNotFoundError(f'{stem}{ext}')


def _vvd_vertices(vvd: bytes):
    ident, version, _, nlods = struct.unpack_from('<4sii i', vvd, 0)
    if ident != b'IDSV':
        raise ValueError('bad VVD')
    lod_counts = struct.unpack_from('<8i', vvd, 16)
    nfix, fixstart, vstart, _ = struct.unpack_from('<4i', vvd, 48)

    def vert(i):
        o = vstart + 48 * i + 16                               # skip mstudioboneweight_t
        x, y, z, nx, ny, nz, u, v = struct.unpack_from('<8f', vvd, o)
        return ((x, y, z), (nx, ny, nz), (u, v))

    if nfix == 0:
        return [vert(i) for i in range(lod_counts[0])]
    out = []
    for f in range(nfix):
        lod, src, num = struct.unpack_from('<3i', vvd, fixstart + 12 * f)
        if lod >= 0:                                           # vertices used by LOD 0
            out.extend(vert(src + i) for i in range(num))
    return out
