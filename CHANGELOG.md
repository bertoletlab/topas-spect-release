# Changelog

All notable changes to `topas-spect` are documented here. The format follows
[Keep a Changelog](https://keepachangelog.com/en/1.1.0/) and
[Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

## [1.2.0] - 2026-10-27

Archived at [10.5281/zenodo.23699194](https://doi.org/10.5281/zenodo.23699194). The concept DOI,
10.5281/zenodo.21251782, resolves to whichever version is newest.

### Added
- **`EDepSpectrum` can write each event in its detector head's own frame.** With
  `b:Sc/<name>/RecordLocalPosition = "True"`, six columns follow the default four: global `Z`, the
  energy-weighted centroid `U`, `V`, `W` in the frame of the scored component's envelope volume,
  `DetectorID` (from `i:Sc/<name>/DetectorID`, default 0) and `Run`, the TOPAS run index, which
  advances once per sequential time bin. Heads that orbit, by a time feature or by one deck per
  view, can then be binned into projections event by event, with one row per event at its
  centroid as an Anger camera records it. Each row carries its centroid in both frames, so a rigid
  fit per (`Run`, `DetectorID`) recovers every head pose from the output. Without the parameter the
  scorer writes the four columns it wrote before: a 140.5 keV energy-response deck gave a
  byte-identical file, 90,190 rows, against a binary built before the change.
- `EDepSpectrum` counts events whose deposits carry different weights and reports the count at the
  end of each run. Summing one event mixes split tracks, so such events do not describe a single
  photon.
- `tests/test_event_frame.py`, a value gate for the head-frame columns: static heads at 0, 90, 180
  and 270 degrees, the four angles as sequential time bins on worker threads, two opposed heads,
  and the overlap check at every angle (`docs/validation.md` section 8).
- `examples/acquisition/dualhead_orbit_acquire.txt`: a dual-head Symbia LEHR orbit around a water
  cylinder holding an off-axis Tc-99m point source, with one head-frame scorer per head.
- `docs/reconstruction_interop.md` section 2.6: the head-frame columns, the pose fit, and the
  mapping of the `Ge/Gantry/RotY` pattern onto a right-handed reconstruction frame.

### Fixed
- `examples/acquisition/dualhead_orbit.txt` now ends its orbit at 180 deg, as its comment states. TOPAS
  spaces N sequential times by (end - start) / N, so 37 times over 36 s put the last view at 175.1 deg;
  the timeline now runs 37 s, one view every 5 deg.

## [1.1.1] - 2026-10-26

Archived at [10.5281/zenodo.23674918](https://doi.org/10.5281/zenodo.23674918). The DOI in
`CITATION.cff` and the README badge is the concept DOI, 10.5281/zenodo.21251782, which resolves to
whichever version is newest.

### Fixed
- **The forced-detection crystal-efficiency default read 14 percent high at 300 keV and 20.5 percent
  high at 364.5 keV.** The compiled `TsForcedDetectionProjection` default had six points (113, 140.5,
  208, 218, 250 and 440 keV) and nothing between 250 and 440 keV, so linear interpolation across that
  gap ran above the real, convex efficiency curve. At 364.5 keV it returned 0.2487 against a measured
  0.2065, and forced-detection sensitivity for any deck windowed in that band (I-131 at 364.5 keV,
  Ga-67 at 300 keV) was high by the same factor. The default is now a 14-point table measured by full
  Geant4 transport into a bare 9.5 mm NaI slab (normal incidence, 20 percent window, unsmeared):
  113, 140.5, 159, 171, 200, 208, 218, 245, 250, 300, 364.5, 440, 500 and 1000 keV. It reproduces
  the six previously tabulated points to 0.4 percent, so decks at 140.5 and 208 keV do not change.
  Decks that declare their own `CrystalEfficiencyEnergies` and `CrystalEfficiencyValues` are
  unaffected. The table is for 9.5 mm NaI only, as the code comment and `docs/variance_reduction.md`
  now say; any other crystal must declare its own. Outside 113-1000 keV the end values are still held
  constant, with the existing one-time warning above the last point, now documented in
  `docs/validation.md`.
- **A converging or diverging `TsParallelHoleCollimator` had a lead wedge over the entrance and exit
  of every tilted channel.** Each tilted channel was shortened until the rim of its end cap touched
  the slab face, which left the rest of the cap inside the slab and a wedge of septum material over
  the aperture, about the hole's circumradius times the sine of the tilt thick and largest at the
  field edge. Photons travelling along a channel's axis had to cross it. On the Table S1 collimator
  (hex 1.5 mm holes, 0.2 mm septa, 35 mm long, 90 x 90, f = 350 mm) with lead at 100 times density, a
  140.5 keV source on axis kept 758 of 19,209 photopeak counts (0.039) in the converging build and
  8,546 of 9,024 (0.947) in the parallel one, so even the channel hit by the on-axis source was
  nearly closed. Every tilted channel is now built long and cut flush by the two face planes
  (`G4IntersectionSolid`), so each bore spans the slab face to face with nothing over the aperture
  and nothing protruding; the same build now keeps 19,307 of 20,585 (0.938). A beam along the axis of
  an outer channel in a 10 mm-hole test collimator with lead at 100 times density went from 0 to
  100.01 percent transmitted. Converging and fan-beam collimators therefore score higher
  sensitivity than before (the on-axis Table S1 count rose 7 percent, 19,209 to 20,585), and
  off-axis sensitivity no longer falls with field position because of the wedge. The inter-channel
  clearance check now uses each channel's full face-to-face axis segment, as it should have.
  Parallel collimators, and the untilted channels of a fan-beam one, are built exactly as before
  and give identical counts (9,024 and 8,546 on the same decks). Converging builds hold one small
  solid per distinct tilt, so a 90 x 90 cone-beam collimator now carries 8,100 solids against 4,095
  before, and tracking through them is about 10 percent slower.
- **The focal length of a converging or diverging collimator is measured from its mid-plane, and
  the documentation said the front face.** Channel axes have always passed through the focus at
  `FocalLengthX`/`FocalLengthY` from the collimator's mid-plane; `docs/design.md` and the header of
  `TsParallelHoleCollimator.cc` described it as the face-to-focus distance. Only the documentation
  changes: for a 35 mm collimator at f = 350 mm the focus sits 332.5 mm from the face, and the
  measured magnification of the Table S1 deck, 1.596, matches the mid-plane closed form 1.602.
- **`tests/test_physics_golden.py` carried a copy of the six-point table** in its generated decks. It
  now carries the 14-point table.

### Added
- **Forced detection now refuses a converging, diverging or fan-beam collimator.**
  `TsForcedDetectionProjection` has no focal-length concept and models parallel holes, so pairing it
  with a `TsParallelHoleCollimator` that has `FocalLengthX` or `FocalLengthY` set silently gave the
  wrong acceptance and field of view, with no warning. Initialization now fails with a `G4Exception`
  (`SPECT_FD_NonParallelCollimator`) when the scorer is given a nonzero `FocalLengthX`/`FocalLengthY`
  or when the same deck defines a `TsParallelHoleCollimator` with one. The message points to full
  transport (`TsEDepSpectrum`) for those collimators. Parallel-hole decks are unaffected. Documented in
  `docs/variance_reduction.md`.

## [1.1.0] - 2026-10-14

Archived at [10.5281/zenodo.23370949](https://doi.org/10.5281/zenodo.23370949). The DOI in
`CITATION.cff` and the README badge is the concept DOI, 10.5281/zenodo.21251782, which resolves to
whichever version is newest.

### Added
- **Documentation for the two bundled sources** (`docs/sources.md`), which had none: `SpectDecaySource`
  and `Y90BremsSource`, including the one-photon-per-history contract that makes per-history results
  per emitted photon rather than per decay, where the tabulated Y-90 photon spectra come from, and
  what that source is not a substitute for (beta dosimetry).
- **`docs/reconstruction_interop.md` and `docs/sources.md` added to the docs navigation.** The export
  contract shipped in the repository but was absent from the built site.
- **`tools/audit_decks.py`, previously maintainer-only.** It checks a deck's declared septal
  attenuation against a tabulation at the deck's own beam energy, which is a check users need for
  their own decks and not only we do; the shipped physics gate also calls it, so leaving it out of the
  release left that suite unable to run.
- **`tools/make_ring_motion.py --acquire` writes an acquiring ring deck, not only a motion deck**, and
  the generated StarGuide example ships as `examples/acquisition/starguide_ring_acquire.txt`. The
  motion deck carried geometry and time features with no activity and no scorer, so the twelve-column
  acquisition the companion paper shows could not be reproduced from the public package: its sources
  and scorers lived in laboratory campaign generators that are not released. The new mode adds a
  volumetric 99mTc background in the body phantom, two hot lesions, and one `StarGuideProjection`
  scorer per column, pixelated from the panel dimensions (`--pixel`, default 2.46 mm, giving 16 x 112
  pixels on the StarGuide panel). `--histories` sets the background histories per time step and each
  lesion takes five percent of it. Verified end to end: the shipped deck builds with no overlapping
  volumes and writes twelve projection files in 98 s on four threads.

### Fixed
- **`docs/variance_reduction.md` described the pre-fix forced-detection scorer as the design.** It
  called the transmission "uncalibrated: geometric only, with penetration off", using the physical
  hole length in place of the effective length, and told users to calibrate the absolute scale around
  the resulting shortfall. That shortfall was the transmission bug this release line fixed. The
  section now states the acceptance model, the traced penetration, the per-energy coefficient and the
  processes the scorer omits, and the mid-plane registration trap is a warning box rather than a
  sentence in passing. `README.md` called the model "Metz-Frey", which it is not.
- **`docs/design.md` did not mention that a converging collimator can refuse to build.** A user whose
  deck aborts on inter-channel clearance now finds the reason, and what to change, in the docs.

### Changed
- **Septal attenuation is now tabulated per photon energy, and `Sc/<name>/SeptalMuPerMM` is
  optional.** It was a single scalar applied to every photon regardless of energy, which a real
  source cannot tolerate: lead attenuates 4.5 times more strongly at 113 keV than at 208 keV and
  177Lu emits both, while across one 20 percent photopeak window the coefficient still varies by 65
  percent. Measured on the Symbia LEHR geometry, a deck pinning the 140.5 keV coefficient and then
  scoring the 208 keV line reads **4.8512e-05 against a correct 8.8276e-05, 82 percent low**.
  Leaving the parameter unset now applies a NIST XCOM tabulation for the septum material at each
  photon's own energy, with lead's K edge at 88 keV and tungsten's at 69.5 keV resolved.
  `Sc/<name>/SeptalMaterial` selects the material (default `G4_Pb`), and
  `Sc/<name>/SeptalMuEnergies` with `Sc/<name>/SeptalMuValues` supply a tabulation for anything
  else, matching how this scorer already takes crystal efficiency and energy resolution. A deck
  that pins a coefficient keeps it, and reproduces its previous result to 4 parts in a million.
  This is a tabulation rather than a call into Geant4 on purpose: `G4EmCalculator` from a worker
  thread forces `G4PenelopePhotoElectricModel::ReadDataFile()`, which is master-thread-only, and
  aborts the run.
- **The ray trace no longer bakes in an attenuation coefficient.** How much lead a ray crosses
  depends on the hole array and the angle alone, so the trace now happens once and every
  coefficient is evaluated on it. Without that, a per-energy coefficient would mean re-tracing per
  energy.
- **`Sc/<name>/PenetrationModel` now defaults to `"RayTrace"`.** The scorer walks each ray through
  the hole array in depth, accumulates the lead it crosses, and averages `exp(-mu*l)` over entry
  position within one aperture and over azimuth, tabulating the result once per geometry at
  construction. Across 14 configurations, five collimators and 113 keV to 1 MeV, the end-to-end
  ratio against full transport runs **0.752 to 1.103, mean absolute error 7.5 percent**, where the
  effective-length correction on the same set ranged from 0.014 to 1.02. `"EffectiveLength"` remains
  selectable and costs one table build less, about 4 to 5 s; per photon the two are the same price,
  since both reduce to a table lookup (measured at 4e7 histories: 21.5 s against 25.8 s of wall
  clock, 208 s against 209 s of CPU).
- **`Sc/<name>/MinTransmissionFraction` now defaults to `1e-6`, from `1e-4`.** The floor bounds the
  output file, and the previous default was discarding most of the penetration tail the ray trace
  exists to produce. On the Symbia LEHR geometry it removed 0.66 percent of the integral, cut the
  modeled support from 118.2 mm to 18.7 mm, and dropped the signal beyond r = 12.3 mm from 0.723
  percent to 0.069 percent against 0.881 percent under full transport: with the floor at 1e-6 the
  model reproduces that transported tail to 0.16 percentage points. The integral is converged there
  (0.009 percent from its value at 1e-9). It never saved run time, contrary to the comment it
  carried (108.7 s against 110.2 s of CPU at 2e7 histories); it saves output size, 56,809 entries
  against 1,559,729 for that run. Raise it when file size matters more than the tail.
  All 14 validation configurations were re-run at the new default: only GE HEGP at 364.5 keV moved
  by more than 0.3 percent, from 1.103 to 1.110 against full transport, and the range over the set
  is 0.752 to 1.110 with a mean absolute error of 7.5 percent.

### Added
- **Aluminium entrance window in `examples/source/decay_source.txt`**: a 2 mm `G4_Al` slab in front
  of the crystal, with a note explaining why it is load-bearing rather than decorative. A decay
  source emits betas, conversion electrons and alphas alongside the gammas; in the example's vacuum
  world those crossed to a bare NaI slab and deposited their full kinetic energy, which no real
  camera records, since every gamma camera has a metal entrance window. Measured on the shipped
  geometry with a particle-tagged phase space (2.4e6 decays): **177Lu** scored 7,639 electrons
  against 123,271 gammas entering the crystal, 5.8 percent of arrivals and 4.7 percent of counts in
  the plotted band; the window cuts that to 553 (−93 percent) at a cost of 7.1 percent of the
  gammas. **225Ac** was worse, scoring alphas directly: maximum deposit 25.6 MeV and 1.204 percent
  of counts above 3 MeV, both of which the window removes completely (alphas 437 → 0, maximum
  deposit 2.03 MeV, above 3 MeV 0.000 percent). Photopeak positions and the 177Lu 208:113
  shape-normalized peak-height ratio (1.107) are unaffected either way; the error was confined to
  the continuum, which the bare-crystal deck inflated by 13-16 percent between and above the peaks
  and by up to 70 percent per bin immediately above the 208 keV line.
- `docs/building.md`: a required-OpenTOPAS-version section. On arm64, radioactive-decay sources need
  OpenTOPAS commit `404fe33` or newer; without it the at-rest ion momentum can evaluate to NaN under
  FMA contraction and every decay happens on the source volume's `+z` surface instead of throughout
  its volume. Per-nuclide and per-build (90Y affected, 177Lu and 225Ac not), so the section ships a
  vertex-tagged self-test rather than a list of safe nuclides.
- `docs/validation.md` §7: topas-starguide retirement cross-check. Confirmed byte-for-bit identical
  output before/after swapping topas-starguide's `TsParallelHoleCollimator`,
  `TsStarGuideDetector`, `TsScoreStarGuideProjection`, and `TsForcedDetectionProjection` for this
  package's copies, same seed, same deck. topas-spect is now the single source of truth for all
  four extensions; topas-starguide's own copies are retiring (PI ruling 2026-08-15).

### Fixed
- **The scorer reports the attenuation coefficient it is using.** `SeptalMuPerMM` defaults to
  tungsten at 3.56/mm while most clinical parallel-hole collimators are lead, and
  `examples/vr/forced_detection.txt` models a Symbia LEHR collimator without setting it: with
  penetration enabled it ran lead septa at a tungsten coefficient and read 7.4321e-05 instead of
  7.6833e-05. The scorer now names the coefficient whenever penetration is on and the deck left it
  implicit, the example sets it, and the example's header no longer calls the model uncalibrated
  with a counts-per-decay figure that predates the transmission fix.
- **The `RayTrace` guard no longer rejects decks that never asked for penetration.** Aborting when
  `PenetrationModel` was set to `"RayTrace"` while `IncludeSeptalPenetration` was false made sense
  while ray tracing had to be opted into; as a default it broke every deck that leaves penetration
  alone, `examples/vr/forced_detection.txt` included. The guard now fires only on an explicit
  request, and the transmission table is built only when penetration is on.
- **The discarded weight is counted rather than predicted.** Workers accumulate it, the master
  collects it in `AbsorbResultsFromWorkerScorer`, and it is reported once per run. An earlier
  analytic estimate from the transmission table overstated it badly where the widest angles miss
  the detector and would be rejected anyway, predicting 5.6 percent for a deck whose true loss is
  0.00 percent. Checked against measurement at three geometries: 0.00006 reported against 0.000
  measured, 0.185 against 0.174, 0.657 against 0.655 percent.
- **Two design/component examples whose collimators did not shadow their crystals.**
  `examples/component/collimator_detector_response.txt`: channel grid 80 x 80 -> **117 x 117**. At 80
  the collimator spanned +-50.7 mm under a +-64 mm crystal, so photons reached the crystal edge
  without crossing any channel. That artifact appears as a plateau at large radius, where
  this example's septal-penetration TAIL is read, so it corrupted the penetration tail this deck exists to
  demonstrate. `examples/design/converging_conebeam.txt`: crystal +-120 mm -> **+-56 mm**, channel
  grid unchanged at 90 x 90, which shadows +-66.4 mm. The magnified image of its off-axis point lands
  near 40 mm and still fits. Enlarging the collimator to 152 x 177 would also have worked, at roughly
  three times the transport cost, which is not worth it for a geometry demonstration. Note that a
  converging collimator legitimately images onto a detector wider than itself, so a narrower
  collimator there is a design choice rather than a defect; the crystal was trimmed only so that a
  whole-crystal total means something.
- **Symbia LEHR sensitivity corrected from 8.6e-5 to 7.5e-5 counts/decay, ratio 0.95 to 0.82.**
  `examples/system/symbia_lehr_sensitivity.txt` built a 152.4 mm collimator over a 180 mm crystal, so
  a photon aimed at the crystal edge left through the collimator's side after crossing only 21 percent
  of its length and reached the detector through far less lead than a full-length path. Measured at
  2e8 histories: 8.6075e-05 against 7.4870e-05 after correcting the hole counts to 158 x 158, a
  **13.0 percent** inflation, and scoring by max(|x|,|y|) puts every one of the 2241 excess counts in
  the 76.1 to 90 mm band the collimator failed to cover while counts inside the footprint are
  identical (14969 against 14969). An opaque-septa control identifies the mechanism as peripheral
  septal penetration rather than photons bypassing the collimator: it moves only 0.05 percent under
  the identical coverage change (6.9620e-05 to 6.9585e-05), whereas a bypassing path contains no
  collimator at all and would have shown the excess in both arms.
- **`docs/validation.md` sections 3 and 4 rewritten.** The Symbia LEHR ratio of 0.95 was two
  compensating errors: the 15 percent inflation above, and a 30 percent deficit because the preset
  builds round holes on a square lattice for a camera whose collimator is hexagonal on a hexagonal
  lattice (measured hex/round 1.432, derived 1.404). Modeled with hexagonal holes and correct
  coverage the row reads 1.07e-04, ratio 1.18. Section 4's claim that the absolute scale is anchored
  to within 5 percent is withdrawn. Section 4's geometric-resolution claim is replaced by simulated
  point-spread widths: it previously combined an analytic resolution with an intrinsic term and
  compared it against 7.2 / 9.7 / 12.1 mm described as "the published planar FWHM", but those are
  Sawant 2025 Table 2, the SIMIND study already supplying the sensitivity references, so one
  source was counted twice. `systems/symbia_lehr.txt` now records that it builds round holes despite
  quoting hexagonal flat-to-flat dimensions, and how to switch it.
- **`SESSION_KICKOFF.md`**: withdrew the pre-fix forced-detection finding (~1.3e-5, "7x low"), which
  came from the superseded transmission formula. The corrected scorer gives 6.93e-5 and recovers
  91 percent of full transport at 140.5 keV.
- **`Ts/ShowHistoryCountAtInterval` set in all 20 example decks that run histories.** None of them set
  it, so TOPAS printed a progress line for EVERY history: running `examples/system/ge_lehr_sensitivity.txt`
  as shipped wrote a **1.8 GB log in five minutes** and ran roughly ten times slower, because the
  worker threads serialize on that output. The interval is scaled per deck to about ten progress lines
  (2,000 for the 20k-history `decay_source.txt` up to 20,000,000 for the 2e8-history
  `symbia_lehr_sensitivity.txt`). Verified on `ge_lehr_sensitivity.txt`: log 1.8 GB -> **28 KB**, 10 progress
  lines, 50.7 s wall clock, and the scored sensitivity unchanged at 9.1260e-05 counts/decay
  (ratio 1.0000 against that identical deck run before this change). The three `examples/acquisition/` decks
  are untouched: they are geometry and motion demonstrations with no radiation source.
- **Hexagonal-lattice hole counts in `examples/system/ge_lehr_sensitivity.txt` (111x111 -> 117x136)
  and `ge_megp_sensitivity.txt` (50x50 -> 49x58)**, so each collimator covers its crystal on BOTH
  axes. `TsParallelHoleCollimator` stacks hex-lattice rows at `pitch*sqrt(3)/2`, so an `NHolesY` sized
  from the pitch, which is the square-lattice formula, builds a collimator only 0.866 as tall as
  intended: LEHR covered ±81.8 mm and MEGP ±87.7 mm against a ±90 mm crystal. Both headers had
  asserted the opposite ("sized wider than the crystal so no photons bypass its edge"), and each now
  documents the row-spacing trap instead. Sensitivity effect measured at 5e7 histories: LEHR
  9.1500e-05 -> 9.1260e-05 (0.997), MEGP 9.3160e-05 -> 9.3980e-05 (1.009), both inside the ~1.5
  percent statistical error, so **the published values for these two presets stand**. The deficit is
  benign here because an oblique ray aimed at the crystal edge still traverses 66 and 94 percent of the
  collimator length respectively; an identical deficit costs a factor of 26.7 in a geometry where that
  fraction falls to zero. The hole geometry itself was verified independently: normal-incidence
  transmission reproduces the exact open-area fraction to within 0.4 percent for hex-on-hex,
  square-on-square, round-on-square and round-on-hex, and the GE datasheet's stated hole counts (LEHR
  86300, LEGP 56560, MEGP 15210, HEGP 7410) come out to better than 0.1 percent from the hex cell area
  `(sqrt(3)/2)p^2`, against 0.866 of the stated count for a square cell.
- `docs/validation.md`: corrected the 364 keV LEHR:HEGP septal-penetration ratio from a stale hand-carried
  value of 64.8 to **59.5**, the run-of-record from the pinned build `89d9c87` (LEHR 118215 / HEGP 1988
  counts, `resim/pulled/s45/`). The 64.8 figure predated the pinned build and was never regenerable; the
  companion manuscript already reports 59.5. SIMIND reference (67.7) unchanged.

## [1.0.0] - 2026-07-07

First stable, citable release: the reference version used to produce the results in the OpenTOPAS-SPECT
companion paper. Tagging the validated state for citation, and marking the public API (extension
classes and their parameters) as stable. No breaking changes from 0.2.0.

### Added
- **Scatter-order separation in `TsForcedDetectionProjection`**: a new boolean parameter
  `b:Sc/<name>/RecordScatterOrder = "True"` appends a 6th ntuple column, `ScatterOrder`, that separates
  primary (0, unscattered) from scattered (1, scatter-order >= 1) photons so the two can be summed into
  independent tallies (e.g. for full-MC-vs-fast-model validation). A photon is flagged scattered iff its
  momentum direction at the FD surface differs from its emission (vertex) direction, which catches
  Compton **and** Rayleigh deflections (an energy-only split would misclassify Rayleigh photons, which
  keep the line energy, as primary). Default `False` preserves the original 5-column format. Verified
  with 208 keV gammas through a water slab: all ScatterOrder=0 rows at 208 keV, ScatterOrder=1
  rows Compton-down-scattered (mean ~145 keV) plus the Rayleigh photons at 208 keV.
- **GE collimator sensitivity validation** (`examples/system/ge_{lehr,megp,hegp}_sensitivity.txt`):
  shipped decks that reproduce the absolute planar sensitivity of the GE LEHR/MEGP/HEGP presets at
  140.5 keV (9.15/9.32/1.09e-4 counts/decay), plus a septal-penetration decomposition (opaque-septa
  geometric-only reference at 140 keV; LEHR/HEGP ratio at 364 keV). `docs/validation.md` section 4
  updated with the reproducible numbers.
- **Continuous integration** (`.github/workflows/build.yml`): compiles and links the extension against
  several released OpenTOPAS versions (Docker Hub `opentopas/opentopas` images spanning Geant4 11.3.2
  and 11.1.3), and runs `tests/smoke_decks.sh` on the pinned latest with cached Geant4 data. A
  non-blocking `latest`-image canary (weekly schedule) surfaces upstream API changes early. This is the
  first cross-version CI for a TOPAS/OpenTOPAS extension.
- **Forward-looking source-build canary** (`.github/workflows/canary-geant4.yml`): weekly, builds the
  latest Geant4 (11.4.x) and OpenTOPAS `main` from source with the extension and runs the smoke test, to
  catch toolchain drift before any prebuilt image exists on the newest Geant4. Non-blocking.

### Fixed
- `docs/validation.md` section 4: corrected the Siemens Symbia LEHR sensitivity row (had the measured
  9.1e-5 / ratio 1.00 entered as the Monte Carlo result; the actual MC value is 8.6e-5 / ratio 0.95,
  consistent with section 3), and refined the SIMIND-discrepancy explanation (dominated by geometric
  open-area, not septal penetration, which is only ~3-5% at 140 keV).

### Removed
- **`tools/make_dynamic_acquisition.py` and the dynamic-acquisition documentation are no longer part of
  the public package.** Deck/campaign generators are lab-private glue that wire the public building
  blocks for a specific job; the generator (and its test) moved to a private home. The public package
  ships building blocks and single-deck examples only. Multi-view acquisitions remain demonstrated by
  `examples/phantom/spect_acquisition.txt`, and the forced-detection per-view technique is documented in
  `docs/variance_reduction.md`.

## [0.2.0] - 2026-07-04

### Added
- **Standard QA phantoms** (`phantoms/qa/`): Jaszczak Deluxe (water cylinder + cold-rod sectors + cold
  spheres for uniformity, resolution, and contrast), the NEMA IEC body phantom (IEC 61675-1 / NEMA NU
  2-2018, six hot fillable spheres in a warm body with a cold foam lung, selectable sphere:background
  ratio), and NEMA NU-1 line/point resolution sources. Geometry is traceable to manufacturer datasheets
  and standards (`research/knowledge/spect-qa-phantom-geometry/`); no dimension is invented.
- `tools/make_qa_phantom.py` generates the fragments (Jaszczak model, sphere ratio, with/without scatter).
- `examples/qa/` decks imaging each QA phantom through a Symbia LEHR head, and `docs/qa_phantoms.md`.
- Geometry figures throughout the docs: the collimator hole pattern, the three full-system cameras
  (dual-head, StarGuide, D-SPECT) as OpenTOPAS Qt viewer snapshots, and the Jaszczak / NEMA IEC / patient
  phantoms.
- Contributor guide, code of conduct, security policy, issue and pull-request templates, and a
  `.zenodo.json` for a citable DOI on release; a runtime-environment section in the installation docs
  (Geant4 data paths and the `TOPAS_G4_DATA_DIR` pitfall).

### Fixed
- D-SPECT full-system geometry corrected to the published specs (Garcia AAPM syllabus; JNMT 2020;48:297;
  JNM 2019;60:1194): nine 40 x 160 mm CZT columns conforming to the left chest, each aimed at the heart
  and fanning ~110 deg, verified collision-free through the swivel.

## [0.1.1] - 2026-07-03

First public release. `topas-spect` adds SPECT imaging to OpenTOPAS as self-contained extension classes
built through the standard `TOPAS_EXTENSIONS_DIR` mechanism, with no changes to the OpenTOPAS core.

### Geometry
- `TsParallelHoleCollimator`: round, hex, or square holes on a square or hex lattice; a focal length
  turns it into a converging fan or cone-beam (or diverging) collimator.
- `TsPinholeCollimator`: pinhole and multi-pinhole (absorber plate with knife-edge apertures).
- `TsSlitSlatCollimator`: a slit aperture crossed with parallel slats.
- `TsStarGuideDetector`: a GE StarGuide-style pixelated CZT ring.
- CAD import through the native `TsCAD` component (binary STL / ASCII PLY) for novel single-piece parts.

### Scoring
- `TsEDepSpectrum`: energy-resolved pulse-height response per event, with a `Weight` column so it is
  correct under variance reduction (histogram `Edep` weighted by `Weight`).
- `TsForcedDetectionProjection`: forced-detection projection scorer using an analytic Metz-Frey
  collimator transmission.
- `TsScoreStarGuideProjection`: pixel-binned projection with Gaussian energy smearing.

### Source
- `SpectDecaySource`: a radioactive decay source bundled with topas-spect. It emits a parent
  radionuclide (`GenericIon(Z,A)`) uniformly in a component and scales the histories per `Tf` time bin
  by the true activity decay (half-life from Geant4). The deck's `g4radioactivedecay` physics produces
  the whole decay chain from each emitted ion, so daughter photons appear natively (verified: 177Lu
  208/113 keV; 225Ac daughters 221Fr 218 keV and 213Bi 440 keV). No external extension is required.

### Physics
- `TsPenelopeNoRayleigh`: `G4EmPenelopePhysics` with Rayleigh scattering removed for gamma.

### Presets and phantoms
- Camera and collimator presets in `systems/` for Siemens Symbia and GE cameras (LEHR through HEGP)
  on NaI or CZT, plus StarGuide and D-SPECT, each with a sourced geometry and a reliability grade.
- Composable analytic patient phantoms (native shapes plus one volumetric source per hot region) with
  literature-sourced uptake scenarios for 177Lu-PSMA, 225Ac-PSMA, 90Y radioembolization, and 131I-mIBG,
  plus the native XCAT voxel path.

### Tools
- `make_dynamic_acquisition.py`: generate a SPECT acquisition from a phantom scenario using clinical
  console parameters (projections, time per projection, angular range, detector heads, matrix,
  photopeak window, orbit radius), coupling detector motion and isotope decay on one clock. Engines:
  `--decay` (bundled `SpectDecaySource`, native chain), native (per-projection `exp()` decay), and
  `--vr` (forced-detection fast path).
- `make_ring_motion.py`: predict collision-safe, contour-hugging detector motion from a patient
  contour (DICOM CT/PET, an analytic ellipse, or measured radii) for StarGuide or a generic ring.

### Variance reduction
- Forced detection as the recommended SPECT fast path (`examples/vr/forced_detection.txt`), and the
  native OpenTOPAS `Vr/` toolkit with the mandatory validate-against-unbiased-baseline workflow
  (`examples/vr/native_vr.txt`).

### Validation and documentation
- Validation suite: software tests (`tests/test_ring_motion.py`, `tests/test_dynamic_acquisition.py`),
  a deck smoke test (`tests/smoke_decks.sh`), and an end-to-end control experiment
  (`tests/control_experiment.py`, sinogram consistency). Scientific validation in `docs/validation.md`
  (energy response, distance-dependent resolution, absolute and multi-system sensitivity, phantom
  consistency, control experiment).
- A full MkDocs user guide (`docs/`, hosted on Read the Docs) and a landing website (`website/`).
