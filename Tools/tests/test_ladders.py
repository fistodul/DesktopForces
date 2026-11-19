"""LadderClimber defaults omitted from exported UE1 maps must survive conversion."""
from pathlib import Path
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'mfport'))
from mflib.actors import ActorMapper, ClassTree


class LadderDefaultsTests(unittest.TestCase):
    def record(self, tree=None, **props):
        actor = {'class': 'LadderClimber', 'name': 'Ladder0', 'location': [0, 0, 0],
                 'rotation': {'pitch': 0, 'yaw': 0, 'roll': 0}, 'props': props}
        return ActorMapper({}, {}, tree=tree).map(actor)[0]['props']

    def test_stock_ladder_defaults_without_exported_scripts(self):
        self.assertEqual(self.record(), {'Radius': '90.00', 'HalfHeight': '225.00', 'ClimbSpeed': '1'})

    def test_retail_map_overrides_remain_authoritative(self):
        self.assertEqual(self.record(CollisionRadius='32', CollisionHeight='152', ClimbSpeed='0.5'),
                         {'Radius': '72.00', 'HalfHeight': '342.00', 'ClimbSpeed': '0.5'})

    def test_inherited_defaults_come_from_available_scripts(self):
        with tempfile.TemporaryDirectory() as tmp:
            classes = Path(tmp) / 'Engine' / 'Classes'
            classes.mkdir(parents=True)
            (classes / 'Actor.uc').write_text('class Actor;\ndefaultproperties\n{\n CollisionRadius=22\n CollisionHeight=22\n}\n')
            (classes / 'LadderClimber.uc').write_text('class LadderClimber expands Actor;\ndefaultproperties\n{\n CollisionRadius=40\n CollisionHeight=100\n ClimbSpeed=1.0\n}\n')
            self.assertEqual(self.record(ClassTree(Path(tmp))),
                             {'Radius': '90.00', 'HalfHeight': '225.00', 'ClimbSpeed': '1.0'})


if __name__ == '__main__':
    unittest.main()
