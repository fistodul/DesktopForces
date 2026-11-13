"""Generated replacement art must keep the originals' layout and regenerate identically."""
from pathlib import Path
import sys
import unittest

import numpy as np

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'artgen'))
from PIL import Image

import ambience
import flipbooks
import hudart
import lookalike
import weaponmesh
from mfport.mflib import textures


class SizeTests(unittest.TestCase):
    def limited(self, w, h, cap):
        return textures._limit_size(Image.new('RGB', (w, h)), cap).size

    def test_squares_shrink_to_the_limit(self):
        self.assertEqual(self.limited(4096, 4096, 1024), (1024, 1024))
        self.assertEqual(self.limited(1024, 512, 512), (512, 256))

    def test_strips_keep_their_short_side(self):
        self.assertEqual(self.limited(4096, 128, 512), (2048, 128))
        self.assertEqual(self.limited(128, 4096, 512), (128, 2048))
        self.assertEqual(self.limited(2048, 512, 1024), (2048, 512))

    def test_small_textures_stay(self):
        self.assertEqual(self.limited(256, 256, 512), (256, 256))

    def test_detail_list_ignores_case_and_leaves_crates_out(self):
        self.assertTrue(textures.is_detail('ak74.Skins.AK74PlayerTex1'))
        self.assertTrue(textures.is_detail('the_bank.dallas'))
        self.assertFalse(textures.is_detail('BoxPhysics.Skins.Box'))


class CoreArtTests(unittest.TestCase):
    def test_atlases_keep_the_hud_layout(self):
        sizes = {'Boxes': 256, 'Backs': 256, 'Icons': 256, 'ScoreBoxes': 128, 'MinimapNewIcons': 256, 'WeaponIcons': 256}
        for name, size in sizes.items():
            self.assertEqual(hudart.SHEETS[name]().size, (size, size), name)

    def test_every_weapon_icon_rect_has_a_picture(self):
        icons = np.asarray(hudart.weapon_icons())[..., 3]
        # WeaponIcon rects of the items in DefaultGame.ini's loadout
        for x0, y0, x1, y1 in ((0, 0, 48, 64), (48, 0, 96, 64), (96, 0, 144, 64), (144, 0, 192, 64), (192, 0, 256, 64), (0, 64, 64, 128),
                               (64, 64, 256, 128), (0, 128, 160, 192), (160, 128, 256, 192), (0, 192, 104, 256), (104, 192, 248, 256)):
            self.assertGreater((icons[y0:y1, x0:x1] > 128).sum(), 150, (x0, y0))

    def test_all_crosshairs_differ(self):
        marks = {hudart.crosshair(i).getchannel('A').tobytes() for i in range(24)}
        self.assertEqual(len(marks), 24)

    def test_crosshair_shadows_stop_inside_the_texture(self):
        for i in range(24):
            a = np.asarray(hudart.crosshair(i).getchannel('A'))
            edge = max(a[0].max(), a[-1].max(), a[:, 0].max(), a[:, -1].max())
            self.assertEqual(edge, 0, i)   # the HUD showed the cut as flat sides on rings

    def test_weapon_models_are_their_originals_size_and_closed(self):
        for name, (paint, size, turn, flip) in weaponmesh.SIZES.items():
            mesh = weaponmesh.build(name, paint, size, turn, flip)
            lo, hi = mesh.bounds()
            for axis in (0, 1, 2):
                self.assertAlmostEqual(hi[axis] - lo[axis], size[axis], 1, name)
            self.assertLess(mesh.triangle_count, 300, name)


class GeneratorTests(unittest.TestCase):
    def test_line_share_tells_ribs_from_stains(self):
        ribs = np.tile(128 + 60 * np.sign(np.sin(np.arange(128) * 0.8)), (128, 1)).astype(np.float32)
        rng = np.random.default_rng(1)
        stains = (lookalike.fbm(rng, 128, 128, 1.2) * 255).astype(np.float32)
        self.assertGreater(lookalike.line_share(ribs, True), 0.5)
        self.assertLess(lookalike.line_share(stains, True), 0.2)

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


class LookalikeRuleTests(unittest.TestCase):
    """What --auto decides from a picture and its name (play-test 2026-11-10)."""

    @staticmethod
    def sheet(bands):
        """A sheet of signs: bands of (height, colour), each with dark lettering across it."""
        img = Image.new('RGB', (256, 256))
        y = 0
        for height, colour in bands:
            img.paste(colour, (0, y, 256, y + height))
            for x in range(20, 236, 24):               # letters: blocks covering a third of the row
                img.paste((20, 20, 20), (x, y + height // 3, x + 10, y + 2 * height // 3))
            y += height
        return img

    def test_a_sheet_of_signs_keeps_its_signs(self):
        four = self.sheet([(64, (240, 240, 240)), (64, (40, 40, 60)), (64, (240, 190, 20)), (64, (190, 30, 30))])
        panels = lookalike.sign_panels(four)
        self.assertEqual([round(p[1] * 4) for p in panels], [0, 1, 2, 3])
        self.assertEqual(panels[2][4][:2], [240, 190])   # each its own colour

    def test_one_sign_is_not_cut_through_its_words(self):
        self.assertEqual(len(lookalike.sign_panels(self.sheet([(256, (30, 60, 200))]))), 1)

    def test_every_sign_on_a_sheet_gets_a_whole_word(self):
        spec = {'size': [256, 256], 'gen': 'sign', 'dark': [0, 0, 0], 'mid': [90, 90, 90], 'light': [200, 200, 200],
                'panels': [[0, 0, 1, 0.5, [200, 30, 30], [255, 255, 255]], [0, 0.5, 1, 1, [20, 20, 20], [255, 255, 255]]]}
        img = np.asarray(lookalike.make('Test.Sign.sheet', spec).convert('RGB'), np.int32)
        for half in (img[:256], img[256:]):
            lit = (half.min(axis=2) > 200)
            self.assertGreater(lit[40:216].sum(), 500)                 # lettering in the middle of each
            self.assertFalse(lit[70:186, 28:44].any() or lit[70:186, -44:-28].any())   # clear of the frame: none cut off

    def test_names_are_read_without_the_package(self):
        gray = Image.fromarray(np.full((64, 64, 3), 128, np.uint8))
        noise = Image.fromarray(np.random.default_rng(1).integers(90, 170, (64, 64, 3), dtype=np.uint8))
        self.assertEqual(lookalike.auto_spec('rage_generic.Sign.Grey', gray, {'opaque'})['gen'], 'flat')
        self.assertEqual(lookalike.auto_spec('rage_airport.Terminal.ap_DepIntWall1', noise, {'opaque'})['gen'], 'noise')
        self.assertNotEqual(lookalike.auto_spec('Rage.Skins.JBallMesh1', noise, {'masked'})['gen'], 'grate')

    def test_snow_is_never_laid_in_courses(self):
        rows = np.zeros((128, 128, 3), np.uint8) + 225
        rows[::16] = 190                                               # even courses, as bricks have
        rows[:, ::32] = 200
        self.assertEqual(lookalike.auto_spec('rage_snow.snow', Image.fromarray(rows), {'opaque'})['gen'], 'noise')

    def test_a_window_is_found_in_a_wall_only(self):
        wall = np.full((128, 128), 170, np.float32)
        wall[40:90, 45:85] = 30
        np.testing.assert_allclose(lookalike.window_rect(wall), [45 / 128, 40 / 128, 85 / 128, 90 / 128], atol=0.02)
        corner = np.full((128, 128), 170, np.float32)
        corner[100:128, 100:128] = 30                                  # a shadow in the corner
        self.assertIsNone(lookalike.window_rect(corner))

    def test_round_faces_are_told_from_stripes(self):
        yy, xx = np.mgrid[0:128, 0:128]
        disc = np.where(np.hypot(xx - 63.5, yy - 63.5) < 60, 180.0, 10.0).astype(np.float32)
        self.assertIsNotNone(lookalike.disc_shape(disc))
        stripe = np.full((128, 128), 10, np.float32)
        stripe[:, 30:98] = 180
        self.assertIsNone(lookalike.disc_shape(stripe))


if __name__ == '__main__':
    unittest.main()
