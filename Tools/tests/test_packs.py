"""Reviewed shared art must not introduce optional-pack dependencies or remaster other art."""
import json
from pathlib import Path
from types import SimpleNamespace
import sys
import tempfile
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from mfport.mflib import packs


class SharedArtTests(unittest.TestCase):
    def test_shared_replacement_stays_bundled_without_a_community_user(self):
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder)
            config = root / 'packs.toml'
            config.write_text('shared_replacements = ["rage_generic.Sign.Black", "rage_generic.Sign.Missing"]\n')
            (root / 'textures.json').write_text(json.dumps({
                'rage_generic.Sign.Black': {'override': True},
                'rage_generic.Sign.Red': {'override': True},
                'rage_generic.Sign.Missing': {'override': False},
            }))
            cfg = SimpleNamespace(out_dir=root, uproject=root / 'DesktopForces.uproject')
            with patch.object(packs, 'PACKS_FILE', config):
                result = json.loads(packs.write_manifest(cfg).read_text())
            self.assertFalse(result['remaster_classic'])
            self.assertEqual(result['shared_replacements'], ['rage_generic.sign.black'])
            self.assertNotIn('rage_generic.sign.black', result['unverified_replaced'])
            self.assertIn('rage_generic.sign.red', result['unverified_replaced'])

    def test_default_manifest_keeps_classic_art(self):
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder)
            config = root / 'packs.toml'
            config.write_text('remaster_classic = false\n')
            cfg = SimpleNamespace(out_dir=root, uproject=root / 'DesktopForces.uproject')
            with patch.object(packs, 'PACKS_FILE', config):
                result = json.loads(packs.write_manifest(cfg).read_text())
            self.assertEqual(result['shared_replacements'], [])


if __name__ == '__main__':
    unittest.main()
