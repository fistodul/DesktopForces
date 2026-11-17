# Updating releases and installing mods

Investigation, 2026-11-17. The current downloader already reuses unchanged downloaded files
across releases. The missing part is updating the executable and core installation. A `Mods/`
loader and a repeatable mod-authoring release are separate, unfinished work.

## What the build match actually means

There are three different identities:

| Identity | Current use | Effect on updates |
|---|---|---|
| `ProjectVersion` | `Tools/package.py` stamps the commit date; Unreal includes it in its network-version checksum | Client/server compatibility; not a download-cache key |
| `ContentBuildId` | Timestamp + commit, baked into the core; chooses `<server>/<Platform>/<id>/BuildManifest-<Platform>.txt` | Selects the set of files approved for that core build |
| Per-file version | `SHA1:<file hash>` written by `package.manifest_line` | Same filename and hash reuse the cached file, even when the build ID changes |

Verified against `Source/DesktopForces/Core/DFDownloads.cpp` and the installed UE 5.8.4
`Engine/Plugins/Runtime/ChunkDownloader/Source/Private/ChunkDownloader.cpp`:

1. `Initialize` loads `LocalManifest.txt` from `ProjectPersistentDownloadDir/PakCache`.
2. DF loads the cached build but mounts its chunks immediately only if its build ID matches
   the core. A new core asks for its own manifest.
3. `TryLoadBuildManifest` requires `$BUILD_ID` to match that request.
4. `LoadManifest` looks up old files by filename and retains an entry when `FileVersion`
   matches; size must match too. The new URL/build folder can be different. Changed or removed
   entries are removed from the cache; unchanged entries retain their cached state.
5. DF mounts complete cached chunks from the accepted manifest.

Thus a new build ID does **not** mean downloading everything again. It does mean obtaining
that build's manifest before reusing packs. A first launch after an update may need the
network even when every optional pack's bytes are already on disk. Preserve the cache and
its `LocalManifest.txt` and `CachedBuildManifest.txt` when replacing the core. On generic
desktop platforms Unreal derives this directory from `ProjectSavedDir`; don't assume that
unpacking a new installation elsewhere preserves it. Do not uninstall an Android app to
update its APK if the intent is to preserve its app data.

This is a code-path verification, not a new packaged-game cross-release integration test.
Only one current manifest per platform was available locally. The two older/newer Windows
URLs in the upload examples were inaccessible through the web tool, so no real-release
reuse percentage was measured.

## Server hardlinks and player downloads

`Tools/upload_downloads.sh` already uses `rsync -rlc --link-dest=...`. The option is
`--link-dest`, and it creates **hardlinks to matching files**, not symlinks to unmatched
ones. Two release directories can name the same underlying file while each has its own
build manifest. HTTP clients see bytes; they do not see or depend on the server's inode.
The [rsync manual](https://rsync.samba.org/ftp/rsync/rsync.1.html#opt--link-dest) describes this
behavior and the requirement that preserved attributes also agree.

A local experiment used same-size files with different contents: the unchanged file shared
its inode across releases, the changed file did not, and the old release remained intact.
Hardlinks save server disk space. The player's file-hash comparison separately saves download
bandwidth. Rsync's network delta algorithm is a third feature; ordinary HTTP downloads do not
inherit it just because the upload used rsync.

Identical output can be reused; it is too strong to promise that every unchanged source asset
produces an unchanged container on every cook. Engine, shader, serialization, compression or
container-layout changes can alter output. One edited asset can shift later content and change
several partitions. Measure the manifests of actual release pairs before choosing delta tools.

Current `MaxIoStorePartitionSizeMB=64` applies to the packaged container data. The locally
available November 9 manifests have these public optional packs:

| Platform | Community bytes / files | Unverified bytes / files | Largest `.ucas` part |
|---|---:|---:|---:|
| Windows | 959,930,345 / 18 | 1,185,072,937 / 22 | 64 MiB |
| Linux | 1,009,365,939 / 19 | 1,217,995,585 / 22 | 64 MiB |
| Android | 636,464,698 / 12 | 942,135,432 / 18 | 64 MiB |

This makes a changed partition a smaller replacement, but is not asset-level or binary-delta
patching. Keep companion `.pak`, `.utoc` and `.ucas` parts consistent with the chosen manifest.
Do not point an old core at `latest` optional packs to avoid the compatibility check.

## A small shared updater base

Filip's direction (2026-11-17): keep the existing MF-updater's straightforward checksum-file
workflow and style. The editable Python adapter **is the configuration**. It may calculate
URLs, choose a platform, check the installation or adapt an existing manifest. There should
be no MF/DF switch or game-specific functions in the shared updater.

A separate local base was prepared from the original MF-updater; the installed original was
not changed. It keeps `download_shasums`, `fix_case`, `check_hash`, `get_file` and print-style
progress, and exposes:

```python
update(url, root, skip_files=(), path_for_file=None,
       url_for_file=None, check_files_exist=None)
```

`url` can be a string or a function; the function is evaluated once per update. The optional
functions describe installation rules and adapt the existing `../Maps/...` manifest paths
and basename URLs. The default expects installation-relative paths and matching server paths.
This is a file updater, not a launcher, mod manager or game-specific release service.

```text
updater.py               shared SHA512 file-update loop, no game/platform branches
MF-updater.py            editable Python configuration for the existing MF layout
DF-updater.example.py    example platform URL function; no live endpoint assumed
bundle.py                inserts the core into either configuration for release
```

The contributor maintains the shared code once. `bundle.py` replaces one import marker with
that source; users receive one editable `.py`, with settings at the top and no runtime code
fetch. That generated script can also go through the existing Nuitka onefile workflow. The
[Nuitka manual](https://nuitka.net/user-documentation/user-manual.html) documents standalone
executables; compilation of this base was not tested here.

The small core streams hashes/downloads instead of reading whole files, checks downloaded
bytes before replacing a file, retains existing file modes and validates manifest paths. It
does nothing when imported. Seven temporary-file tests passed: callable URL and unchanged-file
reuse, MF path/URL/case handling, corrupt and interrupted downloads preserving the old file,
invalid paths rejected before updates, execute-mode preservation and standalone bundled import.
It is a starting point for Filip to adapt, not an installed replacement or final DF updater.

### No required latest.json

The earlier `latest.json` suggestion was optional release discovery, not an Unreal or updater
requirement. For example, the Python configuration can return
`<server>/DesktopForces/<platform>/`, where the publisher serves `sha512.txt` and the named files.
There is no need to move that logic into a declarative file.

If the server instead retains releases under unpredictable build IDs, a client needs some
published indication of the current one. That can be a redirect or a small text file, or
whatever the configuration function resolves. A locally calculated platform name alone cannot
discover an unpublished timestamped release ID. Resolve a consistent release once per run;
changing a mutable directory between fetching hashes and files can produce a checksum failure.
The base does not prescribe or deploy a server layout.

Discovering the newest uploaded manifest from a server directory listing is also a valid
resolver (Filip's suggestion). Use the selected platform's **complete release**, not simply
the newest optional-pack directory: the current `BuildManifest` omits the executable and core
chunk. A full-release `sha512.txt` can cover those files as well and be published last as the
ready marker. The configuration chooses that release once; its core's embedded content build
ID must select the matching optional-pack manifest. Matching files still skip by hash.

The tracked history of `upload_downloads.sh` has one introduction commit (`eeb4b9b7`), which
lists server build directories over SSH but takes the chosen build IDs explicitly. The earlier
automatic-selection version Filip recalls is not present in the available tracked history.
A player updater would need a public HTTP listing/index or redirect, rather than the uploader's
SSH access. None of these choices requires JSON.

An earlier staging-only JSON/channel prototype remains a local experiment; its eight fixture
tests proved file reuse and rsync hardlinks, but its architecture is not the selected direction.

## What to ship and host

If this is later used for DF binaries, one option is updating the **minimal/core installation**
while retaining the in-game optional-pack downloader. Host the changed executable, runtime libraries, core container
files and manifests as ordinary immutable release files. Binaries changing is expected:
the same hash rule skips unchanged libraries and fetches changed ones. Serving only a ZIP
requires downloading the whole ZIP; retain ZIPs for first installs, plus loose release files
for updates. The server can hardlink identical files between both core release directories
and optional-pack directories. Upload complete releases before moving the channel pointer.

Before production use, implement and test activation with the game stopped, rollback/retry
when replacement fails, Linux execute bits, and Windows locked executable/DLL handling. A
Nuitka updater cannot replace its own running Windows executable without a handoff. Hashes
verify bytes against a trusted manifest; HTTPS and a deliberate manifest-signing decision
belong to the binary-update design. Android updates should use the platform APK installer
(same app identity/signing identity), retaining the existing pack cache; desktop file
replacement is not an Android application update mechanism.

Maintain an inventory of files owned by the release. Remove obsolete **owned** files,
especially old container partitions, but never sweep `Saved/`, the cache or `Mods/`. Keep
player settings separate from authoritative binaries. A Full install also owns its bundled
optional packs: update those as a consistent set, or explicitly migrate them to the cache.
Simply overlaying a new core onto old Full/Classic paks is not a verified update path:
`DFDownloads::OnManifest` currently calls a chunk bundled by the presence of its `.pak`
filename, without checking the bundled file's hash against the new manifest.

Do not weaken that distinction by describing a build-ID match as a universal pak-file guard.
Manually copied paks can mount outside ChunkDownloader. Compatibility must be managed by the
installer/mod loader as well as the download manifest.

## Editable assets and a Mods folder

MF's `.umf`, `.utx`, `.uax` and `.u` were already packages, rather than one loose file per
texture or actor. Their package/editor workflow made them convenient to exchange and edit.
DF has two separate transformations:

```text
Editable .umap/.uasset + source art
    -> cook for a platform and compatible game classes/shaders
    -> package cooked output as .pak + .utoc + .ucas parts
```

With DF's IoStore settings, `.utoc` indexes the container and `.ucas` holds the bulk of cooked
package data; companion `.pak` files carry remaining filesystem content. These are not ZIPs
of editable source assets. Disabling packing does not reverse cooking. Epic documents limited
editor use of cooked assets, but they are read-only and cannot be opened in their normal asset
editors ([cooked-content documentation](https://dev.epicgames.com/documentation/unreal-engine/working-with-cooked-content-in-the-unreal-engine)).

| Distribution | Useful for | Cost or limit |
|---|---|---|
| Cooked containers | Normal players; fewer filesystem entries, compressed runtime data, platform-ready loading | Opaque to ordinary editors; a small edit may replace a large container part |
| Loose cooked files | Development/deployment experiments | Still not authoring sources; more filesystem overhead; not a drop-in replacement for this IoStore build |
| Uncooked project/plugin sources | Map makers editing meshes, textures, maps and Blueprint graphs | Editor/toolchain and larger source downloads; cook/bake before distributing to players |
| Loose JSON/INI/CSV | Human-editable settings and data that DF explicitly loads | Requires a defined runtime loader/schema; cannot describe arbitrary UE assets just by dropping files in a folder |

The current priority is straightforward contributions to the **core game** (Filip, 2026-11-17).
This repository already supplies the game/editor modules: contributors edit the ordinary
project's maps, assets and code, and the maintained pipeline includes accepted changes in the
next release. A mod loader or a separate SDK is not a prerequisite for contributing.

Later, an **authoring kit plus separate cooked mods** could reproduce the drag-and-drop
experience. A compiled map such as Source's `.bsp` does not prevent adding another map; neither
does a cooked UE map. The requirements are compatible dependencies and game-side discovery,
which DF has not yet implemented for arbitrary post-release mod roots. Creators could use a
versioned project/plugin template and export cooked content for each supported target.
Original MF content remains a local conversion from the creator's own copy. A source ZIP can
accompany an author's cooked mod so others can edit it without extracting cooked data.

For later mod-loader research, a possible player installation is:

```text
Mods/HarbourNight/
    mod.json                 id, version, platform, compatibility, dependencies, maps, hashes
    HarbourNight.pak
    HarbourNight.utoc
    HarbourNight.ucas
```

Use a distinct package root such as `/HarbourNight/`, and a stable mod/API compatibility
contract. An official cook ID need not be the permanent compatibility rule for independently
built mods, but **same UE version alone is insufficient**: native class layouts, referenced
base assets, shaders and cook settings matter. Initially support content/Blueprint mods
against a specified DF authoring release; prove compatibility before relaxing that bound.
Native C++ extensions in the current packaged game need a game build; dropping an arbitrary
new DLL is not equivalent to MF loading UnrealScript bytecode.

The current implementation is not yet that loader:

- UE 5.8.4 `FPakPlatformFile::GetPakFolders` scans project `Content/Paks`, `Saved/Paks` and
  engine paks. Its `-pakdir` argument is compiled out in Shipping; there is no DF `Mods/` scan.
- `DF::FindMaps` searches only the four built-in roots, with no recursive filter. A new plugin
  root and its map entries require discovery/registration. Existing download packs were
  cooked with the base asset registry, so their success does not prove post-release mods work.
- Mounting a `.pak` also attempts its companion IoStore container, but the mod still needs a
  usable mount point, asset registry/map metadata and any required shaders. Loaded Blueprint
  weapon classes must also be registered before DF caches the loadout list.

When mod support becomes a priority, the first vertical test should package one new map **after** the base release, mount
it from `Mods/`, list and load it in Shipping, then repeat with one cooked Blueprint item.
Test on each supported platform and on a dedicated server/client pair. Select enabled mods
before entering a match, make dependencies and conflicts explicit, and compare the relevant
mod versions between server and client. Arbitrary override order is not a compatibility policy.
The official updater must leave that directory alone.

This keeps convenient individual mod installs without giving up cooked loading. If real-release
measurements show excessive container churn, test per-map containers plus a shared-assets pack
or Unreal patch/DLC cooking next. More containers may improve update isolation, but also add
mount/manifest overhead and can duplicate shared assets without deliberate chunk ownership.
