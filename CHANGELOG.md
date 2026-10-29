# Changelog

## 2026-10-29 — Selected replacement textures and systems review

- Add `mfport.py replacements <selection.json>` to stage chosen local source images at no
  more than 512 px. Keep provenance and acquired images outside source control, preserve
  aspect ratio/alpha, and reject duplicate targets or opaque replacements for transparent art.
- Document the StateTree, animation allocator and Chaos Vehicle comparison, concrete repeated
  prop candidates, and selective Starter/Fab workflow for the community replacement pack.
- Keep runtime/plugin changes and asset application pending coordinated Unreal validation.
  The concrete sample passed offline staging only; no game assets changed.
- Validation: 25 Python tests pass, including six replacement staging checks.

## 2026-10-29 — Downloadable packs, pack reshuffle, one packager

- In-game downloads of content packs (menu > Downloads, ChunkDownloader): a `--downloads` build
  carries the core pack only; community and unverified packs come from
  `https://mf.nofisto.com/download/<Platform>/<build>/` (Options > Game can point elsewhere),
  mount without a restart and at every start. Tested on a Pixel 9a and packaged Linux.
- Classic is the retail game only: the 51 custom maps by authors not asked yet and their
  packages are the unverified pack (a download now); the servers' weapon mods, MF-mods art and
  the community maps' own sounds are community content. `packs.toml`'s "core" is "community";
  "core" is the game's own pak. `mfport.py import --steps repack` moved the imported assets.
- Chunking: maps no longer pull other packs' assets into their pak (classic textures were in the
  community pak); unique pack label names; `--maps` builds no longer cook every map's data.
- `Tools/package.py` replaces `package.sh` and `package.ps1`; `mfport.py packs` shows what the
  last cook put in each pak.

## 2026-10-28 — Map imports and Linux Lightmass repair

- Finish conversion, import, navigation, and lighting for DM-AbandonedMine-KF, DM-Asylum,
  and DM-Cathedral in the local Classic pack. The remaining-map queue has 52 pending maps.
- Add explicit `mfport.py repair-lightmass` for the verified UE 5.8.4 Linux startup assertion.
  Verify the complete library hash and branch bytes, retain the original backup, replace
  atomically, and refuse unverified builds. The old 5.8.3 offset does not apply.
- Preserve the intervening weapon, touch-control, Android, graphics, and expiry-glow updates.
- Validate the combined Linux build with all five gameplay suites, 16 Python checks, map
  smoke runs, and a live standing test on the repaired space-station grating.


## 2026-10-28 — Vehicle and content follow-up

- Replace passenger-only entry with `UseClosest` on Right Alt. It selects the nearest free
  seat, including the driver, and handles loadout selection so it can replace normal Use.
  Normal Use retains MF's front-seat driver preference.
- Reduce tyre traction's overturning leverage while retaining grip and suspension support.
  Recover overturned vehicles through damped torque instead of teleporting upright; include
  sleeping bodies in recovery. Restore truck turbo acceleration under the existing server rule.
- Pulse a red expiry warning faster near the deadline. Occupancy cancels the countdown.
  Expiry emits low-energy debris, smoke, and a fragment sound without blast damage; debris
  lasts three seconds before removal and generators retain their five-second respawn delay.
- Use original MF team flag emblems on a two-sided masked material. Replace box tracers with
  crossed quads and a soft procedural opacity profile.
- Fix collision sidedness for thin imported surfaces. Refresh 55 space-station meshes without
  rebuilding lighting; test a capsule against the reported grating triangle.
- Show owned weapon ammunition in the classic loadout and replicate custom magazine/reserve
  capacities to the owner, including map-modified weapons.
- Add saved player model and preferred team choices. Models replicate; team preferences apply
  at join and remain subject to server mode and balance rules. Keep explicit model URL choices.
- Add resumable remaining-map batches with per-stage logs and checkpoints. The inventory has
  55 missing playable maps; entry and intro are excluded as menu/support maps.

Validation: UE 5.8.4 Linux build; five native gameplay suites across focused runs; 15 Python
checks. The recovery test reaches up.Z = 1.000 with a maximum sampled rotation step of 3.4°.
Truck turbo adds 639 cm/s over the matched unboosted truck after two seconds. Server/client
checks verify closest-seat selection, model/team replication, and 8873/999 custom ammunition.
Flag screenshots show the team emblem from both sides. Map batch completion and remaining
visual/platform limits are recorded in the dated test report.

## 2026-10-28

This checkpoint combines the graphics/options and tooling cleanup with the vehicle,
combat, and effect lifecycle fixes developed in the same working tree.

### Graphics and options

- Default to 120 FPS with VSync off; keep FPS, VSync, and render scaling in Advanced.
- Restore 25% render-scale steps and let anti-aliasing quality control MSAA instead of
  forcing 4× at every quality level. Reset texture mip bias when leaving Low.
- Use Unreal's built-in video settings through the project settings subclass. Fix Linux
  display-mode detection and window resolution application; borderless uses desktop size.
- Make the console checkbox reflect the actual console. Reset game options from factory
  defaults while preserving the active tab and Advanced expansion.
- Remove duplicate crosshair sizes, add a size control, and normalize visible shape bounds.
  Group related options and move mouse controls to Controls.
- Add Effects and Announcer volume controls alongside Master and Music.
- Make Full scorch marks follow static surfaces in forward rendering through bounded
  projected meshes. Simple remains a flat quad; DBuffer stays disabled.

### Gameplay and vehicles

- Use one `ScoreLimit` rule without compatibility aliases. Add a server parachute option,
  prevent normal jumps from deploying it, and close it when entering vehicles.
- Shorten the spawn fade by one third.
- Preserve MF's front-seat preference for an empty driver seat while choosing nearby rear
  seats. Shift+Use and `EnterPassenger` allow deliberate passenger entry without a driver.
- Fix vehicle exit ordering so overlap callbacks cannot kill an unpossessed soldier and
  leave a falling, unrespawnable corpse. Grant one second of immunity to the exited vehicle
  only; other incoming damage still applies.
- Keep exposed drivers hittable through vehicle openings. Wheel query collision blocks
  soldiers, and flat-tyre support follows the ground normal instead of producing sideways thrust.
- Prevent pawn physics from stopping vehicles like fixed obstacles while retaining run-over
  damage, survivor knockback, and a small contact slowdown. Preserve normal prop interactions.
- Recycle moved or damaged generated vehicles after 45 seconds empty, then spawn a replacement
  after five seconds. Any occupant cancels expiry; pristine and locked objective vehicles remain.
- Fix bots failing to resume fire after reloading or changing weapons.
- Add replicated feign death on Left Alt, with a rebindable control, a low damageable stance,
  the original death pose, flag drop, and weapon recovery. Alt, Fire, Alt Fire, or Jump gets up.
- Retain the mine-generator implementation and make its completed roadmap entry explicit.

### Effects and content conversion

- Add health-dependent vehicle smoke and fire/smoke tails for rockets and Predator missiles.
  Keep bounded gameplay cues visible at Low effects quality.
- Stop looping pooled effects on explosion and EndPlay, including remote destruction,
  direct removal, and timeouts. Initialize the Predator mesh on clients too.
- Resolve complete texture palette paths. BloodSpang.Palette25 previously selected the
  unrelated root Palette25, losing red colour and graded transparency.
- Reimport affected local effects and tracked, cleared community effect assets.

### Tools and maintenance

- Use `play.py` on Linux and Windows, with platform-specific TOML paths and compatible
  engine-patch discovery. Simplify setup and the Legendary-based Windows engine downloader.
- Align packaging script names as `package.sh` and `package.ps1`; update CI and examples.
- Replace Windows-specific file sharing helpers with explicit `sync_files.sh`; remove the
  obsolete play/format/path wrappers and duplicate sharing scripts.
- Simplify GitHub sync in Bash while preserving individual source commits. Preview by
  default, use temporary indexes, preserve author metadata, and require `--push` to publish.
- Add native gameplay/menu/lifecycle and Python configuration/sync/palette regressions.
  Record collision reentrancy, pooled-effect ownership, and symlink-path pitfalls in AGENTS.md.

### Validation and limits

- Unreal Engine 5.8.4 Linux editor/module build passed.
- Four native gameplay suites passed, including moving exit, driver death/respawn, exposed
  driver traces, second-magazine firing, feign damage/recovery, recycling, and trail cleanup.
- Separate client checks passed for passenger occupancy, front-seat priority, and feign state.
- Offscreen screenshots checked smoke, missile trails, feign pose, menus, and scorch modes.
- Python regression suite: 15 tests. C++ formatting and patch whitespace checks passed.
- Windows runtime, latest packaged Shipping builds, physical display/VSync behavior, and
  audible mixing need separate checks. Projected scorch edges remain an approximation.
