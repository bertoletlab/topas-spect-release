# Reconstruction interoperability: export formats and the path to STIR/DICOM

topas-spect is a **forward Monte Carlo simulator**. It does not reconstruct images, and
reconstruction stays out of the package by design (see `HANDOFFS/scope.md` and the
manuscript's own scope statement). This chapter is the answer to the question that follows
immediately from that: *if I simulate a SPECT acquisition here, how do I get the projections
into a reconstruction package?*

It has three parts:

1. **The geometry/rotation convention topas-spect actually uses** (Sec. 2): the single most
   important thing to get right, because a sign or axis error here silently mirrors or
   misregisters a reconstruction without any error message.
2. **A field-by-field map** from topas-spect's own parameters and scorer output columns to the
   DICOM Nuclear Medicine (NM) Image IOD and to Interfile v3.3, the two formats essentially every
   SPECT reconstruction package (STIR, SIRF, commercial workstations) reads (Sec. 3-4).
3. **A working exporter**, `tools/export_interfile.py`, that writes real Interfile v3.3 headers
   and matching flat binaries from topas-spect projection output, for the single-head
   circular-orbit case (Sec. 5).

Everything below is graded honestly: fields with a direct, verified topas-spect source are
marked **mapped**; fields that exist in the target format but are not currently produced or
recorded anywhere in topas-spect are marked **gap**, with what you would need to add to close it.

## 1. Why this is not a reconstruction demo

Building an end-to-end MLEM/STIR reconstruction of a topas-spect projection set is explicitly out
of scope for the package (`HANDOFFS/scope.md`: topas-spect ships the simulator and tunable
geometries; reconstruction lives in a separate, non-public engine). This chapter instead documents
the export contract in enough detail that a user, or a referee, can verify coordinate
consistency, center-of-rotation alignment, and tomographic data formatting *without* topas-spect
having to own a reconstruction pipeline.

## 2. topas-spect's geometry/rotation convention (verified against source)

### 2.1 TOPAS rotations are passive, and this is load-bearing

`docs/concepts.md` already states the rule: *"A component's `RotX`/`RotY`/`RotZ` rotate its
frame, not the object."* This section verifies that statement against the actual OpenTOPAS
source and works out its concrete physical consequence for the decks that back this manuscript's
figures, because "the frame is rotated, not the object" is easy to read past without registering
that it means **the physically visible rotation is the inverse of the number you typed.**

Verified chain (OpenTOPAS 4.2.2 source, read at `/Applications/TOPAS/OpenTOPAS/` and
`/Applications/GEANT4/geant4-v11.3.2/`):

- `TsVGeometryComponent::GetRotRelToParentComponent()` (`geometry/TsVGeometryComponent.cc`,
  ~line 1088) builds the rotation matrix directly from the entered parameters:
  `rm->rotateX(RotX); rm->rotateY(RotY); rm->rotateZ(RotZ);` and returns it **unmodified**.
- That matrix is passed as-is as `pRot` into `new G4PVPlacement(rot, trans, ...)`
  (`geometry/TsVGeometryComponent.cc`, ~line 769/797). TOPAS does not invert it before handing it
  to Geant4.
- Geant4's own documentation for that constructor argument
  (`G4PVPlacement.hh`): *"positioned in a frame which is rotated by `*pRot` ... relative to the
  coordinate system of the mother volume."* This is Geant4's **Frame** rotation, explicitly
  distinct from the **Object** rotation. `G4VPhysicalVolume.hh` documents both accessors side by
  side: `GetFrameRotation()` returns *"the rotation ... of the Frame used to position this volume
  ... (opposite of object rot)."* The implementation confirms it:
  `G4VPhysicalVolume::GetObjectRotationValue()` (`G4VPhysicalVolume.cc`, ~line 191) literally
  returns `G4MT_rot->inverse()`, where `G4MT_rot` is the identical matrix object that was passed in
  as `pRot`.

**Net rule:** for a component with entered `d:Ge/X/RotY = θ deg` (or RotX/RotZ, single axis),
the physically/visually apparent rotation of that component relative to its parent (what you'd
measure in a RayTracer view, or what a reconstruction needs to match) is **`-θ`** about that
axis, right-hand rule. A component you tell to rotate by `+90 deg` is physically rotated by
`-90 deg`. This composes once per parent-to-child link in a nested placement (Gantry to Head, Head
to Collimator, etc.), each link independently inverted.

### 2.2 Worked example: the canonical dual-head orbit (backs Fig. 5/7 of the manuscript)

`examples/acquisition/dualhead_orbit.txt`, the deck the Round-1/S2/S7 re-simulation and Fig. 5
resolution/consistency results are built from, drives the whole camera with:

```
d:Ge/Gantry/RotY = Tf/Gantry/Value deg
s:Tf/Gantry/Function = "Linear deg"
d:Tf/Gantry/StartValue = 0. deg
d:Tf/Gantry/Rate = 5.0 deg/s
```

i.e. the entered gantry angle sweeps `0 -> +180 deg` about the parent's Y axis. HeadA starts at
`TransZ = +290 mm` (world +Z at t=0) with `RotY = 180 deg` (self-inverse, so orientation is
convention-independent here); HeadB starts at `TransZ = -290 mm` with `RotY = 0 deg`.

Applying the Sec. 2.1 rule: the **physical** rotation of the Gantry (and both heads with it) is
`0 -> -180 deg` about World +Y. A rotation by `-θ` about +Y carries the +Z axis toward **-X**, not
+X (standard right-hand-rule rotation about Y carries +Z toward +X for *positive* physical angle;
here the physical angle is negative, so it goes the other way). Concretely:

> **As the entered `Tf/Gantry/Value` increases from 0 to 180 deg, HeadA physically sweeps from
> World +Z, through World -X, to World -Z**, viewed by an observer standing on the +Y axis
> looking toward the origin (+Z at 12 o'clock, +X at 3 o'clock, the standard math view of the X-Z
> plane): **this is a clockwise sweep.**

This is a general, reproducible fact about every single-axis `RotY`/`RotZ` orbit built the
package's standard way (translate along one world axis, rotate the parent Group about a second
world axis): **the physical sweep is clockwise as viewed from the positive end of the rotation
axis, looking toward the origin**, because entered-angle-increases-positive always maps to
physical-angle-increases-negative. The identical derivation applied to
`examples/phantom/spect_acquisition.txt` (`d:Ge/Head/RotZ = Tf/Orbit/Value deg`, detector starting
at `TransY = +325.5 mm`) gives an analogous result: the head starts at World +Y and
sweeps toward World +X as the entered angle increases: clockwise viewed from +Z looking toward
the origin.

**Before wiring a topas-spect projection set into a reconstruction package, do not assume CW or
CCW from this document: re-derive it for your specific deck** using Sec. 2.1's rule (physical =
-entered) and your deck's actual `Trans*`/`Rot*` values, then pass the correct
`--direction`/`--view-from` pair to `tools/export_interfile.py` (Sec. 5). The script refuses a
default so this step cannot be silently skipped.

### 2.3 The rotation axis itself is not fixed package-wide: check the specific deck

`docs/concepts.md` states the package's general convention as *"the patient long axis is z ...
[the acquisition] orbits the detector around z."* That is accurate for
`examples/phantom/spect_acquisition.txt` (`Ge/Head/RotZ`, patient long axis along world Z,
detector offset along `TransY`). **It is not what the dual-head and ring-orbit decks that back
this manuscript's SPECT figures do:**

| Deck | Backs | Rotation parameter | Patient/detector long axis | Radial offset axis |
|---|---|---|---|---|
| `examples/phantom/spect_acquisition.txt` | (not a manuscript figure; the package's documented default pattern) | `Ge/Head/RotZ` | World **Z** | `TransY` |
| `examples/acquisition/dualhead_orbit.txt` | **Fig. 5, Fig. 7 (S2/S7)** | `Ge/Gantry/RotY` | World **Y** | `TransZ` |
| `tools/make_ring_motion.py` output (StarGuide ring) | **Fig. 7 (C1)** | `Ge/Gantry/RotY` | World **Y** | `TransX`/`TransZ` (contour-driven) |

Both patterns are internally self-consistent and geometrically correct within their own file
(this is not a bug in either deck), but they are **not interchangeable conventions**, and a
downstream implementer must read the specific deck's `Rot*`/`Trans*` parameters rather than assume
one global axis mapping. This is the kind of silent axis-order mismatch a skeptical
referee is right to worry about; the honest answer is that topas-spect does not enforce a single
world-frame convention. The user does, per acquisition deck.

### 2.4 Coordinate handedness and DICOM's patient (LPS) frame

topas-spect's `World` is a plain, right-handed Geant4/Cartesian frame with no built-in mapping to
DICOM's LPS (Left/Posterior/Superior) patient frame: none of the shipped decks set
`Image Orientation (Patient)` or any DICOM-frame axis label. **Gap:** assigning `(X, Y, Z)` (or
whichever pair is transaxial/axial in your deck, Sec. 2.3) to `(L/R, P/A, S/I)` is left entirely to
the user exporting the data; get this wrong and left-right or anterior-posterior silently flips in
the reconstructed image with no error. If/when a DICOM NM export is built (not yet shipped, Sec.
3), that mapping needs to be a required, explicit argument, like `--direction`/
`--view-from` in `export_interfile.py` (Sec. 5).

### 2.5 Pixel row/column order (PixelID unflattening)

`TsForcedDetectionProjection::ComputePixelID()` and
`TsScoreStarGuideProjection::ComputePixelID()` (identical formula in both, `scoring/*.cc`) encode
the 2-D pixel grid into the ntuple's single `PixelID` integer column as:

```
PixelID = ix * NPixelsY + iy
```

where `ix` is the local-X pixel index and `iy` the local-Y pixel index of the scored component's
own frame (0-indexed, clamped to `[0, NPixelsX)` / `[0, NPixelsY)`). To recover the 2-D image:

```
ix = PixelID // NPixelsY
iy = PixelID %  NPixelsY
```

`tools/export_interfile.py`'s CSV aggregation path implements this formula directly (Sec. 5). It
is the one topas-spect-specific detail baked into the exporter, and it is read straight from the
scorer source, not inferred.

**Which local axis is transaxial (rotates with the gantry) vs. axial (fixed) is a construction
choice, not a scorer guarantee.** In every shipped mounting example, the scored volume (crystal or
FD surface) is built as a thin box with its face-normal along local Z, translated along one world
axis and rotated about a second. Under that pattern, the box's local axis that runs parallel to
the rotation axis is unaffected by the gantry rotation (stays "axial"/fixed), and the other local
in-plane axis sweeps with it ("transaxial"). Concretely: in `dualhead_orbit.txt`
(`Ge/Gantry/RotY`), HeadA/B's local Y (145 mm half-length) is axial/fixed and local X (190 mm) is
transaxial; in `spect_acquisition.txt` (`Ge/Head/RotZ`), the crystal's local Z (`ZBins`) is
axial/fixed and local X (`XBins`) is transaxial. **Verify this per-deck** (it follows directly from
which `Rot*` parameter drives the gantry, per Sec. 2.3) before assuming `ix`/`iy` map to a
particular DICOM/Interfile row or column axis.

## 3. DICOM PS3.3 Nuclear Medicine Image IOD field map

Source: DICOM PS3.3 §A.5 (Nuclear Medicine Image IOD) and §C.8.4.11 (NM Detector Module),
generic citation per the manuscript's convention (no more specific citation available/needed for
a standard reference).

| DICOM field | Tag | topas-spect source | Status |
|---|---|---|---|
| Focal Distance | (0018,1182) | `d:Ge/<Collim>/FocalLengthX`/`FocalLengthY` on `TsParallelHoleCollimator`, with a documented sign convention (`geometry/TsParallelHoleCollimator.hh`, `docs/design.md`): `0` = parallel, finite = converging, negative = diverging. This is an **exact** match to the DICOM field's own semantics | **mapped** |
| X/Y Focus Center | (0018,1183)/(0018,1184) | Not tracked as a physical offset; topas-spect's converging collimators focus on-axis by construction (`FocalLengthX`/`Y` aim at a point/line on the collimator's own central axis, per `docs/design.md`). An off-axis focus is not a shipped parameter. | **gap** (only if an off-axis-focused collimator is built; on-axis converging/diverging is fully covered by Focal Distance above) |
| Matrix Size | Rows/Columns, standard NM Image module | `i:Sc/<Scorer>/NPixelsX`, `NPixelsY` | **mapped** (see Sec. 2.5 for axis order) |
| Pixel Spacing | standard NM Image module | `d:Sc/<Scorer>/PixelSizeX`, `PixelSizeY` | **mapped** |
| Energy Window Information | Energy Window Range Sequence | `d:Sc/<Scorer>/EnergyWindowLow`, `EnergyWindowHigh` (both scorers) | **mapped** (single window only: topas-spect scorers do not natively support multiple simultaneous energy windows in one run; a TEW/scatter window needs separate runs or post-processing on `RecordScatterOrder`/`ListMode` output, Sec. 4) |
| Image Orientation (Patient) | (0020,0037) | Not produced; no LPS-frame mapping exists anywhere in the package (Sec. 2.4) | **gap** |
| Rotation Information: Number of Frames in Rotation | Rotation Information module | Equal to `i:Tf/NumberOfSequentialTimes` in the driving acquisition deck, **if and only if** every sequential time step is a distinct projection view (true for the shipped acquisition decks) | **mapped**, deck-level (not scorer-level) |
| Rotation Information: Angular Step | Rotation Information module | The `Tf/.../Rate` times per-view dwell, or equivalently `(EndValue - StartValue) / (NumberOfSequentialTimes)` of the driving `Tf`: a deck-level derived quantity, not scorer output | **mapped**, deck-level |
| Rotation Information: Rotation Direction (CW/CCW) | Rotation Information module | Derived per Sec. 2.1-2.3; **not recorded anywhere automatically** | **gap**: must be computed by the user per deck and supplied explicitly (`export_interfile.py --direction`) |
| Rotation Information: Start Angle | Rotation Information module | `Tf/.../StartValue` (entered value; apply Sec. 2.1's sign rule for the physical angle) | **mapped**, deck-level |
| Rotation Information: Scan Arc | Rotation Information module | `Tf/.../Rate` times `TimelineEnd`, or the deck comment (e.g. "orbit 0->180 deg") | **mapped**, deck-level |
| Detector Information: Radial Position | Detector Information module, per-frame | **Not recorded anywhere.** Neither `TsForcedDetectionProjection` nor `TsScoreStarGuideProjection` write the detector's radius/position to the ntuple; for `tools/make_ring_motion.py` output it exists only inside the generated deck's `Tf/DetNX/Values` arrays, not in any run output | **gap**: see Sec. 4 for what this means for non-circular (contour-hugging) orbits specifically |

## 4. Interfile v3.3 SPECT field map

Source: Todd-Pokropek et al., Interfile v3.3 (as consumed by STIR and most academic
reconstruction tools).

| Interfile key | topas-spect source | Status |
|---|---|---|
| `!matrix size [1]` / `[2]` | `NPixelsX`/`NPixelsY` (scorer params) | **mapped** |
| `scaling factor (mm/pixel) [1]`/`[2]` | `PixelSizeX`/`PixelSizeY` (scorer params) | **mapped** |
| `!number of projections` | `Tf/.../NumberOfSequentialTimes` (deck-level) | **mapped**, deck-level |
| `!extent of rotation` | derived from the driving `Tf` (Sec. 3) | **mapped**, deck-level |
| `start angle` | `Tf/.../StartValue`, entered value (apply Sec. 2.1 for the physical angle) | **mapped**, deck-level |
| `!process status` | Always `Acquired` for topas-spect output (it is a forward simulator; nothing it emits is `Reconstructed`) | **mapped**, constant |
| `!direction of rotation` (CW/CCW) | Not recorded; must be derived per Sec. 2.1-2.3 | **gap**: user-supplied, per deck |
| `orbit` (Circular / Non-circular) + per-projection `radius` | Circular orbits (`dualhead_orbit.txt`-style): radius equals the fixed `TransZ`/`TransY` value, readable directly from the deck. Non-circular / contour-hugging orbits (`tools/make_ring_motion.py`, StarGuide ring, Fig. 7): the per-detector, per-time-step radius exists **only** inside the generated deck's `Tf/DetNX,Z/Values` arrays; no run output records which radius was in effect for a given accepted photon/projection | **mapped** for circular orbits; **gap** for non-circular orbits (the StarGuide ring headline result, Fig. 7, is the case this doesn't cover yet) |
| Number of energy windows + bounds | `EnergyWindowLow`/`High` (scorer params) | **mapped** (single window; see DICOM table note on TEW) |
| Isotope / radionuclide name | Not scorer output; determined by the deck's `So/` source definition (`GenericIon` Z/A, or `TsSpectDecaySource`'s isotope selector) | **mapped**, deck-level |
| Pixel data (the binary) | `Weight` column, summed per `PixelID` (Sec. 2.5): this is the projection value; downstream parsers must **sum**, not count, per the scorer's own header comment | **mapped**, via `export_interfile.py` |
| `!time per projection (sec)` / `study duration (sec)` | Per-view dwell from the driving `Tf` timeline (deck-level; not itself scorer output) | **mapped**, deck-level |

## 5. Working exporter: `tools/export_interfile.py`

This is a real, tested script, not a description of one. It covers the **single-head
circular-orbit case** (the acquisition geometry of Figs. 6/7's simpler predecessors and the
`spect_acquisition.txt`/`dualhead_orbit.txt`-style decks), and writes a spec-conformant Interfile
v3.3 header (`.hv`) plus a matching flat binary (`.v`, float32 little-endian, row-major).

**What it deliberately does not do:**

- **Parse TOPAS's raw ntuple output directly.** TOPAS's ASCII/Binary/ROOT ntuple file formats are
  a TOPAS/Geant4 implementation detail, not part of topas-spect's own interop contract, and are
  already readable with TOPAS's own ntuple tools or a plain ROOT/pandas reader. Instead the script
  takes either (a) an already-aggregated NumPy stack (`[nviews, ny, nx]`), or (b) a long-format
  per-pixel/per-photon CSV (`PixelID`, `ViewIndex`, `Weight` columns), which it aggregates using
  the **exact, source-verified** `PixelID = ix*NPixelsY + iy` formula from Sec. 2.5. This keeps
  the one topas-spect-specific piece of logic in the script grounded in the scorer source rather
  than guessed.
- **Choose a rotation direction for you.** `--direction {CW,CCW}` and `--view-from` are both
  required with no default, because Sec. 2.1-2.3 show there is no single safe default:
  you must re-derive it for your deck and pass it explicitly. The script prints the pairing back
  to the console specifically so it lands in the run log.
- **Cover non-circular orbits.** `--radius` is a single scalar (Sec. 4's "circular orbit" row).
  The StarGuide ring's contour-hugging orbit (Fig. 7) is not supported by this exporter; that is
  the "gap" flagged in Sec. 4, and would need per-projection radius as a proper Interfile "Non-
  circular" orbit block with a radius array, which the script does not yet write.

**Verification performed (this session, synthetic data, no live STIR install available):**

- NumPy-input path: round-tripped the written `.v` binary byte-for-byte against the source array
  (`np.array_equal`, exact match) and confirmed the `.hv` header's declared matrix size/view count
  matches the binary file size (`self_check()` in the script, runs automatically on every export).
- CSV-aggregation path: built a synthetic per-photon CSV with each pixel's true value split across
  two rows (to exercise the summation, not just pass-through), and confirmed the aggregated output
  matches the known ground-truth array (`np.array_equal`, both `sim_projections` and
  `csv_projections` test cases).
- **Not verified:** that the written header actually opens correctly in a live STIR install (none
  was available in this environment). The keys follow the Interfile v3.3 SPECT minimal key set
  from the spec; before using this for anything submission-critical, open the output in STIR (or
  `stir_print_file_info`/equivalent) once to confirm parsing.

## 6. Precedent and the pinhole/converging caveat

**Precedent that this export path (forward MC to Interfile to STIR) is a real, referee-legible
pattern, not hand-waving:** a GATE-Monte-Carlo-to-STIR pipeline has been published before,
*"Image reconstruction of GATE SPECT simulation data using STIR,"* 2018 STIR User's and
Developer's Meeting proceedings. **Verification status:** confirmed the title and venue exist (a
matching record was located via web search, publication indexed under the 2018 STIR User's and
Developer's Meeting), but this appears to be an informal workshop-proceedings item with no DOI.
It did not turn up in CrossRef or PubMed/PubMed Central, and author names could not be
independently confirmed through those structured sources in this session (a Semantic Scholar
lookup that might have resolved it was rate-limited throughout). **Before this goes into the
manuscript, get the full author list directly from the STIR project's meeting-proceedings archive**
rather than relying on this session's partial verification.

**Honest limitation to state if the manuscript claims pinhole/converging projections are
"reconstruction-ready":** STIR only gained native pinhole/multi-pinhole SPECT modeling support
recently. Verified via PubMed (`esummary` API, fields confirmed): Strugari ME, Falcón C, Erlandsson
K, Hutton BF, Brewer K, Thielemans K. *"Integration of advanced 3D SPECT modeling for pinhole
collimators into the open-source STIR framework."* Frontiers in Nuclear Medicine. 2023;3:1134774.
doi:10.3389/fnume.2023.1134774. PMID 39380953. (Note: the article's own publication/epub date is
**2023**, not 2024/2025. PubMed's indexing timestamp for the record is 2024-10-09, which is
likely the source of the 2024/2025 date if that was inferred from a PubMed listing rather than the
article's own metadata; use 2023 as the citation year.) **Consequence:** older STIR versions cannot
reconstruct pinhole or converging-collimator SPECT projections at all. topas-spect's pinhole
(`TsPinholeCollimator`) and converging (`TsParallelHoleCollimator` with `FocalLengthX/Y`)
capabilities should be described as "export-compatible" with STIR's *current* (post-2023) pinhole
modeling, not as universally "reconstruction-ready": do not overclaim for older STIR
installations.

## 7. Summary: what is and is not covered

**Fully specified, verified against source:** pixel matrix size/spacing, energy window bounds,
the `PixelID` unflattening formula, the Focal-Distance-to-`FocalLengthX/Y` mapping, the TOPAS
passive-rotation mechanism and its worked physical consequence for the two shipped acquisition
conventions, circular-orbit radius/orbit-type, process status (always `Acquired`).

**Explicitly flagged as gaps, not silently assumed:** rotation direction (CW/CCW, must be
user-derived per deck, no safe default), DICOM LPS-frame assignment (no mapping exists anywhere in
the package), per-projection radius for non-circular/contour-hugging orbits (the StarGuide ring
headline result specifically), off-axis pinhole focus tracking, and multi-simultaneous-energy-
window output.

**Shipped, working code, not just documentation:** `tools/export_interfile.py`, for the
single-head circular-orbit case, tested end-to-end against synthetic data in this session (Sec. 5).
