"""Regression checks for shared Linux/Windows checkout configuration and launch commands."""
import importlib.util
import json
import os
from pathlib import Path
import sys
import tempfile
import unittest
from unittest.mock import patch

TOOLS = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(TOOLS))
from mfport.mflib import config
import play
spec = importlib.util.spec_from_file_location('df_setup', TOOLS / 'setup.py')
setup = importlib.util.module_from_spec(spec)
spec.loader.exec_module(setup)


class ToolsTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.root = Path(self.tmp.name)
        self.project = self.root / 'project with spaces' / 'DesktopForces.uproject'
        self.project.parent.mkdir()
        self.project.write_text('{"EngineAssociation":"5.8"}')
        self.file = self.root / 'mfport.toml'
        self.cfg = config.Config(self.root, self.root, self.root / 'engine', self.project, 'wine', 512)

    def load(self, text, env=None):
        self.file.write_text(text, encoding='utf-8')
        with patch.object(config, 'TOOL_DIR', self.root), patch.dict(os.environ, env or {}, clear=True), patch.object(config.platform, 'system', return_value='Linux'):
            return config.load()

    def test_platform_and_environment_precedence(self):
        data = 'ue_root="shared"\ngame_dir="game"\n[windows]\nue_root="C:/WindowsEngine"\n[linux]\nue_root="linux"\n'
        self.assertEqual(self.load(data).ue_root, self.root / 'linux')
        self.assertEqual(self.load(data, {'MFPORT_UE_ROOT': 'pipeline'}).ue_root, self.root / 'pipeline')
        self.assertEqual(self.load(data, {'MFPORT_UE_ROOT': 'pipeline', 'UE_ROOT': 'launcher'}).ue_root, self.root / 'launcher')

    def test_invalid_toml_has_actionable_error(self):
        with self.assertRaisesRegex(ValueError, 'single-quoted Windows paths'):
            self.load('ue_root = "C:\\Program Files\\UE"')

    def test_foreign_path_is_not_treated_as_relative(self):
        with self.assertRaisesRegex(ValueError, r'\[linux\] override'):
            self.load("ue_root = 'C:\\Program Files\\UE'\n")

    def test_relative_paths_are_relative_to_config_not_cwd(self):
        self.assertEqual(self.load('ue_root="engine"\nwork_dir="out"').work_dir, self.root / 'out')

    def test_newest_compatible_patch_uses_version_file(self):
        for name, version in [('Linux_Unreal_Engine_5.8.3', (5, 8, 3)), ('Linux_Unreal_Engine_5.8.4', (5, 8, 4)), ('Linux_Unreal_Engine_5.8.10', (5, 8, 10)), ('Linux_Unreal_Engine_5.9', (5, 9, 0))]:
            engine = self.root / name / 'Engine'
            (engine / 'Build').mkdir(parents=True)
            (engine / 'Build/Build.version').write_text(json.dumps(dict(zip(('MajorVersion', 'MinorVersion', 'PatchVersion'), version))))
            (engine / 'Binaries/Linux').mkdir(parents=True)
            (engine / 'Binaries/Linux/UnrealEditor').touch()
        with patch.object(config.Path, 'home', return_value=self.root), patch.object(config.platform, 'system', return_value='Linux'):
            self.assertEqual(config.discover_engine(self.project).name, 'Linux_Unreal_Engine_5.8.10')

    def test_setup_serializes_windows_paths_and_quotes(self):
        game = 'C:\\Games\\Filip\'s "Mobile Forces"'
        engine = 'D:\\Epic Games\\UE_5.8'
        with patch.object(setup, 'TOML_USER', self.file), patch.object(setup.platform, 'system', return_value='Windows'):
            setup.create_config(game, engine)
        data = config.tomllib.loads(self.file.read_text())
        self.assertEqual(data['windows']['game_dir'], game)
        self.assertEqual(data['windows']['ue_root'], engine)
        self.assertNotIn('ue_root', data)
        self.assertNotIn('game_dir', data)
        self.assertEqual(data['linux'], config.tomllib.loads(setup.TOML_EXAMPLE.read_text())['linux'])

    def test_linux_defaults_and_flags_without_positionals(self):
        args = play.command(self.cfg, ['-nosound'], 'Linux')
        self.assertTrue(args[0].endswith('/Linux/UnrealEditor'))
        self.assertEqual(args[2], '/MobileForcesClassic/Maps/mf-warehouse?game=dm?Bots=0')
        self.assertEqual(args[-1], '-nosound')

    def test_windows_editor_and_build(self):
        args = play.command(self.cfg, ['editor', '-nosound'], 'Windows')
        self.assertTrue(args[0].endswith('/Win64/UnrealEditor.exe'))
        self.assertEqual(args[1], str(self.project))
        self.assertEqual(args[-1], '-nosound')
        ubt = self.cfg.ue_root / 'Engine/Binaries/DotNET/UnrealBuildTool/UnrealBuildTool.dll'
        dotnet = self.cfg.ue_root / 'Engine/Binaries/ThirdParty/DotNet/10.0/win-x64/dotnet.exe'
        for binary in (ubt, dotnet):
            binary.parent.mkdir(parents=True, exist_ok=True)
            binary.touch()
        with patch.dict(os.environ, {}, clear=True):
            args = play.command(self.cfg, ['build', '-MaxParallelActions=4'], 'Windows')
        self.assertEqual(args[:2], [str(dotnet), str(ubt)])
        self.assertEqual(args[3], 'Win64')
        self.assertEqual(args[-1], '-MaxParallelActions=4')

    def test_map_paths_and_extra_url_options(self):
        with patch.dict(os.environ, {'DF_OPTS': '?TimeLimit=5'}):
            args = play.command(self.cfg, ['MF_TestRange', 'tdm', '8', '-nosound'], 'Linux')
        self.assertEqual(args[2], '/Game/MF/Maps/MF_TestRange?game=tdm?Bots=8?TimeLimit=5')
        args = play.command(self.cfg, ['/Game/Maps/Example'], 'Linux')
        self.assertTrue(args[2].startswith('/Game/Maps/Example?game=dm?Bots=0'))


if __name__ == '__main__':
    unittest.main()
