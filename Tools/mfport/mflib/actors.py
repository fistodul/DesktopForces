"""Map Mobile Forces (UE1) actors to spawn records for the UE5 level builder.

All Mobile Forces knowledge about actors lives here; the C++ builder
(DesktopForcesEditor/MFLevelBuilder) just spawns what the records say:

  {"type": "static_mesh", "mesh": ..., "location", "rotation", "scale"}
  {"type": "point_light" | "spot_light" | "directional_light", ..., "color", "intensity", "radius"}
  {"type": "actor", "class": "/Script/DesktopForces.MFPlayerStart", "props": {UE property: text}}
  {"type": "loadout_zone", "team", "box_min", "box_max"}
  {"type": "ambient_sound", "sound", "radius", "volume"}
  {"type": "legacy", "mf_class", "mf_name", "team", "props"}   (kept for future game modes)

T3D only lists properties that differ from the class defaults, so the defaults the
conversion needs are restated below (from the decompiled Engine/Classes/*.uc).
Distances become cm (UU_TO_CM), rotations degrees.
"""
from __future__ import annotations

import math

from . import t3d
from .config import UU_TO_CM

# --- UE1 defaults (Engine/Classes/Light.uc, Actor.uc, ZoneInfo.uc) ------------------
LIGHT_DEFAULTS = {'LightType': 'LT_Steady', 'LightBrightness': 64, 'LightHue': 0, 'LightSaturation': 255,
                  'LightRadius': 64, 'LightCone': 128, 'LightEffect': 'LE_None'}
ACTOR_LIGHT_DEFAULTS = {'LightType': 'LT_None', 'LightBrightness': 64, 'LightHue': 0, 'LightSaturation': 255,
                        'LightRadius': 64, 'LightCone': 128, 'LightEffect': 'LE_None'}
CLASS_LIGHT_EFFECT = {'Sunlight': 'LE_Sunlight', 'Spotlight': 'LE_Spotlight'}
LIGHT_CLASSES = {'Light', 'Sunlight', 'Spotlight', 'TriggerLight'}

# Navigation / editor-only classes that have no use in UE5 (navmesh replaces paths).
SKIP_CLASSES = {'Brush', 'PathNode', 'RagePathNode', 'Camera', 'NavigationInclusion', 'NavigationExclusion',
                'BlockedPath', 'RageAltPath', 'LevelSummary'}

# Loadout crate decorations: class -> (team, team games, non-team games)
CRATES = {
    'LoadoutCrate_Blue': ('0', True, False), 'LoadoutCrate_BlueOpen': ('0', True, False),
    'LoadoutCrate_BlueSkewed': ('0', True, False),
    'LoadoutCrate_Red': ('1', True, False), 'LoadoutCrate_RedOpen': ('1', True, False),
    'LoadoutCrate_RedSkewed': ('1', True, False),
    'LoadoutCrate_DM': ('255', False, True), 'LoadoutCrate_DMOpen': ('255', False, True),
    'LoadoutCrate_DMSkewed': ('255', False, True),
    'LoadoutCrate': ('255', True, True),
}

LIGHT_INTENSITY = 3.0       # UE unitless intensity of a full-brightness MF light: UE's Lambert divides by pi, so ~pi shows the texture at its own brightness
SUN_INTENSITY = 3.0         # lux, same reasoning


def ue1_hsv(hue: int, sat: int, bright: int) -> tuple[float, float, float]:
    """UE1 FGetHSV: light colour from LightHue / LightSaturation (255 = white) / LightBrightness."""
    b = bright * 1.4 / 255.0
    b *= 0.7 / (0.01 + math.sqrt(b)) if b > 0 else 0
    b = max(0.0, min(1.0, b))
    h = hue
    if h < 86:
        base = ((85 - h) / 85.0, (h - 0) / 85.0, 0.0)
    elif h < 171:
        base = (0.0, (170 - h) / 85.0, (h - 85) / 85.0)
    else:
        base = ((h - 170) / 85.0, 0.0, (255 - h) / 84.0)
    s = sat / 255.0
    return tuple((c + s * (1.0 - c)) * b for c in base)


def zone_ambient(props: dict) -> tuple[float, float, float]:
    """UE1 ZoneInfo ambient light: FGetHSV(AmbientHue, AmbientSaturation, AmbientBrightness)."""
    return ue1_hsv(_int(props, 'AmbientHue', {'AmbientHue': 0}),
                   _int(props, 'AmbientSaturation', {'AmbientSaturation': 255}),
                   _int(props, 'AmbientBrightness', {'AmbientBrightness': 0}))


def _int(props: dict, key: str, defaults: dict) -> int:
    v = props.get(key)
    return int(float(v)) if v is not None else int(defaults.get(key, 0))


# Legacy classes whose Team defaults to 0 in UnrealScript (checked in the decompiled .uc);
# others with a Team default it to 255 in their defaultproperties (e.g. RageDominationTimer).
TEAM_DEFAULT_ZERO = {'RageFlagBase', 'RageSafe', 'RageDetonationLock', 'TrailerPark', 'TrailerDelivery',
                     'LoadoutBlocker', 'CaptainStart'}


def rotate(v, rot: dict) -> tuple:
    """Rotate a vector like UE's FRotationMatrix(FRotator(Pitch, Yaw, Roll)) (degrees)."""
    p, y, r = (math.radians(rot.get(k, 0.0)) for k in ('pitch', 'yaw', 'roll'))
    sp, cp, sy, cy, sr, cr = math.sin(p), math.cos(p), math.sin(y), math.cos(y), math.sin(r), math.cos(r)
    ax = (cp * cy, cp * sy, sp)
    ay = (sr * sp * cy - cr * sy, sr * sp * sy + cr * cy, -sr * cp)
    az = (-(cr * sp * cy + sr * sy), cy * sr - cr * sp * sy, cr * cp)
    return tuple(ax[i] * v[0] + ay[i] * v[1] + az[i] * v[2] for i in range(3))


def _team(value) -> str:
    """MF team number -> team id string ('0' blue, '1' red, ... ; '255' = no team)."""
    try:
        n = int(float(value))
    except (TypeError, ValueError):
        return '255'
    return str(n) if 0 <= n < 255 else '255'


class ActorMapper:
    def __init__(self, zone_bounds: dict[int, tuple], static_meshes: dict):
        self.zone_bounds = zone_bounds
        self.static_meshes = static_meshes
        self.ambient = []          # (brightness, hue, saturation) per zone info, for the global ambient

    def map(self, a: dict) -> list[dict]:
        cls, props = a['class'], a['props']
        base = {'name': a['name'], 'location': a['location'], 'rotation': a['rotation']}

        if cls in SKIP_CLASSES:
            return []

        if cls == 'StaticMeshActor' or a.get('static_mesh'):
            mesh = a.get('static_mesh')
            if not mesh or mesh not in self.static_meshes or t3d.parse_bool(props.get('bHidden')):
                return []
            scale = t3d.parse_float(props.get('DrawScale'), 1.0)
            s3 = t3d.parse_vector(props.get('DrawScale3D'), (1.0, 1.0, 1.0))
            sc = [scale * s3[0], scale * s3[1], scale * s3[2]]
            # UE1/UE2 draw a mesh at Location + R * S * (v - PrePivot) (Actor.LocalToWorld);
            # the mesh's own origin is v = 0, so the actor moves by R * S * -PrePivot
            pp = t3d.parse_vector(props.get('PrePivot'), (0.0, 0.0, 0.0))
            off = rotate((-pp[0] * sc[0] * UU_TO_CM, -pp[1] * sc[1] * UU_TO_CM, -pp[2] * sc[2] * UU_TO_CM), a['rotation'])
            loc = [base['location'][i] + off[i] for i in range(3)]
            return [dict(base, location=loc, type='static_mesh', mesh=self.static_meshes[mesh]['mesh'],
                         scale=sc, collision=t3d.parse_bool(props.get('bCollideActors'), True))]

        if cls in LIGHT_CLASSES or props.get('LightType') not in (None, 'LT_None'):
            return self._light(cls, base, props)

        if cls in ('PlayerStart', 'CaptainStart'):
            team = props.get('TeamNumber', '0')
            rec = dict(base, type='actor', **{'class': '/Script/DesktopForces.MFPlayerStart'},
                       props={'TeamNumber': str(int(float(team)) & 255)})
            if t3d.parse_bool(props.get('bEnabled'), True) is False:
                rec['props']['bEnabled'] = 'False'
            if cls == 'CaptainStart':
                rec['props']['bCaptainStart'] = 'True'
            out = [rec]
            if cls == 'CaptainStart':
                out.append(self._legacy(a))
            return out

        if cls == 'LadderClimber':
            r = t3d.parse_float(props.get('CollisionRadius'), 22.0) * UU_TO_CM
            h = t3d.parse_float(props.get('CollisionHeight'), 22.0) * UU_TO_CM
            return [dict(base, type='actor', **{'class': '/Script/DesktopForces.MFLadderVolume'},
                         props={'Radius': f'{r:.2f}', 'HalfHeight': f'{h:.2f}',
                                'ClimbSpeed': props.get('ClimbSpeed', '0.5')})]

        if cls in CRATES:
            team, team_play, non_team = CRATES[cls]
            hidden = t3d.parse_bool(props.get('bHidden'))
            if not t3d.parse_bool(props.get('IsActive'), True):
                return []
            variant = 'Open' if cls.endswith('Open') else ('Skewed' if cls.endswith('Skewed') else 'Closed')
            return [dict(base, type='actor', **{'class': '/Script/DesktopForces.MFLoadoutCrate'},
                         props={'Team': team, 'bTeamPlay': str(team_play), 'bNonTeamPlay': str(non_team),
                                'Variant': variant if not hidden else 'Hidden'})]

        if cls == 'LoadoutZone':
            self._note_ambient(props)
            box = self.zone_bounds.get(a['zone'])
            if not box:
                return []
            return [dict(base, type='loadout_zone', team=_team(props.get('Team', 255)),
                         box_min=box[0], box_max=box[1])]

        if cls == 'CaptainProhibitedZone':
            # MF: captains standing in these corridors next to the start rooms are sent back
            self._note_ambient(props)
            box = self.zone_bounds.get(a['zone'])
            out = [self._legacy(a)]
            if box:
                out.append(dict(base, type='zone_box', tag='CaptainProhibited', box_min=box[0], box_max=box[1]))
            return out

        if cls == 'SkyZoneInfo':
            # UE1 draws the sky zone, seen from here, through the map's sky windows
            # (PF_FakeBackdrop); AMFSkyCapture captures it into the sky material's cubemap
            self._note_ambient(props)
            return [self._legacy(a), dict(base, type='actor', **{'class': '/Script/DesktopForces.MFSkyCapture'}, props={})]

        if cls in ('ZoneInfo', 'RageZoneInfo', 'RageWaterZone'):
            self._note_ambient(props)
            out = [self._legacy(a)]
            # water / pain / current: a volume over the zone's bounds (RageWaterZone: water)
            water = t3d.parse_bool(props.get('bWaterZone'), cls == 'RageWaterZone')
            pain = t3d.parse_bool(props.get('bPainZone')) and t3d.parse_float(props.get('DamagePerSec')) != 0
            vel = t3d.parse_vector(props.get('ZoneVelocity'))
            box = self.zone_bounds.get(a['zone'])
            if box and min(hi - lo for lo, hi in zip(box[0], box[1])) < 10.0:
                box = None   # flat zone bounds (a sheet): nothing to stand in
            if box and (water or pain or any(vel)):
                out.append(dict(base, type='zone_volume', box_min=box[0], box_max=box[1], water=water,
                                damage_per_sec=t3d.parse_float(props.get('DamagePerSec')) if pain else 0.0,
                                damage_type=(t3d.parse_object_ref(props.get('DamageType')) or props.get('DamageType', '') or '').strip("'\""),
                                velocity=[c * UU_TO_CM for c in vel],
                                zone_name=props.get('ZoneName', '').strip('"')))
            return out

        if cls == 'LevelInfo':
            self._note_ambient(props)
            return [dict(base, type='map_info',
                         title=props.get('Title', '').strip('"'), author=props.get('Author', '').strip('"'),
                         ideal_players=props.get('IdealPlayerCount', '').strip('"'),
                         song=t3d.parse_object_ref(props.get('Song')))]

        if cls == 'AmbientSound':
            sound = t3d.parse_object_ref(props.get('AmbientSound'))
            if not sound:
                return []
            radius = 25.0 * (_int(props, 'SoundRadius', {'SoundRadius': 32}) + 1) * UU_TO_CM  # UE1 WorldSoundRadius
            volume = _int(props, 'SoundVolume', {'SoundVolume': 128}) / 255.0
            return [dict(base, type='ambient_sound', sound=sound, radius=radius, volume=volume)]

        return [self._legacy(a)]

    # --- helpers ------------------------------------------------------------------------
    def _legacy(self, a: dict) -> dict:
        props = a['props']
        team = props.get('Team', props.get('TeamNumber'))
        if team is None and a['class'] in TEAM_DEFAULT_ZERO:
            team = 0   # T3D omits defaults: these classes declare `var() byte Team` (default 0 = blue)
        return {'name': a['name'], 'location': a['location'], 'rotation': a['rotation'], 'type': 'legacy',
                'mf_class': a['class'], 'mf_name': a['name'], 'team': _team(team) if team is not None else '255',
                'props': {k: (v if isinstance(v, str) else ' '.join(v)) for k, v in props.items()}}

    def _note_ambient(self, props: dict):
        b = _int(props, 'AmbientBrightness', {'AmbientBrightness': 0})
        if b > 0:
            self.ambient.append((b, _int(props, 'AmbientHue', {}), _int(props, 'AmbientSaturation', {'AmbientSaturation': 255})))

    def _light(self, cls: str, base: dict, props: dict) -> list[dict]:
        defaults = LIGHT_DEFAULTS if cls in LIGHT_CLASSES else ACTOR_LIGHT_DEFAULTS
        if props.get('LightType', defaults['LightType']) == 'LT_None':
            return []
        bright = _int(props, 'LightBrightness', defaults)
        if bright <= 0:
            return []
        color = ue1_hsv(_int(props, 'LightHue', defaults), _int(props, 'LightSaturation', defaults), 255)
        level = ue1_hsv(0, 255, bright)[0]                    # brightness curve on its own
        radius = 25.0 * (_int(props, 'LightRadius', defaults) + 1) * UU_TO_CM   # UE1 WorldLightRadius
        effect = props.get('LightEffect', CLASS_LIGHT_EFFECT.get(cls, defaults['LightEffect']))
        rec = dict(base, color=list(color), mf_effect=effect)
        if effect in ('LE_Sunlight', 'LE_SunlightSpecial'):
            return [dict(rec, type='directional_light', intensity=SUN_INTENSITY * level)]
        if effect in ('LE_Spotlight', 'LE_StaticSpot'):
            cone = _int(props, 'LightCone', defaults)          # 0..255 -> half angle up to 90 deg
            return [dict(rec, type='spot_light', intensity=LIGHT_INTENSITY * level, radius=radius,
                         cone=max(5.0, cone / 255.0 * 90.0))]
        return [dict(rec, type='point_light', intensity=LIGHT_INTENSITY * level, radius=radius)]

    def environment_color(self) -> tuple[list[float], float]:
        """Most common zone ambient -> Lightmass environment colour (MF zone ambient light)."""
        if not self.ambient:
            return [0.0, 0.0, 0.0], 0.0
        best = max(set(self.ambient), key=self.ambient.count)
        col = ue1_hsv(best[1], best[2], 255)
        return list(col), ue1_hsv(0, 255, best[0])[0]
