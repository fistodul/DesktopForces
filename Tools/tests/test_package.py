"""Tools/package.py --downloads: which packs go to the server, the manifest ChunkDownloader reads."""
import hashlib
from pathlib import Path
import sys
import tempfile
import unittest
import zipfile

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
            upload = package.split_downloads(paks, lists, root / 'Out', 'b1', 'Android', team=True)
            self.assertEqual(upload, root / 'Out/Downloads/Android/b1')
            self.assertEqual(sorted(p.name for p in paks.iterdir()), [f'pakchunk0-Android_ASTC{e}' for e in ('.pak', '.ucas', '.utoc')])
            self.assertEqual(sorted(p.name for p in (root / 'Out/Sideload').iterdir()), [f'pakchunk3-Android_ASTC{e}' for e in ('.pak', '.ucas', '.utoc')])
            lines = (upload / 'BuildManifest-Android.txt').read_text().splitlines()
            self.assertEqual(lines[:2], ['$NUM_ENTRIES = 7', '$BUILD_ID = b1'])
            part = next(line.split('\t') for line in lines if line.startswith('pakchunk1-Android_ASTC_s1.ucas'))
            self.assertEqual(part, ['pakchunk1-Android_ASTC_s1.ucas', '4', 'SHA1:' + hashlib.sha1(b'part').hexdigest().upper(), '1', 'pakchunk1-Android_ASTC_s1.ucas'])
            self.assertFalse(any('pakchunk3' in line for line in lines))
            # the team server's folder: every pack, classic too, with its own manifest
            private = root / 'Out/Private/Android/b1'
            self.assertEqual(len([p for p in private.iterdir() if p.name.startswith('pakchunk')]), 10)
            lines = (private / 'BuildManifest-Android.txt').read_text().splitlines()
            self.assertEqual(lines[:2], ['$NUM_ENTRIES = 10', '$BUILD_ID = b1'])
            self.assertTrue(any(line.startswith('pakchunk3-Android_ASTC.pak\t') and line.split('\t')[3] == '3' for line in lines))

    def test_project_version_stamp(self):
        ini = '[/Script/EngineSettings.GeneralProjectSettings]\r\nProjectName=X\r\nProjectVersion=dev\r\nOther=1\r\n'
        self.assertEqual(package.with_project_version(ini, '20261030'), ini.replace('=dev', '=20261030'))
        with self.assertRaises(package.Fail):
            package.with_project_version('ProjectName=X\n', 'v')
        self.assertRegex(package.game_version(Path(__file__).resolve().parents[2]), r'^\d{8}$')

    def test_classic_zip_unzips_into_the_install(self):
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder)
            paks, lists = self.make(root, '')
            package.split_downloads(paks, lists, root / 'Out', 'b1', 'Linux')
            self.assertFalse((root / 'Out/Private').exists())  # team folder only on request
            with zipfile.ZipFile(package.classic_zip(root / 'Out', 'Linux', 'b1')) as z:
                names = z.namelist()
            self.assertIn('DesktopForces/Content/Paks/pakchunk3-Android_ASTC.pak', names)
            self.assertFalse(any('pakchunk1' in n for n in names))

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
