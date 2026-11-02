"""Static mesh triangles must come out facing one way, whatever their smoothed normals say."""
from pathlib import Path
import sys
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from mfport.mflib.t3d import SMTriangle, orient_triangles

UP = (0.0, 0.0, 1.0)


def tri(a, b, c, normal=UP):
    return SMTriangle('t', 0, 0, [(a, normal, (0.0, 0.0)), (b, normal, (0.0, 0.0)), (c, normal, (0.0, 0.0))])


def facing(t):
    (a, b, c) = [v[0] for v in t.verts]
    return (b[0] - a[0]) * (c[1] - a[1]) - (b[1] - a[1]) * (c[0] - a[0])


class OrientTests(unittest.TestCase):
    def test_a_turned_neighbour_comes_back(self):
        quad = [tri((0, 0, 0), (1, 0, 0), (1, 1, 0)), tri((0, 0, 0), (0, 1, 0), (1, 1, 0))]
        self.assertTrue(all(facing(t) > 0 for t in orient_triangles(quad)))

    def test_a_smoothed_normal_cant_turn_one_triangle(self):
        away = (0.0, 0.2, -0.1)
        quad = [tri((0, 0, 0), (1, 0, 0), (1, 1, 0)), tri((0, 0, 0), (1, 1, 0), (0, 1, 0), away)]
        self.assertTrue(all(facing(t) > 0 for t in orient_triangles(quad)))

    def test_a_whole_piece_follows_its_normals(self):
        quad = [tri((0, 0, 0), (1, 1, 0), (1, 0, 0)), tri((0, 0, 0), (0, 1, 0), (1, 1, 0))]
        self.assertTrue(all(facing(t) > 0 for t in orient_triangles(quad)))


if __name__ == '__main__':
    unittest.main()
