# Changelog

All notable changes to `topas-spect` are documented here. The format follows
[Keep a Changelog](https://keepachangelog.com/en/1.1.0/) and
[Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

### Added
- **Standard QA phantoms** (`phantoms/qa/`): Jaszczak Deluxe (water cylinder + cold-rod sectors + cold
  spheres for uniformity, resolution, and contrast), the NEMA IEC body phantom (IEC 61675-1 / NEMA NU
  2-2018, six hot fillable spheres in a warm body with a cold foam lung, selectable sphere:background
  ratio), and NEMA NU-1 line/point resolution sources. Geometry is traceable to manufacturer datasheets
  and standards (`research/knowledge/spect-qa-phantom-geometry/`); no dimension is invented.
- `tools/make_qa_phantom.py` generates the fragments (Jaszczak model, sphere ratio, with/without scatter).
- `examples/qa/` decks imaging each QA phantom through a Symbia LEHR head, and `docs/qa_phantoms.md`.
- Geometry renders (OpenTOPAS RayTracer) throughout the docs: collimator hole pattern, the three
- D-SPECT full-system geometry corrected to the published specs (JNMT 2020;48:297, JNM 2019;60:1194):
  nine 40x160 mm CZT columns in a 90-degree curved arrangement, each swiveling ~110 deg, aimed at the heart.
  full-system cameras (dual-head, StarGuide, D-SPECT), and the Jaszczak / NEMA IEC / patient phantoms.
- **`docs/validation.md` §7 QA-phantom tomographic recovery**: 60-view forced-detection SPECT of the
  QA phantoms (Symbia LEHR, 99mTc), reconstructed externally (FBP/MLEM). Adds tomographic spatial
  resolution (radial/tangential FWHM: 4.8/4.4 mm center, up to 6.1 mm peripheral), detector
  energy-response scatter fractions, and hot-sphere contrast recovery (largest spheres), with an
  honest statement that per-pixel image-quality metrics (uniformity, small-sphere contrast) are
  count-limited under the current partial-VR forced detection (motivating convolution-forced-detection).

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
