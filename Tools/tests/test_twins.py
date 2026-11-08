"""Tools/mfport twins: polygon subtraction and the night/day look swap (layout_from)."""
from pathlib import Path
import sys
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'mfport'))
from mflib import twins  # noqa: E402


class _Tree:
    def is_a(self, cls, base):
        return cls == base or (base == 'ZoneInfo' and cls.endswith('Zone'))


def square(x0, y0, x1, y1):
    return [(x0, y0), (x1, y0), (x1, y1), (x0, y1)]


class SubtractTests(unittest.TestCase):
    def area(self, pieces):
        return sum(twins._area2(p) for p in pieces)

    def test_covered_polygon_leaves_nothing(self):
        self.assertEqual(twins.subtract(square(10, 10, 20, 20), square(0, 0, 100, 100)), [])

    def test_disjoint_polygon_is_kept_whole(self):
        self.assertAlmostEqual(self.area(twins.subtract(square(0, 0, 10, 10), square(50, 50, 60, 60))), 100.0)

    def test_overlap_is_cut_away(self):
        # a 100 x 100 square minus its right half: 50 x 100 left
        self.assertAlmostEqual(self.area(twins.subtract(square(0, 0, 100, 100), square(50, -10, 150, 110))), 5000.0)


class RelightTests(unittest.TestCase):
    def actor(self, cls, name, **props):
        return {'class': cls, 'name': name, 'location': [0.0, 0.0, 0.0], 'props': props}

    def test_layout_keeps_gameplay_and_takes_the_look(self):
        layout = [self.actor('LevelInfo', 'LevelInfo0', Title='noon', DistanceFogColor='blue'),
                  self.actor('PlayerStart', 'PlayerStart0'), self.actor('Light', 'Light0', LightHue='40'),
                  self.actor('CaptainProhibitedZone', 'CaptainProhibitedZone0', DistanceFogColor='blue', Tag='x')]
        look = [self.actor('LevelInfo', 'LevelInfo0', Title='night', DistanceFogColor='dark'),
                self.actor('Light', 'Light0', LightHue='160'), self.actor('Light', 'Light1')]
        out = twins.relight(layout, look, _Tree())
        by_name = {a['name']: a for a in out}
        self.assertIn('PlayerStart0', by_name)
        self.assertEqual(by_name['LevelInfo0']['props']['Title'], 'night')
        self.assertEqual([a['props'].get('LightHue') for a in out if a['class'] == 'Light'], ['160', None])
        zone = by_name['CaptainProhibitedZone0']['props']
        self.assertEqual((zone['DistanceFogColor'], zone['Tag']), ('dark', 'x'))


if __name__ == '__main__':
    unittest.main()
