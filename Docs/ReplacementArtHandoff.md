# Replacement art handoff — 2026-10-30

## Start here

Continue **roadmap thread 9: replacement art for borrowed classic assets**. Read `AGENTS.md`,
this document, and the Overrides section of `Docs/Pipeline.md`. The aim is to make community
maps usable without the retail Classic pack, while preserving recognizable surfaces, holes,
signs, gameplay silhouettes, and the inexpensive rendering path.

The user approved investigating selected Starter Content and free assets. Work in small,
reviewable batches and commit accepted changes. Do not bulk-import Starter Content, reimport
all maps, or revive the removed animation-budget experiment. Keep user-facing replies terse.

**State at handoff:** the working tree was clean before this document. No Starter Content
replacement from this investigation remains applied. The concrete trial was fully reverted.
All 55 previously queued maps were already imported and baked; that batch is finished.

## Locations and constraints

| Item | Location |
|---|---|
| Project | `/home/mrs/Documents/Unreal Projects/DesktopForces` |
| Physical project path | `/media/mrs/OS/Users/filip/DesktopForces` (the project above is a symlink) |
| Engine | `/home/mrs/Linux_Unreal_Engine_5.8.4` |
| Supplied Starter Content | `/home/mrs/Documents/Unreal Projects/StarterContent` |
| Evidence and reusable exports | `/home/mrs/Documents/Unreal Projects/tests/20261029-systems-review` |
| Converted originals/manifests | `Tools/mfport/work/out/` inside the project |

- New replacement art belongs in the **community pack**, normally `/Game/MF/Replacements`.
- Texture dimensions must be **at most 512 px**. Preserve aspect ratio, alpha and original UV
  scale. Check mobile cooked limits separately.
- Start with base-colour textures through existing simple materials. Do not import full
  displacement/PBR graphs just to replace a surface.
- Coordinate Unreal execution with other agents. Builds share locks; another editor/commandlet
  can disrupt Zen during a cook. The previous agent released the lane, but check new activity.
- Use absolute evidence paths outside the project. Relative `../tests` can resolve differently
  through the Windows-drive symlink. Temporary `/tmp` projects/scripts may no longer exist.
- The user removed `CHANGELOG.md`; use normal commits and relevant docs rather than recreating it.

## What is implemented already

Commit `1e80518` added selective texture staging:

```bash
python3 Tools/mfport/mfport.py replacements /absolute/path/selection.json
```

Each JSON entry supplies `target`, `source`, `source_url`, and `license`. Source paths are
relative to the selection JSON. The target must exist in `work/out/textures.json`; staging
resolves its canonical spelling. Implementation: `Tools/mfport/mflib/replacements.py`.

Staging caps the longest side at `min(512, max_texture_size)`, preserves alpha/aspect ratio,
records source SHA256 and provenance, and does not rewrite identical PNGs. It rejects duplicate
or invalid targets and opaque replacements for originals with transparent pixels. It validates
all entries before writing. This alpha check does **not** prove that hole shapes match.

Source precedence in `mflib/textures.py`:

1. `Tools/mfport/overrides/textures/` — committed, redistributable source replacements.
2. `Tools/mfport/overrides/licensed/` — ignored local acquired sources and `provenance.json`.
3. `Tools/mfport/work/generated/textures/` — procedural output from tracked generator recipes.

The source/import relationship is deliberate:

```text
overrides/...png → work/out/Replacements/...png → Content/MF/Replacements/...uasset
```

`overrides` is a durable input layer, **not a scratch folder**. Files there affect the next
conversion. `Content` holds Unreal's usable assets. A direct Content import can test a candidate,
but accepted art needs a reproducible source/mapping and provenance so future imports retain it.
There are currently no accepted hand-made overrides listed in `overrides/CREDITS.md`.

The importer already skips unchanged texture source hashes/settings (`e296d89`). Do not delete
assets merely because hashes match, or accept broad re-save churn as part of a small art edit.

## Candidates already examined — do not repeat these blindly

### Concrete: one tested mapping rejected, both source textures reusable

| Candidate | Work actually done | Result / reuse |
|---|---|---|
| `T_Concrete_Poured_D` (2048² source) | Exported, reduced to 512², imported temporarily as `rage_warehouse.concrete.ware_concrete_9`, and compared in-scene | **Reject this mapping only.** Keep the source for a genuinely plain poured-concrete surface. |
| `T_Concrete_Grime_D` (1024² source) | Exported and included in the visual contact sheet | **Not imported or tested against a target.** Available for a stained/plain concrete candidate. |

The tested original, `rage_warehouse.concrete.ware_concrete_9`, is an opaque 256² blue-grey
**slab/tile floor with visible joints**. Despite the package name, the comparison was in the
community map **`MF-RandomLand`**, not the retail warehouse map. Poured concrete erased the
slab lines and changed the appearance too much. A slab/tile replacement is needed here.

The temporary assets were:

- `Content/MF/Replacements/Textures/rage_warehouse/concrete/ware_concrete_9.uasset`
- `Content/MF/Community/Materials/rage_warehouse/concrete/MI_ware_concrete_9_opaque.uasset`

The new texture was removed. The material was restored byte-for-byte to HEAD. No active
licensed PNG override or global converted-manifest replacement remains for this target.
Commit `2186507` records the rejection and the later test-map repair.

Reusable evidence under the evidence directory:

- `concrete-before.png`, `concrete-after.png`: same-camera scene comparison.
- `concrete-before-command.json`, `concrete-after-command.json`: exact launch arguments.
- `starter/T_Concrete_Poured_D.tga`, `starter/T_Concrete_Grime_D.tga`: source exports.
- `starter/staged-concrete-512.png`: the actual 512² trial image.
- `starter-selection.json`: historical trial selection, **not an approved mapping**.
- `selected-import/`: focused manifests, original/replacement PNGs, original material backup,
  and `ware_concrete_9.rejected.uasset`. Do not reapply this directory as approved work.

The proven scene camera was `DFCam 0 -1000 1400 90 -45` in `MF-RandomLand`. Example:

```bash
python3 Tools/play.py MF-RandomLand dm 0 -RenderOffscreen -nosound -unattended -ResX=1024 -ResY=576 '-ExecCmds=t.MaxFPS 60,r.VSync 0' '-DFExec=@2:DFCam 0 -1000 1400 90 -45|@6:DFShot candidate-check' -DFQuitAfter=9
```

### Other inspected originals and traps

| Original key | Finding | Decision so far |
|---|---|---|
| `rage_urban.brck07S` | 256², real alpha holes; used by `mf-Sardine_City` and `mf-City_Rush` | Opaque Starter brick would erase holes. No scene import was attempted. |
| `rage_dock.ground.dock_ground_7` | 256² masked **metal grate**, despite its name; referenced by space-station/test-facility/warship maps | Do not substitute gravel. Preserve openings. |
| `rage_urban.stuc01S` | 256² opaque speckled stucco, referenced by `MF-RandomLand` | Inspected as an original; no replacement selected or tested. |

`texture-review.png` combines nine Starter candidates with these inspected originals.

### Complete reusable export inventory

All nine were exported as TGA, with PNG previews under `starter/`. `starter/inventory.json`
records source sizes. Apart from Grime (1024²), all sources are 2048²:

- `T_Brick_Clay_Old_D`, `T_Brick_Clay_New_D`
- `T_Concrete_Grime_D`, `T_Concrete_Poured_D`
- `T_Ground_Gravel_D`
- `T_Wood_Pine_D`, `T_Wood_Floor_Walnut_D`
- `T_Metal_Steel_D`, `T_Rock_Basalt_D`

Only the poured-concrete mapping above reached an in-game before/after comparison. The other
exports are an inventory, not validated replacements. Do not reject these sources globally
because they failed to match one original.

## Remaining asset inventory and leads

`missing-community-textures.json` maps 34 unreplaced texture keys to referring maps. It is a
snapshot from converted manifests, includes engine placeholders, and is **not** a count of all
remaining cooked Classic dependencies. Roadmap thread 9 separately records about 300 classic
packages. Refresh the dependency report before choosing a batch:

```bash
python3 Tools/mfport/mfport.py packs
```

Canonical key case comes from `work/out/textures.json`; the missing-texture inventory uses
lowercase keys. Check visible material references before replacing editor symbols/defaults.
Many unresolved entries are grates, foliage, signs, vehicle skins or sky faces; generic surface
textures cannot replace their meaning or shape indiscriminately.

Useful local leads, **not yet auditioned/validated**:

- Audio: `Starter_Wind05/06`, `Starter_Birds01`, `Fire01`, `Fire_Sparks01`, `Steam01`,
  `Explosion01/02`, `Collapse01/02` in `StarterContent/Audio`.
- Props: `SM_Shelf`, `SM_Chair`, `SM_Door`, lamps and `SM_Rock` in `StarterContent/Props`.
  Check silhouette, dimensions, collision, triangle count and material cost before substitution.
- Plain opaque wood/steel/gravel/basalt surfaces using the already exported textures.

**Audio pipeline caveat:** `mflib/sounds.py` supports WAV overrides and writes them back under
`work/out/Sounds/<package>/`. The new `replacements` command is texture-only. Community routing,
reference replacement and Classic-pack independence for sound overrides have not been validated.
Audit those before assuming that swapping a WAV removes a Classic dependency. Sound overrides
also currently depend on enumerating the original package's exported sounds.

Fab research found Quixel's [Concrete Floor](https://www.fab.com/listings/f4b85cb0-9e15-42b5-997e-604cd09cbcd0)
listing free on 2026-10-29, with a base-colour texture. **Nothing was acquired or downloaded.**
Recheck price, source availability and acquired terms. It may suit plain concrete; it has not
been matched to the slab target. Do not assume all Megascans assets are free. Keep acquired raw
art local/private where required; do not publish standalone licensed sources with GitHub code.

## Next work, in order

1. Check HEAD, status, `AGENTS.md`, and whether another agent owns the Unreal execution lane.
2. Reuse the exports/contact sheet. Refresh missing dependencies and select a small batch with
   exact target keys, referring maps, source provenance, and screenshots of the originals.
3. Either audition ambient audio first, or start with one genuinely plain opaque surface.
   Audio needs the routing audit above. Keep the slab target open until a suitable slab source
   exists; do not retry poured concrete against it unchanged.
4. Stage selected image sources through `replacements`; inspect the resized result and alpha.
   Preserve original `usize`/`vsize`, which govern the converted UV scale.
5. Import only the selected assets and repoint the relevant community materials. The regular
   `textures` and `import --steps textures` commands can process a broad manifest: inspect the
   scope before running. The earlier focused import used a separate `-work` directory containing
   one texture entry, matching `packs.json`, source PNGs and **empty `fonts.json` (`{}`)**.
   Omitting `fonts.json` logged an error even though the texture was imported.
6. Compare a fixed camera before/after, then inspect close range, tiling, movement/mips and
   masking. Verify actual texture size, material references and community pack ownership.
   Verify the community map works without Classic dependencies through a controlled build/test;
   do not delete the user's Classic installation to simulate its absence.
7. Accept only reviewed mappings. Keep their source recipe/provenance, update credits where
   appropriate, and commit narrowly. Otherwise restore only that trial's changes and document
   why the mapping failed. Texture-only substitutions normally need no lighting rebake;
   geometry/collision replacements require additional checks and may require a rebake.

## Finished adjacent work — avoid restarting it

StateTree is disabled; existing bots retain Unreal navigation and staggered 5 Hz decisions.
Animation budgeting and temporary CSV scopes were deliberately removed by the user. Vehicle
Project builds/runs; no evidence justified migrating DF vehicle physics wholesale. Prop
instancing remains a separate measured-prototype task, not part of this art handoff.

`MF_TestRange` lost gameplay actors in an older resave. It is now regenerated and rebaked;
`DesktopForces.Maps.TestRange.TeamStarts` verifies starts, both teams and navigation. The final
current-code run passed six native suites and 27 Python tests. Vehicle checks passed at
30/60/120 FPS. These are baseline results, not validation of any new replacement art.

Both old stash entries were dropped after creating verified recovery bundles under the
evidence directory's `stash-backups/`; `README.txt` explains recovery. They contain rejected
art/removed experiments and old import re-save churn. Do not restore them wholesale.

Suggested new-conversation prompt:

> Read AGENTS.md and Docs/ReplacementArtHandoff.md. Continue roadmap thread 9 using the existing
> evidence and exports. Choose a small, suitable replacement batch, validate it in-scene, and
> commit accepted changes. Keep textures ≤512 px and replacements in the community pack.
> Do not restore animation budgeting or repeat the rejected concrete-to-slab mapping.
