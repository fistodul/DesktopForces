"""Selected replacement art must stay bounded and preserve gameplay transparency."""
import json
from pathlib import Path
from types import SimpleNamespace
import sys
import tempfile
import unittest
from unittest.mock import patch

from PIL import Image

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from mfport.mflib import replacements, textures


class ReplacementTests(unittest.TestCase):
    def setUp(self):
        self.folder = tempfile.TemporaryDirectory()
        self.addCleanup(self.folder.cleanup)
        self.root = Path(self.folder.name)
        self.local = self.root / 'licensed'
        self.cfg = SimpleNamespace(out_dir=self.root, max_texture_size=1024)
        self.manifest = self.root / 'selection.json'
        self.patch = patch.object(textures, 'LICENSED_DIR', self.local)
        self.patch.start()
        self.addCleanup(self.patch.stop)
        Image.new('RGB', (2048, 1024), (123, 91, 50)).save(self.root / 'source.png')
        (self.root / 'textures.json').write_text(json.dumps({
            'Pkg.Floor': {'alpha': False, 'file': 'unused.png'},
            'Pkg.Grate': {'alpha': True, 'file': 'grate.png'},
        }))
        Image.new('RGBA', (8, 8), (1, 2, 3, 0)).save(self.root / 'grate.png')

    def entry(self, target):
        return {'target': target, 'source': 'source.png', 'source_url': 'https://example.test/asset', 'license': 'Test fixture'}

    def test_limits_size_canonicalizes_name_and_preserves_source(self):
        original = (self.root / 'source.png').read_bytes()
        self.manifest.write_text(json.dumps([self.entry('pkg.floor')]))
        result = replacements.stage(self.cfg, self.manifest)
        with Image.open(self.local / 'Pkg/Floor.png') as image:
            self.assertEqual(image.size, (512, 256))
        self.assertEqual((self.root / 'source.png').read_bytes(), original)
        self.assertEqual(result[0]['target'], 'Pkg.Floor')
        destination = self.local / 'Pkg/Floor.png'
        before = destination.stat().st_mtime_ns
        replacements.stage(self.cfg, self.manifest)
        self.assertEqual(destination.stat().st_mtime_ns, before)

    def test_transparency_failure_does_not_partially_apply_selection(self):
        self.manifest.write_text(json.dumps([self.entry('Pkg.Floor'), self.entry('Pkg.Grate')]))
        with self.assertRaisesRegex(ValueError, 'needs transparency'):
            replacements.stage(self.cfg, self.manifest)
        self.assertFalse(self.local.exists())

    def test_respects_lower_project_limit(self):
        self.cfg.max_texture_size = 256
        self.manifest.write_text(json.dumps([self.entry('Pkg.Floor')]))
        replacements.stage(self.cfg, self.manifest)
        with Image.open(self.local / 'Pkg/Floor.png') as image:
            self.assertEqual(image.size, (256, 128))

    def test_duplicate_targets_do_not_write_files(self):
        self.manifest.write_text(json.dumps([self.entry('Pkg.Floor'), self.entry('pkg.floor')]))
        with self.assertRaisesRegex(ValueError, 'Duplicate texture target'):
            replacements.stage(self.cfg, self.manifest)
        self.assertFalse(self.local.exists())

    def test_palette_transparency_is_preserved(self):
        source = Image.new('P', (8, 8), 1)
        source.putpixel((0, 0), 0)
        source.save(self.root / 'source.png', transparency=0)
        self.manifest.write_text(json.dumps([self.entry('Pkg.Grate')]))
        replacements.stage(self.cfg, self.manifest)
        with Image.open(self.local / 'Pkg/Grate.png') as image:
            self.assertEqual(image.mode, 'RGBA')
            self.assertEqual(image.getchannel('A').getextrema(), (0, 255))

    def test_rejects_path_traversal(self):
        self.manifest.write_text(json.dumps([self.entry('../Pkg.Floor')]))
        with self.assertRaisesRegex(ValueError, 'Invalid texture target'):
            replacements.stage(self.cfg, self.manifest)
        self.assertFalse(self.local.exists())


if __name__ == '__main__':
    unittest.main()
