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
| `--acquire` | also write activity and one projection scorer per detector |
| `--histories N` | background histories per time step with `--acquire` (each lesion takes 5%) |
| `--pixel MM` | detector pitch with `--acquire`, which sets the scorers' pixel counts |
| `--out FILE` | output motion deck |

## `export_interfile.py`

Writes an Interfile v3.3 header and data pair from a scorer's projection output, for handing
projections to a reconstruction package. It takes either a NumPy stack or the CSV aggregated from a
projection ntuple, plus the acquisition geometry, since a header must state the orbit and the scorer
output does not carry it.

```bash
python3 tools/export_interfile.py \
    --projections proj_stack.npy --pixel-size-x 4.42 --pixel-size-y 4.42 \
    --start-angle 0 --angular-step 3.0 --direction CW --view-from "+Y" \
    --radius 200 --energy-window-low 126.5 --energy-window-high 154.6 \
    --isotope Tc-99m --out sim_projections
```

Three fields have no safe default and must come from your deck: the rotation `--direction`, the
azimuth the first view looks from (`--view-from`), and the orbit `--radius`, which a contour-hugging
orbit does not have as a single number. The reasoning behind each, and the full DICOM and Interfile
field maps, are in [Exporting projections to a reconstruction](reconstruction_interop.md).

## `audit_decks.py`

Checks every forced-detection deck in a tree against its own declared physics: whether a pinned
`SeptalMuPerMM` matches a tabulation of the septal material at the deck's own beam energy. A pinned
coefficient is invisible at run time, since any value produces a plausible projection, so this is the
only cheap way to catch a deck that quietly models the wrong metal or the wrong energy. An unset
coefficient passes, because the scorer then tabulates it per photon energy itself.

```bash
python3 tools/audit_decks.py examples systems
```

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
