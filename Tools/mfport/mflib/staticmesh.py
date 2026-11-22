"""The textures a static mesh is drawn with, read from its package (not the T3D export).

A T3D export names each triangle's texture as it was when the mesh was imported (the raw
triangles' legacy texture). What MF draws comes from the mesh's sections, whose texture the
mapper could change afterwards in the static mesh browser: the Ghetto maps' cars were imported
without one ("Texture None" in the T3D, Rage's brick default texture in our port) and given
car_park.park_ceiling_0 or rage_urban.Panel02 there; urbanterrain's pieces became all dirt.

Native StaticMesh data, package version 83 (after the tagged properties, which are empty):

    FBox     bounding box (6 floats + byte IsValid)       25 bytes
    FSphere  bounding sphere                               16 bytes
    compact  ?, compact ?                                  (purpose unknown, 2 - 3 bytes each)
    compact  NumSections
      per section: int32 ?, compact Material (a Texture), int32 PolyFlags (as the triangles'
                   less PF_TwoSided), uint16 FirstIndex, uint16 NumTriangles,
                   uint16 MinVertexIndex, uint16 MaxVertexIndex

The sections follow the raw triangles in order, one run of a single legacy texture each (the
T3D sometimes leaves a few triangles out at a section's end, so a run can be shorter). Checked
on all 3939 static meshes of the converted maps (2026-11-22).
"""
from __future__ import annotations

import struct
from dataclasses import dataclass

from .upkg import Package, Reader


@dataclass
class Section:
    texture: str | None     # full path (package.group.name), None for no texture
    triangles: int


def sections(pkg: Package, name: str) -> list[Section] | None:
    """The sections of static mesh `name` in `pkg`; None if it isn't there or doesn't parse."""
    exp = pkg.find(name, 'StaticMesh')
    if exp is None:
        return None
    try:
        r = Reader(pkg.data, pkg.read_object(exp).native_offset)
        r.raw(25 + 16)   # bounding box and sphere
        r.index()
        r.index()
        count = r.index()
        if not 0 <= count < 4096:
            return None
        out = []
        for _ in range(count):
            r.u32()
            ref = r.index()
            r.u32()          # poly flags: the triangles' own are kept (they have PF_TwoSided)
            _first, tris, _lo, _hi = struct.unpack('<4H', r.raw(8))
            texture = None
            if ref and _class(pkg, ref) == 'texture':
                texture = pkg.full_path(ref)
            out.append(Section(texture, tris))
        return out
    except (IndexError, struct.error, ValueError):
        return None


def _class(pkg: Package, ref: int) -> str:
    if ref < 0:
        return pkg.imports[-ref - 1].class_name.lower()
    return pkg.class_of(pkg.exports[ref - 1]).lower()


def triangle_textures(raw: list[str | None], secs: list[Section] | None) -> list[str | None] | None:
    """Each raw triangle's texture as drawn: its section's. `raw` is the T3D triangles' own
    textures in order. None when the sections don't line up with them."""
    if not secs:
        return None
    out: list[str | None] = []
    i = 0
    for sec in secs:
        start = i
        while i < len(raw) and i - start < sec.triangles and raw[i] == raw[start]:
            i += 1
        out += [sec.texture] * (i - start)
    return out if i == len(raw) else None
