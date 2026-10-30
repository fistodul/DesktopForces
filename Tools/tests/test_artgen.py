"""Generated replacement art must keep the originals' layout and regenerate identically."""
from pathlib import Path
import sys
import unittest

import numpy as np

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'artgen'))
import ambience
import flipbooks
import lookalike


class GeneratorTests(unittest.TestCase):
    def see_through(self, gen, **options):
        rng = np.random.default_rng(1)
        tones = [np.array(c, np.float32) for c in ((40, 40, 40), (90, 90, 90), (150, 150, 150))]
        rgb, alpha = lookalike.GENERATORS[gen](rng, 64, 64, tones, options)
        self.assertEqual(rgb.shape, (64, 64, 3))
        self.assertEqual(alpha.shape, (64, 64))
        self.assertLess(alpha.min(), 1)
        self.assertGreater(alpha.max(), 200)

    def test_see_through_generators_leave_holes(self):
        self.see_through('lattice', rows=2, cols=2, cross=True)
        self.see_through('tree', leaves=0.5, depth=3)
        self.see_through('palm')
        self.see_through('splat')

    def test_flipbook_keeps_atlas_and_is_deterministic(self):
        key = 'rageeffects.densesmoke.densesmoke_a01'
        info = {'cols': 4, 'rows': 4, 'frames': 16}
        a = flipbooks.make(key, info, (128, 128))
        self.assertEqual(a.size, (128, 128))
        self.assertEqual(a.mode, 'RGBA')
        self.assertEqual(a.tobytes(), flipbooks.make(key, info, (128, 128)).tobytes())
        cell = np.asarray(a)[32:64, 32:64, 3]     # the sixth frame is a puff, not an empty cell
        self.assertGreater(cell.max(), 100)

    def test_every_flipbook_key_has_a_painter(self):
        for paint, options in flipbooks.BOOKS.values():
            self.assertTrue(callable(paint))
            self.assertIsInstance(options, dict)


class AmbienceTests(unittest.TestCase):
    def test_keeps_the_originals_loudness(self):
        pcm = ambience.make('Machinery01', 2.0, 0.1)
        self.assertAlmostEqual(float(np.sqrt(np.mean(pcm ** 2))), 0.1, places=2)

    def test_one_shot_keeps_its_length_and_loop_is_trimmed_for_its_crossfade(self):
        self.assertEqual(len(ambience.make('TrainHorn', 2.0, 0.1)), 2 * ambience.RATE)
        self.assertLess(len(ambience.make('Fans01', 2.0, 0.1)), 2 * ambience.RATE)

    def test_deterministic(self):
        self.assertTrue(np.array_equal(ambience.make('Seagulls1', 1.0, 0.1), ambience.make('Seagulls1', 1.0, 0.1)))


if __name__ == '__main__':
    unittest.main()
