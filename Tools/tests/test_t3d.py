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

    def test_weak_normals_go_by_the_shape(self):
        # a closed box wound inwards, its normals all one way (as smoothing round the corners of
        # overlapping boxes leaves them): it must face out
        box = [tri(a, c, b, (0.6, 0.8, 0.0)) for a, b, c in cube_faces()]
        self.assertTrue(all(outward(t) > 0 for t in orient_triangles(box)))

    def test_a_room_seen_from_inside_stays_inside(self):
        # wound and smoothed inwards, as a room or tunnel: the normals agree, the shape isn't asked
        room = []
        for a, b, c in cube_faces():
            n = tuple(-x for x in face_normal(a, b, c))
            room.append(tri(a, c, b, n))
        self.assertTrue(all(outward(t) < 0 for t in orient_triangles(room)))


def face_normal(a, b, c):
    u = [b[i] - a[i] for i in range(3)]
    w = [c[i] - a[i] for i in range(3)]
    n = (u[1] * w[2] - u[2] * w[1], u[2] * w[0] - u[0] * w[2], u[0] * w[1] - u[1] * w[0])
    length = sum(x * x for x in n) ** 0.5
    return tuple(x / length for x in n)


def cube_faces():
    """A 2 x 2 x 2 box round the origin, wound outwards."""
    c = [(x, y, z) for x in (-1, 1) for y in (-1, 1) for z in (-1, 1)]
    quads = [(0, 1, 3, 2), (4, 6, 7, 5), (0, 4, 5, 1), (2, 3, 7, 6), (0, 2, 6, 4), (1, 5, 7, 3)]
    for q in quads:
        a, b, cc, d = (c[i] for i in q)
        yield a, b, cc
        yield a, cc, d


def outward(t):
    (a, b, c) = [v[0] for v in t.verts]
    n = face_normal(a, b, c)
    mid = [sum(p[i] for p in (a, b, c)) / 3 for i in range(3)]
    return sum(n[i] * mid[i] for i in range(3))


if __name__ == '__main__':
    unittest.main()
