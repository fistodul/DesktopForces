"""`.mfmesh` intermediate mesh format, read by the UE editor importer (MobileForcesEditor module).

A mesh is two files:
  <name>.mfmesh.json  - metadata: materials, sections, counts, options
  <name>.mfmesh.bin   - little-endian float32 vertex data, then uint32 indices

Vertex layout (interleaved, 8 floats): position xyz (UE cm, mesh-local),
normal xyz, uv0 uv; `pos3f_nrm3f_uv2f_col3f` adds a linear RGB vertex colour (world
geometry: the UE1 zone ambient of each polygon). Triangles are grouped by section; a section is a contiguous
index range drawn with one material. Winding: front faces are clockwise when
seen from the front (normal side), which is Unreal's convention.

Kept deliberately trivial so other tools (VMF converter, Blender scripts) can
write it too.
"""
from __future__ import annotations

import json
import struct
from dataclasses import dataclass, field
from pathlib import Path

Vec3 = tuple[float, float, float]
Vec2 = tuple[float, float]


@dataclass
class MeshBuilder:
    name: str
    materials: list[str] = field(default_factory=list)
    # per material index: list of triangles, each 3 x (pos, normal, uv)
    tris: dict[int, list] = field(default_factory=dict)
    collision: str = 'complex'      # complex | none | simple_box
    lightmap_resolution: int = 64
    render: bool = True             # False = collision-only (invisible blocking geometry)
    has_colors: bool = False        # vertices carry a 4th element (r, g, b)

    def material_index(self, key: str) -> int:
        try:
            return self.materials.index(key)
        except ValueError:
            self.materials.append(key)
            return len(self.materials) - 1

    def add_triangle(self, material: str, v0, v1, v2, color=None):
        """Vertices are (pos, normal, uv); color (r, g, b) applies to all three corners."""
        if color is not None:
            self.has_colors = True
            v0, v1, v2 = ((*v0[:3], tuple(color)), (*v1[:3], tuple(color)), (*v2[:3], tuple(color)))
        self.tris.setdefault(self.material_index(material), []).append((v0, v1, v2))

    @property
    def triangle_count(self) -> int:
        return sum(len(t) for t in self.tris.values())

    def bounds(self):
        pts = [v[0] for ts in self.tris.values() for t in ts for v in t]
        if not pts:
            return (0, 0, 0), (0, 0, 0)
        return (tuple(min(p[i] for p in pts) for i in range(3)), tuple(max(p[i] for p in pts) for i in range(3)))

    def write(self, out_dir: Path) -> Path:
        out_dir.mkdir(parents=True, exist_ok=True)
        verts = bytearray()
        indices = []
        sections = []
        vmap: dict = {}
        nverts = 0
        white = (1.0, 1.0, 1.0)
        for mat in sorted(self.tris):
            first = len(indices)
            for tri in self.tris[mat]:
                for v in tri:
                    col = (v[3] if len(v) > 3 else white) if self.has_colors else ()
                    key = (round(v[0][0], 3), round(v[0][1], 3), round(v[0][2], 3),
                           round(v[1][0], 4), round(v[1][1], 4), round(v[1][2], 4),
                           round(v[2][0], 5), round(v[2][1], 5), *(round(c, 4) for c in col))
                    idx = vmap.get(key)
                    if idx is None:
                        idx = vmap[key] = nverts
                        nverts += 1
                        verts += struct.pack(f'<{8 + len(col)}f', *v[0], *v[1], *v[2], *col)
                    indices.append(idx)
            sections.append({'material': mat, 'first_index': first, 'num_triangles': (len(indices) - first) // 3})
        binname = f'{self.name}.mfmesh.bin'
        (out_dir / binname).write_bytes(bytes(verts) + struct.pack(f'<{len(indices)}I', *indices))
        lo, hi = self.bounds()
        meta = {
            'format': 'mfmesh', 'version': 1,
            'name': self.name,
            'bin': binname,
            'vertex_count': nverts,
            'index_count': len(indices),
            'vertex_layout': 'pos3f_nrm3f_uv2f_col3f' if self.has_colors else 'pos3f_nrm3f_uv2f',
            'materials': self.materials,
            'sections': sections,
            'collision': self.collision,
            'render': self.render,
            'lightmap_resolution': self.lightmap_resolution,
            'bounds': {'min': lo, 'max': hi},
        }
        path = out_dir / f'{self.name}.mfmesh.json'
        path.write_text(json.dumps(meta, indent=1))
        return path

    def write_obj(self, path: Path, texture_dir: str | None = None):
        """Debug dump (Wavefront OBJ + MTL) to inspect a conversion in Blender."""
        lines = [f'mtllib {path.stem}.mtl']
        mtl = []
        n = 1
        for mat in sorted(self.tris):
            lines.append(f'usemtl m{mat}')
            tex = self.materials[mat].split('|')[0]
            mtl.append(f'newmtl m{mat}')
            if texture_dir:
                mtl.append(f'map_Kd {texture_dir}/{"/".join(tex.split("."))}.png')
            for tri in self.tris[mat]:
                for v in tri:
                    lines.append('v {:.3f} {:.3f} {:.3f}'.format(*v[0]))
                    lines.append('vn {:.4f} {:.4f} {:.4f}'.format(*v[1]))
                    lines.append('vt {:.5f} {:.5f}'.format(v[2][0], 1 - v[2][1]))
                lines.append(f'f {n}/{n}/{n} {n+1}/{n+1}/{n+1} {n+2}/{n+2}/{n+2}')
                n += 3
        path.write_text('\n'.join(lines))
        path.with_suffix('.mtl').write_text('\n'.join(mtl))


def read(json_path: Path) -> MeshBuilder:
    """Load a written .mfmesh back into a builder (for tools that edit converted meshes)."""
    meta = json.loads(Path(json_path).read_text())
    data = Path(json_path).parent.joinpath(meta['bin']).read_bytes()
    n = meta['vertex_count']
    nf = 11 if meta.get('vertex_layout', '').endswith('col3f') else 8
    verts = [struct.unpack_from(f'<{nf}f', data, i * nf * 4) for i in range(n)]
    idx = struct.unpack_from(f"<{meta['index_count']}I", data, n * nf * 4)
    mb = MeshBuilder(meta['name'], materials=list(meta['materials']), collision=meta.get('collision', 'complex'),
                     lightmap_resolution=meta.get('lightmap_resolution', 64), render=meta.get('render', True))
    for sec in meta['sections']:
        tris = mb.tris.setdefault(sec['material'], [])
        for t in range(sec['num_triangles']):
            i0 = sec['first_index'] + 3 * t
            tris.append(tuple((v[0:3], v[3:6], v[6:8], *([v[8:11]] if nf == 11 else [])) for v in (verts[idx[i0 + k]] for k in range(3))))
    mb.has_colors = nf == 11
    return mb
