# Python tools

OpenTOPAS-SPECT ships two Python helper scripts under `tools/`. They generate OpenTOPAS decks; they do not
run the simulation, and the simulation itself needs no Python. Install their dependencies once with
`pip install -r requirements.txt` (numpy, scipy, pydicom).

## `make_dynamic_acquisition.py`

Turns a single-view phantom scenario into a full multi-view acquisition in which the detector moves
and the source activity decays on a shared timeline. See
[Dynamic acquisitions](dynamic_acquisition.md) for the concepts; this is the command reference.

You prescribe the study in clinical console terms and the tool derives the acquisition.

```bash
python3 tools/make_dynamic_acquisition.py phantoms/scenarios/lu177_psma.txt --decay --isotope "71 177" \
        --projections 60 --time-per-projection 20 --angular-range 360 --heads 2 --out lu177_study
```

| Option | Meaning |
|---|---|
| `--projections N` | number of projections (angular samples) |
| `--time-per-projection SEC` | dwell time per projection (default 20 s) |
| `--angular-range DEG` | 360 (whole body) or 180 (cardiac) |
| `--heads {1,2,3}` | detector heads acquiring in parallel (shortens the study) |
| `--matrix N` | projection matrix size (sets the crystal binning) |
| `--photopeak CENTER WIDTH` | energy-window center keV and total width percent |
| `--radius CM` | orbit radius (detector-to-axis) |
| `--out DIR` | output directory for the generated decks |

The study duration that drives the decay is `(projections / heads) × time-per-projection`.

**Engines** (choose one):

| Option | Behavior |
|---|---|
| `--decay --isotope "Z A"` | one deck; each organ becomes a bundled `SpectDecaySource` that emits the parent ion and decays natively across the timeline, with the full decay chain produced by `g4radioactivedecay`. No external extension needed. |
| *(default)* native | one deck per projection; the detector rotates and each projection's histories are scaled by `exp(-λt)` for the isotope. Requires `--half-life-h H`. |
| `--vr` | forced-detection fast path per projection (the phantom is rotated to the projection angle and imaged analytically). See [Variance reduction](variance_reduction.md). |

## `make_ring_motion.py`

Derives a collision-safe, contour-hugging detector motion deck from a patient body contour, for
StarGuide or a generic ring. See [CT-driven detector motion](ring_motion.md) for the full chapter;
in brief:

```bash
# from an analytic ellipse
python3 tools/make_ring_motion.py --system starguide --ellipse 150 100 --out motion.txt

# from a patient DICOM series
python3 tools/make_ring_motion.py --system ring --ndet 16 --dicom /path/to/CT --out motion.txt

# from pre-extracted radii
python3 tools/make_ring_motion.py --system starguide --radii 91 94 140 157 138 121 101 114 120 153 153 115 --out motion.txt
```

| Option | Meaning |
|---|---|
| `--system {starguide,ring}` | detector layout |
| `--ndet N` | number of detectors (generic `ring`) |
| `--ellipse A B` / `--dicom DIR` / `--radii ...` | contour source (pick one) |
| `--hu-threshold HU` | body-mask threshold for the DICOM path |
| `--clearance MM` | gap to leave between each detector and the body |
| `--out FILE` | output motion deck |

## `make_qa_phantom.py`

Generates the standard QA phantom fragments (`phantoms/qa/`) from datasheet-exact geometry. See
[QA phantoms](qa_phantoms.md) for the full chapter; in brief:

```bash
python3 tools/make_qa_phantom.py jaszczak --model deluxe    # Jaszczak (ultradeluxe/deluxe/standard/benchmark)
python3 tools/make_qa_phantom.py nema-iec --ratio 4         # NEMA IEC body, sphere:background ratio
python3 tools/make_qa_phantom.py resolution --in-water      # NEMA NU-1 line sources (with-scatter variant)
python3 tools/make_qa_phantom.py all                        # regenerate the shipped set
```

| Option | Meaning |
|---|---|
| `--model {ultradeluxe,deluxe,standard,benchmark}` | Jaszczak rod/sphere diameter set |
| `--ratio R` | NEMA IEC sphere:background activity-concentration ratio |
| `--in-water` | surround the resolution sources with a 20 cm water cylinder |

Image the results with the shipped [`examples/qa/`](qa_phantoms.md) decks (a Symbia LEHR head).

## Working with real patient data

When you feed a clinical DICOM series to `make_ring_motion.py`, keep patient data governance in mind.
The tool only needs the **geometry** of the body contour, not the images. A good practice is to run
the contour extraction where the data lives and move only the resulting radii, then generate the deck
with `--radii`. Nothing patient-identifying needs to leave the imaging system.
