# Validation

Monte Carlo settings: `g4em-penelope` electromagnetic physics with atomic
de-excitation (fluorescence, Auger, PIXE) turned on; 0.05 mm production cuts.
Reference systems: Siemens Symbia (NaI, 9.5 mm) and GE StarGuide (CZT, 7.25 mm).

!!! note "What this page is, and what the companion paper is"
    This page is the package's own record: what was checked, with which deck, and what came out, at the
    statistics each check was run with. The **companion paper is the reference of record** for the
    benchmark comparisons, which it reports at higher statistics with stated uncertainties and against
    the published references. Where a number here differs from the paper's, the paper's is the one to
    quote, and the difference is run statistics rather than a different model.

## 1. Crystal energy response

Mono-energetic pencil beam into a large bare crystal slab, scored with
`TsEDepSpectrum` (`examples/component/energy_response.txt`). The NaI response at 140.5 keV
shows the expected structure:

- Full-energy photopeak at 140.5 keV.
- Iodine K-escape peak at ~112 keV (140.5 keV - 28.6 keV Kalpha).
- Compton continuum terminating at the Compton edge (~49.8 keV predicted).

Photopeak-window efficiency (20% window, fraction of incident photons depositing
within the window), from 10^6 photons:

| Crystal      | 113 keV | 140.5 keV | 208 keV | 218 keV | 250 keV | 440 keV |
|--------------|:-------:|:---------:|:-------:|:-------:|:-------:|:-------:|
| NaI 9.5 mm   | 0.944   | 0.862     | 0.545   | 0.505   | 0.400   | 0.149   |
| CZT 7.25 mm  | 0.968   | 0.905     | 0.607   | 0.566   | 0.456   | 0.178   |

Trends are physical: efficiency falls with energy; CZT exceeds NaI at matched
energy (higher density and effective Z despite the thinner crystal).

Note: Geant4 scores energy *deposition*. The CZT low-energy tail from incomplete
charge collection (hole tailing / charge sharing) is a charge-transport effect and
is not included; it is applied as a separate empirical model where required.

## 2. Distance-dependent geometric resolution

Point source at 5-30 cm in front of a `TsParallelHoleCollimator` + crystal
(`examples/component/collimator_detector_response.txt`). The simulated point-spread width
(FWHM) broadens linearly with source-to-collimator distance, as expected for a
parallel-hole collimator.

Symbia LEHR at 140.5 keV (mm):

| Distance | 10 cm | 15 cm | 20 cm | 30 cm |
|----------|:-----:|:-----:|:-----:|:-----:|
| MC       | 5.3   | 7.3   | 9.3   | 13.3  |
| Analytic | 5.9   | 8.3   | 10.6  | 15.4  |

The MC point-spread width matches the analytic parallel-hole geometric-resolution
expression across the full distance range, reproducing its linear
distance-dependence and agreeing at the 10 cm reference distance to within a
fraction of a millimeter.

Higher-energy lines broaden through septal penetration. For the Symbia high-energy
collimator at 440 keV the core FWHM is 14.3 mm at 10 cm, but including the
penetration wing the full width grows by ~70%.

## 3. Absolute system sensitivity

Isotropic point source + collimator + crystal, counts in the photopeak window
divided by emitted decays (`examples/system/symbia_lehr_sensitivity.txt`). Symbia LEHR at 10 cm:

| Quantity | Value |
|----------|-------|
| Monte Carlo, round holes as the preset builds them | 7.5e-5 counts/decay |
| Monte Carlo, hexagonal holes as the real camera has them | 1.07e-4 counts/decay |
| Reference (5460 cpm/MBq) | 9.1e-5 counts/decay |
| Ratio, round holes | 0.82 |
| Ratio, hexagonal holes | 1.18 |

**This row previously read 8.6e-5 and a ratio of 0.95, and that agreement was two errors
canceling.** Both are now measured separately (2e8 histories, 0.8 percent statistical):

- The deck's collimator was 152.4 mm across a 180 mm crystal. An obliquely incident photon aimed at
  the crystal edge crossed only 21 percent of the collimator length before leaving through its side,
  so it reached the detector through far less lead than a full-length path. Worth **+15 percent**.
  An opaque-septa control is insensitive to the same coverage change, which identifies the mechanism
  as peripheral septal penetration rather than photons bypassing the collimator. Corrected to
  158 x 158 holes.
- The preset builds **round holes on a square lattice**, while the real Symbia LEHR is hexagonal on a
  hexagonal lattice. Sensitivity scales as (open fraction) x (hole area), making hex-on-hex 1.40x
  this geometry; measured 1.432 on otherwise identical decks. Worth **-30 percent**.

0.823 x 1.15 = 0.946, which is the 0.95 that was reported. The forward chain is exercised at absolute
scale, but the round-hole number understates the modeled camera and the hexagonal number sits 18
percent above the reference, in the same direction and of similar size to the independent LEHR
replications recorded in the companion campaign (Siemens e.cam 1.26, GE Infinia LEHR 1.24).

The 9.1e-5 reference is quoted here as 5460 cpm/MBq with no primary citation; treat its provenance as
unverified until one is supplied.

## 4. Multi-system collimator sensitivity and septal penetration

The collimator presets in [`systems/`](https://github.com/bertoletlab/topas-spect-release/blob/main/systems/) were checked by absolute planar
sensitivity: an isotropic point source at 10 cm, a collimator sized wider than the crystal on BOTH
axes, and photopeak-window (126.5-154.6 keV) counts divided by emitted decays. Sizing the margin is
not automatic: a hexagonal lattice stacks rows at pitch*sqrt(3)/2, so `NHolesY` computed from the
pitch builds a collimator only 0.866 as tall as intended. Every deck below was re-checked on both
axes and corrected where needed. Each row is reproducible from a shipped deck (`examples/system/<preset>_sensitivity.txt`).
Results are compared against an independent Monte Carlo characterization of the GE Discovery NM/CT
670 geometry (Sawant et al., *J Nucl Med Technol* 2025;53:30-35, SIMIND), against the GE Infinia
product datasheet where it covers the same collimator, and for the Siemens Symbia LEHR against a
5460 cpm/MBq figure whose primary source is not identified anywhere in this package (see section 3).
Sawant is used with two reservations: it states neither a source-to-collimator distance nor a medium,
and its table units disagree with its figure 3.

99mTc (140.5 keV), counts/decay (5x10^7 histories, ~1.5% statistical). These are the survey runs that
set the presets' evidence grades; the companion paper re-ran four of them at 2x10^8 histories with
stated counting errors, so quote those:

| Collimator | OpenTOPAS-SPECT | Reference | Ratio |
|------------|:-----------:|-----------|:-----:|
| Siemens Symbia LEHR (round holes, see section 3) | 7.49e-5 | 9.1e-5 (reference, section 3) | 0.82 |
| Siemens Symbia LEHR (hexagonal holes) | 1.07e-4 | 9.1e-5 (reference, section 3) | 1.18 |
| GE LEHR             | 9.13e-5 | 7.2e-5 (Infinia datasheet, +-10%) | 1.27 |
| GE MEGP             | 9.40e-5 | 7.4-8.3e-5 (SIMIND)          | 1.13-1.27 |
| GE HEGP             | 1.09e-4 | 1.00e-4 (SIMIND)             | 1.09 |

The Symbia and GE LEHR/MEGP rows were re-run after the coverage correction. GE LEHR moved 9.15e-5 to
9.13e-5 and GE MEGP 9.32e-5 to 9.40e-5, both inside the ~1.5 percent statistical error, because their
edge-bound oblique rays still crossed 66 and 94 percent of the collimator length. The Symbia row moved
13 percent, because its equivalent figure was 21 percent. GE HEGP was not re-run; its coverage margin
was already adequate.

**The absolute scale is NOT anchored to within 5 percent, and an earlier version of this section said
it was.** That claim rested on the Symbia LEHR ratio of 0.95, which section 3 now shows to be two
compensating errors. Modeling the camera's actual hexagonal holes with corrected coverage puts the
Symbia row at 1.18, and the GE LEHR row at 1.27 against its vendor datasheet. Every parallel-hole LEHR
arm in this package and its companion campaign now reads 18 to 27 percent ABOVE its published or quoted
reference, consistently in one direction. The cause is not established; candidate explanations and the
evidence against a hole-shape error are recorded in the companion campaign's replication report. Decomposing that excess against an
opaque-septa (geometric-only) reference separates two contributions:

- **Geometric hole efficiency (dominant).** The geometric-only GE LEHR sensitivity (8.7e-5) already
  sits at the top of the SIMIND range before any penetration, so most of the gap is the open-area of
  the explicit hexagonal-hole geometry (open fraction `[d/(d+t)]^2` = 78% LEHR, 55% MEGP, 48% HEGP)
  versus the reference collimator model, plus inter-code differences in window, backscatter, and the
  unmodeled crystal cover.
- **Septal penetration and collimator scatter (secondary).** Adding the real lead septa raises the
  140 keV sensitivity by 4.8% (LEHR, 0.2 mm septa), 2.9% (MEGP), and 2.7% (HEGP): small at the
  imaging energy but correctly ordered by septal thickness, which is what makes the low-resolution
  collimators read slightly higher than the high-resolution ones. These three figures come from decks
  whose edge-bound oblique ray crossed 56 to 87 percent of the collimator length, so each is biased
  high by at most a percent or so of its own value; the ordering and the magnitudes stand.

Septal penetration is strongly energy-dependent, and is exercised directly at 364 keV (131I), where
a low-resolution collimator becomes penetration-dominated: the ratio of LEHR to HEGP sensitivity at
364 keV is **59.5** (OpenTOPAS-SPECT) versus **67.7** (SIMIND), confirming that penetration through
the thin LEHR septa is transported correctly.

Geometric resolution, **simulated** rather than computed analytically. An earlier version of this
section combined an analytic parallel-hole resolution with a typical intrinsic term and compared it
against 7.2 / 9.7 / 12.1 mm described as "the published planar FWHM". Those three numbers are Sawant
2025 Table 2, the same SIMIND study already supplying the sensitivity references above, so that
comparison counted one source twice and was not an independent check. It is replaced here by
transported point-spread widths measured at 100 mm from the collimator face, fitted with the project's
4-parameter Gaussian convention, against the GE Infinia datasheet's own system-resolution column:

| Collimator | simulated, collimator only | + intrinsic 3.8 mm (datasheet) | datasheet system FWHM | ratio |
|---|:---:|:---:|:---:|:---:|
| GE LEHR (99mTc) | 5.83 mm | 6.96 mm | 7.4 mm +-4% | 0.94 |
| GE MEGP (67Ga) | 7.45 mm | 8.36 mm | 9.4 mm +-4% | 0.89 |
| GE HEGP (131I) | 11.78 mm | 12.38 mm | 12.0 mm +-4% | 1.03 |

The collimator-only widths reproduce the closed-form geometric resolution `d(L_eff + b)/L_eff` to
0.9 percent for LEHR and exceed it by 10 percent for HEGP, which is the expected direction because the
closed form carries no septal penetration. The residual 6 to 11 percent against the datasheet sits
inside terms this package does not model at all: light transport, Anger positioning beyond the quoted
intrinsic figure, the acquisition pixel, and any collimator cover.

All `systems/` presets were additionally confirmed to build with no geometry overlaps and to
score correctly for round, hexagonal, and square holes across the NaI and CZT/tungsten material
sets (OpenTOPAS 4.2.p2 / Geant4 11.2.2).

## 5. Phantom-scenario consistency (activity where it should be)

The [`phantoms/scenarios/`](https://github.com/bertoletlab/topas-spect-release/blob/main/phantoms/scenarios/) presets were checked not only for building
and emitting, but for *physical correctness* of the activity distribution. Each phantom was imaged
in an anterior view by a **numerical parallel-hole collimator**: a phase-space plane just outside
the torso records exiting photons, and only those traveling nearly perpendicular to it (direction
cosine along the bore < −0.985) are kept, so a photon that survives that cut leaves the detector-plane
position equal to its origin `(x, z)`, giving a clean parallel projection that also carries the
tissue attenuation. Three quantitative tests, all passed (OpenTOPAS 4.2.p2):

- **Localization**, for every hot region across all four scenarios (~30 regions), the detected-signal
  centroid falls within **1–2 mm** of the region's true `(x, z)` (method verified against a known
  point source: recovered 59.9/100.5 mm vs true 60/100 mm).
- **Left/right symmetry**, mirror-image organ pairs with equal assigned activity produce equal
  detected counts: kidneys **0.97**, salivary glands **0.973** (isolated high-statistics test, 500k
  emissions each). Wide-ROI apparent asymmetries traced to ROI contamination from a neighboring
  lesion and to low-count fluctuation; the geometry itself was correct.
- **Attenuation ordering**, detection efficiency (counts per emission) is lowest for the large, deep
  liver (~0.017) and rises monotonically for smaller, more superficial organs, as expected from
  self-attenuation and depth.

A full anterior projection of each scenario visually reproduces the intended hot pattern (kidneys,
salivary glands, liver, lesions for 177Lu/225Ac PSMA; liver tumor + lungs for 90Y; heart, salivary,
adrenal, bladder and tumor for 131I mIBG), each hot spot coincident with its source marker. 90Y's
statistics are photon-starved by design (β → bremsstrahlung is low-yield), consistent with the case
for variance reduction ([`variance_reduction.md`](variance_reduction.md)).

## 6. End-to-end control experiment (sinogram consistency)

`tests/control_experiment.py` exercises the whole imaging chain against a closed-form answer. An
off-axis point source (radial offset rho) is imaged over a 360 deg acquisition through a Symbia LEHR
collimator with forced detection. The SPECT consistency law requires the projected position of the
point to trace a **sinusoid of amplitude rho** versus view angle. The test decodes each view's
weighted centroid and checks: (a) fitted sinogram amplitude equals rho, (b) per-view centroid follows
`rho*cos(phi)` to sub-pixel residual, (c) counts are flat across views (uniform angular sensitivity for
a source at fixed radius in vacuum).

Result (rho = 60 mm, 12 views, LEHR): fitted amplitude **60.0 mm** (expected 60), max residual
**0.5 mm** (< one 4 mm pixel), count spread **0.0%**, so the acquisition geometry, detector projection,
forced detection, and multi-view motion are all correct end to end. The test exits non-zero on
failure, so it can gate a release.

## 7. topas-starguide retirement cross-check (2026-08-15)

topas-starguide is being retired in favor of this package (PI ruling 2026-08-15); its
`TsStarGuideDetector`, `TsScoreStarGuideProjection`, and `TsForcedDetectionProjection` were already
byte-identical to the copies here, and `TsParallelHoleCollimator` here is a strict generalization
(`HoleShape`/`Lattice`/`FocalLengthX,Y`, all defaulting to Round/Square/0 = parallel) of
topas-starguide's fixed round-hole, square-lattice implementation. To confirm the general collimator
reproduces topas-starguide's numbers rather than just algebraically, topas-starguide's own
`TsParallelHoleCollimator` was swapped for this package's version (defaults only, no new parameters
set) and its `examples/single_head_point_source.txt` deck (Tc-99m point source, single head,
`Ts/Seed=1`, 1e6 histories) was rerun before and after the swap, then again after swapping in this
package's `TsStarGuideDetector`/`TsScoreStarGuideProjection`/`TsForcedDetectionProjection` too.

Result: **byte-for-bit identical** output in all cases, same 43,942 scored rows, same order, same
values, both before/after the collimator swap and before/after the full four-file swap. The forced-
detection scorer was additionally smoke-tested standalone (50k histories) and produces the expected
5-column weighted output. No tolerance was needed because none of the differences are float-level , 
the geometry construction math is provably the same formula under matching defaults.

Not carried over from topas-starguide: an uncommitted, in-progress `ListMode` feature on the two
scorers (raw un-smeared/un-windowed event output for post-hoc Python-side acquisition sweeps), ruled
stale/abandoned by the PI and discarded rather than migrated. topas-starguide's OSEM reconstruction
and 7-module PyQt6 GUI are also not migrated here: the PI ruled that capability is superseded by the
lab's separate inverse-Monte-Carlo reconstruction work, not carried forward into this package.
