# Python tools

OpenTOPAS-SPECT ships Python helper scripts under `tools/`. They generate OpenTOPAS decks; they do not
run the simulation, and the simulation itself needs no Python. Install their dependencies once with
`pip install -r requirements.txt` (numpy, scipy, pydicom).

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
