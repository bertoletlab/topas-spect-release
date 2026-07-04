# Changelog

All notable changes to `topas-spect` are documented here. The format follows
[Keep a Changelog](https://keepachangelog.com/en/1.1.0/) and
[Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

### Added
- **GE collimator sensitivity validation** (`examples/system/ge_{lehr,megp,hegp}_sensitivity.txt`):
  shipped decks that reproduce the absolute planar sensitivity of the GE LEHR/MEGP/HEGP presets at
  140.5 keV (9.15/9.32/1.09e-4 counts/decay), plus a septal-penetration decomposition (opaque-septa
  geometric-only reference at 140 keV; LEHR/HEGP ratio at 364 keV). `docs/validation.md` section 4
  updated with the reproducible numbers.

### Fixed
- `docs/validation.md` section 4: corrected the Siemens Symbia LEHR sensitivity row (had the measured
  9.1e-5 / ratio 1.00 entered as the Monte Carlo result; the actual MC value is 8.6e-5 / ratio 0.95,
  consistent with section 3), and refined the SIMIND-discrepancy explanation (dominated by geometric
  open-area, not septal penetration, which is only ~3-5% at 140 keV).
- **Continuous integration** (`.github/workflows/build.yml`): compiles and links the extension against
  several released OpenTOPAS versions (Docker Hub `opentopas/opentopas` images spanning Geant4 11.3.2
  and 11.1.3), and runs `tests/smoke_decks.sh` on the pinned latest with cached Geant4 data. A
  non-blocking `latest`-image canary (weekly schedule) surfaces upstream API changes early. This is the
  first cross-version CI for a TOPAS/OpenTOPAS extension.
- **Forward-looking source-build canary** (`.github/workflows/canary-geant4.yml`): weekly, builds the
  latest Geant4 (11.4.x) and OpenTOPAS `main` from source with the extension and runs the smoke test, to
  catch toolchain drift before any prebuilt image exists on the newest Geant4. Non-blocking.
- **Scatter-order separation in `TsForcedDetectionProjection`**: a new boolean parameter
  `b:Sc/<name>/RecordScatterOrder = "True"` appends a 6th ntuple column, `ScatterOrder`, that separates
  primary (0, unscattered) from scattered (1, scatter-order >= 1) photons so the two can be summed into
  independent tallies (e.g. for full-MC-vs-fast-model validation). A photon is flagged scattered iff its
  momentum direction at the FD surface differs from its emission (vertex) direction, which catches
  Compton **and** Rayleigh deflections (an energy-only split would misclassify Rayleigh photons, which
  keep the line energy, as primary). Default `False` preserves the original 5-column format. Verified
  with 208 keV gammas through a water slab: all ScatterOrder=0 rows at exactly 208 keV, ScatterOrder=1
  rows Compton-down-scattered (mean ~145 keV) plus the Rayleigh photons at 208 keV.

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
