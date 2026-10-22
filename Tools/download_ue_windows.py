#!/usr/bin/env python3
"""
Downloads the official Windows Unreal Engine 5.8 binary release directly from Epic Games CDN
onto Linux or directly into a mounted Windows NTFS partition.

Uses the underlying Epic Games manifest & chunk streaming protocol from legendary-gl.
"""

import codecs
import os
import shutil
import sys
import json
import glob
import argparse
import tempfile
import requests

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import winpaths  # noqa: E402  (finds the Windows partition and user)

# legendary-gl installed with pipx lives in its own venv: use its packages
for _site in glob.glob(os.path.expanduser("~/.local/share/pipx/venvs/legendary-gl/lib/python3*/site-packages")):
    if _site not in sys.path:
        sys.path.insert(0, _site)

try:
    from legendary.core import LegendaryCore
    from legendary.models.game import Game
except ImportError:
    print("Error: legendary-gl not found. Please install it with: pipx install legendary-gl")
    sys.exit(1)

def _engine_version() -> str:
    """The project's EngineAssociation ("5.8")."""
    uproject = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), "DesktopForces.uproject")
    try:
        with open(uproject, encoding="utf-8") as f:
            return json.load(f).get("EngineAssociation", "5.8")
    except (OSError, ValueError):
        return "5.8"


UE_NAMESPACE = "ue"
UE_CATALOG_ID = "18b3b415bc434c5b974f50488360ca31"
UE_APP_NAME = f"UE_{_engine_version()}"


def register_in_windows_launcher(install_dir: str, build_version: str, dat_path: str):
    """Registers the installed engine in Epic Games Launcher's LauncherInstalled.dat so Windows recognizes it."""
    if not os.path.exists(os.path.dirname(dat_path)):
        os.makedirs(os.path.dirname(dat_path), exist_ok=True)

    # the Windows spelling of the place, if it is on the Windows partition
    try:
        win_install_dir = winpaths.to_windows(install_dir)
    except (ValueError, SystemExit):
        win_install_dir = install_dir

    install_data = {
        "InstallationList": []
    }

    # the launcher's own list of every engine and game it installed: read it in whatever encoding
    # it has (UTF-8, with or without BOM, or UTF-16), never write over one that doesn't parse
    encoding = "utf-8"
    if os.path.exists(dat_path):
        raw = open(dat_path, "rb").read()
        if raw.startswith((codecs.BOM_UTF16_LE, codecs.BOM_UTF16_BE)):
            encoding = "utf-16"
        elif raw.startswith(codecs.BOM_UTF8):
            encoding = "utf-8-sig"
        try:
            install_data = json.loads(raw.decode(encoding))
            if not isinstance(install_data.get("InstallationList"), list):
                raise ValueError("no InstallationList")
        except (UnicodeDecodeError, ValueError) as e:
            sys.exit(f"{dat_path} couldn't be read ({e}): left as it is. The engine is downloaded; "
                     f"register it by starting the Epic Games Launcher on Windows (or pass -UePath to build_windows.ps1).")
        shutil.copy2(dat_path, dat_path + ".bak")

    # Remove existing entry for UE_5.8 if present
    install_list = [item for item in install_data.get("InstallationList", []) if item.get("AppName") != UE_APP_NAME]

    new_entry = {
        "InstallLocation": win_install_dir,
        "NamespaceId": UE_NAMESPACE,
        "ItemId": UE_CATALOG_ID,
        "ArtifactId": UE_APP_NAME,
        "AppVersion": build_version,
        "AppName": UE_APP_NAME
    }
    install_list.append(new_entry)
    install_data["InstallationList"] = install_list

    tmp = dat_path + ".tmp"
    with open(tmp, "w", encoding=encoding) as f:
        json.dump(install_data, f, indent=2)
    os.replace(tmp, dat_path)

    print(f"[+] Registered {UE_APP_NAME} in Windows LauncherInstalled.dat: {win_install_dir}")


def main():
    parser = argparse.ArgumentParser(description=f"Download the {UE_APP_NAME} Windows binaries directly from Epic's CDN.")
    parser.add_argument("--target-dir", default=None,
                        help=f"Target directory (default: <Windows partition>/Program Files/Epic Games/{UE_APP_NAME})")
    parser.add_argument("--include-symbols", action="store_true",
                        help="Also download debug symbols (+53.8 GB disk, +13.8 GB download). Default is False.")
    parser.add_argument("--max-workers", type=int, default=8,
                        help="Number of concurrent download worker processes (default: 8)")
    parser.add_argument("--dry-run", action="store_true",
                        help="Inspect manifest and sizes without downloading files")
    args = parser.parse_args()
    if not args.target_dir:
        args.target_dir = str(winpaths.root() / "Program Files" / "Epic Games" / UE_APP_NAME)
    launcher_dat = str(winpaths.launcher_dat())

    print("[*] Initializing Legendary API session...")
    core = LegendaryCore()
    if not core.login():
        print("[-] Login failed. Run 'legendary auth' first.")
        sys.exit(1)

    print(f"[*] Fetching release manifest metadata for {UE_APP_NAME} ({UE_NAMESPACE})...")
    manifest_meta = core.egs.get_game_manifest(UE_NAMESPACE, UE_CATALOG_ID, UE_APP_NAME, platform="Windows")

    elements = manifest_meta.get("elements", [])
    if not elements:
        print("[-] No manifest elements returned by Epic Games backend.")
        sys.exit(1)

    element = elements[0]
    build_version = element.get("buildVersion", "5.8.4")
    manifest_info = element.get("manifests", [])[0]
    manifest_uri = manifest_info["uri"]
    query_params = manifest_info.get("queryParams", [])
    if query_params:
        manifest_uri += "?" + "&".join(f"{p['name']}={p['value']}" for p in query_params)

    # Base URL for chunk downloads
    base_url = manifest_info["uri"].rsplit("/", 1)[0]

    print(f"[+] Found engine build: {build_version}")
    print(f"[*] Fetching full binary manifest from CDN...")
    manifest_path = os.path.join(tempfile.gettempdir(), f"{UE_APP_NAME}.manifest")
    resp = requests.get(manifest_uri, timeout=30)
    resp.raise_for_status()
    with open(manifest_path, "wb") as f:
        f.write(resp.content)
    print(f"[+] Saved manifest to {manifest_path} ({len(resp.content) / (1024 * 1024):.2f} MB)")

    # Prepare download tags
    install_tags = [""]
    if args.include_symbols:
        install_tags.append("editor_symbols")

    game = Game(app_name=UE_APP_NAME, app_title=f"Unreal Engine {UE_APP_NAME[3:]}")

    # Parent directory for base_path, folder name will be UE_5.8
    target_abs = os.path.abspath(args.target_dir)
    base_path = os.path.dirname(target_abs)
    game_folder = os.path.basename(target_abs)

    print(f"[*] Analyzing download packages (target: {target_abs})...")
    dlm, analysis, _ = core.prepare_download(
        game=game,
        base_path=base_path,
        game_folder=game_folder,
        override_manifest=manifest_path,
        override_base_url=base_url,
        file_install_tag=install_tags,
        max_workers=args.max_workers,
    )

    print("\n" + "=" * 50)
    print(f" Build Version:  {build_version}")
    print(f" Download Size:  {analysis.dl_size / (1024**3):.2f} GB")
    print(f" Install Size:   {analysis.install_size / (1024**3):.2f} GB")
    print(f" Target Path:    {target_abs}")
    print("=" * 50 + "\n")

    if args.dry_run:
        print("[*] Dry run requested. Exiting without downloading.")
        return

    print(f"[*] Starting download with {args.max_workers} worker threads directly to Windows drive...")
    dlm.start()
    dlm.join()

    print("\n[+] Download and verification completed successfully!")

    # Register in Windows Epic Games Launcher configuration
    if os.path.exists(os.path.dirname(launcher_dat)):
        register_in_windows_launcher(target_abs, build_version, launcher_dat)
    else:
        print(f"[*] Note: {launcher_dat} not accessible from Linux; engine is placed at {target_abs}.")


if __name__ == "__main__":
    main()
