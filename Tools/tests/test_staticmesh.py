"""Static meshes drawn with their sections' textures (mflib/staticmesh.py), not the T3D's."""
from pathlib import Path
from types import SimpleNamespace
import struct
import sys
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'mfport'))
from mflib import staticmesh  # noqa: E402
from mflib.staticmesh import Section  # noqa: E402


def compact(v: int) -> bytes:
    """UE1 compact index."""
    a = abs(v)
    out = [(0x80 if v < 0 else 0) | (a & 0x3F) | (0x40 if a >= 0x40 else 0)]
    a >>= 6
    while a:
        out.append((a & 0x7F) | (0x80 if a >= 0x80 else 0))
        a >>= 7
    return bytes(out)


class _Package:
    """Just what staticmesh.sections reads: one StaticMesh export, two texture imports."""

    def __init__(self, sections):
        native = bytes(25 + 16) + compact(3165) + compact(-53) + compact(len(sections))
        for ref, tris in sections:
            native += struct.pack('<I', 0) + compact(ref) + struct.pack('<I', 0) + struct.pack('<4H', 0, tris, 0, 0)
        self.data = native
        self.imports = [SimpleNamespace(class_name='Texture', name='park_ceiling_0'),
                        SimpleNamespace(class_name='Package', name='car_park')]

    def find(self, name, class_name=None):
        return SimpleNamespace(offset=0) if name == 'car01' else None

    def read_object(self, exp):
        return SimpleNamespace(props={}, native_offset=0)

    def full_path(self, ref):
        return 'car_park.park_ceiling_0' if ref == -1 else None


class SectionTests(unittest.TestCase):
    def test_a_section_gives_its_texture_to_the_triangles_without_one(self):
        # COOP-GhettoZomburaUpdated's car01: 374 triangles, "Texture None" in the T3D
        pkg = _Package([(-1, 374)])
        secs = staticmesh.sections(pkg, 'car01')
        self.assertEqual(secs, [Section('car_park.park_ceiling_0', 374)])
        self.assertEqual(set(staticmesh.triangle_textures([None] * 374, secs)), {'car_park.park_ceiling_0'})

    def test_a_non_texture_reference_or_none_draws_without_texture(self):
        pkg = _Package([(-2, 2), (0, 4)])
        self.assertEqual(staticmesh.sections(pkg, 'car01'), [Section(None, 2), Section(None, 4)])
        self.assertIsNone(staticmesh.sections(pkg, 'no-such-mesh'))

    def test_sections_follow_the_raw_runs_and_may_be_longer(self):
        # FIREESCAPE: 870 + 10 + 12 in the sections, the T3D left 6 of the last run out
        raw = ['a'] * 870 + ['b'] * 10 + [None] * 6
        secs = [Section('a', 870), Section('b', 10), Section('stwalk1', 12)]
        drawn = staticmesh.triangle_textures(raw, secs)
        self.assertEqual(drawn[-6:], ['stwalk1'] * 6)
        self.assertEqual(drawn[:880], raw[:880])

    def test_a_retextured_run_takes_the_new_texture(self):
        # urbanterrain: brick pieces in the T3D, dirt in the sections
        drawn = staticmesh.triangle_textures(['dirt'] * 2 + ['brick'] * 3, [Section('dirt', 2), Section('dirt', 3)])
        self.assertEqual(drawn, ['dirt'] * 5)

    def test_sections_that_dont_line_up_give_nothing(self):
        self.assertIsNone(staticmesh.triangle_textures(['a'] * 5, [Section('a', 3)]))
        self.assertIsNone(staticmesh.triangle_textures(['a'], []))


class DefaultTextureTests(unittest.TestCase):
    def test_engine_default_texture_is_converted_its_sprites_are_not(self):
        from mflib import textures
        usage = {'engine.defaulttexture': {'opaque'}, 'engine.s_ladder': {'masked'}, 'rage_urban.panel02': {'opaque'}}
        self.assertEqual(set(textures.drawn(usage)), {'engine.defaulttexture', 'rage_urban.panel02'})


if __name__ == '__main__':
    unittest.main()
