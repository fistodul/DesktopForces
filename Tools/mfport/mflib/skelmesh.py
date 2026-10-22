"""Mobile Forces skeletal meshes (the soldiers, the first-person weapons) and their
animations, written as glTF binaries (.glb) for UE's Interchange importer.

SkeletalMesh layout (Rage v83; every byte of RageGfx.RagePlayer1Mesh accounted for):

    ULodMesh       see lodmesh.py: Faces (iWedge x3, Material), Wedges (iVertex, U, V bytes),
                   Materials (PolyFlags, TextureIndex), ... OldFrameVerts
    (empty array)
    Points         TArray<FVector>: the reference pose, mesh space
    RefSkeleton    TArray<FMeshBone>: FName Name, DWORD Flags, FQuat Orientation, FVector
                   Position, FLOAT Length, FVector Size, INT NumChildren, INT ParentIndex
    BoneInfluence  TArray<(WORD First, WORD Count, DWORD)>: per bone, its run of influences
    Influences     TArray<(WORD Point, WORD Weight)>, weight / 65535
    LocalPoints    TArray<FVector>: per influence, the point in that bone's space
    SkeletalDepth INT, then attachment data (MESH WEAPONATTACH; not needed)

Bones follow the .psk convention: the root's quaternion as stored, every other bone's
conjugated, world = parent * local. Rebuilding Points from LocalPoints that way matches to
0.006 uu. Animation keys (anims.py) use the same convention; key times are seconds.

The mesh's #exec ORIGIN / SCALE (vertex = RotOrigin * ((p - Origin) * Scale)) is baked in, so
the glTF is in actor space: X forward, Z up, centred on the collision cylinder, cm.

Usage:  python3 -m mflib.skelmesh <package.u> <SkeletalMesh> <Animation> <out.glb>
"""
from __future__ import annotations

import json
import math
import struct
import sys
from dataclasses import dataclass, field
from pathlib import Path

from .anims import read_animation
from .config import UU_TO_CM
from .lodmesh import _array, _lazy_array
from .upkg import Package, Reader

# looped with LoopAnim in MF (Pawn.uc / RPlayer.uc): wrap the last key back to the first
LOOP_WORDS = ('Run', 'Walk', 'Still', 'Strafe', 'Backrun', 'Swim', 'Tread', 'Drive', 'Chat', 'Look', 'Turn')
LOOP_EXCEPT = ('Throw', 'Fire', 'Reload')


@dataclass
class SkeletalMesh:
    name: str
    bones: list = field(default_factory=list)       # (name, parent, quat, pos), file convention
    points: list = field(default_factory=list)      # mesh space
    wedges: list = field(default_factory=list)      # (point, u, v)
    faces: list = field(default_factory=list)       # (w0, w1, w2, material)
    materials: list = field(default_factory=list)   # (polyflags, texture index)
    weights: list = field(default_factory=list)     # per point: [(bone, weight)]
    scale: tuple = (1.0, 1.0, 1.0)
    origin: tuple = (0.0, 0.0, 0.0)
    rot: tuple = (0, 0, 0)


def read(pkg: Package, name: str) -> SkeletalMesh:
    exp = pkg.find(name, 'SkeletalMesh')
    obj = pkg.read_object(exp)
    r = Reader(pkg.data, obj.native_offset)
    v = pkg.version
    r.p += 25 + 16                                   # UPrimitive bounds
    _lazy_array(r, v, 4)                             # Verts
    _lazy_array(r, v, 20)                            # Tris
    for _ in range(r.index()):                       # AnimSeqs
        r.index(); r.index(); r.i32(); r.i32()
        for _ in range(r.index()):
            r.f32(); r.index()
        r.f32()
    _lazy_array(r, v, 8)                             # Connects
    r.p += 25 + 16
    _lazy_array(r, v, 4)                             # VertLinks
    for _ in range(r.index()):                       # Textures
        r.index()
    _array(r, 25)
    _array(r, 16)
    r.i32(); r.i32(); r.u32(); r.u32()               # FrameVerts, AnimFrames, AND / OR flags
    m = SkeletalMesh(name)
    m.scale, m.origin = r.vec(), r.vec()
    m.rot = (r.i32(), r.i32(), r.i32())
    r.i32(); r.i32()
    _array(r, 4)                                     # TextureLOD
    _array(r, 2); _array(r, 2)                       # CollapsePointThus, FaceLevel
    faces_pos, nfaces = _array(r, 8)
    _array(r, 2)                                     # CollapseWedgeThus
    wedges_pos, nwedges = _array(r, 4)
    mats_pos, nmats = _array(r, 8)
    _array(r, 8)                                     # SpecialFaces
    r.i32(); r.i32()                                 # ModelVerts, SpecialVerts
    r.f32(); r.f32(); r.f32(); r.i32(); r.f32(); r.f32()
    _array(r, 2)                                     # RemapAnimVerts
    r.i32()                                          # OldFrameVerts
    if r.index():
        raise ValueError(f'{name}: unexpected data before the points')
    m.points = [r.vec() for _ in range(r.index())]
    for _ in range(r.index()):
        bname = pkg.names[r.index()]
        r.u32()
        q = (r.f32(), r.f32(), r.f32(), r.f32())
        p = r.vec()
        r.f32(); r.vec(); r.i32()
        m.bones.append((bname, r.i32(), q, p))
    ranges = [struct.unpack_from('<HHI', pkg.data, r.p + 8 * i) for i in range(r.index())]
    r.p += 8 * len(ranges)
    infl = [struct.unpack_from('<HH', pkg.data, r.p + 4 * i) for i in range(r.index())]
    m.weights = [[] for _ in m.points]
    for bone, (first, count, _) in enumerate(ranges):
        for point, w in infl[first:first + count]:
            m.weights[point].append((bone, w / 65535.0))
    m.wedges = [struct.unpack_from('<HBB', pkg.data, wedges_pos + 4 * i) for i in range(nwedges)]
    m.faces = [struct.unpack_from('<4H', pkg.data, faces_pos + 8 * i) for i in range(nfaces)]
    m.materials = [struct.unpack_from('<Ii', pkg.data, mats_pos + 8 * i) for i in range(nmats)]
    return m


# --- maths (plain tuples; quaternions are (x, y, z, w)) -----------------------------------

def qmul(a, b):
    ax, ay, az, aw = a
    bx, by, bz, bw = b
    return (aw * bx + ax * bw + ay * bz - az * by, aw * by - ax * bz + ay * bw + az * bx,
            aw * bz + ax * by - ay * bx + az * bw, aw * bw - ax * bx - ay * by - az * bz)


def qconj(q):
    return (-q[0], -q[1], -q[2], q[3])


def qrot(q, v):
    return qmul(qmul(q, (v[0], v[1], v[2], 0.0)), qconj(q))[:3]


def qnorm(q):
    n = math.sqrt(sum(c * c for c in q)) or 1.0
    return tuple(c / n for c in q)


def _yaw_quat(yaw_units: int):
    a = yaw_units * math.pi * 2 / 65536.0
    return (0.0, 0.0, math.sin(a / 2), math.cos(a / 2))


class _Space:
    """MF mesh space -> UE actor space (cm), and UE -> glTF (Y up, metres)."""

    def __init__(self, mesh: SkeletalMesh):
        if mesh.rot[0] or mesh.rot[2]:
            raise ValueError(f'{mesh.name}: only a yaw RotOrigin is handled')
        self.rot = _yaw_quat(mesh.rot[1])
        self.s = mesh.scale[0] * UU_TO_CM
        self.origin = mesh.origin

    def point(self, p):
        return qrot(self.rot, tuple((p[i] - self.origin[i]) * self.s for i in range(3)))

    def bone(self, index, q, p):
        """A bone's (rotation, translation) relative to its parent, in UE space."""
        if index == 0:
            return qnorm(qmul(self.rot, q)), self.point(p)
        return qnorm(qconj(q)), tuple(c * self.s for c in p)


def gl_vec(v):
    return (v[0] / 100.0, v[2] / 100.0, v[1] / 100.0)


def gl_dir(v):
    return (v[0], v[2], v[1])


def gl_quat(q):
    return (-q[0], -q[2], -q[1], q[3])


def _matrix(q, t):
    """Column-major 4x4 of rotation q then translation t."""
    x, y, z, w = q
    m = [1 - 2 * (y * y + z * z), 2 * (x * y + z * w), 2 * (x * z - y * w), 0.0,
         2 * (x * y - z * w), 1 - 2 * (x * x + z * z), 2 * (y * z + x * w), 0.0,
         2 * (x * z + y * w), 2 * (y * z - x * w), 1 - 2 * (x * x + y * y), 0.0,
         t[0], t[1], t[2], 1.0]
    return m


def _inverse_rigid(q, t):
    qi = qconj(q)
    ti = qrot(qi, t)
    return _matrix(qi, (-ti[0], -ti[1], -ti[2]))


# --- glTF ---------------------------------------------------------------------------------

class _Gltf:
    def __init__(self):
        self.bin = bytearray()
        self.doc = {'asset': {'version': '2.0', 'generator': 'mfport skelmesh'}, 'buffers': [], 'bufferViews': [],
                    'accessors': [], 'nodes': [], 'meshes': [], 'skins': [], 'materials': [], 'animations': [],
                    'scenes': [{'nodes': [0]}], 'scene': 0}

    def accessor(self, fmt: str, values: list, ctype: int, atype: str, target=None, minmax=False) -> int:
        while len(self.bin) % 4:
            self.bin.append(0)
        off = len(self.bin)
        per = {'SCALAR': 1, 'VEC2': 2, 'VEC3': 3, 'VEC4': 4, 'MAT4': 16}[atype]
        for v in values:
            self.bin += struct.pack('<' + fmt * per, *(v if per > 1 else (v,)))
        view = {'buffer': 0, 'byteOffset': off, 'byteLength': len(self.bin) - off}
        if target:
            view['target'] = target
        self.doc['bufferViews'].append(view)
        acc = {'bufferView': len(self.doc['bufferViews']) - 1, 'componentType': ctype, 'count': len(values), 'type': atype}
        if minmax:
            cols = list(zip(*values)) if per > 1 else [values]
            acc['min'] = [min(c) for c in cols]
            acc['max'] = [max(c) for c in cols]
        self.doc['accessors'].append(acc)
        return len(self.doc['accessors']) - 1

    def write(self, path: Path):
        while len(self.bin) % 4:
            self.bin.append(0)
        self.doc['buffers'] = [{'byteLength': len(self.bin)}]
        js = json.dumps(self.doc, separators=(',', ':')).encode()
        js += b' ' * (-len(js) % 4)
        path.parent.mkdir(parents=True, exist_ok=True)
        with open(path, 'wb') as f:
            f.write(struct.pack('<III', 0x46546C67, 2, 12 + 8 + len(js) + 8 + len(self.bin)))
            f.write(struct.pack('<II', len(js), 0x4E4F534A) + js)
            f.write(struct.pack('<II', len(self.bin), 0x004E4942) + self.bin)


FLOAT, USHORT, UINT = 5126, 5123, 5125


def is_loop(seq_name: str) -> bool:
    return any(w in seq_name for w in LOOP_WORDS) and not any(w in seq_name for w in LOOP_EXCEPT)


def export(pkg: Package, mesh_name: str, anim_pkg: Package | None, anim_name: str | None, out: Path,
           only: set[str] | None = None) -> dict:
    mesh = read(pkg, mesh_name)
    sp = _Space(mesh)
    g = _Gltf()
    doc = g.doc

    # skeleton: node 0 is the root bone, bone i is node i; the mesh node comes last
    local = [sp.bone(i, q, p) for i, (_, _, q, p) in enumerate(mesh.bones)]
    world = []
    for i, (bname, parent, _, _) in enumerate(mesh.bones):
        q, t = local[i]
        if i:
            pq, pt = world[parent]
            q, t = qnorm(qmul(pq, q)), tuple(a + b for a, b in zip(pt, qrot(pq, t)))
        world.append((q, t))
        doc['nodes'].append({'name': bname, 'rotation': list(gl_quat(local[i][0])), 'translation': list(gl_vec(local[i][1]))})
    for i, (_, parent, _, _) in enumerate(mesh.bones):
        if i:
            doc['nodes'][parent].setdefault('children', []).append(i)

    # vertices = wedges; winding so glTF's counter-clockwise faces point out of the body
    pts = [sp.point(p) for p in mesh.points]
    centre = tuple(sum(p[i] for p in pts) / len(pts) for i in range(3))

    def cross(a, b):
        return (a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0])

    out_votes = 0
    for w0, w1, w2, _ in mesh.faces:
        a, b, c = (pts[mesh.wedges[w][0]] for w in (w0, w1, w2))
        n = cross(tuple(b[i] - a[i] for i in range(3)), tuple(c[i] - a[i] for i in range(3)))
        mid = tuple((a[i] + b[i] + c[i]) / 3 - centre[i] for i in range(3))
        out_votes += 1 if sum(n[i] * mid[i] for i in range(3)) > 0 else -1
    # in UE space the plain cross product must point *into* the body (the axis swap to glTF
    # mirrors it back out)
    flip = out_votes > 0
    faces = [(w0, w2, w1, m) if flip else (w0, w1, w2, m) for w0, w1, w2, m in mesh.faces]
    normals = [[0.0, 0.0, 0.0] for _ in pts]
    for w0, w1, w2, _ in faces:
        a, b, c = (pts[mesh.wedges[w][0]] for w in (w0, w1, w2))
        n = cross(tuple(b[i] - a[i] for i in range(3)), tuple(c[i] - a[i] for i in range(3)))
        for w in (w0, w1, w2):
            for i in range(3):
                normals[mesh.wedges[w][0]][i] -= n[i]
    for n in normals:
        ln = math.sqrt(sum(c * c for c in n)) or 1.0
        n[:] = [c / ln for c in n]

    joints, weights = [], []
    for wl in mesh.weights:
        wl = sorted(wl, key=lambda x: -x[1])[:4]
        tot = sum(w for _, w in wl) or 1.0
        wl = [(b, w / tot) for b, w in wl] + [(0, 0.0)] * (4 - len(wl))
        joints.append(tuple(b for b, _ in wl))
        weights.append(tuple(w for _, w in wl))

    pos_acc = g.accessor('f', [gl_vec(pts[p]) for p, _, _ in mesh.wedges], FLOAT, 'VEC3', 34962, minmax=True)
    nrm_acc = g.accessor('f', [gl_dir(normals[p]) for p, _, _ in mesh.wedges], FLOAT, 'VEC3', 34962)
    uv_acc = g.accessor('f', [(u / 256.0, v / 256.0) for _, u, v in mesh.wedges], FLOAT, 'VEC2', 34962)
    jnt_acc = g.accessor('H', [joints[p] for p, _, _ in mesh.wedges], USHORT, 'VEC4', 34962)
    wgt_acc = g.accessor('f', [weights[p] for p, _, _ in mesh.wedges], FLOAT, 'VEC4', 34962)
    prims = []
    slots = sorted({mesh.materials[m][1] if m < len(mesh.materials) else 0 for *_, m in faces})
    for slot in slots:
        idx = [w for w0, w1, w2, m in faces if (mesh.materials[m][1] if m < len(mesh.materials) else 0) == slot
               for w in (w0, w1, w2)]
        doc['materials'].append({'name': f'Skin{slot}', 'pbrMetallicRoughness': {'metallicFactor': 0.0, 'roughnessFactor': 1.0}})
        prims.append({'attributes': {'POSITION': pos_acc, 'NORMAL': nrm_acc, 'TEXCOORD_0': uv_acc, 'JOINTS_0': jnt_acc,
                                     'WEIGHTS_0': wgt_acc},
                      'indices': g.accessor('I', idx, UINT, 'SCALAR', 34963), 'material': len(doc['materials']) - 1})
    doc['meshes'].append({'name': mesh_name, 'primitives': prims})
    gl_world = []
    for i, (q, t) in enumerate(world):
        gl_world.append((gl_quat(q), gl_vec(t)))
    ibm = g.accessor('f', [_inverse_rigid(q, t) for q, t in gl_world], FLOAT, 'MAT4')
    doc['skins'].append({'name': mesh_name, 'joints': list(range(len(mesh.bones))), 'skeleton': 0, 'inverseBindMatrices': ibm})
    doc['nodes'].append({'name': mesh_name, 'mesh': 0, 'skin': 0})
    doc['scenes'][0]['nodes'] = [0, len(doc['nodes']) - 1]

    info = {'mesh': mesh_name, 'bones': [b[0] for b in mesh.bones], 'skins': slots, 'sequences': {}}
    if anim_pkg and anim_name:
        anim = read_animation(anim_pkg, anim_name)
        if [b[0] for b in anim.bones] != [b[0] for b in mesh.bones]:
            raise ValueError(f'{anim_name}: bones differ from {mesh_name}')
        for seq, move in zip(anim.seqs, anim.moves):
            if only and seq.name not in only:
                continue
            loop = is_loop(seq.name)
            _animation(g, sp, seq, move, len(mesh.bones), loop)
            info['sequences'][seq.name] = {'frames': seq.num_frames, 'rate': seq.rate, 'loop': loop, 'group': seq.groups[0],
                                           'length': move['track_time'] if loop else max(seq.num_frames - 1, 1) / seq.rate,
                                           'notifies': [[t, f] for t, f in seq.notifies]}
    g.write(out)
    return info


def _animation(g: _Gltf, sp: _Space, seq, move, nbones: int, loop: bool):
    channels, samplers = [], []
    bone_of = move['bone_indices'] or list(range(nbones))
    end = move['track_time']
    for ti, track in enumerate(move['tracks']):
        bone = bone_of[ti]
        if not track.times:
            continue
        times = list(track.times)
        quats, poss = list(track.quats), list(track.positions)
        n = len(times)
        quats += [quats[-1]] * (n - len(quats))
        poss += [poss[-1]] * (n - len(poss))
        if loop and n > 1 and end > times[-1] + 1e-4:
            times.append(end)
            quats.append(quats[0])
            poss.append(poss[0])
        if n == 1:
            times = [0.0]
        rots, trs = [], []
        prev = None
        for q, p in zip(quats, poss):
            uq, ut = sp.bone(bone, q, p)
            gq = gl_quat(uq)
            if prev and sum(a * b for a, b in zip(prev, gq)) < 0:
                gq = tuple(-c for c in gq)
            prev = gq
            rots.append(gq)
            trs.append(gl_vec(ut))
        t_acc = g.accessor('f', times, FLOAT, 'SCALAR', minmax=True)
        for path, values in (('rotation', rots), ('translation', trs)):
            samplers.append({'input': t_acc, 'output': g.accessor('f', values, FLOAT, 'VEC4' if path == 'rotation' else 'VEC3'),
                             'interpolation': 'LINEAR'})
            channels.append({'sampler': len(samplers) - 1, 'target': {'node': bone, 'path': path}})
    g.doc['animations'].append({'name': seq.name, 'channels': channels, 'samplers': samplers})


# RageGfx: the helmet (team A) and balaclava (team B) soldiers, their animation sets and
# skins (Rage.RagePlayerHelmet / RagePlayerBalaclava TeamMeshName, TeamSkinName)
SOLDIERS = (('RagePlayer1Mesh', 'RagePlayerAAnim', 'RagePlayerGfx.MFTeamA'),
            ('RagePlayer2Mesh', 'RagePlayerBAnim', 'RagePlayerGfx.MFTeamB'))


def convert(cfg) -> Path:
    from .textures import _package_path
    pkg = Package(str(_package_path(cfg, 'RageGfx')))
    out = cfg.out_dir / 'Soldiers'
    meshes = []
    for mesh, anim, skin in SOLDIERS:
        info = export(pkg, mesh, pkg, anim, out / f'{mesh}.glb')
        print(f"  {mesh}: {len(info['bones'])} bones, {len(info['sequences'])} sequences")
        meshes.append({'name': mesh, 'file': f'Soldiers/{mesh}.glb', 'package': 'RageGfx', 'skin': skin,
                       'sequences': info['sequences']})
    path = out / 'soldiers.json'
    path.write_text(json.dumps({'meshes': meshes}, indent=1))
    return path


def main():
    pkg = Package(sys.argv[1])
    info = export(pkg, sys.argv[2], pkg, sys.argv[3] if len(sys.argv) > 4 else None, Path(sys.argv[-1]))
    print(f"{info['mesh']}: {len(info['bones'])} bones, skins {info['skins']}, {len(info['sequences'])} sequences")


if __name__ == '__main__':
    main()
