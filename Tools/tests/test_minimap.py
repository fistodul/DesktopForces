"""Generated floor plans must remain visible under enclosing ceilings."""
from pathlib import Path
import sys
import unittest
from unittest.mock import patch

import numpy as np

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from mfport.mflib import minimap


class FloorProjectionTests(unittest.TestCase):
    def test_ceiling_does_not_cover_floor_heights_or_expand_bounds(self):
        def quad(x0, y0, x1, y1, z, ceiling=False):
            a, b, c, d = (x0, y0, z), (x1, y0, z), (x1, y1, z), (x0, y1, z)
            return [(a, c, b), (a, d, c)] if ceiling else [(a, b, c), (a, c, d)]

        floors = quad(0, 0, 1440, 1440, 0) + quad(1440, 0, 2880, 1440, 500)
        ceiling = quad(-1440, -1440, 4320, 2880, 2000, ceiling=True)
        with patch.object(minimap, '_play_area', return_value=(-5000, -5000, 5000, 5000, -3000, 3000)), \
                patch.object(minimap, 'save') as save:
            with patch.object(minimap, '_world_triangles', return_value=floors):
                expected_bounds = minimap.generate(None, Path('.'), {}, 'test')
                expected = np.asarray(save.call_args.args[1]).copy()
            with patch.object(minimap, '_world_triangles', return_value=floors + ceiling):
                actual_bounds = minimap.generate(None, Path('.'), {}, 'test')
                actual = np.asarray(save.call_args.args[1])
        self.assertEqual(actual_bounds, expected_bounds)
        np.testing.assert_array_equal(actual, expected)
        self.assertGreater(int(actual.max()) - int(actual.min()), 50)


if __name__ == '__main__':
    unittest.main()
