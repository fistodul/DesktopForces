"""Exercise real Git indexes and rsync in disposable directories, never a real remote."""
import re
from pathlib import Path
import subprocess
import tempfile
import unittest

TOOLS = Path(__file__).resolve().parents[1]


class Git:
    def __init__(self, root):
        self.root = root

    def run(self, *args, data=None):
        return subprocess.run(
            ['git', *args], cwd=self.root, input=data, check=True,
            stdout=subprocess.PIPE, stderr=subprocess.PIPE,
        ).stdout

    def text(self, *args, **kwargs):
        return self.run(*args, **kwargs).decode().strip()


class PublicSyncTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.git = Git(self.root)
        self.git.run('init', '-b', 'azure')
        self.git.run('config', 'user.name', 'Sync test')
        self.git.run('config', 'user.email', 'sync@example.invalid')
        self.write('.gitignore.github', '/Content/Private/\n/Plugins/*/Content/\n')
        self.write('.gitignore', '/Content/Private/\n/Plugins/*/Content/\n')
        self.write('Source/example.cpp', 'base\n')
        self.base = self.commit('Base')
        self.git.run('update-ref', 'refs/remotes/github/main', self.base)

    def write(self, name, content):
        path = self.root / name
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(content)

    def commit(self, message):
        self.git.run('add', '-f', '.')
        self.git.run('commit', '-m', message)
        return self.git.text('rev-parse', 'HEAD')

    def export(self, source, target):
        self.git.run('update-ref', 'refs/remotes/github/main', target)
        result = subprocess.run(
            ['bash', str(TOOLS / 'sync_github.sh'), '--source', source],
            cwd=self.root, text=True, capture_output=True,
        )
        if result.returncode:
            raise RuntimeError(result.stderr)
        match = re.search(r'Prepared head: ([0-9a-f]+) \((\d+) commits\)', result.stdout)
        self.assertIsNotNone(match, result.stdout)
        return match[1], int(match[2])

    def test_mixed_commits_authorship_deletions_and_repeat(self):
        self.write('Source/example.cpp', 'changed\n')
        self.write('Content/Private/secret.uasset', 'private')
        self.write('Plugins/Classic/Content/private.uasset', 'private too')
        self.git.run('config', 'user.name', 'Original author')
        first = self.commit('Mixed change')
        self.write('Content/Private/secret.uasset', 'private version two')
        self.commit('Only content')
        (self.root / 'Source/example.cpp').unlink()
        last = self.commit('Remove source file')
        self.write('uncommitted.txt', 'keep this')
        before = self.git.run('status', '--porcelain')
        public, count = self.export(last, self.base)
        self.assertEqual(count, 2)
        self.assertEqual(self.git.text('log', '-1', '--format=%an', public), 'Original author')
        self.assertIn('DesktopForces-Source: ' + first, self.git.text('log', '--format=%B', public))
        names = self.git.text('ls-tree', '-r', '--name-only', public)
        self.assertNotIn('secret', names)
        self.assertNotIn('private.uasset', names)
        self.assertNotIn('example.cpp', names)
        # Excluded content must not be reachable through an earlier exported commit either.
        self.assertNotIn('secret.uasset', self.git.text('rev-list', '--objects', public))
        self.assertEqual(self.git.text('rev-parse', 'HEAD'), last)
        self.assertEqual(self.git.run('status', '--porcelain'), before)
        self.assertEqual(self.export(last, public), (public, 0))

    def test_legacy_cherry_pick_is_recognized_by_patch(self):
        self.write('Source/example.cpp', 'change\n')
        source = self.commit('Old source change')
        tree = self.git.text('rev-parse', f'{source}^{{tree}}')
        public = self.git.text('commit-tree', tree, '-p', self.base, data=b'Previously cherry-picked\n')
        self.assertEqual(self.export(source, public), (public, 0))

    def test_conflict_stops_without_touching_checkout_or_refs(self):
        self.write('Source/example.cpp', 'public edit\n')
        public = self.commit('Public edit')
        self.git.run('reset', '--hard', self.base)  # disposable fixture only
        self.write('Source/example.cpp', 'different source edit\n')
        source = self.commit('Source edit')
        self.git.run('update-ref', 'refs/remotes/github/main', public)
        before = self.git.run('show-ref')
        with self.assertRaisesRegex(RuntimeError, 'Conflict exporting'):
            self.export(source, public)
        self.assertEqual(self.git.run('show-ref'), before)
        self.assertEqual((self.root / 'Source/example.cpp').read_text(), 'different source edit\n')
        self.assertEqual(self.git.run('status', '--porcelain'), b'')

    def test_newly_allowed_file_is_exported(self):
        self.write('Content/Private/allowed.txt', 'now shareable')
        source = self.commit('Private addition')
        public, _ = self.export(source, self.base)
        self.write('.gitignore.github', '/Content/Private/*\n!/Content/Private/allowed.txt\n/Plugins/*/Content/\n')
        source = self.commit('Allow this file')
        public, _ = self.export(source, public)
        self.assertEqual(self.git.text('show', f'{public}:Content/Private/allowed.txt'), 'now shareable')


class FileSyncTests(unittest.TestCase):
    def test_preview_copy_delete_and_exclusions(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            source, target = root / 'source', root / 'target'
            source.mkdir()
            target.mkdir()
            (source / 'DesktopForces.uproject').write_text('{}')
            (source / 'README.md').write_text('new')
            (target / 'obsolete.txt').write_text('old')
            for base in (source, target):
                (base / 'Saved').mkdir()
                (base / 'Tools/mfport').mkdir(parents=True)
                (base / 'Tools/mfport/mfport.toml').write_text(base.name)
            (target / 'Saved/keep.log').write_text('keep')
            cmd = ['bash', str(TOOLS / 'sync_files.sh'), str(source), str(target)]

            def run(*options):
                return subprocess.run([*cmd, *options], check=True, capture_output=True)

            run('--dry-run', '--delete')
            self.assertFalse((target / 'README.md').exists())
            run()
            self.assertTrue((target / 'obsolete.txt').exists())
            run('--delete')
            self.assertFalse((target / 'obsolete.txt').exists())
            self.assertEqual((target / 'README.md').read_text(), 'new')
            self.assertEqual((target / 'Tools/mfport/mfport.toml').read_text(), 'target')
            self.assertTrue((target / 'Saved/keep.log').exists())
            nested = subprocess.run(['bash', str(TOOLS / 'sync_files.sh'), str(source), str(source / 'copy')], capture_output=True)
            self.assertNotEqual(nested.returncode, 0)


if __name__ == '__main__':
    unittest.main()
