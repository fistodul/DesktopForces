"""Read the compiled BSP of a Mobile Forces level (.umf) and turn it into polygons.

A level's world geometry is the result of CSG over hundreds of brushes. Instead of
re-doing that CSG, we read the BSP the original editor already built (the level's
`Model` export): every BSP node that carries vertices is one convex polygon of
final, visible world geometry, and its surface (`FBspSurf`) gives the texture,
polygon flags and texture-mapping vectors.

The serialized layout is identical to Unreal Tournament (v69) UModel, verified
on mf-warehouse.umf (package version 83).
"""
from __future__ import annotations

from dataclasses import dataclass

from .upkg import Package, Reader

# Polygon flags (UE1 EPolyFlags) that matter for conversion
PF_INVISIBLE = 0x00000001
PF_MASKED = 0x00000002
PF_TRANSLUCENT = 0x00000004
PF_NOTSOLID = 0x00000008
PF_ENVIRONMENT = 0x00000010
PF_SEMISOLID = 0x00000020
PF_MODULATED = 0x00000040
PF_FAKEBACKDROP = 0x00000080
PF_TWOSIDED = 0x00000100
PF_AUTOUPAN = 0x00000200
PF_AUTOVPAN = 0x00000400
PF_NOSMOOTH = 0x00000800
PF_BIGWAVY = 0x00001000
PF_SPECIALPOLY = 0x00001000
PF_SMALLWAVY = 0x00002000
PF_FLAT = 0x00004000
PF_LOWSHADOWDETAIL = 0x00008000
PF_NOMERGE = 0x00010000
PF_CLOUDWAVY = 0x00020000
PF_DIRTYSHADOWS = 0x00040000
PF_BRIGHTCORNERS = 0x00080000
PF_SPECIALLIT = 0x00100000
PF_GOURAUD = 0x00200000
PF_NOBOUNDREJECTION = 0x00200000
PF_UNLIT = 0x00400000
PF_HIGHSHADOWDETAIL = 0x00800000
PF_PORTAL = 0x04000000
PF_MIRRORED = 0x08000000


@dataclass
class BspNode:
    plane: tuple[float, float, float, float]
    zone_mask: int
    flags: int
    i_vert_pool: int
    i_surf: int
    i_back: int
    i_front: int
    i_plane: int
    i_collision_bound: int
    i_render_bound: int
    i_zone: tuple[int, int]
    num_vertices: int
    i_leaf: tuple[int, int]


@dataclass
class BspSurf:
    texture: str | None
    poly_flags: int
    p_base: int
    v_normal: int
    v_texture_u: int
    v_texture_v: int
    i_light_map: int
    i_brush_poly: int
    pan_u: int
    pan_v: int
    actor: str | None


@dataclass
class WorldPoly:
    """One convex polygon of final level geometry."""
    vertices: list[tuple[float, float, float]]
    normal: tuple[float, float, float]
    texture: str | None
    poly_flags: int
    base: tuple[float, float, float]
    texture_u: tuple[float, float, float]
    texture_v: tuple[float, float, float]
    pan_u: int
    pan_v: int
    zone: int
    surf_index: int
    brush: str | None


class LevelModel:
    def __init__(self, pkg: Package, model_name: str | None = None):
        self.pkg = pkg
        exp = pkg.find(model_name, 'Model') if model_name else self._find_level_model()
        self.export = exp
        r = Reader(pkg.data, exp.offset)
        obj = pkg.read_object(exp)
        r.p = obj.native_offset
        # UPrimitive: FBox BoundingBox (Min, Max, IsValid byte) + FSphere BoundingSphere
        r.raw(25 + 16)
        self.vectors = [r.vec() for _ in range(r.index())]
        self.points = [r.vec() for _ in range(r.index())]
        self.nodes: list[BspNode] = []
        for _ in range(r.index()):
            plane = (r.f32(), r.f32(), r.f32(), r.f32())
            zone_mask = r.u64()
            flags = r.u8()
            i_vert_pool, i_surf, i_back, i_front, i_plane = r.index(), r.index(), r.index(), r.index(), r.index()
            i_cb, i_rb = r.index(), r.index()
            iz = (r.u8(), r.u8())
            nverts = r.u8()
            il = (r.i32(), r.i32())
            self.nodes.append(BspNode(plane, zone_mask, flags, i_vert_pool, i_surf, i_back, i_front,
                                      i_plane, i_cb, i_rb, iz, nverts, il))
        self.surfs: list[BspSurf] = []
        for _ in range(r.index()):
            tex = pkg.full_path(r.index())
            pf = r.u32()
            pb, vn, vu, vv, lm, bp = r.index(), r.index(), r.index(), r.index(), r.index(), r.index()
            pu, pv = r.i16(), r.i16()
            actor = pkg.ref_path(r.index())
            self.surfs.append(BspSurf(tex, pf, pb, vn, vu, vv, lm, bp, pu, pv, actor))
        self.verts = []
        for _ in range(r.index()):
            self.verts.append((r.index(), r.index()))  # pVertex, iSide
        self.num_shared_sides = r.i32()
        self.num_zones = r.i32()
        self.zones = []
        for _ in range(self.num_zones):
            actor = pkg.ref_path(r.index())
            connectivity, visibility = r.u64(), r.u64()
            self.zones.append(actor)  # MF has no LastRenderTime float here (UT does)
        self.end_offset = r.p

    def zone_at(self, p) -> int:
        """BSP zone index of a point (UE1 FBspNode: iZone[1] in front of the plane, iZone[0] behind)."""
        if not self.nodes:
            return 0
        i = 0
        while True:
            n = self.nodes[i]
            front = n.plane[0] * p[0] + n.plane[1] * p[1] + n.plane[2] * p[2] - n.plane[3] >= 0
            nxt = n.i_front if front else n.i_back
            if nxt < 0:
                return n.i_zone[1] if front else n.i_zone[0]
            i = nxt

    def _find_level_model(self):
        """The level BSP is the Model referenced by the Level export (it is the biggest one)."""
        models = self.pkg.exports_of('Model')
        return max(models, key=lambda e: e.size)

    def polygons(self, skip_flags: int = 0) -> list[WorldPoly]:
        out = []
        for node in self.nodes:
            if node.num_vertices < 3:
                continue
            surf = self.surfs[node.i_surf]
            if surf.poly_flags & skip_flags:
                continue
            verts = [self.points[self.verts[node.i_vert_pool + k][0]] for k in range(node.num_vertices)]
            out.append(WorldPoly(
                vertices=verts,
                normal=self.vectors[surf.v_normal],
                texture=surf.texture,
                poly_flags=surf.poly_flags,
                base=self.points[surf.p_base],
                texture_u=self.vectors[surf.v_texture_u],
                texture_v=self.vectors[surf.v_texture_v],
                pan_u=surf.pan_u,
                pan_v=surf.pan_v,
                zone=node.i_zone[1],
                surf_index=node.i_surf,
                brush=surf.actor,
            ))
        return out
