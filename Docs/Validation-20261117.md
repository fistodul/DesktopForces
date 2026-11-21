# November 17 fixes: validation checkpoint

**Current status (November 19):** new voting and gameplay changes need host verification with `tests/20261118-regressions/run-regressions.py` (absolute command at the end). Packaging requires the user's explicit in-game approval. Earlier instructions and results below are historical; the November 19 continuation supersedes them.

Host build and all 43 map patches completed. Texture import saved 63 textures with zero failures; shared-art validation repointed 10 materials and found no remaining hard references to the five selected originals. All checked BuiltData hashes stayed unchanged. Native vehicle collision, handling and lifecycle tests passed. **Release validation is incomplete**: the suite used DM instead of required TDM, and the ladder fixture never initialized movement. The runner and fixture are corrected; completed content work is preserved for retry. Do not mark the fixes or release complete until the checks below pass.

- Ladder movement preserves distance from solid rungs, separates climb from the top-out push, and filters overlaps to the capsule. Inherited UE1 ladder defaults are resolved through ClassTree. Python staging changed 261 records across 39 maps; 32 maps need navigation rebuilt for corrected dimensions. Map patching preserves geometry and baked lighting.
- HMG muzzle flash projects the animated GunBarrel opening. Other weapons retain existing offsets pending visual evidence.
- Vehicle pawn collision uses the imported body instead of the broad box, with a fallback for missing collision. Handling values remain unchanged. Original engine/nitro defaults match; estimated drag suppresses boost speed, but original runtime measurements are still needed before tuning.
- Generated minimaps now omit downward-facing ceilings: 51 images updated, five bounds changes. Ten Shuttle replacement recipes correct stale brown/gray sampling and wrong lamp shapes. User screenshots ScreenShot00116/117 show stretched white-disc ceiling fixtures; prioritize the other tasks as requested.
- Five reviewed, exact flat-color replacements are shared with Classic. Original textures remain locally; hard-reference checks precede cook exclusion. Shared textures must cook only in core, avoiding a new dependency on the optional Community pack. Unchanged material imports no longer regenerate GUIDs.

Prior lighting/docs commits are `2d190bc6` and `81afa2f2`. The 151 material-instance changes present before this task contain state GUID/hash changes only, plus cached skeletal-use flags on 20 unlit instances. Texture references, parent materials and authored parameters match HEAD. Preserve these files; do not include them as an unrelated material-art commit.

## Evidence and execution

Artifacts: `/home/mrs/Documents/Unreal Projects/tests/20261117-fixes/`, `20261117-visuals/`, `20261117-vehicles/`, `20261117-sharing/`, and `20261117-material-audit/`.

The session can write project files but has no `/dev/dri`. Rendered startup fails Vulkan profile validation. Normal builds fail on read-only `~/.config/Epic`; redirected build/cache attempts hung in native UBA startup. Stopping their wrappers left host dotnet children 499119 and 499749 running (reported by the host verification guard). They are invisible in the session PID namespace; host termination is required before retrying verification. Docker socket access is denied. `.git` remains read-only. These restrictions require the host terminal for Unreal, packaging and commits.

1. Run `python3 -u '/home/mrs/Documents/Unreal Projects/tests/20261117-fixes/run-verify.py'`. It resumes completed steps, builds, captures before/after scenes, imports textures/pack labels, patches affected maps, verifies unchanged BuiltData hashes, runs native tests, and records HMG/ladder captures. Its process guard ignores persistent Roslyn services but stops for actual Unreal builds. Review every native result, ladder trajectory, HMG alignment and scene/minimap changes.
2. Fix any compile/import/test failures. Python suite currently passes 95 tests; rerun affected checks after further edits. Source changes invalidate the runner's completed-step cache.
3. After review, record `review.json` with `approved: true` and the reviewed `source` fingerprint from `verify-state.json` beside the script. Run `run-package.py`: Linux and Android Shipping downloads/team packages and a matching Linux dedicated server, version `20261117`. All built maps are selected except `mfs-warehouse`. It checks client cook manifests for map exclusion and shared-texture deduplication, creates public/team download ZIPs and checksums. No upload is performed. Windows requires the native Windows build host with the same version and map selection. Android full offline APK remains subject to the existing size limit; core APK plus packs are available.
4. Smoke-test packaged client and dedicated server together; verify map lists, actual joins and pack downloads/reuse. Check server cook/archive for `mfs-warehouse` exclusion too. Inventory artifacts and sizes. Nothing has been packaged yet.
5. Review a scoped source/assets diff, excluding unrelated 151 metadata-only materials and all Classic/plugin content. Commit only verified changes via the host if `.git` is still read-only. Update Roadmap with outcomes, remaining lamp/handling limitations, and release locations.

Do not discard unrelated edits, publish packages, or enable blanket Classic remastering. Keep a single Unreal execution lane. No additional lighting rebake is planned unless verification proves one is needed.

Host retry note: the first verification run stopped in Python tests because the runner globally set `DF_OPTS=?TurboCars=1`. Fixed the runner to clear inherited options for ordinary subprocesses and set TurboCars only for native automation. All 95 Python tests pass again. Resume the same verification command.

## Frame-rate / slomo follow-up (November 18)

Confirmed in the installed UE 5.8 source: PhysicsSettings defaults disable substepping and cap rigid-body time to 1/30 s per frame; ChaosScene::SetUpForFrame discards excess time. At 30 FPS, slomo20 requests about 0.667 game seconds per frame but Chaos advances about 0.033. DFVehicle additionally caps its wheel calculation delta to 0.05 s. Default maximum global dilation is20 (Engine/Config/BaseGame.ini). Nitro is already a force; multiplying it by dt again would be wrong.

Do not claim physics is independent of rendering FPS. Normal-rate handling still needs cross-rate measurement. `physics-rates.py`, now run after the verification captures, compares 15/30/60/120 Hz with current settings and a process-local six-substep 60 Hz trial. It changes no project physics settings. Results land in `physics-rates/results.json`. Compare equal game-time speed, distance and turns; inspect stability before accepting substeps. Six substeps cover at most0.1 s/frame and cannot preserve slomo20 at30 FPS. Faithful20x simulation at60 Hz would need about40 solver steps per rendered frame plus per-step vehicle-force updates; this has real CPU cost and is beyond a one-line torque change.

## November 18 host review

The corrected suite passed all 14 native tests. Warehouse ladder replay climbed and descended at 450 cm/s, with no downward pull while ascending. Before/after captures show SpaceZombura changing from orange gravel-like surfaces to the intended gray panel family; no new lighting artifacts were seen in the 20 sampled views across both maps. Turpowia's changed indoor concrete also reads as gray. These samples do not cover every surface. HMG widescreen capture contains a flash; the intended 4:3 capture was clamped to 888x500 and caught no flash, so that check needs repeating.

At equal 10-second game time, the original 15 FPS vehicle runs traveled up to 61.56% less distance than 60 FPS. Original 30/120 FPS distance differences were at most0.30%/0.09%. With six synchronous 60 Hz substeps, 15 FPS distance differed by at most 0.45%. Steering still has small rate-dependent differences because wheel forces are evaluated per game frame. These results justify enabling bounded substeps, not claiming fully fixed-step vehicle dynamics.

`DefaultEngine.ini` now enables the tested six-substep configuration. `DFVehicle::Tick` now derives its admitted delta from the same physics settings instead of its independent 50 ms cap. Rebuild/native tests and cross-rate comparisons, now including explicit slomo 2 at 30 FPS, remain pending for this final change. Existing imported assets and their verification checkpoints are preserved. Release version is now 20261118, shared by client and dedicated server.

HMG geometry review: the implemented bone offset projects onto the skinned front-octagon center; no transform or axis error was found. The apparent hole above the flare may be a sight rather than the muzzle. Do not adjust it by screenshot guesswork. The retry now forces 1280x720 and 960x720, captures carry/deployed plus eight firing frames each, and validates actual PNG dimensions.

## User gameplay review: November 18 — release blocked

Do not package until the user explicitly approves their own in-game verification run. Automated checks did not establish overall gameplay fidelity. The HMG flash was prematurely described as fixed; matching a guessed geometric point does not prove the visible barrel exit is correct.

Continue these tasks before asking for that verification:

- Finish the visual vote menu (partial code exists in DFMenu.cpp/h and DFHUD.cpp; subagent stopped at a usage limit). Preserve existing server authority and test multiplayer voting.
- Ladders: climbing now works, but preserving the entire approach distance leaves excessive empty space in front during ascent. Descent is closer and feels more natural. Test both directions and top-out; avoid replacing one regression with another.
- Vehicles: substepping supports 2x/30FPS in the measured flat-ground scenario, but 5x exceeds the six-step budget at30FPS. Contact/spawn bouncing and failure to climb Turpowia curbs remain separate unresolved cases. Check chassis and wheel contact shape before tuning torque.
- Muzzle flashes: several weapons remain visibly misplaced, including HMG. Inspect original code, imported transforms/animation and supplied videos; do not approve from one numerical point or a single frontal image.
- Authored weapon pickups in Turpowia/COOP maps should pick up on touch subject to original carrying limits. Armour/adrenaline appear as black rectangles; inspect original visibility/mesh defaults before choosing invisible originals or new simple art.
- Low graphics preset: consider Simple decals/scorches, keeping Full for Medium and above.
- Key rebinding: wheel input reaches the options scroll box before the binding capture. Capture must consume it immediately.
- Thrown knife: inspect original spin and impact orientation; it currently embeds handle-first.

Evidence: user videos under `/home/mrs/Documents/Unreal Projects/`: `2026-11-18 14-07-44.mp4` (111.27s), `14-05-14.mp4` (30.9s), `14-03-40.mp4` (3.83s), `14-02-44.mp4` (3.33s). Screenshots `ScreenShot00118.png`, `ScreenShot00119.png`, and `hmg-review-960x720-fire7.png`. Review artifacts go in `tests/20261118-regressions/`. Additional unrelated tool-script changes appeared in the shared working tree; preserve them and do not silently fold them into this task's commits.

Implementation checkpoint after video review (not yet built): voting page now has live ballots and proposal controls, with a new native VotingMenu test. Key capture has a full-menu hit-test shield so wheel events do not reach the scroll box; Low selects Simple decals and Medium+ selects Full. Ladder entry now sweeps towards the rungs and backs off 2 cm; its fixture checks closer entry plus descent using the same stand-off. Vehicle tyres now use sphere sweeps instead of centre rays, with a curb-contact fixture; damping uses relative contact velocity instead of a compression jump from zero on spawn. These vehicle changes need fresh stability and handling comparisons. KnifeThrownMesh/KnifeThrownAnim were exported successfully, and the runtime switches replicated flight spin to the original still pose on impact; focused asset import is staged under `20261118-regressions/knife-import/out`.

No automatic-pickup flag was found in either the exported records or binary properties of sampled Turpowia/COOP weapon actors. The original native player behavior and the user's intended property remain under investigation. Do not invent an MF field name or silently change every dropped weapon to auto-pickup.

WeapTools source resolved: `WeapReplacer` explicitly replaces 10 stock weapon classes with `WeapPickup`, dropping authored ammo overrides and respawn settings. `WeapPickup` checks carry space and per-class limits, gives a default weapon, and destroys itself after success. It uses the weapon class's default mesh; armour/adrenaline have none. The pipeline now applies this rule only when the map contains the replacer, or to directly authored WeapPickup actors. Staging changed 86 Turpowia pickup records; the inspected COOP maps contain BotEquiper instead, which is a separate bot-loadout helper. Ordinary pickups remain manual. Native MapPickup and Python WeapTools regression checks were added. Meshless consumables no longer use black placeholder boxes.


## November 19 continuation — source review, host verification pending

Packaging remains blocked until the user approves their own in-game run. The older
verification results above apply to the earlier build, not the new draft fixes.
All 98 Python tests pass. The new runner is
`/home/mrs/Documents/Unreal Projects/tests/20261118-regressions/run-regressions.py`.
It builds once, imports only the thrown knife, patches Turpowia's WeapTools pickups,
checks unchanged baked lighting, requires all 18 native test results, and saves menu
and ladder captures beside its logs. It never packages or commits. Gameplay
`DFMenuPage` and `DFShotUI` now support the voting capture; missing captures stop the runner.

Remaining review gaps: actual network voting and knife replication, mouse-wheel input
routing through the displayed menu, ladder ascent/descent/top-out on real maps,
vehicle spawn/touch stability and driven curb climbs, then fresh physics-rate comparisons.
The ladder fixture uses dense rungs; sparse-rung entry is not yet covered. Contact
fixtures alone cannot establish vehicle handling. Muzzle rendering investigation
found that UE1 draws the flash before the weapon, while the current HUD draws it over
the weapon. The HMG's earlier geometric projection therefore did not establish a
correct visual result. Compare new firing captures before marking this fixed.


The HMG candidate now uses an owner-only, depth-tested billboard attached to the
animated muzzle position. Flash expiry and holstering have a native lifecycle check.
Its physical radius remains provisional and requires capture review; other weapons
still use the old canvas renderer and remain unresolved. The new runner captures HMG
firing at 1280x720 and 960x720 in the lit warehouse and writes a separate five-run
physics comparison under `20261118-regressions/physics-rates/`, preserving earlier results.
The suspension damper now derives compression rate from the contact normal, including
horizontal motion up a curb; a ramp fixture compares it with measured sphere-sweep
travel. No engine, traction or spring gains changed. Source formatting and Python
checks are complete; none of these new native checks have run yet.


Other muzzle alignment findings to retain: `RageWeapon.RenderMuzzleFlash` subtracts
`0.5 * MuzzleScale * FlashS`; the current HUD instead multiplies FlashS by viewport
scales. Original Pistol FlashYZoom is 0.1 (current 0.025), and MachineGun is 0.3
(current 0.09). These are known divergences, not proof that restoring the numbers
alone will align the imported viewmodels. Check hip/aimed views and FOV before changing them.
The host-run request has been sent. Source is frozen for that build. Current source
fingerprint and remaining tasks are saved in `20261118-regressions/review-checkpoint.json`.


## November 19 host attempt 1

Build passed without compiler warnings. Native results: 16/18 passed, including
MapPickup (real overlap/capacity), MuzzleFlash lifecycle/depth testing, WheelCurbContact,
LadderRungs, and all earlier vehicle checks. MenuOptions and VotingMenu failed.

Corrections staged for retry:

- Key-capture shielding now changes visibility synchronously with capture state;
  it no longer relies on Slate evaluating a bound visibility attribute next frame.
- The older Options test spawned a fully initialized extra GameMode, replacing the
  world's GameState and NetworkManager. Voting then read a different state from the
  authority. Its options fixture now follows the real InitGame order with deferred
  spawning, cleans its temporary sessions, and checks that the live state survived.
  Voting also asserts world/authority state identity before testing ballots.
- UnixCommonStartup adds quotes around argument values containing spaces. Literal
  quotes in the work-directory argument were doubled, yielding an empty FParse value
  and fallback to the complete skeletal manifest. The script and maintained Linux
  importer now pass a raw argument. A regression test covers paths containing spaces;
  the retry additionally requires the imported-name set to be exactly KnifeThrownMesh.

The unintended full skeletal import completed, including the knife's two animations.
It may introduce additional saved-asset metadata changes; audit these before staging.
The completed Turpowia patch is retained: 2018 nav tiles and all 23 starts on navigation.
Its BuiltData check passed. Attempt-1 logs/state are archived under
`tests/20261118-regressions/attempt1/`. Python checks now pass 99/99. A host retry was requested.

Warning classification: UE 5.8's RecastNavMeshGenerator calls ComputeConfigBorderSizes
before assigning OutConfig.tileSize, explaining the transient tileSize=0 warning;
the subsequent built navigation is nonempty and all starts project. The CrowdManager
warning occurs during commandlet teardown. Skeletal hull warnings name unweighted
bones (bOne/Generale/Culo) during automatic import, not the runtime vehicle chassis.
EditorDataStorageUI widget-purpose warnings come from editor-only UI startup in -game.
None of these findings justify changing vehicle collision or navigation settings to
silence the messages. Gameplay warning review continues with the remaining captures.


## November 19 host retry reviewed

Both attempts were inspected. The retry built without compiler warnings, imported only
KnifeThrownMesh without import warnings, and passed all 99 Python and 18 native tests.
The current Source/Config fingerprint matches the tested build. Artifacts and detailed
review: `tests/20261118-regressions/review-results.json` and
`physics-rates/comparison.json`.

The voting proposal page is readable and has the expected controls. Native ballot
logic passes; a real multiplayer session remains untested. Warehouse ladder ascent
and descent captures show closer spacing to the rungs. The fixture rises 450 cm in one
second without a downward pull; real-map top-out and user feel still need verification.

**HMG remains unresolved.** None of the 16 firing captures has a visible muzzle flash.
(Resolved 2026-11-21: the HMG draws MF's canvas flash again, `FlashY` 0.045 just over the
receiver, seen in sustained-fire captures; see Gameplay.md, first-person muzzle flashes.)
The native test proves component visibility, depth-test material and timer cleanup,
not rendered flash visibility. An offline ray through the projected selected muzzle,
using all four imported primitives in the Fire pose, meets Skin2 about29.3 cm from the
eye before the selected muzzle at65.7 cm. This supports weapon self-occlusion as the
reason the depth-tested candidate disappeared. The previous analysis considered only
primitive0 and missed that blocking geometry. Do not enlarge or relocate the effect
blindly; compare the full viewmodel and original flare placement. Other canvas flashes
remain unresolved. `hmg-depth-contact.png` shows all16 captured muzzle regions.

Fresh equal-game-time vehicle comparisons versus60FPS: maximum distance differences
0.111% at30FPS and0.134% at120FPS; 2.884% at15FPS or30FPS/slomo2, both the boosted buggy.
Maximum final-speed difference across all cases is0.089%. These are flat-ground tests,
not proof of driven curb climbing or spawn/touch stability. Slomo5 remains beyond the
six-substep budget at30FPS.

The ladder capture also logged an engine Dataflow initialization-check error.
`Engine/Plugins/Dataflow/Source/DataflowNodes/Public/Dataflow/DataflowToolNode.h` sets
FDataflowToolNodeSnapshot::Date to FDateTime::Now() without the checker exemption for
nondeterministic fields. This explains the intermittent diagnostic; no project class
is named in it. Engine files were not patched or diagnostic checks disabled. Crowd
warnings in all warehouse captures occur after PreExit Game during world teardown.
The release remains unapproved, with packaging blocked pending fixes and user review.
