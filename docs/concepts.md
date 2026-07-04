# How a deck is put together

Before diving into the feature chapters, it helps to have a mental model of what a SPECT simulation
deck contains and how the OpenTOPAS-SPECT pieces fit into a standard OpenTOPAS parameter file. Everything
here is ordinary OpenTOPAS syntax; OpenTOPAS-SPECT only adds a handful of new *components* (geometry) and
*scorers* (measurement).

## The five ingredients

Every SPECT deck combines the same five parts. You mix and match them across the feature chapters.

| Ingredient | OpenTOPAS prefix | What OpenTOPAS-SPECT provides |
|---|---|---|
| **World and physics** | `Ge/World`, `Ph/` | nothing new, use standard OpenTOPAS |
| **Collimator** | `Ge/<name>` | `TsParallelHoleCollimator`, `TsPinholeCollimator`, `TsSlitSlatCollimator` |
| **Detector** | `Ge/<name>` | a scintillator/semiconductor crystal volume (any OpenTOPAS solid) |
| **Source** | `So/<name>` | native OpenTOPAS sources, or one per phantom region |
| **Scorer** | `Sc/<name>` | `TsEDepSpectrum`, `TsForcedDetectionProjection` |

A ready-made **camera preset** (see [systems](systems.md)) bundles the collimator and detector so a
single `IncludeFile` gives you a validated Symbia or GE geometry. You then add the world, physics,
source, and scorer for your particular study.

## The detector chain

A photon that reaches the camera passes through two stages, and you choose how much of each to
simulate:

1. **Collimation.** A parallel-hole collimator only passes photons travelling nearly parallel to its
   holes; everything else is absorbed in the septa. This is what gives SPECT its direction
   information, and why it throws away ~99.98% of emitted photons. OpenTOPAS-SPECT models the holes and
   septa explicitly, so the transport captures septal penetration and scatter directly.

2. **Detection.** Photons that survive the collimator deposit energy in the crystal. The
   `TsEDepSpectrum` scorer records the pulse-height spectrum, from which you apply an energy window
   to select the photopeak.

Because full collimator transport is so wasteful, OpenTOPAS-SPECT also offers a fast path, the
`TsForcedDetectionProjection` scorer, that replaces stages 1 and 2 with an analytic projection
through the collimator. Use full transport when you care about scatter and penetration physics; use
forced detection when you need many views quickly. See [Variance reduction](variance_reduction.md).

## Coordinate conventions

The phantoms and acquisitions in this package follow one convention, and it is worth stating up front
because it determines how detectors are placed and rotated:

- The **patient long axis is `z`** (the torso is long in `z`).
- A gamma camera images a **transaxial** plane; a single anterior detector sits beside the patient
  (offset in `y`) with its face looking back along the axis toward the patient.
- A rotating acquisition orbits the detector **around `z`**, or, equivalently and often more
  reliably, holds the detector fixed and rotates the phantom about `z`. The [CT-driven motion](ring_motion.md)
  chapter and `examples/phantom/spect_acquisition.txt` use both approaches and explain when each
  applies.

!!! note "OpenTOPAS rotations are passive"
    A component's `RotX`/`RotY`/`RotZ` rotate its *frame*, not the object, and are applied in the
    order X→Y→Z about the parent axes. This matters when you aim a detector face at the patient: the
    surest way to confirm a face points where you intend is to compute the world direction of its
    local normal, instead of eyeballing a render. The tools in this package do that check for you.
