# CT-driven detector motion

Modern CZT cameras like GE StarGuide do not orbit on a fixed circle. Each detector column moves in
radially to hug the patient's body contour, then the gantry rotates and each column sweeps, keeping
the detectors as close to the patient as possible for the best resolution and sensitivity. The
`make_ring_motion.py` tool reproduces that behavior: it reads a patient's body contour and writes a
OpenTOPAS motion deck in which each detector docks against the contour and moves without ever colliding
with the body.

## What it produces

Given a body contour, the tool computes, for each detector, a docking radius and a motion sequence,
and emits an OpenTOPAS deck driven by time features:

```
HOME  →  APPROACH (each detector moves in to hug the contour)
      →  ACQUIRE  (the gantry rotates and detectors sweep)
      →  RETRACT  (detectors return home)
```

It supports two detector layouts:

- **`starguide`**, the 12-column CZT ring, with the true StarGuide column geometry.
- **`ring`**, a generic ring of `N` detectors you specify.

## Getting the contour

You can supply the body contour three ways:

=== "Analytic ellipse"

    The simplest option: approximate the cross-section as an ellipse with semi-axes A and B (mm):

    ```bash
    python3 tools/make_ring_motion.py --system starguide --ellipse 150 100 --out motion.txt
    ```

=== "Patient CT/PET (DICOM)"

    Extract the real body contour from a DICOM series. The tool builds a clean body mask (
    thresholding, filling internal air, removing the scanner table and detached arms, and keeping the
    largest connected component), then casts rays from the body centroid to measure the radial
    profile:

    ```bash
    python3 tools/make_ring_motion.py --system ring --ndet 16 \
            --dicom /path/to/CT_series --hu-threshold -300 --out motion.txt
    ```

=== "Measured radii"

    If you have already extracted the contour as a list of radii (for example, from a de-identified,
    on-server extraction that returns only geometry), pass them directly:

    ```bash
    python3 tools/make_ring_motion.py --system starguide \
            --radii 91 94 140 157 138 121 101 114 120 153 153 115 --out motion.txt
    ```

## Why the docking is collision-safe

A detector that docks against the contour at its *starting* azimuth would collide with the patient as
the gantry rotates it into a wider part of the body. The tool avoids this: each detector docks at the
**maximum contour distance over the entire angular range it will sweep** (its azimuth through the
gantry rotation, plus the panel's tangential extent), so it clears the body throughout the motion.
This is checked automatically: the generated geometry builds with zero overlaps, and the regression
tests assert that no detector penetrates the body at any gantry angle.

## Visualizing and running

The generated deck is an ordinary OpenTOPAS parameter file. Open it in the Qt viewer to watch the motion,
or include it in an acquisition. To run it headless, strip the viewer lines and set
`Ts/PauseBeforeQuit = "False"`.

## Verifying it yourself

The contour extraction and docking geometry are covered by ground-truth regression tests
(`tests/test_ring_motion.py`) that need no patient data: they generate synthetic phantoms of known
size and confirm the tool recovers them:

```bash
python3 tests/test_ring_motion.py
```

The tests check that a known circle and ellipse are recovered to within one pixel, that a scanner
table and a detached arm are correctly removed, that each detector docks at contour-plus-clearance
and faces the patient, and that no detector collides during rotation.
