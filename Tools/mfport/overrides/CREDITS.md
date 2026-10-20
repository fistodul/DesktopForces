# Override credits

Replacement art that core maps use instead of the original Mobile Forces files (classic maps
keep the originals unless `packs.toml` sets `remaster_classic`). Every file here must be
legally redistributable. List each one:

| Files | Source | Author | Licence |
|---|---|---|---|
| `textures/**` listed in `Tools/artgen/lookalikes.toml` | made by `Tools/artgen/lookalike.py` (procedural; only the originals' dark / mid / light colours sampled) | Desktop Forces | MPL-2.0 |

Good CC0 sources for hand-picked replacements: ambientCG, Poly Haven, Kenney, OpenGameArt
(check licence), freesound (CC0 filter). A hand-made file replaces the generated one: remove its
entry from `lookalikes.toml` so it isn't regenerated.
