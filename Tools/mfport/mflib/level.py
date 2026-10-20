"""Convert a Mobile Forces level (.umf) to the intermediate form the UE importer builds a map from.

Output, under work/out/Maps/<map>/:
  geo/World_<cx>_<cy>.mfmesh.*      world (BSP) geometry, chunked on a grid
  geo/Collision_<cx>_<cy>.mfmesh.*  invisible-but-solid BSP surfaces (collision only)
  meshes/<mesh>.mfmesh.*            the map's static meshes (props, terrain...)
  level.json                        actors (converted transforms + raw MF properties),
                                    zones, level info, list of used materials

Geometry comes from the level's compiled BSP (see bsp.py); actors come from the
UCC T3D export of the level (see t3d.py), because T3D already resolves every
property to text.
"""
from __future__ import annotations

import json
import math
from pathlib import Path

from . import t3d, ucc
from .actors import ActorMapper, zone_ambient
from .bsp import (LevelModel, PF_FAKEBACKDROP, PF_INVISIBLE, PF_MASKED, PF_MODULATED, PF_NOTSOLID,
                  PF_PORTAL, PF_TRANSLUCENT, PF_TWOSIDED, PF_UNLIT)
from .config import Config, UU_TO_CM
from .mfmesh import MeshBuilder
from .polyfix import newell_normal, triangulate, weld_tjunctions
from .upkg import Package

CHUNK_SIZE = 2048.0            # world geometry grid cell, in MF units (~46 m)
LIGHTMAP_TEXEL = 16.0          # target lightmap texel size, MF units (36 cm)
LIGHTMAP_BUDGET = 6_000_000    # lightmap texels per map (world + every prop instance): big outdoor
                               # maps get coarser texels instead of 25M (VRAM on 4 GB cards, bake time)
LIGHTMASS_BOUNCES = 2          # baked bounce light (free at runtime; fills what UE1 zone ambient lit)

ROT_TO_DEG = 360.0 / 65536.0


def material_key(texture: str | None, flags: int) -> str:
    """Material identity = texture + blend mode + shading flags.

    UE1 blend modes: PF_Translucent is *additive*, PF_Modulated multiplies (x2),
    PF_Masked is alpha-test on palette index 0. PF_FakeBackdrop marks sky windows.
    """
    tex = texture if texture and texture != 'None' else 'Engine.DefaultTexture'
    if flags & PF_FAKEBACKDROP:
        return f'{tex}|sky'
    mode = 'opaque'
    if flags & PF_TRANSLUCENT:
        mode = 'additive'
    elif flags & PF_MODULATED:
        mode = 'modulated'
    elif flags & PF_MASKED:
        mode = 'masked'
    if flags & PF_UNLIT:
        mode += '_unlit'
    if flags & PF_TWOSIDED:
        mode += '_2s'
    return f'{tex}|{mode}'


def _sub(a, b):
    return (a[0] - b[0], a[1] - b[1], a[2] - b[2])


def _dot(a, b):
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]


def _cross(a, b):
    return (a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0])


def _scale(v, s=UU_TO_CM):
    return (v[0] * s, v[1] * s, v[2] * s)


def _area(verts) -> float:
    a = 0.0
    for i in range(1, len(verts) - 1):
        c = _cross(_sub(verts[i], verts[0]), _sub(verts[i + 1], verts[0]))
        a += math.sqrt(_dot(c, c)) * 0.5
    return a


def _next_pow2(x: float) -> int:
    return 1 << max(0, math.ceil(math.log2(max(1.0, x))))


class LevelConverter:
    def __init__(self, cfg: Config, map_name: str):
        self.cfg = cfg
        self.map = map_name
        self.umf = cfg.game_dir / 'Maps' / f'{map_name}.umf'
        self.out = cfg.out_dir / 'Maps' / map_name
        self.raw = cfg.raw_dir / 'Maps' / map_name
        self.texture_sizes: dict[str, tuple[int, int]] = {}
        self.texture_polyflags: dict[str, int] = {}
        self.usage: dict[str, set[str]] = {}     # texture key (lower) -> blend modes

    # --- inputs -----------------------------------------------------------
    def export_t3d(self) -> Path:
        t3d_path = self.raw / 'MyLevel.t3d'
        if not t3d_path.exists():
            ucc.batch_export(self.cfg, f'Maps/{self.map}.umf', 'Level', 't3d', self.raw)
        return t3d_path

    def texture_size(self, key: str | None) -> tuple[int, int]:
        """Original texture size (UV normalisation). Looked up lazily from the texture's package."""
        if not key:
            return (256, 256)
        self._load_texture_info(key)
        return self.texture_sizes[key.lower()]

    def texture_flags(self, key: str | None) -> int:
        """Poly flags a texture adds to every surface using it (UE1: surface | texture PolyFlags;
        bMasked textures are see-through at palette index 0 wherever they're used)."""
        if not key or key == 'None':
            return 0
        self._load_texture_info(key)
        return self.texture_polyflags.get(key.lower(), 0)

    def _load_texture_info(self, key: str):
        lk = key.lower()
        if lk in self.texture_sizes:
            return
        from .textures import texture_index
        pkg = key.split('.')[0]
        try:
            for k, info in texture_index(self.cfg, pkg).items():
                self.texture_sizes[k] = (info['usize'] or 256, info['vsize'] or 256)
                self.texture_polyflags[k] = info['polyflags'] | (PF_MASKED if info['masked'] else 0)
        except FileNotFoundError:
            pass
        self.texture_sizes.setdefault(lk, (256, 256))

    def add_minimap(self, actors: list[dict], spawns: list[dict]):
        """MF MiniMapInfo -> the map_info record: texture + world bounds (cm, MapLeft/Top/Right/Bottom)."""
        mm = next((a for a in actors if a['class'] == 'MiniMapInfo'), None)
        info = next((s for s in spawns if s['type'] == 'map_info'), None)
        tex = t3d.parse_object_ref(mm['props'].get('MiniMapTex(0)')) if mm else None
        if not (mm and info and tex):
            return
        p = mm['props']
        info['minimap'] = {
            'texture': tex,
            'bounds': [float(p.get(k, 0)) * UU_TO_CM for k in ('MapLeft', 'MapTop', 'MapRight', 'MapBottom')],
        }
        self.usage.setdefault(tex.lower(), set()).add('lumalpha')   # black = see-through, like MF's alpha style

    def _use(self, key: str):
        tex, mode = key.split('|')
        self.usage.setdefault(tex.lower(), set()).add(mode.split('_')[0])

    # --- world geometry ---------------------------------------------------
    def zone_bounds(self, model: LevelModel) -> dict[int, tuple]:
        """Axis-aligned bounds (UE cm) of every BSP zone, from the polygons facing into it."""
        lo: dict[int, list] = {}
        hi: dict[int, list] = {}
        for poly in model.polygons():
            for z in {poly.zone}:
                l = lo.setdefault(z, [1e18] * 3)
                h = hi.setdefault(z, [-1e18] * 3)
                for v in poly.vertices:
                    for i in range(3):
                        l[i] = min(l[i], v[i])
                        h[i] = max(h[i], v[i])
        return {z: (_scale(lo[z]), _scale(hi[z])) for z in lo}

    @staticmethod
    def zone_ambients(model: LevelModel, actors: list[dict]) -> list[tuple]:
        """UE1 ambient light colour per BSP zone (its ZoneInfo, else the LevelInfo)."""
        by_name = {a['name']: a for a in actors}
        level_info = next((a for a in actors if a['class'] == 'LevelInfo'), None)
        out = []
        for name in model.zones:
            a = by_name.get(name) if name else level_info
            out.append(tuple(round(c, 4) for c in zone_ambient(a['props'])) if a else (0.0, 0.0, 0.0))
        return out

    def convert_world(self, model: LevelModel, ambients: list[tuple]) -> list[dict]:
        chunks: dict[tuple, MeshBuilder] = {}
        coll: dict[tuple, MeshBuilder] = {}
        areas: dict[tuple, float] = {}
        skipped = 0
        polys = []
        for poly in model.polygons():
            f = poly.poly_flags
            if f & PF_INVISIBLE and f & (PF_NOTSOLID | PF_PORTAL):
                skipped += 1
                continue
            polys.append(poly)
        # main ambient = area-weighted over the drawn surfaces (zones without a ZoneInfo use the
        # LevelInfo's, e.g. MF-Filip lights everything that way)
        weights: dict[tuple, float] = {}
        for poly in polys:
            if not poly.poly_flags & PF_INVISIBLE and 0 <= poly.zone < len(ambients):
                weights[ambients[poly.zone]] = weights.get(ambients[poly.zone], 0.0) + _area(poly.vertices)
        self.map_ambient = max(weights, key=weights.get) if weights else (0.0, 0.0, 0.0)
        welded, inserted = weld_tjunctions([p.vertices for p in polys])
        print(f'  world: {inserted} T-junction vertices welded')
        for poly, verts in zip(polys, welded):
            f = poly.poly_flags
            render = not f & PF_INVISIBLE
            collide = not f & (PF_NOTSOLID | PF_PORTAL)
            c = tuple(sum(v[i] for v in poly.vertices) / len(poly.vertices) for i in range(3))
            cell = (math.floor(c[0] / CHUNK_SIZE), math.floor(c[1] / CHUNK_SIZE))
            if render:
                key = material_key(poly.texture, f | self.texture_flags(poly.texture))
                self._use(key)
                # sky windows go in their own chunks: drawn, but they cast no shadow, so sunlight
                # and the sky's ambient come in through them (UE1 sunlight lit what saw the sky)
                sky = key.endswith('|sky')
                ck = (cell, sky)
                mb = chunks.get(ck)
                if mb is None:
                    mb = chunks[ck] = MeshBuilder((f'{"Sky" if sky else "World"}_{cell[0]}_{cell[1]}').replace('-', 'm'))
                    mb.collision = 'none'
                # UE1 zone ambient -> vertex colour (alpha 1 = per-zone; see FMFImporter::AddAmbientFloor)
                amb = ambients[poly.zone] if 0 <= poly.zone < len(ambients) else self.map_ambient
                self._emit_poly(mb, key, poly, verts, cell, amb)
                areas[ck] = areas.get(ck, 0.0) + _area(poly.vertices)
            if collide:
                cb = coll.get(cell)
                if cb is None:
                    cb = coll[cell] = MeshBuilder(f'Collision_{cell[0]}_{cell[1]}'.replace('-', 'm'))
                    cb.render = False
                    cb.collision = 'complex'
                self._emit_poly(cb, 'Engine.DefaultTexture|collision', poly, verts, cell)
        out = []
        geo = self.out / 'geo'
        for (cell, sky), mb in chunks.items():
            texels = math.sqrt(areas[(cell, sky)]) / LIGHTMAP_TEXEL
            mb.lightmap_resolution = 4 if sky else int(min(1024, max(64, _next_pow2(texels))))
            out.append(dict(self._chunk_entry(mb, cell, geo), sky=sky, lightmap_size_uu=0.0 if sky else math.sqrt(areas[(cell, sky)])))
        for cell, cb in coll.items():
            out.append(self._chunk_entry(cb, cell, geo))
        print(f'  world: {len(chunks)} render chunks (incl. sky), {len(coll)} collision chunks, {skipped} portal polys skipped')
        return out

    def apply_lightmap_budget(self, world: list[dict], meshes: dict[str, dict], spawns: list[dict]):
        """Coarser texels when the map would need more than LIGHTMAP_BUDGET lightmap texels.

        Every static mesh instance gets its own lightmap, so props count once per placement;
        small props go down to 8 x 8 (they are mostly evenly lit anyway)."""
        uses: dict[str, int] = {}
        for rec in spawns:
            if rec['type'] == 'static_mesh':
                name = rec['mesh'].split('/')[-1]
                uses[name] = uses.get(name, 0) + 1
        chunks = [w for w in world if w.get('render') and not w.get('sky')]

        def plan(texel: float) -> tuple[int, dict, dict]:
            cres = {w['mesh']: int(min(1024, max(32, _next_pow2(w['lightmap_size_uu'] / texel)))) for w in chunks}
            mres = {n: int(min(256, max(8, _next_pow2(m['lightmap_size_uu'] / texel)))) for n, m in meshes.items()}
            total = sum(r * r for r in cres.values()) + sum(mres[n] ** 2 * uses.get(n, 0) for n in mres)
            return total, cres, mres

        texel = LIGHTMAP_TEXEL
        total, cres, mres = plan(texel)
        start = total
        while total > LIGHTMAP_BUDGET and texel < LIGHTMAP_TEXEL * 64:
            texel *= 1.25
            total, cres, mres = plan(texel)
        for w in chunks:
            w['lightmap_resolution'] = cres[w['mesh']]
            self._set_lightmap(self.out / (w['mesh'] + '.mfmesh.json'), w['lightmap_resolution'])
        for n, m in meshes.items():
            m['lightmap_resolution'] = mres[n]
            self._set_lightmap(self.out / (m['mesh'] + '.mfmesh.json'), mres[n])
        print(f'  lightmaps: {total / 1e6:.1f}M texels at {texel * UU_TO_CM:.0f} cm (unbudgeted {start / 1e6:.1f}M)')

    @staticmethod
    def _set_lightmap(path: Path, res: int):
        data = json.loads(path.read_text())
        if data.get('lightmap_resolution') != res:
            data['lightmap_resolution'] = res
            path.write_text(json.dumps(data, indent=1))

    def _chunk_entry(self, mb: MeshBuilder, cell, geo: Path) -> dict:
        pivot = self._cell_pivot(cell)
        mb.write(geo)
        return {'mesh': f'geo/{mb.name}', 'location': _scale(pivot), 'render': mb.render,
                'triangles': mb.triangle_count, 'lightmap_resolution': mb.lightmap_resolution}

    @staticmethod
    def _cell_pivot(cell):
        return ((cell[0] + 0.5) * CHUNK_SIZE, (cell[1] + 0.5) * CHUNK_SIZE, 0.0)

    def _emit_poly(self, mb: MeshBuilder, key: str, poly, verts, cell, color=None):
        pivot = self._cell_pivot(cell)
        usize, vsize = self.texture_size(poly.texture)
        n = poly.normal
        # orient triangles so cross(v1-v0, v2-v0) points along the normal (from all vertices:
        # BSP splits often leave the first three collinear)
        if _dot(newell_normal(verts), n) < 0:
            verts = list(reversed(verts))

        def vtx(p):
            d = _sub(p, poly.base)
            u = (_dot(d, poly.texture_u) + poly.pan_u) / usize
            v = (_dot(d, poly.texture_v) + poly.pan_v) / vsize
            return (_scale(_sub(p, pivot)), n, (u, v))

        pts, tris = triangulate(verts)
        vs = [vtx(p) for p in pts]
        for a, b, c in tris:
            mb.add_triangle(key, vs[a], vs[b], vs[c], color)

    # --- static meshes ----------------------------------------------------
    def convert_static_meshes(self, level: t3d.Block) -> dict[str, dict]:
        meshes = {}
        out = self.out / 'meshes'
        for actor in level.children:
            for blk in actor.find('StaticMesh'):
                name = blk.name
                if name in meshes:
                    continue
                mb = MeshBuilder(name)
                mb.collision = 'complex'
                area = 0.0
                for tri in t3d.static_mesh_triangles(blk):
                    tex = tri.texture
                    key = material_key(tex, tri.poly_flags | self.texture_flags(tex))
                    self._use(key)
                    p = [v[0] for v in tri.verts]
                    vs = [(_scale(v[0]), v[1], v[2]) for v in tri.verts]
                    geo_n = _cross(_sub(p[1], p[0]), _sub(p[2], p[0]))
                    avg_n = tuple(sum(v[1][i] for v in tri.verts) for i in range(3))
                    if _dot(avg_n, avg_n) < 1e-12:
                        # exporter wrote NaN normals: use the face's own
                        ln = math.sqrt(_dot(geo_n, geo_n)) or 1.0
                        unit = tuple(c / ln for c in geo_n)
                        vs = [(v[0], unit, v[2]) for v in vs]
                    elif _dot(geo_n, avg_n) < 0:
                        vs = [vs[0], vs[2], vs[1]]
                    mb.add_triangle(key, *vs)
                    area += _area(p)
                mb.lightmap_resolution = int(min(256, max(32, _next_pow2(math.sqrt(area) / LIGHTMAP_TEXEL))))
                mb.write(out)
                meshes[name] = {'mesh': f'meshes/{name}', 'triangles': mb.triangle_count,
                                'lightmap_resolution': mb.lightmap_resolution, 'lightmap_size_uu': math.sqrt(area)}
        print(f'  static meshes: {len(meshes)}')
        return meshes

    # --- actors -------------------------------------------------------------
    def convert_actors(self, level: t3d.Block) -> list[dict]:
        actors = []
        for a in level.children:
            if a.kind != 'Actor' or a.cls in ('Brush',):
                continue
            loc = t3d.parse_vector(a.props.get('Location'))
            pitch, yaw, roll = t3d.parse_rotator(a.props.get('Rotation'))
            props = {k: v for k, v in a.props.items()
                     if k not in ('Level', 'Region', 'OldLocation', 'ColLocation', 'Location', 'Rotation', 'Name')
                     and not k.startswith('ZoneMask')}
            entry = {
                'class': a.cls,
                'name': a.name,
                'location': _scale(loc),
                'rotation': {'pitch': pitch * ROT_TO_DEG, 'yaw': yaw * ROT_TO_DEG, 'roll': roll * ROT_TO_DEG},
                'zone': _zone_number(a.props.get('Region')),
                'props': props,
            }
            if a.cls == 'StaticMeshActor' or 'StaticMesh' in a.props:
                ref = t3d.parse_object_ref(a.props.get('StaticMesh'))
                if ref:
                    entry['static_mesh'] = ref.split('.')[-1]
            actors.append(entry)
        return actors

    # --- driver -------------------------------------------------------------
    def run(self) -> Path:
        print(f'[{self.map}]')
        self.out.mkdir(parents=True, exist_ok=True)
        pkg = Package(str(self.umf))
        model = LevelModel(pkg)
        level = t3d.parse(t3d.read_text(self.export_t3d())).children[0]
        actors = self.convert_actors(level)
        ambients = self.zone_ambients(model, actors)
        world = self.convert_world(model, ambients)
        meshes = self.convert_static_meshes(level)
        bounds = self.zone_bounds(model)
        mapper = ActorMapper(bounds, meshes)
        spawns = [rec for a in actors for rec in mapper.map(a)]
        self.add_minimap(actors, spawns)
        self.apply_lightmap_budget(world, meshes, spawns)
        # UE1 zone ambient (flat light everywhere in a zone) becomes mostly UE-native lighting:
        # the map's main ambient colour is Lightmass's sky colour (what rays leaving through sky
        # windows pick up) and bounced light fills the rest; a small unshadowed floor stays in the
        # lit materials: world vertex colour = its zone's ambient, everything else uses the main
        # one (MPC_MF.Ambient), both scaled by AmbientFloor x the player's brightness
        amb = [round(c, 4) for c in self.map_ambient]
        for rec in spawns:
            if rec['type'] == 'map_info':
                rec['ambient'] = amb
        env_color, env_intensity = amb, 1.0
        data = {
            'map': self.map,
            'uu_to_cm': UU_TO_CM,
            'world': world,
            'static_meshes': meshes,
            'spawns': spawns,
            'lightmass': {'environment_color': env_color, 'environment_intensity': env_intensity, 'bounces': LIGHTMASS_BOUNCES},
            'actors': actors,
            'zones': model.zones,
            'zone_bounds': {str(z): [list(lo), list(hi)] for z, (lo, hi) in bounds.items()},
            'texture_usage': {k: sorted(v) for k, v in sorted(self.usage.items())},
        }
        path = self.out / 'level.json'
        path.write_text(json.dumps(data, indent=1))
        from collections import Counter
        kinds = Counter(r['type'] for r in spawns)
        print(f'  actors: {len(actors)} -> spawns {dict(kinds)} -> {path}')
        return path


def _zone_number(region: str | None) -> int:
    if not region:
        return 0
    d = t3d.parse_struct(region)
    return int(d.get('ZoneNumber', 0))
