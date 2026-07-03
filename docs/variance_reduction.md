# Variance reduction

A collimated SPECT camera detects only about 1 in 5000 emitted photons, so brute-force Monte Carlo is
slow, both for characterizing the collimator-detector response (CDR) and for imaging patient phantoms.
This package gives you two ways to speed it up, and they suit different needs.

| Strategy | Bias | Speed | Use |
|----------|------|-------|-----|
| **Forced detection** (`TsForcedDetectionProjection`) | approximate model, needs calibration | very fast | fast projections, patient sims |
| **Native OpenTOPAS VR** (`Vr/…`) | unbiased, preserves the answer | faster | speeding up full-MC while keeping absolute accuracy |

## Forced detection: fast projections

Forced detection replaces the physical collimator and crystal with a thin **forced-detection surface**
and the `ForcedDetectionProjection` scorer, so it does not transport every photon through them.
Every forward gamma that crosses the surface is projected analytically through the collimator (the
Metz-Frey transmission) onto a weighted detector pixel. Because no collimator or crystal transport
happens, this runs orders of magnitude faster. See
[`examples/vr/forced_detection.txt`](https://github.com/bertoletlab/topas-spect-release/blob/main/examples/vr/forced_detection.txt).

The scorer parameters are `HoleDiameter`, `SeptalThickness`, and `CollimatorLength` (the collimator
model); `PixelSizeX/Y` and `NPixelsX/Y` (the detector); and `EnergyWindowLow/High` (or `ListMode` to
record every photon for post-hoc windowing). `IncludeSeptalPenetration` and `SeptalMuPerMM` add
penetration. The surface component's local **+z** must point toward the detector. Sum the `Weight`
column per pixel to form the projection.

!!! warning "Calibrate the absolute scale"
    The built-in transmission is uncalibrated: it is geometric only, with penetration off, and it uses
    the physical hole length `L` in place of the effective length `L_eff`. The absolute sensitivity
    therefore differs from full-MC (about 1.3e-5 against the full-MC and measured 9.1e-5 for Symbia
    LEHR). The projection shape and the relative contrast are correct, so calibrate the absolute scale
    against a full-MC or measured reference before you trust absolute counts. Producing calibrated
    fast models is the job of a separate model-building pipeline; this package ships the scorer.

## Native OpenTOPAS variance reduction

OpenTOPAS ships its own biasing techniques, turned on with `b:Vr/UseVarianceReduction = "True"` and
defined as `s:Vr/<name>/Type = "<type>"`. A complete, runnable example is
[`examples/vr/native_vr.txt`](https://github.com/bertoletlab/topas-spect-release/blob/main/examples/vr/native_vr.txt),
which also shows the validation workflow below.

- **Directional splitting** (`UniformSplitting` with `UseDirectionalSplitting`) splits secondaries in a
  region and aims them at a target point (`TransX/Y/Z`) within a radius (`RMax`), applied through
  Geant4's `setSecBiasing` and `setDirectionalSplitting`.
- **Forced interaction** (`ForcedInteraction`) forces gammas to interact in a region
  (`ForcedDistances`, `processesNamed`, `CorrectByWeight`).
- **Particle splitting** (`GeometricalParticleSplit`) splits tracks entering a component, with an
  optional Russian-roulette ROI (`SplitNumber`, `RussianRoulette/ROIRadius`, `RussianRoulette/ROITrans`).
- Also available: `ImportanceSampling`, `WeightWindow`, `CrossSectionEnhancement`, `RangeRejection`.

!!! danger "Validate every biased run, and sum weights"
    Native VR changes particle **weights**. Always run the biased setup against a short unbiased
    baseline (`Vr/UseVarianceReduction = "False"`) and confirm the scored quantity agrees within
    statistics, and only score with a weight-aware scorer. OpenTOPAS's `EnergyDeposit` applies the weight,
    and this package's `EDepSpectrum` writes a **`Weight`** column, so histogram `Edep` weighted by
    `Weight` (sum weights per bin), not raw row counts. Without VR every weight is 1, so summing
    weights equals counting rows. Verified: with bremsstrahlung split ×10, `EDepSpectrum` records the
    split photons at weight 0.1.

Geant4 directional splitting biases secondary photon *production* (for example bremsstrahlung); it does
not split the Compton-scattered gamma that continues as the primary track. On its own it therefore
gives little gain for the core SPECT problem, getting scattered and primary gammas through a collimator
to a small detector (run `native_vr.txt` with VR on and off: the deposited energy is essentially
identical). For SPECT the effective variance reduction is **forced detection** above. Reach for native
VR when a technique matches your problem (for example importance sampling through thick shielding), and
always validate it.

## Variance reduction for dynamic acquisitions

`tools/make_dynamic_acquisition.py --vr` applies forced detection to every view of a
[dynamic acquisition](dynamic_acquisition.md). Each per-view deck holds the forced-detection detector
fixed and rotates the phantom about its long axis to the view angle. This choice
sidesteps two OpenTOPAS 4.2.p2 quirks: the forced-detection scorer's transform is wrong
for a component inside a `Group`, and the surface then needs only one static `RotX=90` instead of a
compound rotation. The per-view activity decay still applies. On `lu177_psma` this projects correctly
at every view angle (φ = 0/45/90), each view in seconds.

Forced detection replaces only the collimator and crystal transport, not the photon transport through
the patient. At high history counts, where the phantom's self-attenuation dominates the run time, the
wall-clock is therefore close to full-MC. Its real gain is detection efficiency: every forward photon
contributes its weighted collimator transmission instead of surviving a roughly 1-in-5000 lottery,
which yields clean projection statistics from far fewer histories. That gain is largest for
high-resolution collimators such as LEHR and for count-starved views (low activity, late decay). The
absolute scale is model-based, so calibrate it against full-MC or measurement as above.

## Which to use

- For SPECT, use **forced detection**: CDR characterization, fast projections, dynamic acquisitions, and
  large patient sweeps. Calibrate the absolute scale once against full-MC. This is the recommended path.
- Reach for **native OpenTOPAS VR** only when a specific technique fits your problem (for example importance
  sampling through thick shielding, or particle splitting into a hard-to-reach region), and always
  validate the biased run against an unbiased baseline while summing weights.
