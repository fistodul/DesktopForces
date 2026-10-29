"""Tools/package.py --downloads: which packs go to the server, the manifest ChunkDownloader reads."""
import hashlib
from pathlib import Path
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import package


class DownloadsTests(unittest.TestCase):
    def make(self, root: Path, chunk1_list: str) -> tuple[Path, Path]:
        paks, lists = root / 'Paks', root / 'Lists'
        paks.mkdir()
        lists.mkdir()
        for n in range(4):
            for ext in ('.pak', '.utoc', '.ucas'):
                (paks / f'pakchunk{n}-Android_ASTC{ext}').write_bytes(f'{n}{ext}'.encode())
        (paks / 'pakchunk1-Android_ASTC_s1.ucas').write_bytes(b'part')
        (lists / 'pakchunk1.txt').write_text(chunk1_list)
        (lists / 'pakchunk2.txt').write_text('..\\Plugins\\MobileForcesUnverified\\Content\\X\n')
        (lists / 'pakchunk3.txt').write_text('..\\Plugins\\MobileForcesClassic\\Content\\Maps\\mf-warehouse\n')
        return paks, lists

    def test_split_keeps_core_uploads_public_sideloads_classic(self):
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder)
            paks, lists = self.make(root, '..\\DesktopForces\\Content\\Maps\\mf-Tropics\n')
            upload = package.split_downloads(paks, lists, root / 'Out', 'b1', 'Android')
            self.assertEqual(upload, root / 'Out/Downloads/Android/b1')
            self.assertEqual(sorted(p.name for p in paks.iterdir()), [f'pakchunk0-Android_ASTC{e}' for e in ('.pak', '.ucas', '.utoc')])
            self.assertEqual(sorted(p.name for p in (root / 'Out/Sideload').iterdir()), [f'pakchunk3-Android_ASTC{e}' for e in ('.pak', '.ucas', '.utoc')])
            lines = (upload / 'BuildManifest-Android.txt').read_text().splitlines()
            self.assertEqual(lines[:2], ['$NUM_ENTRIES = 7', '$BUILD_ID = b1'])
            part = next(line.split('\t') for line in lines if line.startswith('pakchunk1-Android_ASTC_s1.ucas'))
            self.assertEqual(part, ['pakchunk1-Android_ASTC_s1.ucas', '4', 'SHA1:' + hashlib.sha1(b'part').hexdigest().upper(), '1', 'pakchunk1-Android_ASTC_s1.ucas'])
            self.assertFalse(any('pakchunk3' in line for line in lines))

    def test_a_chunk_with_classic_content_is_never_uploaded(self):
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder)
            paks, lists = self.make(root, '..\\Plugins\\MobileForcesClassic\\Content\\Sounds\\levelSFX\\Seagulls1\n')
            upload = package.split_downloads(paks, lists, root / 'Out', 'b1', 'Android')
            self.assertFalse(any('pakchunk1' in p.name for p in upload.iterdir()))
            self.assertIn('pakchunk1-Android_ASTC_s1.ucas', [p.name for p in (root / 'Out/Sideload').iterdir()])

    def test_missing_chunk_list_stops_before_moving(self):
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder)
            paks, lists = self.make(root, '')
            (lists / 'pakchunk3.txt').unlink(missing_ok=True)
            with self.assertRaises(package.Fail):
                package.split_downloads(paks, lists, root / 'Out', 'b1', 'Android')
            self.assertEqual(len(list(paks.iterdir())), 13)


if __name__ == '__main__':
    unittest.main()
