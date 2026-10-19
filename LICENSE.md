# Licence

The code, tools and documentation in this repository are licensed under the
**GNU Lesser General Public License v3.0 or later** — see [COPYING.LESSER](COPYING.LESSER)
(which builds on the GNU GPL v3, [COPYING](COPYING)).

## Additional permission: Unreal Engine

> **Draft — to be confirmed by the copyright holders before the repository goes public.**

The LGPL requires that people receiving a binary can relink it with a modified version of
the LGPL code. Unreal Engine shipping builds link game modules statically with the engine,
whose code may only be shared under the Epic Games EULA. To avoid that conflict the
project grants, under section 7 of the GNU GPL v3:

> As an additional permission, the copyright holders of this project give you permission
> to combine or link this work with the Unreal® Engine (or a modified version of it),
> including the Unreal Engine's own libraries and object code, and to convey the resulting
> work, without the Unreal Engine parts being subject to the terms of the GNU (L)GPL.
> The source code of this project itself remains covered by the LGPL v3+.

## What is *not* covered

- **Original Mobile Forces data** (maps, textures, sounds, music, models) is © Rage Software /
  its successors. It is not in this repository; `Tools/mfport` converts it from your own copy.
- **Mods** under `Plugins/Mods/` keep their authors' licences (listed in each plugin's
  `.uplugin` "CreatedBy" and README). The MF-mods ports are MIT.
- **Override assets** (`Tools/mfport/overrides/`) carry their own licences, listed in
  `Tools/mfport/overrides/CREDITS.md`.
- Unreal Engine itself is under the Epic Games EULA.
