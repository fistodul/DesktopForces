"""Map edits applied at conversion: retextured world faces, textures of ours, MoBall's zones, UE1 events, sprites."""
from pathlib import Path
from types import SimpleNamespace
import sys
import tempfile
import unittest

from PIL import Image

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'mfport'))
from mflib import actors, mapedits, mfmesh, textures  # noqa: E402
from mflib.bsp import PF_MASKED  # noqa: E402
from mflib.level import LevelConverter  # noqa: E402


def face(texture, normal_z):
    return SimpleNamespace(texture=texture, normal=(0.0, 0.0, normal_z))


class RetextureTests(unittest.TestCase):
    RULES = [{'from': 'rage_snow.snow', 'to': 'DesktopForces.Paving.snow_bricks', 'faces': 'floor', 'below': -300.0}]

    def test_flat_ground_gets_the_new_texture(self):
        self.assertEqual(LevelConverter._retextured(face('rage_snow.Snow', 1.0), (0, 0, -260), self.RULES),
                         'DesktopForces.Paving.snow_bricks')

    def test_slopes_roofs_and_other_textures_keep_theirs(self):
        for poly, centre in ((face('rage_snow.snow', 0.8), (0, 0, -260)),      # a snow drift
                             (face('rage_snow.snow', 1.0), (0, 0, 500)),       # a roof (1125 cm up)
                             (face('rage_urban.road', 1.0), (0, 0, -260))):
            self.assertEqual(LevelConverter._retextured(poly, centre, self.RULES), poly.texture)

    def test_the_map_table_parses(self):
        rules = mapedits.retexture('MF-SARDINE_CITY')   # names match without case
        self.assertTrue(rules and all(r['from'] == r['from'].lower() and r['to'] for r in rules))
        self.assertEqual(mapedits.retexture('no-such-map'), [])


class DeathPitTests(unittest.TestCase):
    def test_a_pit_becomes_a_zone_that_kills(self):
        edits = {'death_pits': [{'min': [0, 0, -100], 'max': [400, 200, 300]}]}
        real = mapedits._edits_for
        mapedits._edits_for = lambda name: edits
        try:
            (pit,) = mapedits.death_pits('any')
        finally:
            mapedits._edits_for = real
        self.assertEqual((pit['type'], pit['box_min'], pit['box_max'], pit['location']),
                         ('zone_volume', [0.0, 0.0, -100.0], [400.0, 200.0, 300.0], [200.0, 100.0, 100.0]))
        self.assertGreaterEqual(pit['damage_per_sec'] * 0.4, 10000)   # feet only: still kills anyone
        self.assertEqual(mapedits.death_pits('no-such-map'), [])


class DeathPitScanTests(unittest.TestCase):
    def setUp(self):
        sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'mfport' / 'debug'))
        import deathpits
        self.dp = deathpits

    def test_only_pits_every_way_into_which_killed(self):
        pit = {'min_fall': 2300, 'max_fall': 2300, 'hurts': False, 'area': 23.5, 'clashes': 0, 'boxes': [[0, 0, 0, 1, 1, 1]]}
        self.assertTrue(self.dp.is_death_pit(pit))
        for change in ({'min_fall': 1220}, {'hurts': True}, {'area': 0.3}, {'boxes': []}, {'water': True}, {'clashes': 3}, {'boxes': [[0, 0, 0, 1, 1, 1]] * 51}):
            self.assertFalse(self.dp.is_death_pit(dict(pit, **change)))
        self.assertIn('max = [1.0, 1.0, 1.0]', self.dp.toml('m', self.dp.death_pits([pit])))


class LightmapChartTests(unittest.TestCase):
    def test_a_named_mesh_carries_its_chart_source_to_the_importer(self):
        self.assertEqual(mapedits.lightmap_uvs('mf-aquarius'), {'staticmesh97': 'texture'})
        self.assertEqual(mapedits.lightmap_uvs('no-such-map'), {})
        mb = mfmesh.MeshBuilder('Column', lightmap_uvs='texture')
        mb.add_triangle('a.b|opaque', ((0, 0, 0), (0, 0, 1), (0, 0)), ((1, 0, 0), (0, 0, 1), (1, 0)), ((0, 1, 0), (0, 0, 1), (0, 1)))
        with tempfile.TemporaryDirectory() as tmp:
            self.assertEqual(mfmesh.read(mb.write(Path(tmp))).lightmap_uvs, 'texture')
            plain = mfmesh.MeshBuilder('Plain')
            plain.add_triangle('a.b|opaque', ((0, 0, 0), (0, 0, 1), (0, 0)), ((1, 0, 0), (0, 0, 1), (1, 0)), ((0, 1, 0), (0, 0, 1), (0, 1)))
            self.assertNotIn('lightmap_uvs', plain.write(Path(tmp)).read_text())   # unnamed meshes' files stay as they were


class OwnTextureTests(unittest.TestCase):
    def test_a_generated_picture_becomes_a_manifest_entry(self):
        with tempfile.TemporaryDirectory() as tmp:
            tmp = Path(tmp)
            src = tmp / 'generated' / textures.OWN_PACKAGE / 'Paving' / 'snow_bricks.png'
            src.parent.mkdir(parents=True)
            Image.new('RGB', (512, 512), (220, 224, 230)).save(src)
            cfg = SimpleNamespace(out_dir=tmp / 'out', max_texture_size=512, detail_texture_size=1024)
            roots = (textures.OVERRIDE_DIR, textures.LICENSED_DIR, textures.GENERATED_DIR)
            textures.OVERRIDE_DIR, textures.LICENSED_DIR, textures.GENERATED_DIR = tmp / 'o', tmp / 'l', tmp / 'generated'
            try:
                usage = {'desktopforces.paving.snow_bricks': {'opaque'}, 'desktopforces.paving.missing': {'opaque'},
                         'rage_snow.snow': {'opaque'}}
                manifest = textures.convert_own(cfg, usage)
            finally:
                textures.OVERRIDE_DIR, textures.LICENSED_DIR, textures.GENERATED_DIR = roots
            self.assertEqual(list(manifest), ['DesktopForces.Paving.snow_bricks'])
            entry = manifest['DesktopForces.Paving.snow_bricks']
            self.assertEqual((entry['usize'], entry['vsize'], entry['override']), (256, 256, False))
            self.assertTrue((cfg.out_dir / entry['file']).exists())


class _Tree:
    PARENTS = {'MoBallWaterGoalZone': 'MoBallGoalZone', 'MoBallGoalZone': 'ZoneInfo', 'MoBallDestructZone': 'ZoneInfo',
               'PlayerPawn': 'Pawn', 'RagePlayer': 'PlayerPawn'}

    def is_a(self, cls, base):
        while cls and cls != base:
            cls = self.PARENTS.get(cls)
        return cls == base

    def is_water(self, a):
        return False


class MoBallZoneTests(unittest.TestCase):
    def zone_boxes(self, cls, **props):
        bounds = {3: ((0.0, 0.0, 0.0), (900.0, 2000.0, 500.0))}
        mapper = actors.ActorMapper(bounds, {}, [None, None, None, 'Zone0'], _Tree())
        actor = {'class': cls, 'name': 'Zone0', 'location': [1.0, 2.0, 3.0], 'zone': 3,
                 'rotation': {'pitch': 0.0, 'yaw': 0.0, 'roll': 0.0}, 'props': props}
        return [r for r in mapper.map(actor) if r['type'] == 'zone_box']

    def test_goal_zones_carry_their_team(self):
        self.assertEqual([r['tag'] for r in self.zone_boxes('MoBallGoalZone')], ['MoBallGoal0'])
        self.assertEqual([r['tag'] for r in self.zone_boxes('MoBallWaterGoalZone', Team='1')], ['MoBallGoal1'])

    def test_destruct_zone(self):
        box, = self.zone_boxes('MoBallDestructZone')
        self.assertEqual((box['tag'], box['box_max']), ('MoBallDestruct', (900.0, 2000.0, 500.0)))


class TerminalVelocityTests(unittest.TestCase):
    def volumes(self, **props):
        mapper = actors.ActorMapper({3: ((0.0, 0.0, 0.0), (900.0, 2000.0, 500.0))}, {}, [None, None, None, 'Zone0'], _Tree())
        actor = {'class': 'ZoneInfo', 'name': 'Zone0', 'location': [1.0, 2.0, 3.0], 'zone': 3,
                 'rotation': {'pitch': 0.0, 'yaw': 0.0, 'roll': 0.0}, 'props': props}
        return [r for r in mapper.map(actor) if r['type'] == 'zone_volume']

    def test_a_zone_of_its_own_fall_speed_gets_a_volume(self):
        vol, = self.volumes(ZoneTerminalVelocity='830.000000')   # BR-thebigisland's island
        self.assertAlmostEqual(vol['terminal_velocity'], 830 * 2.25)

    def test_the_default_speed_adds_nothing(self):
        self.assertEqual(self.volumes(ZoneTerminalVelocity='2500.000000'), [])
        self.assertEqual(self.volumes(), [])


class ZoneFogTests(unittest.TestCase):
    """UE1 distance fog per zone, sampled from the BSP onto a grid."""
    def actors(self):
        rot = {'pitch': 0.0, 'yaw': 0.0, 'roll': 0.0}
        return [{'class': 'LevelInfo', 'name': 'LevelInfo0', 'location': [0, 0, 0], 'zone': 0, 'rotation': rot, 'props': {}},
                {'class': 'ZoneInfo', 'name': 'Fogged', 'location': [0, 0, 0], 'zone': 1, 'rotation': rot,
                 'props': {'bDistanceFog': 'True', 'DistanceFogColor': '(R=56,G=52,B=41)', 'DistanceFogStart': '10.000000', 'DistanceFogEnd': '5000.000000'}},
                {'class': 'ZoneInfo', 'name': 'Clear', 'location': [0, 0, 0], 'zone': 2, 'rotation': rot, 'props': {}}]

    def grid(self, zone_at):
        bounds = {0: ((0.0, 0.0, 0.0), (900.0, 300.0, 300.0)), 1: ((0.0, 0.0, 0.0), (600.0, 300.0, 300.0)),
                  2: ((600.0, 0.0, 0.0), (900.0, 300.0, 300.0))}
        tree = _Tree()
        tree.PARENTS = dict(_Tree.PARENTS, ZoneInfo='Info', LevelInfo='ZoneInfo')
        tree.default = lambda cls, prop, fallback=None: fallback
        return actors.zone_fog(self.actors(), bounds, [None, 'Fogged', 'Clear'], tree, zone_at)

    def cells(self, z):
        out, prev = [], 0
        for end, value in zip(z['run_ends'], z['run_values']):
            out += [value] * (end - prev)
            prev = end
        return out

    def test_each_cell_gets_its_zones_fog(self):
        # x in uu: cells 0-1 zone 1 (fog), cell 2 zone 2 (clear)
        z = self.grid(lambda p: 1 if p[0] * 2.25 < 600 else 2)
        self.assertEqual((z['size'], z['fogs']), ([3, 1, 1], [{'color': [56, 52, 41], 'start': 22.5, 'end': 11250.0}]))
        self.assertEqual(self.cells(z), [1, 1, 0])

    def test_zone_0_takes_its_neighbours_fog(self):
        # the middle cell is zone 0 (a rail bridge no zone claims)
        z = self.grid(lambda p: 0 if 300 <= p[0] * 2.25 < 600 else 1)
        self.assertEqual(self.cells(z), [1, 1, 1])

    def test_no_fog_no_grid(self):
        a = self.actors()
        del a[1]['props']['bDistanceFog']
        tree = _Tree()
        tree.default = lambda cls, prop, fallback=None: fallback
        self.assertIsNone(actors.zone_fog(a, {1: ((0.0, 0.0, 0.0), (300.0, 300.0, 300.0))}, [None, 'Fogged', 'Clear'], tree, lambda p: 1))


class EventTests(unittest.TestCase):
    def records(self, cls, **props):
        mapper = actors.ActorMapper({}, {}, [], _Tree())
        actor = {'class': cls, 'name': cls + '0', 'location': [0.0, 0.0, 0.0], 'zone': 0,
                 'rotation': {'pitch': 0.0, 'yaw': 0.0, 'roll': 0.0}, 'props': props}
        return [r for r in mapper.map(actor) if r['type'] != 'legacy']

    def test_touch_trigger(self):
        rec, = self.records('Trigger', Tag='Trigger', Event='"Pit"', CollisionRadius='5000.000000', CollisionHeight='100.000000',
                            TriggerType='TT_ClassProximity', ClassProximityType="Class'Engine.PlayerPawn'", bTriggerOnceOnly='True')
        self.assertEqual((rec['type'], rec['event'], rec['who'], rec['once'], rec['active']), ('trigger', 'Pit', 'human', True, True))
        self.assertEqual((rec['radius'], rec['height']), (11250.0, 225.0))

    def test_trigger_without_event_or_blocking_stays_as_it_was(self):
        self.assertEqual(self.records('Trigger', Tag='Trigger'), [])
        wall, = self.records('Trigger', Tag='Trigger', Event='x', bBlockPlayers='True')
        self.assertEqual(wall['type'], 'blocker')

    def test_special_event_states(self):
        hurt, = self.records('SpecialEvent', Tag='Pit', InitialState='DamageInstigator', Damage='4000')
        self.assertEqual((hurt['type'], hurt['kind'], hurt['tag'], hurt['damage']), ('event', 'damage', 'Pit', 4000.0))
        bell, = self.records('SpecialEvent', Tag='Bell', InitialState='PlaySoundEffect', Sound="Sound'School.bell'")
        self.assertEqual((bell['kind'], bell['sound']), ('sound', 'School.bell'))
        self.assertEqual(self.records('SpecialEvent', Tag='Quiet', InitialState='PlaySoundEffect'), [])        # no sound set
        self.assertEqual(self.records('SpecialEvent', Tag='Hum', InitialState='PlayAmbientSoundEffect'), [])   # not ported
        said, = self.records('SpecialEvent', Tag='Hello', Message='"Engine doors open"', bBroadcast='True')
        self.assertEqual((said['kind'], said['message'], said['broadcast']), ('message', 'Engine doors open', True))

    def test_dispatcher_and_counter(self):
        chain, = self.records('Dispatcher', Tag='Go', **{'OutEvents(0)': 'A', 'OutEvents(2)': 'C', 'OutDelays(2)': '1.500000'})
        self.assertEqual((chain['kind'], chain['out']), ('dispatcher', [{'event': 'A', 'delay': 0.0}, {'event': 'C', 'delay': 1.5}]))
        count, = self.records('Counter', Tag='Lever', Event='Door', NumToCount='3', CompleteMessage='"Open"')
        self.assertEqual((count['kind'], count['count'], count['event'], count['message']), ('counter', 3, 'Door', 'Open'))

    def test_loose_ends_are_dropped(self):
        def rec(what, tag, event, **more):
            return dict({'type': what, 'name': tag, 'tag': tag, 'event': event, 'kind': 'damage'}, **more)
        spawns = [rec('trigger', 'T1', 'Pit'), rec('event', 'Pit', ''), rec('trigger', 'T2', 'Door'),      # Door: a mover's
                  rec('event', 'Apple', ''),                                                              # fired by a mover
                  rec('trigger', 'T3', 'Go'), rec('event', 'Go', '', kind='dispatcher', out=[{'event': 'Bell', 'delay': 1.0}]),
                  rec('event', 'Bell', ''),
                  rec('trigger', 'T4', 'Lamps'), rec('event', 'Lamps', 'T4', kind='dispatcher', out=[{'event': 'Lamp1', 'delay': 0.0}]),
                  {'type': 'legacy', 'name': 'x'}]
        self.assertEqual([r['name'] for r in actors.prune_events(spawns)], ['T1', 'Pit', 'T3', 'Go', 'Bell', 'x'])

    def test_a_trigger_left_alone_takes_its_event_with_it(self):
        spawns = [dict(type='trigger', name='T', tag='T', event='Gone'), dict(type='event', kind='damage', name='E', tag='E', event='T')]
        self.assertEqual(actors.prune_events(spawns), [])   # E toggles T, T fires at nothing

    def test_movers_answer_and_fire(self):
        spawns = [dict(type='trigger', name='T', tag='T', event='Door'),
                  dict(type='mover', name='M', tag='Door', event='Glass', initial_state='TriggerOpenTimed'),
                  dict(type='event', kind='explode', name='G', tag='Glass', event=''),
                  dict(type='trigger', name='T2', tag='T2', event='Lift'),
                  dict(type='mover', name='L', tag='Lift', event='', initial_state='BumpOpenTimed')]   # bumped only
        self.assertEqual([r['name'] for r in actors.prune_events(spawns)], ['T', 'M', 'G', 'L'])

    def test_breaking_glass(self):
        rec, = self.records('BreakingGlass', Tag='glass1', BreakingSound="Sound'levelSFX.GlassSmash'")
        self.assertEqual((rec['kind'], rec['tag'], rec['sound']), ('explode', 'glass1', 'levelSFX.GlassSmash'))

    def test_any_actor_plays_its_ambient_sound(self):
        mapper = actors.ActorMapper({}, {}, [], _Tree())
        hum = {'class': 'OmniSound', 'name': 'OmniSound0', 'location': [1.0, 2.0, 3.0], 'zone': 0,
               'rotation': {'pitch': 0.0, 'yaw': 0.0, 'roll': 0.0}, 'props': {'AmbientSound': "Sound'levelSFX.Carpassby'", 'SoundRadius': '16'}}
        sound, = [r for r in mapper.map(hum) if r['type'] == 'ambient_sound']
        self.assertEqual((sound['sound'], sound['radius'], sound['volume']), ('levelSFX.Carpassby', 25.0 * 17 * 2.25, 128 / 255))
        quiet = dict(hum, props={})
        self.assertEqual([r for r in mapper.map(quiet) if r['type'] == 'ambient_sound'], [])


class _SpriteTree(_Tree):
    DEFAULTS = {('ScaledSprite', 'DrawType'): 'DT_Sprite', ('ScaledSprite', 'bHidden'): 'False',
                ('Light', 'DrawType'): 'DT_Sprite', ('Light', 'bHidden'): 'True', ('Light', 'Texture'): "Texture'Engine.S_Light'"}

    def is_a(self, cls, base):
        return base == 'Decoration' and cls == 'ScaledSprite'

    def default(self, cls, prop, fallback=None):
        return self.DEFAULTS.get((cls, prop), fallback)


class SpriteTests(unittest.TestCase):
    def convert(self, *placed):
        conv = LevelConverter.__new__(LevelConverter)
        conv.usage, conv.texture_sizes = {}, {'patric.grass00': (64, 128)}
        conv.texture_polyflags = {'patric.grass00': PF_MASKED}
        found = []
        with tempfile.TemporaryDirectory() as tmp:
            conv.out = Path(tmp)
            rec = conv.convert_sprites([{'class': cls, 'name': cls, 'location': [100.0, 200.0, 300.0], 'props': props}
                                        for cls, props in placed], _SpriteTree())
            if rec:
                found = mfmesh.read(conv.out / (rec['mesh'] + '.mfmesh.json'))
        return rec, found

    def test_a_scaled_sprite_becomes_a_camera_facing_quad(self):
        rec, mesh = self.convert(('ScaledSprite', {'Texture': "Texture'patric.Grass00'", 'DrawScale': '0.5'}))
        self.assertEqual((rec['type'], rec['count']), ('sprites', 1))
        self.assertTrue(mesh.has_colors)
        self.assertEqual(len(mesh.materials), 1)
        self.assertTrue(mesh.materials[0].startswith('patric.Grass00|') and mesh.materials[0].endswith('_sprite'))
        corners = {tuple(v[3][:2]) for t in mesh.tris[0] for v in t}
        self.assertEqual(corners, {(0.0, 0.0), (1.0, 0.0), (1.0, 1.0), (0.0, 1.0)})
        w, h = 64 * 0.5 * 2.25, 128 * 0.5 * 2.25
        for t in mesh.tris[0]:
            for pos, _, size, corner in t:
                self.assertEqual(tuple(round(s, 3) for s in size), (round(w, 3), round(h, 3)))   # the material's offset
                self.assertAlmostEqual(pos[2], 300.0 + (corner[1] - 0.5) * h, places=3)       # baked upright round it

    def test_hidden_actors_and_editor_icons_are_left_out(self):
        rec, _ = self.convert(('Light', {}), ('ScaledSprite', {'Texture': "Texture'patric.Grass00'", 'bHidden': 'True'}),
                              ('ScaledSprite', {'Texture': "Texture'patric.Grass00'", 'DrawType': 'DT_Mesh'}))
        self.assertIsNone(rec)


if __name__ == '__main__':
    unittest.main()
