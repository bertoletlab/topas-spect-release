# Dynamic acquisitions

A real SPECT study is not one frame. The detector steps through N projection angles over a total
acquisition time, and while it moves the isotope keeps decaying; for a chain like 225Ac the daughters
also re-equilibrate. To reproduce a study faithfully, the detector motion and the source activity have
to advance on the same clock. `tools/make_dynamic_acquisition.py` builds that from any phantom
scenario.

## Prescribing the study

You set up the acquisition the way a technologist would at the SPECT console, and the tool derives the
simulation from that prescription.

| Console parameter | Option | Meaning |
|---|---|---|
| Projections | `--projections N` | number of angular samples (60, 64, 120, 128) |
| Time per projection | `--time-per-projection SEC` | dwell time at each stop (default 20 s) |
| Angular range | `--angular-range DEG` | 360 (whole body) or 180 (cardiac) |
| Detector heads | `--heads {1,2,3}` | heads acquiring in parallel; more heads, shorter study |
| Matrix | `--matrix N` | projection matrix size (N × N; sets the crystal binning) |
| Photopeak window | `--photopeak CENTER WIDTH` | window center keV and total width percent (e.g. `208 20`) |
| Orbit radius | `--radius CM` | detector-to-axis distance |

The **study duration**, which drives the decay, is `(projections / heads) × time-per-projection`. A
dual-head 360° study of 60 projections at 20 s therefore takes 10 minutes, not 20. Pick an engine
below to say how the activity is modeled.

## Native isotope decay: the `--decay` backend

This is the recommended backend. It uses `SpectDecaySource`, a decay source bundled with OpenTOPAS-SPECT,
so no external extension is required (see [Python tools](tools.md) and the standalone example
`examples/source/decay_source.txt`).

```
tools/make_dynamic_acquisition.py phantoms/scenarios/lu177_psma.txt --decay --isotope "71 177" \
    --projections 60 --time-per-projection 20 --angular-range 360 --heads 2 --out lu177_study
```

It emits a single deck. Each organ becomes a `SpectDecaySource` that emits the parent radionuclide
(`GenericIon(Z, A)`) uniformly in the organ. The deck turns on `g4radioactivedecay`, so Geant4 produces
the whole decay chain from each emitted ion, which means the 225Ac daughters (221Fr at 218 keV, 213Bi
at 440 keV) appear natively with no hand-rolled `exp()`. Across the `Tf` timeline the source scales the
histories per time bin by the true activity decay (the half-life comes from Geant4), and the gantry
rotates through a `Linear` time feature on that same `Tf` timeline, so motion and activity share one
clock and stay locked together. Relative organ activity is the per-source `NumberOfHistoriesInRun`
(uptake × volume). Pass the isotope as its Z and A: 177Lu `"71 177"`, 225Ac `"89 225"`, 90Y `"39 90"`,
131I `"53 131"`.

## Per-view decks: the native fallback

When you want one self-contained deck per projection instead of a single timeline deck, use the
per-projection backend.

```
tools/make_dynamic_acquisition.py phantoms/scenarios/lu177_psma.txt \
    --half-life-h 159.5 --projections 60 --time-per-projection 20 --angular-range 360 --out lu177_study
```

It emits one deck per view, each carrying that view's gantry angle and its histories scaled by
`exp(-ln2·t_k/T½)` with `t_k = k·acq_time/N`. This backend needs no extra build and gives one clean
projection CSV per view, though it models simple exponential decay with no daughter kinetics. It bakes
the exact history count and angle into each view because driving `NumberOfHistoriesInRun` from a time
feature lags by one run in OpenTOPAS 4.2.p2, which makes the last view unreliable. The tool also writes
`run_all.sh` to run the views in order.

## Detector motion

In both backends the head orbits the patient long axis (z): view *k* sits at gantry angle
`orbit·k/N`, with the collimator bore facing the patient (the orientation is verified numerically, not
by eye). By default the head is a Symbia ME collimator with a NaI crystal; edit the `DETECTOR` block in
the tool to use a different collimator or detector, or a ring geometry.

## Notes and scope

- The tool works with any scenario, since it scales every `NumberOfHistoriesInRun` it finds, and with
  any isotope once you give its half-life. Over a typical 20–30 min SPECT the decay is small for 177Lu
  (~0.2%) or 131I, yet it is modeled correctly and matters for shorter-lived isotopes and for longer
  or dynamic studies.
- For a chain isotope such as 225Ac, simple exponential decay misses the daughter-equilibrium shift
  during the acquisition. Model the daughter redistribution in the scenario, as `ac225_psma.txt` does
  for the 213Bi kidney component, or use the `--decay` backend for full chain
  kinetics.
- Full-MC transport through the collimator is count-starved per view, so pair a dynamic acquisition
  with [variance reduction](variance_reduction.md), or use many histories, for usable projections.
