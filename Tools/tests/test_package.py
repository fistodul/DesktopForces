"""Tools/package.py --downloads: which packs go to the server, the manifest ChunkDownloader reads."""
import hashlib
from pathlib import Path
import sys
import tarfile
import tempfile
import unittest
from unittest import mock
import zipfile

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import package


def chunks(paths) -> list[str]:
    return sorted({Path(p).name.split('-')[0] for p in paths if 'pakchunk' in str(p)})


def make(root: Path, chunk1_list: str) -> tuple[Path, Path]:
    """A staged build's four packs (chunk 1 parted) and their cooker lists, chunk 1's given."""
    paks, lists = root / 'Paks', root / 'Lists'
    paks.mkdir()
    lists.mkdir()
    for n in range(4):
        for ext in ('.pak', '.utoc', '.ucas'):
            (paks / f'pakchunk{n}-Android_ASTC{ext}').write_bytes(f'{n}{ext}'.encode())
    (paks / 'pakchunk1-Android_ASTC_s1.ucas').write_bytes(b'part')
    cooked = '..\\Saved\\Cooked\\Android_ASTC\\'
    (lists / 'pakchunk0.txt').write_text(f'{cooked}Engine\\Plugins\\Animation\\X\n{cooked}DesktopForces\\Content\\MF\\UI\\X\n')
    (lists / 'pakchunk1.txt').write_text(chunk1_list)
    (lists / 'pakchunk2.txt').write_text(f'{cooked}DesktopForces\\Plugins\\MobileForcesUnverified\\Content\\X\n')
    (lists / 'pakchunk3.txt').write_text(f'{cooked}DesktopForces\\Plugins\\MobileForcesClassic\\Content\\Maps\\mf-warehouse\n')
    return paks, lists


class DownloadsTests(unittest.TestCase):
    def test_core_with_classic_or_unverified_content_stops_before_moving(self):
        for listing in ('..\\DesktopForces\\Plugins\\MobileForcesClassic\\Content\\T\n', '..\\DesktopForces\\Plugins\\MobileForcesUnverified\\Content\\T\n'):
            with tempfile.TemporaryDirectory() as folder:
                root = Path(folder)
                paks, lists = make(root, '')
                (lists / 'pakchunk0.txt').write_text(listing)
                with self.assertRaisesRegex(package.Fail, 'core pak'):
                    package.split_downloads(paks, lists, root / 'Out', 'b1', 'Windows')
                self.assertEqual(len(list(paks.iterdir())), 13)

    def test_a_new_plugins_content_is_never_community(self):
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder)
            paks, lists = make(root, '..\\Saved\\Cooked\\Windows\\DesktopForces\\Plugins\\NewMods\\Content\\Maps\\X\n')
            with self.assertRaisesRegex(package.Fail, 'newmods'):
                package.split_downloads(paks, lists, root / 'Out', 'b1', 'Windows')

    def test_team_files_go_apart_from_the_public_ones(self):
        with tempfile.TemporaryDirectory() as folder:
            release = Path(folder)
            self.assertEqual(package.share_path(release, 'release'), release)
            self.assertEqual(package.share_path(release, 'community'), release)
            for what in ('team', 'classic', 'unverified', 'full'):
                self.assertEqual(package.share_path(release, what), release / 'team')

    def test_split_keeps_core_uploads_community_and_unverified(self):
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder)
            paks, lists = make(root, '..\\DesktopForces\\Content\\Maps\\mf-Tropics\n')
            packs = package.split_downloads(paks, lists, root / 'Out', 'b1', 'Android')
            self.assertEqual(sorted(p.name for p in paks.iterdir()), [f'pakchunk0-Android_ASTC{e}' for e in ('.pak', '.ucas', '.utoc')])
            upload, private = root / 'Out/Downloads/Android/b1', root / 'Out/Private/Android/b1'
            self.assertEqual(chunks(upload.iterdir()), ['pakchunk1', 'pakchunk2'])
            lines = (upload / 'BuildManifest-Android.txt').read_text().splitlines()
            self.assertEqual(lines[:2], ['$NUM_ENTRIES = 7', '$BUILD_ID = b1'])
            part = next(line.split('\t') for line in lines if line.startswith('pakchunk1-Android_ASTC_s1.ucas'))
            self.assertEqual(part, ['pakchunk1-Android_ASTC_s1.ucas', '4', 'SHA1:' + hashlib.sha1(b'part').hexdigest().upper(), '1', 'pakchunk1-Android_ASTC_s1.ucas'])
            self.assertFalse(any('pakchunk3' in line for line in lines))
            # the team's folder: every pack the build lacks, classic too, with its own manifest
            self.assertEqual(chunks(private.iterdir()), ['pakchunk1', 'pakchunk2', 'pakchunk3'])
            lines = (private / 'BuildManifest-Android.txt').read_text().splitlines()
            self.assertEqual(lines[:2], ['$NUM_ENTRIES = 10', '$BUILD_ID = b1'])
            self.assertTrue(any(line.startswith('pakchunk3-Android_ASTC.pak\t') and line.split('\t')[3] == '3' for line in lines))
            self.assertEqual(packs[private / 'pakchunk3-Android_ASTC.pak'], 'classic')
            self.assertEqual(packs[private / 'pakchunk1-Android_ASTC_s1.ucas'], 'community')
            self.assertFalse((root / 'Out/Sideload').exists())

    def test_downloads_are_hard_links_to_private(self):
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder)
            paks, lists = make(root, '..\\DesktopForces\\Content\\Maps\\mf-Tropics\n')
            packs = package.split_downloads(paks, lists, root / 'Out', 'b1', 'Windows')
            self.assertEqual(chunks(paks.iterdir()), ['pakchunk0'])  # every platform: the core pack alone
            self.assertEqual(sorted(set(packs.values())), ['classic', 'community', 'unverified'])
            # Downloads/ holds hard links to Private/'s files, not copies
            self.assertTrue((root / 'Out/Downloads/Windows/b1/pakchunk2-Android_ASTC.pak').samefile(root / 'Out/Private/Windows/b1/pakchunk2-Android_ASTC.pak'))

    def test_project_version_stamp(self):
        ini = '[/Script/EngineSettings.GeneralProjectSettings]\r\nProjectName=X\r\nProjectVersion=dev\r\nOther=1\r\n'
        self.assertEqual(package.with_project_version(ini, '20261030'), ini.replace('=dev', '=20261030'))
        with self.assertRaises(package.Fail):
            package.with_project_version('ProjectName=X\n', 'v')
        self.assertRegex(package.game_version(Path(__file__).resolve().parents[2]), r'^\d{8}$')

    def test_addon_zip_unzips_into_the_install(self):
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder)
            paks, lists = make(root, '')
            packs = package.split_downloads(paks, lists, root / 'Out', 'b1', 'Linux')
            team = package.addon_zip(packs, 'Linux', 'b1', root / 'Team.zip', 'Team')
            with zipfile.ZipFile(team) as z:
                names = z.namelist()
                readme = z.read('README-Team.txt').decode()
            self.assertIn('DesktopForces/Content/Paks/pakchunk3-Android_ASTC.pak', names)
            self.assertEqual(chunks(names), ['pakchunk1', 'pakchunk2', 'pakchunk3'])
            self.assertIn('classic, community, unverified', readme)
            self.assertIn('for the team only', readme)
            unverified = {p: kind for p, kind in packs.items() if kind == 'unverified'}
            with zipfile.ZipFile(package.addon_zip(unverified, 'Linux', 'b1', root / 'Unverified.zip', 'Unverified')) as z:
                self.assertEqual(chunks(z.namelist()), ['pakchunk2'])
                self.assertNotIn('team only', z.read('README-Unverified.txt').decode())
            with zipfile.ZipFile(package.addon_zip(packs, 'Android', 'b1', root / 'Phone.zip', 'Team')) as z:
                self.assertTrue(all(n.startswith(package.ANDROID_PAKS) for n in z.namelist() if 'pakchunk' in n))

    def test_share_names_follow_unreals_order(self):
        self.assertEqual(package.share_name('Windows', 'Shipping', '20261030'), 'DesktopForces-Windows-20261030')
        self.assertEqual(package.share_name('Android', 'Shipping', '20261030', 'Classic'), 'DesktopForces-Android-20261030-Classic')
        self.assertEqual(package.share_name('Linux', 'Development', '20261030', 'Full'), 'DesktopForces-Linux-Development-20261030-Full')

    def test_full_zip_is_the_build_with_the_packs_in_place(self):
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder)
            out = root / 'Windows-Shipping'
            (out / 'DesktopForces/Content/Paks').mkdir(parents=True)
            (out / 'DesktopForces.exe').write_text('game')
            (out / 'DesktopForces/Content/Paks/pakchunk0-Windows.pak').write_bytes(b'core')
            (root / 'pakchunk3-Windows.pak').write_bytes(b'pack')
            full = package.build_zip(root / 'Full.zip', out, [root / 'pakchunk3-Windows.pak'], 'Windows-Shipping/DesktopForces/Content/Paks')
            with zipfile.ZipFile(full) as z:
                self.assertEqual(z.read('Windows-Shipping/DesktopForces/Content/Paks/pakchunk3-Windows.pak'), b'pack')
                self.assertEqual(z.read('Windows-Shipping/DesktopForces.exe'), b'game')
                # paks stored (compressed already), the rest deflated
                self.assertEqual(z.getinfo('Windows-Shipping/DesktopForces/Content/Paks/pakchunk0-Windows.pak').compress_type, zipfile.ZIP_STORED)
                self.assertEqual(z.getinfo('Windows-Shipping/DesktopForces.exe').compress_type, zipfile.ZIP_DEFLATED)

    def test_a_chunk_with_classic_content_is_never_uploaded(self):
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder)
            paks, lists = make(root, '..\\Plugins\\MobileForcesClassic\\Content\\Sounds\\levelSFX\\Seagulls1\n')
            package.split_downloads(paks, lists, root / 'Out', 'b1', 'Android')
            upload = root / 'Out/Downloads/Android/b1'
            self.assertEqual(chunks(upload.iterdir()), ['pakchunk2'])
            self.assertIn('pakchunk1-Android_ASTC_s1.ucas', [p.name for p in (root / 'Out/Private/Android/b1').iterdir()])

    def test_missing_chunk_list_stops_before_moving(self):
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder)
            paks, lists = make(root, '')
            (lists / 'pakchunk3.txt').unlink(missing_ok=True)
            with self.assertRaises(package.Fail):
                package.split_downloads(paks, lists, root / 'Out', 'b1', 'Android')
            self.assertEqual(len(list(paks.iterdir())), 13)


class ArgumentTests(unittest.TestCase):
    def check(self, argv: list[str]) -> list[str]:
        """--zip as main() reads it, without packaging."""
        seen = []
        with mock.patch.object(package, 'package', seen.append), mock.patch.object(package, 'package_server', seen.append):
            self.assertEqual(package.main(argv), 0)
        return seen[0].zip

    def refused(self, argv: list[str]) -> None:
        with self.assertRaises(SystemExit), mock.patch('sys.stderr'):
            package.main(argv)

    def test_zip_choices(self):
        self.assertEqual(self.check(['Windows', '--downloads', '--zip']), ['release'])
        self.assertEqual(self.check(['Windows', '--downloads', '--zip', 'release,team']), ['release', 'team'])
        self.assertEqual(self.check(['Windows']), [])
        self.assertEqual(self.check(['Windows', '--zip']), ['release'])
        self.assertEqual(self.check(['--zip', 'Windows', '--downloads']), ['release'])  # a bare --zip before the platform
        self.assertEqual(self.check(['--zip', '--downloads', 'Windows']), ['release'])
        self.assertEqual(self.check(['Linux', '--server']), ['release', 'team'])
        self.assertEqual(self.check(['Linux', '--server', '--zip', 'full']), ['full'])
        self.refused(['Windows', '--downloads', '--zip', 'everything'])
        self.refused(['Windows', '--zip', 'team'])  # without --downloads the build has every pack
        self.refused(['Android', '--downloads', '--zip', 'full'])
        self.assertEqual(self.check(['Windows', '--downloads', '--zip', 'community']), ['community'])


class ServerTests(unittest.TestCase):
    def test_server_build_is_server_only_and_cooks_the_maps(self):
        cmd = package.server_uat(['/Game/Maps/mf-Tropics', '/MobileForcesClassic/Maps/mf-warehouse'], '/home/ue4')
        self.assertTrue(cmd[0].startswith('/home/ue4/UnrealEngine/'))
        for flag in ('-server', '-noclient', '-serverplatform=Linux', '-ubtargs=-NoUBA', '-archive'):
            self.assertIn(flag, cmd)
        self.assertIn('-map=/Game/Maps/mf-Tropics+/MobileForcesClassic/Maps/mf-warehouse', cmd)
        self.assertFalse(any(c.startswith('-map=') for c in package.server_uat([], '/home/ue4')))

    def test_server_tree_leaves_this_checkouts_builds_out(self):
        for folder in ('/Binaries/', '/Intermediate/', '/Saved/', '/DerivedDataCache/', '/.git/', '/Tools/mfport/work/'):
            self.assertIn(folder, package.SERVER_SKIP)

    def test_server_archives_release_addons_and_full(self):
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder)
            out = root / 'LinuxServer-Shipping'
            paks = out / 'DesktopForces/Content/Paks'
            paks.mkdir(parents=True)
            (out / 'DesktopForcesServer.sh').write_text('run')
            _, lists = make(root, '..\\DesktopForces\\Content\\Maps\\mf-Tropics\n')
            for p in (root / 'Paks').iterdir():
                p.rename(paks / p.name)
            made = package.server_archives(out, paks, lists, {w: root / w for w in ('release', 'team', 'classic', 'full')})
            self.assertEqual([p.name for p in made], ['release.tar.gz', 'team.tar.gz', 'classic.tar.gz', 'full.tar.gz'])
            names = {}
            for p in made:
                with tarfile.open(p) as t:
                    names[p.name] = t.getnames()
            self.assertIn('LinuxServer-Shipping/DesktopForcesServer.sh', names['release.tar.gz'])
            self.assertEqual(chunks(names['release.tar.gz']), ['pakchunk0'])
            self.assertEqual(chunks(names['team.tar.gz']), ['pakchunk1', 'pakchunk2', 'pakchunk3'])
            self.assertTrue(all(n.startswith('LinuxServer-Shipping/DesktopForces/Content/Paks/') for n in names['team.tar.gz']))
            self.assertEqual(chunks(names['classic.tar.gz']), ['pakchunk3'])
            self.assertEqual(chunks(names['full.tar.gz']), ['pakchunk0', 'pakchunk1', 'pakchunk2', 'pakchunk3'])
            self.assertEqual(len(list(paks.iterdir())), 13)  # the server folder keeps every pack


if __name__ == '__main__':
    unittest.main()
