# Camera and collimator systems

The `systems/` directory ships ready-made camera presets. Each is an `IncludeFile` fragment that
defines a collimator and a detector crystal with validated, literature-sourced geometry. Including one
gives you a clinical camera in a single line; you then add the parts specific to your study.

## Using a preset

Include a single preset per deck, then supply the world, physics, source, scorer, and the detector
*extents* and *placement* the preset leaves open:

```
IncludeFile = systems/symbia_lehr.txt      # defines Ge/Collim and Ge/Crystal

# you provide the size of the head and where it sits:
i:Ge/Collim/NHolesX = 120
i:Ge/Collim/NHolesY = 120
d:Ge/Collim/TransZ  = 12.025 mm            # front collimator face at z = 0
d:Ge/Crystal/HLX    = 90. mm
d:Ge/Crystal/HLY    = 90. mm
d:Ge/Crystal/TransZ = 29.05 mm             # small gap behind the collimator
```

The preset fixes the physics-bearing geometry (hole diameter, septal thickness, hole length, crystal
material and thickness); you fix the *size* of the detector head (number of holes, crystal extent) and
*where* it sits. Each preset's header comment gives the recommended placement and the photopeak
window to apply in post-processing. See `systems/README.md` for the full contract.

## Available presets

**Siemens Symbia (NaI):**

| Preset | Collimator | Typical use |
|---|---|---|
| `symbia_lehr.txt` | Low-Energy High-Resolution | 99mTc, 177Lu (208 keV with care) |
| `symbia_lphr.txt` | Low-Penetration High-Resolution | low-energy, penetration-sensitive |
| `symbia_me.txt` | Medium-Energy | 111In, 177Lu |
| `symbia_he.txt`, `symbia_hegp.txt` | High-Energy | 131I (364 keV) |

**GE Anger cameras (NaI):**

| Preset | Collimator |
|---|---|
| `ge_lehr.txt` | Low-Energy High-Resolution |
| `ge_legp.txt` | Low-Energy General-Purpose |
| `ge_elegp.txt` | Extended Low-Energy General-Purpose |
| `ge_megp.txt` | Medium-Energy General-Purpose |
| `ge_hegp.txt` | High-Energy General-Purpose |
| `ge_uhehr.txt` | Ultra-High-Energy High-Resolution |

**CZT systems:**

| Preset | System |
|---|---|
| `ge870czt_wehr.txt` | GE Discovery NM/CT 870 CZT, WEHR-analogue |
| `starguide.txt` | GE StarGuide (12-column CZT ring) |
| `dspect.txt` | Spectrum Dynamics D-SPECT (tungsten square holes) |

**Detector-only building blocks:**

`detector_nai.txt` and `detector_czt.txt` define just the crystal material and thickness, for pairing
with a custom collimator you build yourself (see [Designing detectors and collimators](design.md)).

!!! note "How proprietary geometry is graded"
    Vendor collimator dimensions are largely proprietary. Every preset's header states its source and
    a reliability grade: manufacturer datasheet, multi-source-corroborated, single peer-reviewed, or
    third-party Monte Carlo reconstruction. Where a system's geometry is genuinely unpublished (for
    example, StarGuide's collimator), the preset says so. Treat the low-graded presets as reasonable
    working models that approximate the real hardware, and see [Validation](validation.md) §4 for how the sensitivities
    compare against independent measurements and simulations.

## Choosing a collimator

The collimator is the fundamental resolution–sensitivity trade-off in SPECT, and the right choice
depends on your isotope's photon energy:

- **Low-energy collimators (LEHR, LEGP)**, for ~100–160 keV emitters (99mTc, and the 113/208 keV
  lines of 177Lu with attention to penetration). Thin septa give the best resolution but leak at
  higher energies.
- **Medium-energy (MEGP, ME)**, for ~200–300 keV (111In, 177Lu 208 keV).
- **High-energy (HEGP, HE)**, for ~364 keV and above (131I). Thick septa suppress penetration at
  the cost of sensitivity and resolution.

If you image a high-energy isotope through a low-energy collimator, expect a large septal-penetration
tail, which OpenTOPAS-SPECT shows explicitly because it models the collimator
approximated.

## Full-system examples

<div style="display:flex;gap:2%;flex-wrap:wrap">
<img src="../assets/renders/dualhead.jpg" alt="Dual-head Anger camera" style="width:32%">
<img src="../assets/renders/starguide.jpg" alt="StarGuide 12-column CZT ring" style="width:32%">
<img src="../assets/renders/dspect.jpg" alt="D-SPECT swiveling CZT panels" style="width:32%">
</div>
*Left to right: the dual-head Anger camera, the StarGuide 12-column CZT ring, and the D-SPECT
swiveling-panel array (OpenTOPAS Qt viewer snapshots of the shipped decks).*

The presets above define a single detector head. To mount an **entire camera with its motion**, ready
to combine with a phantom, see
[`examples/acquisition/`](https://github.com/bertoletlab/topas-spect-release/blob/main/examples/acquisition/):

- `dualhead_orbit.txt` — a dual-head Anger camera (two heads 180° apart) orbiting a body phantom on a
  gantry time feature. This same arrangement models any NaI Anger camera (Siemens Symbia, GE
  Discovery/Infinia); the collimator/crystal geometry for a given camera comes from the `systems/`
  presets.
- `starguide_ring.txt` — the GE StarGuide 12-column CZT ring with its adaptive dock-and-rotate motion,
  generated by [`make_ring_motion.py`](tools.md).
- `dspect_scan.txt` — the Spectrum Dynamics D-SPECT cardiac system: 9 CZT columns in a fixed fan over
  the chest, each swiveling in place to sweep a heart region of interest (the columns do not orbit).

These are geometry-and-motion examples: open any of them in the OpenTOPAS Qt viewer to watch the full
system move around the patient, then add your phantom's activity sources and a scorer for a complete
acquisition. The three arrangements plus the `systems/` presets cover the shipped cameras: dual-head
Anger (all the NaI cameras, by collimator swap), the StarGuide CZT ring, and the D-SPECT swiveling
CZT panels.
