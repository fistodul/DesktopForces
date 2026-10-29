# Licence

The code, tools and documentation in this repository are licensed under the
**Mozilla Public License 2.0** — full text in [LICENSE](LICENSE).

In short (the licence text is what counts):
- Anyone may use, change and distribute the code, also commercially and in closed products.
- Changes to the **files** covered by the MPL must stay available as source under the MPL.
  New files you add, assets, and the rest of a larger product can use any licence.
- No relinking requirement, so it combines with Unreal Engine (Epic Games EULA) without an
  exception.

This notice covers every file in the repository unless the file says otherwise (MPL
Exhibit A allows a top-level notice). Source files may also carry the header:

    // This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
    // If a copy of the MPL was not distributed with this file, You can obtain one at
    // https://mozilla.org/MPL/2.0/.

Contributions are accepted under the same licence (inbound = outbound), signed off under the
[Developer Certificate of Origin](DCO): see [CONTRIBUTING.md](CONTRIBUTING.md).

## What is *not* covered

- **Original Mobile Forces data** (maps, textures, sounds, music, models) is © Rage Software /
  its successors. It is not in this repository; `Tools/mfport` converts it from your own copy.
- **Mods** ported into the game (`Source/DesktopForces/Game/`, `Weapons/`, `Vehicles/`) are
  under the MPL as well, with their authors' agreement ([CREDITS.md](CREDITS.md)); the original
  UnrealScript mods keep their own licences.
- **Override assets** (`Tools/mfport/overrides/`) carry their own licences, listed in
  `Tools/mfport/overrides/CREDITS.md`.
- Unreal Engine itself is under the Epic Games EULA.
