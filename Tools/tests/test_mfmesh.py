"""Tools/mfport mfmesh: faces stored twice are written once, unless translucent."""
from pathlib import Path
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'mfport'))
from mflib.mfmesh import MeshBuilder, read  # noqa: E402

A = ((0.0, 0.0, 0.0), (0.0, 0.0, 1.0), (0.0, 0.0))
B = ((100.0, 0.0, 0.0), (0.0, 0.0, 1.0), (1.0, 0.0))
C = ((0.0, 100.0, 0.0), (0.0, 0.0, 1.0), (0.0, 1.0))


def written(material):
    mb = MeshBuilder('m')
    mb.add_triangle(material, A, B, C)
    mb.add_triangle(material, B, C, A)  # the same face, from another corner
    mb.add_triangle(material, A, C, B)  # its back: kept
    with tempfile.TemporaryDirectory() as d:
        return read(mb.write(Path(d))).triangle_count


class DuplicateFaceTests(unittest.TestCase):
    def test_opaque_face_written_once(self):
        self.assertEqual(written('T.Wood|opaque'), 2)

    def test_masked_two_sided_face_written_once(self):
        self.assertEqual(written('T.Fence|masked_2s'), 2)

    def test_additive_copies_stay(self):
        self.assertEqual(written('T.Smoke|additive_2s'), 3)


if __name__ == '__main__':
    unittest.main()
