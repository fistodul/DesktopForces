# Mobile Forces Classic (content plugin)

Everything here is **generated on your machine** from your own Mobile Forces install by
`Tools/mfport` and is never committed (only this README and the `.uplugin` are):

| Folder | What |
|---|---|
| `Content/Maps/<map>`, `<map>_Data` | stock maps and community maps not cleared yet (`Tools/mfport/packs.toml`) |
| `Content/Textures`, `Materials` | the original textures (and their materials) |
| `Content/Sounds`, `Music` | the original sounds and music |
| `Content/Fonts`, `Props` | HUD fonts; vehicles, crates, safes, weapon models |

The game code reaches it through `MF::LoadClassic()` (`Source/DesktopForces/Core/MFContent.h`)
and copes when it is missing: no sounds, simple HUD, placeholder meshes.

Building Desktop Forces without it: disable the plugin (`DesktopForces.uproject`, or the
editor's Plugins window). Community maps cleared for the game live in `/Game/Maps` and are
listed in `packs.toml`; until their textures get replacement art they still pull a few
textures from here (`python3 Tools/mfport/mfport.py packs` lists them).
