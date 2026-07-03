# Patient-like activity phantoms

Compose realistic SPECT test scenarios, physiological organ uptake plus lesions (liver tumors, bone
metastases, lymph-node mets), entirely from the parameter file, then image them through any
collimator/detector from the [design module](../docs/design.md). Two routes:

1. **Composable analytic** (this directory), organs and lesions as native shapes with per-region
   activity. Flexible and self-contained; ideal for sweeping lesion size/location/contrast.
2. **Voxelized XCAT**, realistic anatomy via the native `TsImagingToMaterialXcat`; see
   [`docs/phantoms.md`](../docs/phantoms.md).

Per-isotope scenario presets with **literature-sourced** uptake ratios are in
[`scenarios/`](scenarios/); their provenance lives in the workspace knowledge hub
(`research/knowledge/rpt-biodistribution/`), not in this repo.

## How an analytic phantom is built

A phantom = an **attenuating body** (soft tissue) containing **organs** and **lesions**, each a
shape with a material (for attenuation) and, for the active ones, a **volumetric source**.

### 1. Geometry (native OpenTOPAS shapes; NIST materials by name)

| Region | Shape (`Ge/<n>/Type`) | Half-sizes | Typical material |
|--------|-----------------------|-----------|------------------|
| Body / torso | `G4EllipticalTube` | `HLX`,`HLY` semi-axes, `HLZ` half-length | `G4_TISSUE_SOFT_ICRP` |
| Kidney, liver, salivary, spleen, bladder | `G4Ellipsoid` | `HLX`,`HLY`,`HLZ` semi-axes | `G4_TISSUE_SOFT_ICRP` |
| Spine / ribs (marrow) | `TsCylinder` / `TsBox` |, | `G4_BONE_CORTICAL_ICRP` |
| Tumor / bone met / node | `TsSphere` (`RMax`) |, | `G4_TISSUE_SOFT_ICRP` |

Nest organs/lesions inside the body with `Parent` and place them anatomically with `TransX/Y/Z`. Keep
`b:Ge/CheckForOverlaps = "True"`.

### 2. Activity (one volumetric source per hot region)

Each active region gets a `volumetric` source that emits uniformly from its `ActiveMaterial`:

```
s:So/<region>Src/Type              = "Volumetric"
s:So/<region>Src/Component         = "<region>"
s:So/<region>Src/VolumetricParticle = "gamma"
d:So/<region>Src/VolumetricEnergy   = 208. keV      # the isotope's photopeak line
s:So/<region>Src/ActiveMaterial     = "G4_TISSUE_SOFT_ICRP"
i:So/<region>Src/NumberOfHistoriesInRun = <weight>
```

**Relative activity is set by the history count.** There is no becquerel parameter, so weight each
source in proportion to its activity concentration and volume:

```
NumberOfHistoriesInRun(region) = round( BaseHistories * concentration_ratio(region) * volume(region)/refVolume )
```

- `concentration_ratio`, the region's activity concentration relative to a reference (e.g.
  background soft tissue), from the sourced tables in `scenarios/`.
- `volume`, the analytic region volume (ellipsoid = 4/3·π·HLX·HLY·HLZ; sphere = 4/3·π·R³).
- `BaseHistories`/`refVolume`, free normalization (set total counts to your statistics budget).

Multi-line isotopes (e.g. Lu-177 208 + 113 keV): add one source per line per region, or use a
discrete energy spectrum, weighting by the line abundances.

### 3. Image it

Add a collimator + detector (design-module presets or your own) and a scorer. For tractable runs use
the forced-detection fast path (`TsForcedDetectionProjection`); for reference use full-MC. See
[`examples/phantom/`](../examples/phantom/).

## Rules
- Materials are NIST names (`G4_TISSUE_SOFT_ICRP`, `G4_BONE_CORTICAL_ICRP`, …) built on demand, no
  `Ma/*` definitions needed, so fragments compose without redefinition conflicts.
- Run from the repository root (or a space-free path) so relative includes resolve.
