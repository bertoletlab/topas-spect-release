# Quickstart

This walkthrough runs your first SPECT simulation: an isotropic 99mTc point source imaged through a
Siemens Symbia low-energy high-resolution (LEHR) collimator onto a NaI crystal. It takes a couple of
minutes and reproduces a validated absolute-sensitivity result.

## 1. Run the example

Run it from the example's own folder (its `IncludeFile` paths are relative to that folder, so OpenTOPAS
finds them from there):

```bash
cd examples/system
/path/to/OpenTOPAS-build/topas symbia_lehr_sensitivity.txt
```

OpenTOPAS builds the geometry, transports the photons, and writes an energy-deposition spectrum to
`sensitivity_symbia_lehr_140p5keV_10cm.phsp` in the current directory.

## 2. Read the deck

Open `examples/system/symbia_lehr_sensitivity.txt`. Every SPECT deck is assembled from the same few
ingredients; here they are in order.

**The camera preset** pulls in the validated collimator and crystal in one line:

```
IncludeFile = systems/symbia_lehr.txt
```

This defines the lead collimator (`Ge/Collim`) with the Symbia LEHR hole geometry and the NaI crystal
(`Ge/Crystal`). You only add the parts specific to your study (the world, physics, source, and
scorer), plus the detector extents and placement.

**The physics** is the standard low-energy electromagnetic list with fluorescence turned on:

```
sv:Ph/Default/Modules     = 1 "g4em-penelope"
b:Ph/Default/Fluorescence = "True"
```

**The source** is an isotropic 140.5 keV gamma point, placed 10 cm in front of the collimator face:

```
s:So/Beam/Type       = "Isotropic"
s:So/Beam/BeamParticle = "gamma"
d:So/Beam/BeamEnergy = 140.5 keV
i:So/Beam/NumberOfHistoriesInRun = 200000000
```

**The scorer** records the pulse-height spectrum in the crystal:

```
s:Sc/Spec/Quantity  = "EDepSpectrum"
s:Sc/Spec/Component = "Crystal"
```

## 3. Interpret the output

The scorer output is a histogram of energy deposited per detected event. To get the absolute system
**sensitivity**, count the events whose energy falls in the 99mTc photopeak window (126.5–154.6 keV,
a 20% window around 140.5 keV) and divide by the number of histories:

```
sensitivity = (counts in 126.5–154.6 keV) / NumberOfHistoriesInRun
```

You should get about `8.6e-5` counts/decay, within 5% of the published Symbia LEHR value of
`9.1e-5`. That agreement, with no tuning, is the whole point: the collimator transport, the crystal
response, and the energy windowing are all being modeled explicitly. See
[Validation](validation.md) for the full comparison.

!!! note "Histories are activity"
    OpenTOPAS has no becquerel unit. One history is one nuclear decay, so `NumberOfHistoriesInRun` **is**
    the number of emitted particles. You convert to real counts by scaling to your source activity
    and acquisition time in post-processing. Relative activity between two sources is set by giving
    them proportional history counts, which is how the [phantoms](phantoms.md) encode organ uptake.

## 4. Where to go next

- Swap in a different camera by changing the one `IncludeFile` line; see
  [Camera and collimator systems](systems.md).
- Replace the point source with a patient-like activity distribution; see
  [Patient-like phantoms](phantoms.md).
- Turn a single view into a rotating, decaying acquisition; see
  `examples/phantom/spect_acquisition.txt`.
- If your simulation is too slow because few photons reach the detector, switch on
  [variance reduction](variance_reduction.md).
