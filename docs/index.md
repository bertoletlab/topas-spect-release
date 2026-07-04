# OpenTOPAS-SPECT

**OpenTOPAS-SPECT** extends [OpenTOPAS](https://opentopas.readthedocs.io/) with everything you need to
simulate a clinical SPECT study: a physical collimator, an energy-resolved detector, patient-like
activity phantoms, moving-detector acquisitions, and the variance-reduction tools that make those
simulations finish in a reasonable time.

OpenTOPAS is a powerful, well-validated Monte Carlo platform, but out of the box it has no concept of
a parallel-hole collimator, a scintillation crystal's pulse-height response, or a rotating gamma
camera. This package adds those pieces as self-contained OpenTOPAS classes, so you can build a SPECT
simulation entirely from parameter files, with no C++ and no changes to the OpenTOPAS core.

## What you can do with it

- **Model any clinical camera.** Ship-ready presets for Siemens Symbia and GE collimators (LEHR
  through HEGP) on NaI or CZT detectors, plus the building blocks to design your own parallel-hole,
  converging, pinhole, or slit-slat collimator.
- **Image a realistic patient.** Composable analytic phantoms with literature-grounded uptake for
  177Lu PSMA, 225Ac, 90Y, and 131I therapies, or bring your own voxelized XCAT phantom.
- **Run a full acquisition.** Generate a multi-view study in which the detector moves and the source
  decays on a shared clock, using either a hand-rolled orbit or a CT-driven, collision-safe detector
  path derived from the patient's own body contour.
- **Make it tractable.** Forced-detection projection scoring replaces the ~1-in-5000 collimator
  lottery with an analytic projection, so a patient acquisition runs in minutes per view.

## Who this guide is for

This manual assumes you can already build and run OpenTOPAS and are comfortable writing OpenTOPAS
parameter files. You do **not** need any C++ experience. If you have never run OpenTOPAS before, work
through the [OpenTOPAS documentation](https://opentopas.readthedocs.io/) first, then come back here.

## How this manual is organized

| If you want to… | Read |
|---|---|
| Build the extension and confirm it works | [Installation](installation.md) |
| Run your first SPECT simulation | [Quickstart](quickstart.md) |
| Understand the pieces of a SPECT deck | [How a deck is put together](concepts.md) |
| Pick a camera and collimator | [Camera and collimator systems](systems.md) |
| Design a custom collimator or detector | [Designing detectors and collimators](design.md) |
| Put activity in a patient-like body | [Patient-like phantoms](phantoms.md) |
| Acquire multiple views with motion and decay | [Variance reduction](variance_reduction.md) |
| Derive detector motion from a patient CT | [CT-driven detector motion](ring_motion.md) |
| Speed up count-starved simulations | [Variance reduction](variance_reduction.md) |
| Use the helper scripts | [Python tools](tools.md) |
| See how the physics was checked | [Validation](validation.md) |

## Citing and licensing

OpenTOPAS-SPECT is released under the MIT License. If you use it in published work, please cite the
project (see the repository's `CITATION` file) alongside OpenTOPAS and Geant4.
