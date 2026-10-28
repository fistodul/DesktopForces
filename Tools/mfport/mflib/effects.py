"""MF sprite particles (Engine.RageSprite) -> flipbook textures + emitter descriptions.

MF draws its fire, smoke, sparks, blood and rain with RageSprite: script calls
AddOne(Location, Velocity, Texture, Alpha, Size, DrawType flags, Lifetime) and native code
moves, animates (the texture's AnimNext chain) and fades each particle. In-level emitters are
RageEffects.ParticleEffect actors (Particles[8] of ParticleEmitter structs, FireyExample is one
with defaults); explosions and burning wrecks are scripted RageSprites (RageEffects.Explosion,
VehicleExplosion, ExplosionFire) and EffectsFix's SmokePuff(Veh).

Here every effect becomes a list of emitter descriptions (UE1 units, see EMITTER below) that the
UE importer turns into particle systems; textures become flipbook atlases (frames left to
right, top to bottom). Output: work/out/Effects/effects.json + Effects/<package>/.../<name>.png.

EMITTER keys (missing = default):
  tex          texture key of the first frame ("rageeffects.densesmoke.densesmoke_a01")
  draw         'add' (RSDT_Additive) | 'sub' (RSDT_Subtractive) | 'alpha' (Normal / Masked / AlphaTexture)
  scale_spawn_rate  false preserves bounded gameplay cues on Low effects quality
  alpha_param, rate_param  optional runtime float parameter names
  alpha        0..1 (AddOne Alpha / 255)
  size         [min, max] times the texture's width (RageSprite Size, UE1 sprites: DrawScale x USize)
  life         seconds
  burst        particles at once when the effect starts, or
  rate         particles per second while on: 'period' s on, 'interval' s off, 'phase' s into the
               cycle at the start (ParticleEmitter.PhaseShift; negative = a delay); rate_keys
               [[t, rate], ...] instead for a rate that changes over a one-shot effect (each
               held until the next key)
  loop         repeat forever (map emitters) or play once (scripted effects)
  loc, loc_rand        offset and random offset (VRand() * loc_rand, an ellipsoid shell)
  vel, vel_rand        velocity and random velocity (uu/s)
  vel_radial           speed away from the effect's origin (Normal(loc - origin) * speed)
  anim_once    play the frames once over the particle's life (flag 16), else loop them
  fade         alpha falls to 0 over the life (flag 128)
  gravity      falls with the zone's gravity (flag 64)
  collide, kill_on_collide   bounce on / die at the level (flags 32, 4096)
  stretch      0, 1, 2, 4: drawn along the velocity, length scaled by speed (flags 256 + 512/1024)
  rotate       spin, fraction of the full rotation rate (flag 2048 + rate bits); 0 = none
"""
from __future__ import annotations

import json
import math

from PIL import Image

from . import t3d
from .config import Config
from .textures import _package_path, decode_p8, texture_index
from .upkg import Package

# ParticleEffect.ROTRATE -> fraction of the full rate
ROTRATE = {'EIGTH': 1 / 8, 'QUARTER': 2 / 8, 'THREE_EIGTHS': 3 / 8, 'HALF': 4 / 8, 'FIVE_EIGHTHS': 5 / 8,
           'THREE_QUARTERS': 6 / 8, 'SEVEN_EIGTHS': 7 / 8, 'FULL': 1.0}
DRAW = {'RSDT_Normal': 'alpha', 'RSDT_Additive': 'add', 'RSDT_Subtractive': 'sub', 'RSDT_Masked': 'alpha',
        'RSDT_AlphaTexture': 'alpha'}
STRETCH = {'SLBV_None': 0, 'SLBV_ScaleByVelocity': 1, 'SLBV_ScaleByDoubleVelocity': 2, 'SLBV_ScaleByFourTimesVelocity': 4}

DENSE = 'rageeffects.densesmoke.densesmoke_a01'
THICK = 'rageeffects.thicksmoke.thicksmoke_a01'
WINDY_FIRE = 'rageeffects.windyfire.windyfire_a01'
OLD_FIRE = 'rageeffects.oldfire.oldfire_a01'
MUSH = 'rageeffects.mushboom.mush_a01'
WINDY_SMOKE = 'rageeffects.windysmoke.windysmoke_a01'

# RageEffects.FireyExample defaultproperties (ParticleEffect itself has none)
FIREY_EXAMPLE = [
    dict(Texture=WINDY_FIRE, RandomLocation='(X=25,Y=25,Z=5)', Frequency='10', Period='4', Interval='6', Alpha='255',
         Lifetime='1.1', DrawSize='0.9', DrawType='RSDT_Additive', bAnimOnce='True', bFadeAlpha='True', RotationRate='FULL'),
    dict(Texture=DENSE, SetLocation='(Z=44)', RandomLocation='(X=30,Y=30,Z=50)', ConstVelocity='(Z=150)', Frequency='5',
         Period='5', Interval='5', Alpha='255', Lifetime='1.5', DrawSize='0.7', DrawType='RSDT_Subtractive', bAnimOnce='True',
         bFadeAlpha='True', RotationRate='FULL'),
    dict(Texture=THICK, SetLocation='(Z=44)', RandomLocation='(X=30,Y=30,Z=50)', ConstVelocity='(Z=150)', Frequency='5',
         Period='5', Interval='5', PhaseShift='-0.1', Alpha='255', Lifetime='1.5', DrawSize='0.7', DrawType='RSDT_Subtractive',
         bAnimOnce='True', bFadeAlpha='True', RotationRate='FULL'),
    dict(Texture=DENSE, SetLocation='(Z=44)', RandomLocation='(X=30,Y=30,Z=50)', ConstVelocity='(Z=150)', Frequency='1',
         Period='4.25', Interval='5.75', PhaseShift='-1.25', Alpha='255', Lifetime='1.5', DrawSize='0.7', DrawType='RSDT_Additive',
         bAnimOnce='True', bFadeAlpha='True', RotationRate='FULL'),
    dict(Texture=THICK, SetLocation='(Z=44)', RandomLocation='(X=30,Y=30,Z=50)', ConstVelocity='(Z=150)', Frequency='1',
         Period='4.25', Interval='5.75', PhaseShift='-0.75', Alpha='255', Lifetime='1.5', DrawSize='0.7', DrawType='RSDT_Additive',
         bAnimOnce='True', bFadeAlpha='True', RotationRate='FULL'),
]
PARTICLE_EFFECT_CLASSES = {'ParticleEffect': [], 'FireyExample': FIREY_EXAMPLE}


def _merge(default: dict, override: str | None) -> dict:
    """T3D lists a struct's fields that differ from the class default (nested vectors too)."""
    out = dict(default)
    if override:
        for k, v in t3d.parse_struct(override).items():
            if v.startswith('(') and k in out and out[k].startswith('('):
                inner = t3d.parse_struct(out[k])
                inner.update(t3d.parse_struct(v))
                v = '(' + ','.join(f'{a}={b}' for a, b in inner.items()) + ')'
            out[k] = v
    return out


def particle_emitters(cls: str, props: dict) -> list[dict]:
    """ParticleEffect.Particles[8] of a placed actor -> emitter descriptions (see EMITTER)."""
    defaults = PARTICLE_EFFECT_CLASSES[cls]
    out = []
    for i in range(8):
        pe = _merge(defaults[i] if i < len(defaults) else {}, props.get(f'Particles({i})'))
        tex = t3d.parse_object_ref(pe.get('Texture'))
        freq = t3d.parse_float(pe.get('Frequency'))
        if not tex or freq <= 0:
            continue   # ParticleEffect.Tick skips these
        period, interval = t3d.parse_float(pe.get('Period')), t3d.parse_float(pe.get('Interval'))
        if period == 0 and interval == 0:
            period = 1000.0   # "avoid infinite loops": always on
        rot = pe.get('RotationRate', 'EIGTH')
        out.append(dict(
            tex=tex.lower(), draw=DRAW.get(pe.get('DrawType', 'RSDT_Normal'), 'alpha'),
            alpha=int(t3d.parse_float(pe.get('Alpha'))) / 255.0,
            size=[t3d.parse_float(pe.get('DrawSize'))] * 2, life=t3d.parse_float(pe.get('Lifetime')),
            rate=freq, period=period, interval=interval, phase=t3d.parse_float(pe.get('PhaseShift')), loop=True,
            loc=list(t3d.parse_vector(pe.get('SetLocation'))), loc_rand=list(t3d.parse_vector(pe.get('RandomLocation'))),
            vel=list(t3d.parse_vector(pe.get('ConstVelocity'))), vel_rand=list(t3d.parse_vector(pe.get('RandomVelocity'))),
            anim_once=t3d.parse_bool(pe.get('bAnimOnce')), fade=t3d.parse_bool(pe.get('bFadeAlpha')),
            gravity=t3d.parse_bool(pe.get('bGravity')), collide=t3d.parse_bool(pe.get('bCollide')),
            kill_on_collide=t3d.parse_bool(pe.get('bDestroyOnCollide')),
            stretch=STRETCH.get(pe.get('Scale', 'SLBV_None'), 0) if t3d.parse_bool(pe.get('bFaceCamera')) else 0,
            rotate=ROTRATE.get(rot, 1 / 8) if t3d.parse_bool(pe.get('bRotate')) else 0.0))
    return out


def _smoke_burst(n, alpha_sub, alpha_add):
    """Explosion / VehicleExplosion smoke: n DenseSmoke puffs 90 up, 1 in 10 additive."""
    common = dict(tex=DENSE, size=[2.0, 3.0], life=1.5, loc=[0, 0, 90], loc_rand=[120, 120, 40], vel=[0, 0, 150], anim_once=True)
    return [dict(common, draw='sub', alpha=alpha_sub, burst=round(n * 0.9)),
            dict(common, draw='add', alpha=alpha_add, burst=max(1, round(n * 0.1)))]


def _fire_burst(mush):
    common = dict(draw='add', alpha=1.0, life=1.5, anim_once=True)
    return [dict(common, tex=MUSH, size=[1.8, 1.8], burst=mush, loc_rand=[100, 100, 100], vel_radial=200),
            dict(common, tex=WINDY_FIRE, size=[1.5, 2.1], burst=5, loc_rand=[120, 120, 70]),
            dict(common, tex=OLD_FIRE, size=[1.5, 2.0], burst=5, loc_rand=[120, 120, 70])]


def _burning():
    """ExplosionFire: every 4th tick (15 a second at 60 fps) a puff of smoke and a flame while
    ScaleGlow (1, -0.2 a second) is over 0.5, then flames ever rarer; gone at 0.1 (4.5 s)."""
    smoke = dict(loc=[0, 0, 44], loc_rand=[30, 30, 50], vel=[0, 0, 150], size=[0.6, 0.8], life=1.5, anim_once=True,
                 fade=True, alpha=1.0)
    # smoke: Dense or Thick, 1 in 10 additive; flames: every spawn, then every 4t - 10 spawns
    flame_keys = [[0, 15], [2.5, 15]] + [[t, 15 / max(1.0, 4 * t - 10)] for t in (3.0, 3.5, 4.0, 4.4)] + [[4.5, 0]]
    return [dict(smoke, tex=DENSE, draw='sub', rate_keys=[[0, 6.75], [4.5, 0]]),
            dict(smoke, tex=THICK, draw='sub', rate_keys=[[0, 6.75], [4.5, 0]]),
            dict(smoke, tex=DENSE, draw='add', rate_keys=[[0, 1.5], [4.5, 0]]),
            dict(tex=WINDY_FIRE, draw='add', alpha=1.0, size=[0.8, 1.0], life=1.1, anim_once=True,
                 loc_rand=[25, 25, 5], rate_keys=flame_keys)]


# Scripted RageSprites, at DrawScale 1 (the game scales the effect by the actor's DrawScale)
SYSTEMS = {
    # RageEffect.AddExplosion (grenades 0.4, trip bombs 0.8, rockets 1.2): RageEffects.Explosion
    'Explosion': _smoke_burst(10, 0.8, 0.2) + _fire_burst(15),
    # EffectsFix.VehExplosion -> RageEffects.VehicleExplosion (+ 2-4 ExplosionFires on the ground)
    'VehicleExplosion': _smoke_burst(10, 0.8, 0.2) + _fire_burst(25),
    'ExplosionFire': _burning(),
    # Vehicle.GenerateSmokeParticle: damage increases both smoke frequency and opacity.
    'VehicleDamageSmoke': [dict(tex=DENSE, draw='alpha', alpha=0.5, alpha_param='SmokeAlpha',
                                size=[0.7, 1.1], life=2.0, anim_once=True, fade=True,
                                loc_rand=[12, 12, 8], vel=[0, 0, 110], vel_rand=[15, 15, 20],
                                rate=2, rate_param='SmokeRate', loop=True, scale_spawn_rate=False)],
    # Rocket.Timer: old-fire sprites and wispy smoke left along the flight path.
    # Emission is bounded in time rather than spawning actors at every flight step.
    'RocketTrail': [dict(tex=OLD_FIRE, draw='add', alpha=1.0, size=[0.3, 0.5], life=0.35,
                        anim_once=True, fade=True, loc=[-12, 0, 0], loc_rand=[4, 4, 4],
                        vel=[-80, 0, 0], rate=60, loop=True, rotate=0.25, scale_spawn_rate=False),
                    dict(tex='rageeffects.wispysmoke.wispysmoke_a01', draw='add', alpha=0.5,
                         size=[0.5, 0.7], life=0.9, anim_once=True, fade=True,
                         loc=[-12, 0, 0], vel=[0, 0, 15], rate=18, loop=True, scale_spawn_rate=False)],
    # EffectsFix.SmokePuffVeh: a tyre blown out; SmokePuff: one puff
    'SmokePuffVeh': [dict(tex=DENSE, draw='sub', alpha=1.0, size=[0.6, 0.6], life=0.8, anim_once=True, burst=8, loc_rand=[20, 20, 20])],
    'SmokePuff': [dict(tex=DENSE, draw='sub', alpha=1.0, size=[0.6, 0.6], life=0.8, anim_once=True, burst=1)],
    # RageBubble (RPlayer.PlayHit 'Drowned', PlayDeathHit under water): a few bubbles rising
    'Bubbles': [dict(tex='rageeffects.tex_bubble1', draw='alpha', alpha=0.9, size=[0.06, 0.06], life=3.0, burst=3,
                     loc_rand=[15, 15, 10], vel=[0, 0, 90], vel_rand=[15, 15, 30])],
    # mineGenerator.mineExplosionEffect (mf-Pobojowisko_VerII's minefield): a fountain of dirt
    # (dense smoke, falling back) and the mod's own blast flipbook
    'MineExplosion': [dict(tex=DENSE, draw='sub', alpha=1.0, size=[0.15, 0.15], life=2.0, gravity=True, burst=120,
                           loc_rand=[50, 50, 20], vel=[0, 0, 600], vel_rand=[500, 500, 500]),
                      dict(tex='minegenerator.tripmines_a00', draw='add', alpha=0.4, size=[4.0, 4.0], life=1.4, gravity=True,
                           fade=True, burst=20, loc_rand=[0, 0, 50], vel=[0, 0, 600], vel_rand=[150, 150, 0])],
    # mineGenerator.splashEffect: the river's splashes (splashMaker)
    'Splash': [dict(tex=DENSE, draw='add', alpha=0.6, size=[0.05, 0.05], life=0.5, fade=True, burst=20,
                    loc_rand=[10, 10, 0], vel=[0, 0, 20], vel_rand=[0, 0, 200]),
               dict(tex=WINDY_SMOKE, draw='add', alpha=0.2, size=[0.8, 0.8], life=1.4, gravity=True, fade=True, burst=20,
                    loc=[0, 0, 30], vel=[0, 0, 150], vel_rand=[200, 200, 0])],
    # M18 smokeGrenEffect (servers' weapon mod): dense smoke, 15 a second for smokeDuration 15 s,
    # rising 60 uu/s, drifting 100 uu/s, 8 s each, size 2 (MF drew it additive: a bright fog; a
    # translucent one here, so it actually screens)
    # (the mod drew DenseSmoke additive, a glowing orange cloud; subtractive like MF's own
    # explosion smoke reads as smoke and actually screens)
    'SmokeGrenade': [dict(tex=DENSE, draw='sub', alpha=1.0, size=[2.0, 2.0], life=8.0, anim_once=True, fade=True,
                          vel=[0, 0, 60], vel_rand=[100, 100, 0], rate_keys=[[0, 15], [15, 15], [15.05, 0]]),
                     dict(tex=DENSE, draw='sub', alpha=1.0, size=[0.4, 0.4], life=1.0, anim_once=True, fade=True,
                          vel=[0, 0, 20], vel_rand=[0, 0, 20], rate_keys=[[0, 15], [15, 15], [15.05, 0]])],
    # RageEffect.AddWallHit: a puff of windy smoke, 0-5 specks of smoke flying off the wall (+X)
    'WallHit': [dict(tex=WINDY_SMOKE, draw='add', alpha=1.0, size=[0.2, 0.2], life=0.5, anim_once=True, fade=True, burst=1,
                     loc=[10, 0, 0], vel=[0, 0, 100]),
                dict(tex=THICK, draw='add', alpha=1.0, size=[0.05, 0.05], life=0.5, anim_once=True, fade=True, burst=3,
                     loc=[10, 0, 0], vel=[100, 0, 0], vel_rand=[20, 20, 20])],
    # RageEffect.AddBlood: a spurt (AlphaTexture, gravity)
    'Blood': [dict(tex='rageeffects.bloodspang.spang_a01', draw='alpha', alpha=1.0, size=[0.3, 0.3], life=0.4, anim_once=True,
                   gravity=True, burst=1, vel=[0, 0, 100])],
    'BloodBlack': [dict(tex='rageeffects.bloodspang.spangblack_a01', draw='alpha', alpha=1.0, size=[0.3, 0.3], life=0.4,
                        anim_once=True, gravity=True, burst=1, vel=[0, 0, 100])],
}


# --- flipbooks -------------------------------------------------------------------------------

def _next_pow2(n: int) -> int:
    return 1 << max(0, (n - 1).bit_length())


def flipbook(cfg: Config, key: str, cache: dict) -> dict | None:
    """A texture and its AnimNext chain -> one atlas PNG (RGBA); manifest entry. Rage's effect
    palettes carry alpha (DenseSmoke, WindyFire: white under see-through texels), which every
    draw type weighs the colour by (the particle materials do too)."""
    alpha = True
    package = key.split('.')[0]
    if package not in cache:
        try:
            path = _package_path(cfg, package)
        except FileNotFoundError as e:
            print(f'  ! {e}')
            cache[package] = None
            return None
        pkg = Package(str(path))
        cache[package] = (pkg, texture_index(cfg, path.stem))
    if cache[package] is None:
        return None
    pkg, index = cache[package]
    info = index.get(key)
    if info is None:
        print(f'  ! texture {key} not found')
        return None
    frames, seen = [], set()
    while info is not None and info['export'] not in seen:
        seen.add(info['export'])
        img = decode_p8(pkg, info, alpha)
        if img is None:
            print(f'  ! can\'t decode {info["key"]}')
            break
        frames.append(img)
        nxt = pkg.read_object(pkg.exports[info['export'] - 1]).props.get('AnimNext')
        info = None
        if nxt:
            info = index.get(f'{package}.{nxt}'.lower())   # the reader gives object refs as paths
    if not frames:
        return None
    w, h = frames[0].size
    cols = _next_pow2(math.ceil(math.sqrt(len(frames))))
    rows = _next_pow2(math.ceil(len(frames) / cols))
    atlas = Image.new('RGBA' if alpha else 'RGB', (w * cols, h * rows), (0, 0, 0, 0) if alpha else (0, 0, 0))
    for i, f in enumerate(frames):
        atlas.paste(f.resize((w, h)).convert(atlas.mode), ((i % cols) * w, (i // cols) * h))
    first = index[key]
    rel = '/'.join(first['key'].split('.'))
    dst = cfg.out_dir / 'Effects' / f'{rel}.png'
    dst.parent.mkdir(parents=True, exist_ok=True)
    atlas.save(dst, optimize=True)
    return {'file': str(dst.relative_to(cfg.out_dir)), 'key': first['key'], 'frames': len(frames), 'cols': cols, 'rows': rows,
            'usize': first['usize'], 'vsize': first['vsize'], 'alpha': alpha}


def convert(cfg: Config) -> str:
    """Flipbooks for the built-in effects and every map's particle_effect records."""
    emitters = [e for es in SYSTEMS.values() for e in es]
    for lj in (cfg.out_dir / 'Maps').glob('*/level.json'):
        for a in json.loads(lj.read_text()).get('spawns', []):
            if a.get('type') == 'particle_effect':
                emitters += a['emitters']
    keys = {e['tex'] for e in emitters}
    cache, books = {}, {}
    for key in sorted(keys):
        if (fb := flipbook(cfg, key, cache)) is not None:
            books[key] = fb
            print(f'  {fb["key"]}: {fb["frames"]} frame(s)')
    path = cfg.out_dir / 'Effects' / 'effects.json'
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps({'flipbooks': books, 'systems': SYSTEMS}, indent=1, sort_keys=True))
    return f'{path} ({len(books)} flipbooks, {len(SYSTEMS)} effects)'
