#!/usr/bin/env python3
"""Write a SPECT projection set to Interfile v3.3 (the format STIR and most academic
reconstruction packages consume), for a single-head circular-orbit acquisition.

SCOPE: topas-spect is a forward Monte Carlo simulator; it does not read or write Interfile
itself, and this tool does not reconstruct anything. It only repackages projection images you
have already built from topas-spect's ntuple scorer output (TsForcedDetectionProjection or
TsScoreStarGuideProjection) into a format STIR/SIRF/other Interfile-reading tools can open
directly, so the "export -> external reconstruction" path is a real, working example rather
than only a documented one. See docs/reconstruction_interop.md for the full field-mapping
this implements and the coordinate-convention warnings that apply to every argument below.

WHAT THIS DOES NOT DO
- It does not parse TOPAS's raw ntuple ASCII/Binary/ROOT output directly (that format is a
  TOPAS/Geant4 implementation detail, not part of the public interop contract, and is best
  read with TOPAS's own ntuple readers or a plain ROOT/pandas reader). You supply projection
  images either as a stacked NumPy array (.npy, shape [nviews, ny, nx]) or as a long-format
  per-pixel CSV (columns PixelID, Weight, and a view index) that this script aggregates using
  topas-spect's OWN documented pixel-unflattening formula (ix = PixelID // NPixelsY,
  iy = PixelID % NPixelsY, per TsForcedDetectionProjection::ComputePixelID /
  TsScoreStarGuideProjection::ComputePixelID) -- so the one topas-spect-specific piece of
  knowledge this script embeds is exactly the one that is verified against source, not guessed.
- It does not choose a rotation direction or axis for you. topas-spect does not have one fixed
  world-axis convention across its shipped examples (see docs/reconstruction_interop.md
  Sec. 2) -- you must pass --direction explicitly, after checking which one your deck
  actually produces.

Example (NumPy input, 60 views, 128x128, single-head circular orbit, 99mTc 20% window):
    python3 tools/export_interfile.py \\
        --projections proj_stack.npy --pixel-size-x 4.42 --pixel-size-y 4.42 \\
        --start-angle 0 --angular-step 3.0 --direction CW --view-from "+Y" \\
        --radius 200 --energy-window-low 126.5 --energy-window-high 154.6 \\
        --isotope Tc-99m --out sim_projections

Example (CSV input aggregated from the FD ntuple, one row per accepted photon, columns
PixelID,ViewIndex,Weight):
    python3 tools/export_interfile.py \\
        --projections-csv fd_hits.csv --n-pixels-x 128 --n-pixels-y 128 --n-views 60 \\
        --pixel-size-x 4.42 --pixel-size-y 4.42 --start-angle 0 --angular-step 3.0 \\
        --direction CW --view-from "+Y" --radius 200 \\
        --energy-window-low 126.5 --energy-window-high 154.6 --isotope Tc-99m --out sim_projections

Output: <out>.hv (Interfile ASCII header) + <out>.v (flat binary, float32, little-endian,
row-major: matrix size [1] = nx varies fastest, matrix size [2] = ny next, then view).
"""
import argparse
import csv
from collections import defaultdict

import numpy as np


def load_from_npy(path):
    arr = np.load(path)
    if arr.ndim != 3:
        raise SystemExit(f"--projections array must be 3-D [nviews, ny, nx], got shape {arr.shape}")
    return arr.astype(np.float32)


def load_from_csv(path, n_pixels_x, n_pixels_y, n_views, pixel_col, weight_col, view_col):
    """Aggregate a long-format per-pixel/per-photon CSV into a [nviews, ny, nx] stack, using
    topas-spect's own PixelID = ix * NPixelsY + iy convention (verified against
    TsForcedDetectionProjection.cc / TsScoreStarGuideProjection.cc ComputePixelID)."""
    sums = defaultdict(float)
    with open(path, newline="") as f:
        reader = csv.DictReader(f)
        missing = {pixel_col, weight_col, view_col} - set(reader.fieldnames or [])
        if missing:
            raise SystemExit(f"CSV is missing expected column(s): {sorted(missing)}. "
                              f"Found columns: {reader.fieldnames}")
        for row in reader:
            pixel_id = int(row[pixel_col])
            view = int(row[view_col])
            weight = float(row[weight_col])
            sums[(view, pixel_id)] += weight

    arr = np.zeros((n_views, n_pixels_y, n_pixels_x), dtype=np.float32)
    for (view, pixel_id), total in sums.items():
        if not (0 <= view < n_views):
            continue
        ix = pixel_id // n_pixels_y
        iy = pixel_id % n_pixels_y
        if 0 <= ix < n_pixels_x and 0 <= iy < n_pixels_y:
            arr[view, iy, ix] += total
    return arr


INTERFILE_HEADER_TEMPLATE = """!INTERFILE :=
!imaging modality := nucmed
!originating system := topas-spect
!version of keys := 3.3
!GENERAL DATA :=
data description := topas-spect Monte Carlo SPECT projection set
!GENERAL IMAGE DATA :=
!type of data := Tomographic
!total number of images := {n_views}
imagedata byte order := LITTLEENDIAN
!number format := float
!number of bytes per pixel := 4
!number of dimensions := 2
!matrix size [1] := {nx}
!matrix size [2] := {ny}
scaling factor (mm/pixel) [1] := {pixel_size_x}
scaling factor (mm/pixel) [2] := {pixel_size_y}
number of energy windows := 1
energy window [1] := {isotope} photopeak
energy window lower level[1] := {e_low}
energy window upper level[1] := {e_high}
flood corrected := N
decay corrected := N
!SPECT STUDY (general) :=
isotope name := {isotope}
number of detector heads := 1
!process status := Acquired
!number of projections := {n_views}
!extent of rotation := {extent}
!time per projection (sec) := {time_per_view}
!maximum pixel count := {max_count}
patient orientation := unknown_orientation
patient rotation := unknown_rotation
!SPECT STUDY (acquired data) :=
!direction of rotation := {direction}
start angle := {start_angle}
first projection angle in data set := {start_angle}
acquisition mode := stepped
orbit := Circular
radius := {radius}
!END OF INTERFILE :=
"""


def write_interfile(out_base, stack, args):
    n_views, ny, nx = stack.shape
    extent = args.angular_step * n_views
    max_count = float(stack.max()) if stack.size else 0.0
    header = INTERFILE_HEADER_TEMPLATE.format(
        n_views=n_views, nx=nx, ny=ny,
        pixel_size_x=args.pixel_size_x, pixel_size_y=args.pixel_size_y,
        isotope=args.isotope, e_low=args.energy_window_low, e_high=args.energy_window_high,
        extent=extent, time_per_view=args.time_per_view, max_count=max_count,
        direction=args.direction, start_angle=args.start_angle, radius=args.radius,
    )
    with open(out_base + ".hv", "w") as f:
        f.write(header)

    # Flat binary: view-major, then row-major (ny, nx) per view, float32 little-endian.
    with open(out_base + ".v", "wb") as f:
        for view in range(n_views):
            f.write(stack[view].astype("<f4").tobytes())

    print(f"Wrote {out_base}.hv (Interfile header) and {out_base}.v "
          f"({n_views} views, {ny}x{nx}, float32 LE, {stack.nbytes} bytes)")


def self_check(out_base):
    """Round-trip sanity check: re-read the header keys we just wrote and confirm the
    declared matrix size / view count match the actual binary file size. Does not validate
    against a live STIR install -- see docs/reconstruction_interop.md for that caveat."""
    keys = {}
    with open(out_base + ".hv") as f:
        for line in f:
            if ":=" not in line:
                continue
            k, v = line.split(":=", 1)
            keys[k.strip().lstrip("!").lower()] = v.strip()

    nx = int(keys["matrix size [1]"])
    ny = int(keys["matrix size [2]"])
    n_views = int(keys["total number of images"])
    expected_bytes = nx * ny * n_views * 4

    import os
    actual_bytes = os.path.getsize(out_base + ".v")
    if actual_bytes != expected_bytes:
        raise SystemExit(f"Self-check FAILED: header declares {nx}x{ny}x{n_views} "
                          f"({expected_bytes} bytes) but {out_base}.v is {actual_bytes} bytes.")
    print(f"Self-check OK: header ({nx}x{ny}x{n_views}) matches binary file size.")


def main():
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    src = p.add_mutually_exclusive_group(required=True)
    src.add_argument("--projections", help="NumPy .npy file, shape [nviews, ny, nx]")
    src.add_argument("--projections-csv", help="long-format CSV to aggregate (PixelID, ViewIndex, Weight)")

    p.add_argument("--pixel-col", default="PixelID")
    p.add_argument("--weight-col", default="Weight")
    p.add_argument("--view-col", default="ViewIndex")
    p.add_argument("--n-pixels-x", type=int, help="required with --projections-csv")
    p.add_argument("--n-pixels-y", type=int, help="required with --projections-csv")
    p.add_argument("--n-views", type=int, help="required with --projections-csv")

    p.add_argument("--pixel-size-x", type=float, required=True, help="mm")
    p.add_argument("--pixel-size-y", type=float, required=True, help="mm")
    p.add_argument("--start-angle", type=float, required=True, help="deg, TOPAS-entered start angle")
    p.add_argument("--angular-step", type=float, required=True, help="deg between consecutive views")
    p.add_argument("--direction", choices=["CW", "CCW"], required=True,
                    help="Interfile 'direction of rotation'. NOT inferred: derive it yourself "
                         "from the deck per docs/reconstruction_interop.md Sec. 2 (TOPAS RotX/Y/Z "
                         "are PASSIVE frame rotations -- the physical sweep direction is the "
                         "INVERSE of the entered angle's sign).")
    p.add_argument("--view-from", required=True,
                    help="free-text note on which end of the rotation axis this direction is "
                         "viewed from (e.g. 'from +Y', 'looking toward -Z') -- written nowhere in "
                         "the Interfile format itself, but printed to the console so it is "
                         "captured in your run log; CW/CCW is meaningless without it. If your "
                         "value starts with '-' (e.g. '-Z'), use --view-from='-Z' or write it as "
                         "'from -Z' so argparse does not mistake it for a flag.")
    p.add_argument("--radius", type=float, required=True, help="mm, circular-orbit detector radius")
    p.add_argument("--time-per-view", type=float, default=1.0, help="sec (informational only)")
    p.add_argument("--energy-window-low", type=float, required=True, help="keV")
    p.add_argument("--energy-window-high", type=float, required=True, help="keV")
    p.add_argument("--isotope", default="Tc-99m")
    p.add_argument("--out", required=True, help="output basename (writes <out>.hv and <out>.v)")
    args = p.parse_args()

    if args.projections:
        stack = load_from_npy(args.projections)
    else:
        for name in ("n_pixels_x", "n_pixels_y", "n_views"):
            if getattr(args, name) is None:
                raise SystemExit(f"--projections-csv requires --{name.replace('_', '-')}")
        stack = load_from_csv(args.projections_csv, args.n_pixels_x, args.n_pixels_y, args.n_views,
                               args.pixel_col, args.weight_col, args.view_col)

    print(f"direction={args.direction}, view-from={args.view_from!r} "
          f"(this pairing is what makes CW/CCW unambiguous -- record it in your run log)")

    write_interfile(args.out, stack, args)
    self_check(args.out)


if __name__ == "__main__":
    main()
