"""UE1 `LodMesh` / `Mesh` reader (vertex-animated meshes: weapon pickups / third-person guns,
rockets, grenades, the detonation key, gibs, mod meshes imported with #exec MESH IMPORT ...),
one animation frame as a static mesh.

Layout (UT v69 family; Rage v83 checked against RageWeapons.u):

    UPrimitive  FBox BoundingBox, FSphere BoundingSphere
    UMesh       Verts (lazy, packed 11/11/10-bit ints), Tris (lazy), AnimSeqs, Connects (lazy),
                FBox, FSphere, VertLinks (lazy), Textures (object refs), BoundingBoxes,
                BoundingSpheres, FrameVerts, AnimFrames, ANDFlags, ORFlags, Scale, Origin,
                RotOrigin, CurPoly, CurVertex, TextureLOD (floats)
    ULodMesh    CollapsePointThus, FaceLevel, Faces (iWedge x3, Material), CollapseWedgeThus,
                Wedges (iVertex, U, V bytes), Materials (PolyFlags, TextureIndex), SpecialFaces,
                ModelVerts, SpecialVerts, MeshScaleMax, LODHysteresis, LODStrength, LODMinVerts,
                LODMorph, LODZDisplace, RemapAnimVerts, OldFrameVerts

A plain `Mesh` stops after UMesh: its triangles are the Tris array (FMeshTri: iVertex[3] words,
Tex[3] U/V bytes, PolyFlags, TextureIndex).

Vertex positions: (v - Origin) * Scale, then rotated by RotOrigin (the #exec MESH ORIGIN).
Wedge UVs are bytes over 256 (U / 256, V / 256).
"""
from __future__ import annotations

import math
import struct
from dataclasses import dataclass, field

from .upkg import Package, Reader


@dataclass
class LodMesh:
    name: str
    textures: list[str | None] = field(default_factory=list)
    # (pos uu, normal, uv) per wedge-triangle corner; material = texture index
    triangles: list[tuple[int, int, list]] = field(default_factory=list)   # (texture index, polyflags, 3 x (pos, uv))
    scale: tuple = (1.0, 1.0, 1.0)
    origin: tuple = (0.0, 0.0, 0.0)
    rot_origin: tuple = (0, 0, 0)


def _lazy_array(r: Reader, version: int, elem: int) -> tuple[int, int]:
    """Skip a TLazyArray: returns (data offset, count)."""
    if version >= 62:
        r.u32()
    n = r.index()
    pos = r.p
    r.p += n * elem
    return pos, n


def _array(r: Reader, elem: int) -> tuple[int, int]:
    n = r.index()
    pos = r.p
    r.p += n * elem
    return pos, n


def _unpack_vert(v: int) -> tuple[float, float, float]:
    """UT FMeshVert: X 11 bits, Y 11 bits, Z 10 bits, all signed."""
    x = v & 0x7FF
    y = (v >> 11) & 0x7FF
    z = (v >> 22) & 0x3FF
    if x & 0x400:
        x -= 0x800
    if y & 0x400:
        y -= 0x800
    if z & 0x200:
        z -= 0x400
    return float(x), float(y), float(z)


def _rotate(p, rot):
    """UE1 rotator (pitch, yaw, roll in 65536 units) applied like FCoords."""
    pitch, yaw, roll = (a * math.pi * 2 / 65536.0 for a in rot)
    x, y, z = p
    # roll about X
    cr, sr = math.cos(roll), math.sin(roll)
    y, z = y * cr - z * sr, y * sr + z * cr
    # pitch about Y
    cp, sp = math.cos(pitch), math.sin(pitch)
    x, z = x * cp - z * sp, x * sp + z * cp
    # yaw about Z
    cy, sy = math.cos(yaw), math.sin(yaw)
    x, y = x * cy - y * sy, x * sy + y * cy
    return x, y, z


def _frame_positions(pkg: Package, verts_pos: int, frame_verts: int, anim_frames: int, scale, origin, rot, frame: int) -> list:
    base = verts_pos + 4 * frame_verts * min(frame, max(0, anim_frames - 1))
    positions = []
    for i in range(frame_verts):
        p = _unpack_vert(struct.unpack_from('<I', pkg.data, base + 4 * i)[0])
        p = ((p[0] - origin[0]) * scale[0], (p[1] - origin[1]) * scale[1], (p[2] - origin[2]) * scale[2])
        positions.append(_rotate(p, rot))
    return positions


def _plain_mesh(pkg: Package, name: str, textures, verts_pos, nverts, tris_pos, ntris, frame_verts, anim_frames,
                scale, origin, rot, frame) -> LodMesh | None:
    if frame_verts <= 0 or nverts < frame_verts or ntris == 0:
        return None
    positions = _frame_positions(pkg, verts_pos, frame_verts, anim_frames, scale, origin, rot, frame)
    mesh = LodMesh(name, textures, [], scale, origin, rot)
    for i in range(ntris):
        a, b, c, u0, v0, u1, v1, u2, v2, flags, tex_index = struct.unpack_from('<3H6BIi', pkg.data, tris_pos + 20 * i)
        if max(a, b, c) >= frame_verts:
            return None
        mesh.triangles.append((tex_index, flags, [(positions[a], (u0 / 256.0, v0 / 256.0)),
                                                  (positions[b], (u1 / 256.0, v1 / 256.0)),
                                                  (positions[c], (u2 / 256.0, v2 / 256.0))]))
    return mesh


def read(pkg: Package, name: str, frame: int = 0) -> LodMesh | None:
    exp = pkg.find(name, 'LodMesh')
    plain = exp is None
    if plain:
        exp = pkg.find(name, 'Mesh')
    if exp is None:
        return None
    obj = pkg.read_object(exp)
    r = Reader(pkg.data, obj.native_offset)
    v = pkg.version
    r.p += 25 + 16                                   # UPrimitive bounds
    verts_pos, nverts = _lazy_array(r, v, 4)
    tris_pos, ntris = _lazy_array(r, v, 20)          # Tris (a plain Mesh's faces)
    for _ in range(r.index()):                       # AnimSeqs
        r.index(); r.index(); r.i32(); r.i32()
        for _ in range(r.index()):
            r.f32(); r.index()
        r.f32()
    _lazy_array(r, v, 8)                             # Connects
    r.p += 25 + 16
    _lazy_array(r, v, 4)                             # VertLinks
    textures = [pkg.ref_path(r.index()) for _ in range(r.index())]
    _array(r, 25)                                    # BoundingBoxes
    _array(r, 16)                                    # BoundingSpheres
    frame_verts, anim_frames = r.i32(), r.i32()
    r.u32(); r.u32()                                 # AND / OR flags
    scale = r.vec()
    origin = r.vec()
    rot = (r.i32(), r.i32(), r.i32())
    r.i32(); r.i32()                                 # CurPoly, CurVertex
    if plain:
        return _plain_mesh(pkg, name, textures, verts_pos, nverts, tris_pos, ntris, frame_verts, anim_frames, scale, origin, rot, frame)
    _array(r, 4)                                     # TextureLOD
    # ULodMesh
    _array(r, 2)                                     # CollapsePointThus
    _array(r, 2)                                     # FaceLevel
    faces_pos, nfaces = _array(r, 8)
    _array(r, 2)                                     # CollapseWedgeThus
    wedges_pos, nwedges = _array(r, 4)
    mats_pos, nmats = _array(r, 8)
    if frame_verts <= 0 or nverts < frame_verts or nfaces == 0:
        return None
    positions = _frame_positions(pkg, verts_pos, frame_verts, anim_frames, scale, origin, rot, frame)
    wedges = [struct.unpack_from('<HBB', pkg.data, wedges_pos + 4 * i) for i in range(nwedges)]
    mats = [struct.unpack_from('<Ii', pkg.data, mats_pos + 8 * i) for i in range(nmats)]
    mesh = LodMesh(name, textures, [], scale, origin, rot)
    for i in range(nfaces):
        w0, w1, w2, m = struct.unpack_from('<4H', pkg.data, faces_pos + 8 * i)
        if max(w0, w1, w2) >= nwedges or m >= max(1, nmats):
            return None
        flags, tex_index = mats[m] if mats else (0, 0)
        corners = []
        for w in (w0, w1, w2):
            vi, u, vv = wedges[w]
            if vi >= frame_verts:
                return None
            corners.append((positions[vi], (u / 256.0, vv / 256.0)))
        mesh.triangles.append((tex_index, flags, corners))
    return mesh
