<p align="center">
  <img src="docs/assets/lockup.png" alt="OpenTOPAS-SPECT" width="460">
</p>

<p align="center"><b>OpenTOPAS extensions for SPECT imaging simulation.</b></p>

<p align="center">
  <a href="https://github.com/bertoletlab/topas-spect-release/actions/workflows/build.yml"><img src="https://github.com/bertoletlab/topas-spect-release/actions/workflows/build.yml/badge.svg" alt="build"></a>
  <a href="LICENSE"><img src="https://img.shields.io/badge/License-MIT-blue.svg" alt="License: MIT"></a>
  <a href="https://topas-spect-release.readthedocs.io/"><img src="https://img.shields.io/badge/docs-Read%20the%20Docs-8CA1AF?logo=readthedocs&logoColor=white" alt="Documentation"></a>
  <a href="https://github.com/bertoletlab/topas-spect-release/releases"><img src="https://img.shields.io/github/v/release/bertoletlab/topas-spect-release?label=release&color=1f2a56" alt="Release"></a>
  <img src="https://img.shields.io/badge/OpenTOPAS-4.2.p2-1f2a56" alt="OpenTOPAS 4.2.p2">
</p>

OpenTOPAS is a powerful, well-validated Monte Carlo platform, but it ships without
the geometry and scoring components needed to model a clinical SPECT system:
a physical parallel-hole collimator, an energy-resolved detector response, and
efficient projection scoring. OpenTOPAS-SPECT adds those pieces, bringing GATE-style
SPECT modeling (explicit collimator transport, crystal pulse-height response,
forced-detection projections) into the OpenTOPAS parameter-file workflow.

The extensions are self-contained OpenTOPAS classes; no changes to the OpenTOPAS core are
required. They are built through the standard `TOPAS_EXTENSIONS_DIR` mechanism.

## Contents

| Category | Class | Purpose |
|----------|-------|---------|
| Geometry | `TsParallelHoleCollimator`   | Parallel-hole collimator: round/hex/square holes on a square/hex lattice; a focal length turns it into a converging fan/cone-beam (or diverging) collimator. |
| Geometry | `TsPinholeCollimator`         | Pinhole and multi-pinhole collimator: absorber plate with knife-edge aperture(s). |
| Geometry | `TsSlitSlatCollimator`        | Slit-slat collimator: slit aperture crossed with parallel slats. |
| Geometry | `TsStarGuideDetector`         | GE StarGuide-style pixelated CZT ring detector. |
| Scoring  | `TsEDepSpectrum`              | Energy-resolved detector response: per-event deposited-energy spectrum R(E_true -> E_meas), plus energy-weighted interaction position. |
| Scoring  | `TsForcedDetectionProjection` | Forced-detection (variance-reduction) projection scorer using an analytic Metz-Frey collimator transmission. |
| Scoring  | `TsScoreStarGuideProjection`  | Pixel-binned projection scorer with Gaussian energy smearing for the StarGuide geometry. |
| Source   | `SpectDecaySource`            | Radioactive decay source: emits a parent radionuclide uniformly in a component and scales histories per `Tf` time bin by the true activity decay; `g4radioactivedecay` produces the full chain. |
| Physics  | `TsPenelopeNoRayleigh`        | `G4EmPenelopePhysics` with Rayleigh scattering removed for gamma (clean single-Compton bookkeeping). |

## Requirements

- OpenTOPAS 4.2.p2
- Geant4 11.2.2 (geant4-11-02-patch-02)

## Building

Point `TOPAS_EXTENSIONS_DIR` at this repository (subdirectories are scanned
recursively) when configuring the OpenTOPAS build:

```bash
cmake -S /path/to/OpenTOPAS \
      -B /path/to/OpenTOPAS-build \
      -DTOPAS_EXTENSIONS_DIR="/path/to/topas-spect" \
      -DGeant4_DIR=/path/to/geant4/lib/cmake/Geant4 \
      -DCMAKE_BUILD_TYPE=Release

cmake --build /path/to/OpenTOPAS-build --parallel 8
```

To combine with other extension sets, separate paths with `;`:
`-DTOPAS_EXTENSIONS_DIR="/path/to/topas-spect;/path/to/other-extensions"`.

## Quick start

Worked parameter files are in [`examples/`](examples/), split into single-component
characterisations and full-system runs:

- `component/energy_response.txt`, mono-energetic beam into a bare crystal slab; produces
  the crystal pulse-height spectrum R(E_true -> E_meas) (`TsEDepSpectrum`).
- `component/collimator_detector_response.txt`, point source in front of a collimator +
  crystal; produces the distance-dependent point-spread function and penetration tail.
- `system/symbia_lehr_sensitivity.txt`, isotropic point source + collimator + crystal; the
  ratio of photopeak-window counts to emitted decays gives the absolute system sensitivity.
  Selects the scanner geometry with a single `IncludeFile = ../../systems/symbia_lehr.txt`.

Run each example from its own folder (OpenTOPAS resolves `IncludeFile` relative to the launch
directory, and the examples' includes point to `../../systems`, `../../phantoms`):

```bash
cd examples/component && topas energy_response.txt
cd examples/system   && topas symbia_lehr_sensitivity.txt
```

## System presets

[`systems/`](systems/) holds ready-to-include geometry presets for real scanners (Siemens
Symbia LEHR/LPHR/ME/HE and GE StarGuide). A deck selects a system in one line, e.g.
`IncludeFile = systems/symbia_lehr.txt`, and adds its own source, scorer and placements.
See [`systems/README.md`](systems/README.md) for the preset/deck contract.

## Designing your own geometry

Beyond the presets, you can vary the collimator and detector design entirely from the parameter
file: parallel-hole (round/hex/square), converging fan/cone-beam (focal length), pinhole and
multi-pinhole, and slit-slat collimators; a general pixelated detector (`TsPixelatedBox`); and
importing CAD parts (STL/PLY) for novel geometries. See [`docs/design.md`](docs/design.md) and the
worked decks in [`examples/design/`](examples/design/).

## Patient-like phantoms

Emit from realistic activity distributions, physiological organ uptake plus lesions (bone/nodal
metastases), to test acquisition parameters on clinically relevant tasks. Composable analytic
phantoms (native shapes + weighted volumetric sources) with literature-sourced uptake ratios live in
[`phantoms/`](phantoms/) (a Lu-177 PSMA scenario is included); voxelized XCAT anatomy is also
supported. See [`docs/phantoms.md`](docs/phantoms.md) and [`examples/phantom/`](examples/phantom/).

## Variance reduction

SPECT detects ~1 in 5000 photons, so full-MC is slow. Use **forced detection**
(`TsForcedDetectionProjection`, a fast analytic-collimator projection) or the native OpenTOPAS `Vr/`
toolkit (directional Russian roulette toward the detector, forced interaction, splitting). See
[`docs/variance_reduction.md`](docs/variance_reduction.md) and [`examples/vr/`](examples/vr/).

## Extension reference

### `TsParallelHoleCollimator` (geometry)

A septa slab (`Ge/<name>/Material`, e.g. `Lead` or `Tungsten`) perforated by a grid of
channels (`Ge/<name>/Hole/Material`, e.g. `Vacuum`). Hole shape and lattice are selectable.

| Parameter | Type | Meaning |
|-----------|------|---------|
| `HoleDiameter`    | `d` (Length) | Channel size; for `Hex` holes this is the across-flats dimension |
| `SeptalThickness` | `d` (Length) | Wall thickness between channels (pitch = diameter + septum) |
| `CollimatorLength`| `d` (Length) | Slab thickness along the bore axis (z) |
| `NHolesX`, `NHolesY` | `i` | Number of channels in each transverse direction |
| `HoleShape`       | `s` | `Round` (default), `Hex`, or `Square` (`HoleDiameter` = side, for CZT square-hole collimators) |
| `Lattice`         | `s` | `Square` (default) or `Hex` (offset rows, spacing = pitch·√3/2) |

The default `Round` holes on a `Square` lattice reproduce the distance-dependent geometric
resolution and septal-penetration tail, and are the configuration used for the results in
[`docs/validation.md`](docs/validation.md). Real clinical collimators use hexagonal holes on
a hexagonal lattice: set `HoleShape = "Hex"` and `Lattice = "Hex"`. The hexagonal packing
fits more open area (packing factor ~0.907 relative to a square lattice of the same pitch)
and so gives higher absolute sensitivity.

### `TsEDepSpectrum` (scoring)

Attach to a crystal component (`Sc/<name>/Component`). Accumulates the total energy
deposited per incident primary and writes one ntuple row per event:

| Column | Unit | Meaning |
|--------|------|---------|
| `Edep` | keV | Total deposited energy for the event (photopeak, Compton continuum, K-escape, ...) |
| `X`, `Y` | mm | Energy-weighted interaction centroid (global coordinates) |

With a mono-energetic source this directly yields the detector energy-response
function; with a collimator upstream and an energy-window cut in post-processing it
yields counts-in-window system sensitivity.

The remaining scorers and the `TsStarGuideDetector` geometry are documented in their
source headers.

## Validation

See [`docs/validation.md`](docs/validation.md). Summary against published figures for
the Siemens Symbia and GE StarGuide systems:

- Crystal energy response reproduces the expected photopeak, Compton edge and
  characteristic K-escape peaks.
- Distance-dependent geometric resolution matches the analytic parallel-hole
  expression at the 10 cm reference distance.
- Absolute Symbia LEHR system sensitivity: 8.6e-5 counts/decay vs. the published
  9.1e-5 counts/decay (5460 cpm/MBq), within 5%.

## Acknowledgements

Built on [OpenTOPAS](https://opentopas.github.io/) and
[Geant4](https://geant4.web.cern.ch/). Physics uses the Geant4 Penelope
electromagnetic models.

## Documentation

The full user guide, installation, quickstart, camera systems, phantoms,
CT-driven detector motion, variance reduction, and validation, is in [`docs/`](docs/). It builds into
a hosted site with `mkdocs build`, or on Read the Docs via the included `.readthedocs.yaml`.

## Citation

If you use OpenTOPAS-SPECT in published work, please cite it via [`CITATION.cff`](CITATION.cff),
alongside OpenTOPAS and Geant4.

## License

OpenTOPAS-SPECT is released under the [MIT License](LICENSE), the same permissive
license as OpenTOPAS, so the extensions can be built and distributed alongside a
OpenTOPAS installation without additional restrictions. Copyright (c) 2026 Bertolet Lab.
