# System geometry presets

Ready-to-include OpenTOPAS fragments encoding the **validated physical geometry** of real
SPECT scanners, so a deck selects a system in one line:

```
IncludeFile = systems/symbia_lehr.txt
```

Each preset is the published, isotope-independent scanner spec, the "tunable geometry"
topas-spect ships. Isotope choice only sets the energy window (documented per preset as a
comment; applied by the user in post-processing).

## Provenance

Every geometry value is traceable to a source. The full reconciled evidence base, per-source
quotes, conflict resolutions, and reliability grades, lives in
`research/knowledge/spect-collimator-geometry/` (workspace knowledge hub, not shipped in this repo).
Reliability grades: **A** = manufacturer datasheet (+corroboration); **B** = multi-source
peer-reviewed; **C** = single peer-reviewed. Hole diameter is **flat-to-flat** throughout
(convention confirmed by Van Audenhaege 2015 and Könik 2019).

## Available presets

Geometry columns are hole diameter / septal thickness / hole length (mm), holes hexagonal
unless noted.

| Preset | Scanner / family | Collimator | Geometry | Septa / Crystal | Rel | Key source(s) |
|--------|------------------|-----------|----------|-----------------|-----|---------------|
| `symbia_lehr.txt` | Siemens Symbia/e.cam | LEHR (Tc-99m) | 1.11/0.16/24.05 | Lead / NaI 9.5 | B | Dong 2018; Rodrigues 2007 |
| `symbia_me.txt`   | Siemens Symbia | ME/MELP (Lu-177) | 2.94/1.14/40.64 | Lead / NaI 9.5 | B | Benabdallah 2021; Shahmari 2019 |
| `symbia_he.txt`   | Siemens Symbia | HE (I-131/Ac-225 440) | 4.0/2.0/59.7 | Lead / NaI 9.5 | B | Shahmari 2019; Pb-203/212 2025 |
| `symbia_hegp.txt` | Siemens e.cam/Symbia T6 | HEGP | 3.4/2.0/50.8 | Lead / NaI 9.5 | B | Benabdallah 2021; Shahmari 2019 |
| `symbia_lphr.txt` | Siemens Symbia | LPHR (I-123) | 1.5/0.2/35 | Lead / NaI 9.5 | **D (uncited)** | none, see caveat in file |
| `ge_lehr.txt`  | GE Anger family | LEHR | 1.5/0.2/35 | Lead / NaI 9.5 | A | GE Infinia DS; Sawant 2025; Pandey 2015 |
| `ge_legp.txt`  | GE Anger family | LEGP | 1.9/0.2/35 | Lead / NaI 9.5 | B | GE Infinia DS; Pandey 2015 |
| `ge_elegp.txt` | GE Anger family | ELEGP | 2.5/0.4/40 | Lead / NaI 9.5 | B | GE Infinia DS; Cerić 2024 |
| `ge_megp.txt`  | GE Anger family | MEGP | 3.0/1.05/58 | Lead / NaI 9.5 | A | GE Infinia DS; Sawant 2025; Pandey 2015 |
| `ge_hegp.txt`  | GE Anger family | HEGP | 4.0/1.8/66 | Lead / NaI 9.5 | A | GE Infinia DS; Sawant 2025; Pandey 2015 |
| `ge_uhehr.txt` | GE Infinia | UHEHR | 3.0/1.9/80 | Lead / NaI 25.4 | B | GE Infinia DS |
| `dspect.txt`   | Spectrum Dyn. D-SPECT | tungsten square-hole | 2.26/0.2/21.7 (square) | Tungsten / CZT 5.0 | B | Gambhir 2009 |
| `ge870czt_wehr.txt` | GE Discovery 870 CZT | WEHR (square) | 2.26/0.2/45 (square) | Tungsten / CZT 7.25 | B | Ito 2021 |
| `starguide.txt`   | GE StarGuide   | WEHR-analog | calibrated (see file) | Tungsten / CZT 7.25 | analog | calibrated, not vendor geometry |

The GE Anger family geometry is shared across Infinia/InfiniaVC, Millennium VG, Discovery
NM/CT 670(Pro)/630, Optima NM/CT 640, and Discovery NM/CT 870 (NaI mode).

### Not shipped (deliberately)
Vendor-proprietary or out-of-scope geometries are **not** encoded (no invented numbers):
Spectrum Dynamics VERITON (all geometry confidential); GE StarGuide exact septa/hole-length
(only hole width + pixel pitch are published, hence `starguide.txt` is a calibrated analog);
GE CZT LMEGP ("undisclosed"); Siemens LEAP/LEGP/LEUHR (no openable source); GE Discovery
530c/570c (multi-pinhole) and GE fan-beam (converging), different collimator classes.

## What a preset defines vs. what your deck must provide

OpenTOPAS forbids defining a parameter twice (it aborts on redefinition), so each preset owns
its parameters exactly once and deliberately leaves the deck-dependent ones to you.

A preset **defines** (the intrinsic, validated spec):

- the septa and crystal materials (`Ma/*`),
- the collimator hole/septum/length and `Ge/Collim` identity/material,
- the crystal material and thickness (`Ge/Crystal/HLZ`).

Your top-level deck **must add**:

- `Ge/World` and the physics list (`Ph/*`),
- the source (`So/*`) and scorer (`Sc/*`),
- the detector **extents**, `Ge/Collim/NHolesX`, `Ge/Collim/NHolesY`, `Ge/Crystal/HLX`,
  `Ge/Crystal/HLY`, and (for `EnergyDeposit`) `Ge/Crystal/XBins`, `Ge/Crystal/YBins`,
- the absolute **Z placements**, `Ge/Collim/TransZ`, `Ge/Crystal/TransZ`, `Ge/Src/TransZ`.

Each preset lists the recommended placement numbers (front collimator face at z = 0) and the
photopeak window in its header comment. See [`examples/system/`](../examples/system/) for a
complete deck that includes a preset and reproduces the validated LEHR sensitivity result.

## Rules

- **Include exactly one preset per deck.** Two presets would both define `Ma/*` and
  `Ge/Collim`/`Ge/Crystal` and OpenTOPAS would abort on the redefinition.
- **Do not redefine a preset's materials** (`Ma/Lead`, `Ma/NaI`, `Ma/Tungsten`, `Ma/CZT`)
  in your deck.
- OpenTOPAS resolves `IncludeFile` relative to the directory you launch from, so run OpenTOPAS from
  the folder your deck's relative path is written against (the shipped examples use `../../systems/...`
  and are run from their own folder), or give an absolute path.

## Fast (analytic-collimator) path

The `TsForcedDetectionProjection` scorer computes an analytic collimator transmission and
reads the collimator numbers as *scorer* parameters (`Sc/<name>/HoleDiameter`,
`SeptalThickness`, `CollimatorLength`, ...), not from `Ge/Collim`. Each preset repeats its
collimator numbers in a comment so they can be copied onto the fast-path scorer.

## Verification status

- The original Symbia lead + NaI round-hole geometry is validated in the shipped examples
  (`docs/validation.md`; absolute LEHR sensitivity within 5%).
- The GE/Siemens presets use **hexagonal** holes (`HoleShape=Hex`, `Lattice=Hex`) and the
  D-SPECT/GE-870 presets use **square** holes (`HoleShape=Square`). These hole-shape code paths
  and the tungsten/CZT element-name material builds have been **compile/run-verified** against the
  target build (OpenTOPAS 4.2.p2 / Geant4 11.2.2): the hex-hole (Symbia ME/HE), square-hole
  (D-SPECT, GE 870 CZT), and tungsten/CZT presets all construct with zero overlaps.
- Absolute-sensitivity validation now covers the round-hole Symbia LEHR and the **hexagonal-hole GE
  LEHR/MEGP/HEGP** presets (`docs/validation.md` section 4; reproducible from
  `examples/system/*_sensitivity.txt`). The GE presets read ~10-25% above the reference SIMIND study,
  which decomposes into a dominant geometric open-area difference plus a few-percent septal-penetration
  term (verified at 140 and 364 keV); where a measured reference exists (Symbia NEMA) the model agrees
  to ~5%. The **square-hole CZT/tungsten presets** (D-SPECT, GE 870) remain geometry-verified only;
  their absolute sensitivity/resolution has not yet been benchmarked.
