# Variance reduction

A collimated SPECT camera detects only about 1 in 5000 emitted photons, so brute-force Monte Carlo is
slow, both for characterizing the collimator-detector response (CDR) and for imaging patient phantoms.
This package gives you two ways to speed it up, and they suit different needs.

| Strategy | Bias | Speed | Use |
|----------|------|-------|-----|
| **Forced detection** (`TsForcedDetectionProjection`) | models the collimator and crystal, so it omits what it does not model | very fast | fast projections, patient sims |
| **Native OpenTOPAS VR** (`Vr/…`) | unbiased, preserves the answer | faster | speeding up full-MC while keeping absolute accuracy |

## Forced detection: fast projections

Forced detection replaces the physical collimator and crystal with a thin **forced-detection surface**
and the `ForcedDetectionProjection` scorer, so no photon is transported through either. Everything
upstream of the surface stays ordinary transport: attenuation and scatter in the object are simulated
explicitly, and photons arrive with their true energy and direction. Each forward gamma crossing the
surface is then projected onto a weighted detector pixel. See
[`examples/vr/forced_detection.txt`](https://github.com/bertoletlab/topas-spect-release/blob/main/examples/vr/forced_detection.txt).

### What the analytic model does

**Geometric acceptance** comes from the aperture autocorrelation of Metz, Atkins and Beck (1980),
evaluated at each photon's angle of incidence. The open-area fraction of the collimator face is
computed in closed form from `HoleShape` and `HoleLattice`, so hexagonal holes on a hexagonal lattice,
round holes on a hexagonal lattice and round holes on a square lattice each get their own expression.
Declaring `HoleFraction` yourself overrides it, and the scorer aborts if your value disagrees with the
one it traces through the geometry.

**Septal penetration** is traced, not approximated. For each angle of incidence the scorer walks a ray
through the hole array in depth, accumulates the lead thickness crossed, and averages the transmission
over entry position within one aperture and over azimuth. The result is tabulated once per geometry
and interpolated at run time, so the cost is a few seconds at startup rather than per photon.

**The septal attenuation coefficient is applied at each photon's own energy.** Leaving `SeptalMuPerMM`
unset is the normal case: the scorer then uses a NIST XCOM tabulation for `SeptalMaterial` (default
`G4_Pb`), with the K edge resolved. This matters for any source with more than one line, since a
single scalar pinned at one line misrepresents the others. Supply `SeptalMuEnergies` and
`SeptalMuValues` for a material the built-in tables do not cover.

**The crystal** is an energy-dependent photopeak detection probability plus a Gaussian energy
resolution, with `EnergyWindowLow/High` applied to the drawn energy (or `ListMode` to record every
photon for post-hoc windowing).

### What it does not model

Intrinsic spatial resolution, depth-of-interaction spread, mispositioning by scatter inside the
crystal, backscatter from structures behind it, and scatter in the collimator itself. The practical
consequence is that agreement with full transport degrades as the share of the signal arriving through
the septa grows, which you can measure for your own geometry by running transport twice, once with
real septa and once with the septa given an artificial density so they absorb completely. The
companion paper reports that behavior across fourteen configurations; `docs/validation.md` points at
it rather than repeating it.

### Parameters and gotchas

`HoleDiameter`, `SeptalThickness`, `CollimatorLength`, `HoleShape`, `HoleLattice` describe the
collimator; `PixelSizeX/Y` and `NPixelsX/Y` the detector; `PenetrationModel` selects the penetration
treatment and defaults to `RayTrace`; `MinTransmissionFraction` floors the transmission a ray may
carry and defaults to `1e-6`, which keeps the wide-angle penetration tail while bounding the table.

!!! warning "The scoring surface is the collimator MID-PLANE"
    `TsForcedDetectionProjection` measures source distance to the mid-plane of the collimator it
    models, not to its entrance face. Place the surface at `CollimatorLength/2 + crystal/2` behind
    where the face would be, as `examples/vr/forced_detection.txt` does, or the fast path and a
    matching full-transport deck will sit at different distances from their own collimator faces.

Sum the `Weight` column per pixel to form the projection, and note that the recorded entries are not
independent samples: a weighted estimator's precision follows the effective sample size,
`(sum w)^2 / sum(w^2)`, not the number of rows.

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

## Variance reduction for multi-view acquisitions

Forced detection extends to a full tomographic study: build one deck per view with the
`ForcedDetectionProjection` surface held fixed and the phantom rotated about its long axis to the view
angle, applying each view's activity decay. Rotating the phantom rather than the detector sidesteps two
OpenTOPAS 4.2.p2 quirks: the forced-detection scorer's transform is wrong for a component inside a
`Group`, and the surface then needs only one static `RotX=90` instead of a compound rotation. Each view
projects in seconds.

Forced detection replaces only the collimator and crystal transport, not the photon transport through
the patient. At high history counts, where the phantom's self-attenuation dominates the run time, the
wall-clock is therefore close to full-MC. Its real gain is detection efficiency: every forward photon
contributes its weighted collimator transmission instead of surviving a roughly 1-in-5000 lottery,
which yields clean projection statistics from far fewer histories. That gain is largest for
high-resolution collimators such as LEHR and for count-starved views (low activity, late decay). Judge
the gain at matched statistical precision rather than at matched histories, and remember that the
startup cost of tabulating transmission makes very short runs understate it.

## Which to use

- For SPECT, use **forced detection**: CDR characterization, fast projections, multi-view acquisitions, and
  large patient sweeps. Check its absolute scale against full transport in a geometry like yours, since
  how much of the signal crosses septa sets how well it holds. This is the recommended path.
- Reach for **native OpenTOPAS VR** only when a specific technique fits your problem (for example importance
  sampling through thick shielding, or particle splitting into a hard-to-reach region), and always
  validate the biased run against an unbiased baseline while summing weights.
