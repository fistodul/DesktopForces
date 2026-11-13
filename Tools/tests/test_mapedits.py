"""Map edits applied at conversion: retextured world faces, textures of ours, MoBall's zones."""
from pathlib import Path
from types import SimpleNamespace
import sys
import tempfile
import unittest

from PIL import Image

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'mfport'))
from mflib import actors, mapedits, textures  # noqa: E402
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
    PARENTS = {'MoBallWaterGoalZone': 'MoBallGoalZone', 'MoBallGoalZone': 'ZoneInfo', 'MoBallDestructZone': 'ZoneInfo'}

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


if __name__ == '__main__':
    unittest.main()
