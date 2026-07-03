#!/usr/bin/env python3
"""Predict per-detector motion for a ring SPECT system from a patient contour, and write the OpenTOPAS
motion parameter file.

The body contour comes from either a real CT/PET DICOM series or an analytic ellipse. For each detector
the tool computes the radius at which it docks against the body (contour distance + clearance), then
builds a time-feature motion deck: HOME (uniform circle) -> APPROACH (each detector drives radially in
to its own docked radius, following the contour) -> ACQUIRE (gantry rotates while detectors sweep) ->
RETRACT home. This mirrors StarGuide's optical-scout body-contouring: each of the 12 columns is driven
to its own radius, so the "ring" is an irregular shape hugging the patient.

Examples:
  make_ring_motion.py --system starguide --ellipse 150 100 --out sg_motion.txt
  make_ring_motion.py --system ring --ndet 16 --dicom /path/to/CT --hu-threshold -300 --out ring_motion.txt
"""
import argparse, math, os

# (label, tangential half-width mm, axial half-length mm, CZT half-thickness mm)
SYSTEMS = {
    "starguide": dict(ndet=12, hlx=19.65, hly=137.5, hlz=3.625, mat="G4_CADMIUM_TELLURIDE", color="60 90 230"),
    "ring":      dict(ndet=16, hlx=25.0,  hly=120.0, hlz=4.0,   mat="G4_CADMIUM_TELLURIDE", color="60 90 230"),
}

def contour_from_ellipse(a, b):
    """Return R(theta_deg) for an ellipse with semi-axes a (x), b (z)."""
    def R(thdeg):
        th = math.radians(thdeg)
        return 1.0 / math.sqrt((math.sin(th)/a)**2 + (math.cos(th)/b)**2)
    return R

def body_mask(arr, threshold):
    """Clean patient-body mask from a slice: threshold, fill internal air (lungs), break thin
    connections to the table/straps, and keep only the largest connected component (drops the scanner
    table, detached arms, and noise). Returns the mask, or None if empty."""
    import numpy as np
    from scipy import ndimage
    m = arr > threshold
    if not m.any():
        return None
    m = ndimage.binary_closing(m, iterations=2)     # close small gaps in the body wall
    m = ndimage.binary_fill_holes(m)                # fill lungs / air pockets so the contour is the outer body
    m = ndimage.binary_opening(m, iterations=3)     # sever thin table bridges / straps, despeckle
    lbl, n = ndimage.label(m)
    if n == 0:
        return None
    sizes = ndimage.sum(np.ones_like(lbl), lbl, range(1, n + 1))
    m = lbl == (int(np.argmax(sizes)) + 1)          # largest component = torso (table + detached arms dropped)
    return ndimage.binary_fill_holes(m)


def contour_from_radii(radii):
    """Return R(theta_deg) interpolated from N measured contour radii at evenly spaced azimuths
    (0, 360/N, ...). Lets a PHI-safe upstream step (e.g. a de-identified on-server extraction) hand in
    just the geometry."""
    n = len(radii)
    def R(thdeg):
        x = (thdeg % 360.0) / (360.0 / n)
        i = int(math.floor(x)) % n
        f = x - math.floor(x)
        return radii[i] * (1 - f) + radii[(i + 1) % n] * f
    return R


def contour_from_dicom(dcmdir, threshold, axial_frac):
    """Radial body-contour profile R(theta) from a DICOM series: stack slices, build a cleaned body
    mask (table/arm removal) at the chosen axial level, cast rays from the mask centroid, return the
    distance to the boundary."""
    import pydicom, numpy as np, glob
    files = sorted(glob.glob(os.path.join(dcmdir, "*.dcm")))
    if not files:
        raise SystemExit(f"no .dcm files in {dcmdir}")
    slices = []
    for f in files:
        d = pydicom.dcmread(f)
        if not hasattr(d, "pixel_array"):
            continue
        z = float(getattr(d, "ImagePositionPatient", [0, 0, 0])[2]) if "ImagePositionPatient" in d else len(slices)
        arr = d.pixel_array.astype(float) * float(getattr(d, "RescaleSlope", 1)) + float(getattr(d, "RescaleIntercept", 0))
        px = [float(v) for v in getattr(d, "PixelSpacing", [1.0, 1.0])]
        slices.append((z, arr, px))
    slices.sort(key=lambda s: s[0])
    z, arr, (dy, dx) = slices[int(axial_frac * (len(slices) - 1))]
    mask = body_mask(arr, threshold)
    if mask is None:
        raise SystemExit("empty body mask - adjust --hu-threshold")
    ys, xs = np.where(mask)
    cy, cx = ys.mean(), xs.mean()
    def R(thdeg):
        th = math.radians(thdeg); ux, uz = math.sin(th), math.cos(th)   # ring plane = image (x, z=y-of-image)
        r = 0.0
        while True:
            ix, iy = int(round(cx + r/dx*ux)), int(round(cy + r/dy*uz))
            if iy < 0 or iy >= mask.shape[0] or ix < 0 or ix >= mask.shape[1] or not mask[iy, ix]:
                break
            r += min(dx, dy)
        return max(r, 1.0)
    return R

def collision_safe_radii(R, N, clearance, gantry, sweep, hlx):
    """Per-detector docked radius that clears the body throughout the motion. A detector at azimuth th
    sweeps through [th, th+gantry] as the ring rotates, and its panel spans +/- a tangential half-angle;
    dock it at the MAX contour over that whole range (+ clearance), not just the contour at its start
    azimuth, so it never enters the body while rotating/sweeping."""
    out = []
    for i in range(N):
        th0 = i * 360.0 / N
        rnom = R(th0) + clearance
        dpanel = math.degrees(hlx / max(rnom, 50.0))
        lo, hi = th0 - dpanel - sweep, th0 + gantry + dpanel + sweep
        out.append(clearance + max(R(lo + (hi - lo) * k / 24) for k in range(25)))
    return out


def main():
    p = argparse.ArgumentParser()
    p.add_argument("--system", choices=list(SYSTEMS), default="starguide")
    p.add_argument("--ndet", type=int, help="override number of detectors (generic ring)")
    p.add_argument("--ellipse", nargs=2, type=float, metavar=("A", "B"), help="analytic contour semi-axes (mm)")
    p.add_argument("--radii", nargs="+", type=float, help="measured contour radii (mm) at evenly spaced azimuths")
    p.add_argument("--dicom", help="DICOM series directory (CT or PET)")
    p.add_argument("--hu-threshold", type=float, default=-300.0, help="body-mask threshold (HU for CT)")
    p.add_argument("--axial-frac", type=float, default=0.5, help="axial level of the ring (0=first slice, 1=last)")
    p.add_argument("--clearance", type=float, default=40.0, help="detector-face gap from the body surface (mm)")
    p.add_argument("--home", type=float, default=300.0, help="retracted (home) radius (mm)")
    p.add_argument("--sweep", type=float, default=15.0, help="per-detector sweep half-range (deg)")
    p.add_argument("--gantry", type=float, default=30.0, help="gantry rotation range (deg)")
    p.add_argument("--napproach", type=int, default=10)
    p.add_argument("--nacquire", type=int, default=16)
    p.add_argument("--out", default="ring_motion.txt")
    a = p.parse_args()

    sysd = dict(SYSTEMS[a.system]); N = a.ndet or sysd["ndet"]
    R = (contour_from_ellipse(*a.ellipse) if a.ellipse
         else contour_from_radii(a.radii) if a.radii
         else contour_from_dicom(a.dicom, a.hu_threshold, a.axial_frac) if a.dicom
         else p.error("give --ellipse A B, --radii ..., or --dicom DIR"))
    Rc = collision_safe_radii(R, N, a.clearance, a.gantry, a.sweep, sysd["hlx"])

    Na, Nac, Nr = a.napproach, a.nacquire, 8; Ntot = Na + Nac + Nr
    def state(k):
        if k < Na:                return k/(Na-1), 0.0, 0.0
        if k < Na+Nac:
            kk = k-Na; return 1.0, a.gantry*(kk+1)/Nac, a.sweep*math.sin(2*math.pi*kk/8)
        kk = k-(Na+Nac); f = 1-(kk+1)/Nr; return f, a.gantry*f, 0.0
    times = " ".join(str(k) for k in range(Ntot))

    src = f"ellipse {a.ellipse}" if a.ellipse else "measured radii" if a.radii else f"DICOM {a.dicom}"
    L = [f"# {a.system} ring motion from {src}",
         f"# {N} detectors; per-detector docked radii (mm): " + ", ".join(f"{r:.0f}" for r in Rc),
         "# HOME -> APPROACH (contour-hug) -> ACQUIRE (gantry+sweep) -> RETRACT. No radiation; open in the Qt GUI, click run.",
         "i:Ts/NumberOfThreads = 1", f"i:Tf/NumberOfSequentialTimes = {Ntot}", "d:Tf/TimelineStart = 0. s", f"d:Tf/TimelineEnd = {Ntot-1}. s",
         "d:Ge/World/HLX = 420. mm", "d:Ge/World/HLY = 420. mm", "d:Ge/World/HLZ = 420. mm", 's:Ge/World/Material = "Vacuum"',
         'b:Ge/World/Invisible = "True"', 'sv:Ph/Default/Modules = 1 "g4em-penelope"',
         f'iv:Gr/Color/tc_det = 3 {sysd["color"]}', f'sv:Ma/CZT/Components = 3 "Cadmium" "Zinc" "Tellurium"',
         'uv:Ma/CZT/Fractions = 3 0.430 0.028 0.542', 'd:Ma/CZT/Density = 5.78 g/cm3',
         # a body ellipse for context (approx bounding size)
         's:Ge/Body/Type = "G4EllipticalTube"', 's:Ge/Body/Parent = "World"', 's:Ge/Body/Material = "G4_TISSUE_SOFT_ICRP"',
         f'd:Ge/Body/HLX = {max(Rc)-a.clearance:.0f} mm', f'd:Ge/Body/HLY = 130. mm', f'd:Ge/Body/HLZ = {min(Rc)-a.clearance:.0f} mm',
         'd:Ge/Body/RotX = 90. deg', 's:Ge/Body/DrawingStyle = "WireFrame"', 's:Ge/Body/Color = "lightblue"',
         's:Ge/Gantry/Type = "Group"', 's:Ge/Gantry/Parent = "World"', 'd:Ge/Gantry/RotY = Tf/Gantry/Value deg',
         's:Tf/Gantry/Function = "Step"', f'dv:Tf/Gantry/Times = {Ntot} {times} s',
         f'dv:Tf/Gantry/Values = {Ntot} ' + " ".join(f"{state(k)[1]:.2f}" for k in range(Ntot)) + ' deg']
    for i in range(N):
        th = i*360.0/N; s = math.sin(math.radians(th)); c = math.cos(math.radians(th)); n = f"Det{i}"
        xs = " ".join(f"{(a.home+(Rc[i]-a.home)*state(k)[0])*s:.2f}" for k in range(Ntot))
        zs = " ".join(f"{(a.home+(Rc[i]-a.home)*state(k)[0])*c:.2f}" for k in range(Ntot))
        rs = " ".join(f"{-th+state(k)[2]:.2f}" for k in range(Ntot))
        L += [f's:Ge/{n}/Type = "TsBox"', f's:Ge/{n}/Parent = "Gantry"', f's:Ge/{n}/Material = "{sysd["mat"]}"',
              f'd:Ge/{n}/HLX = {sysd["hlx"]} mm', f'd:Ge/{n}/HLY = {sysd["hly"]} mm', f'd:Ge/{n}/HLZ = {sysd["hlz"]} mm',
              f's:Ge/{n}/Color = "tc_det"',
              f'd:Ge/{n}/TransX = Tf/{n}X/Value mm', f's:Tf/{n}X/Function = "Step"', f'dv:Tf/{n}X/Times = {Ntot} {times} s', f'dv:Tf/{n}X/Values = {Ntot} {xs} mm',
              f'd:Ge/{n}/TransZ = Tf/{n}Z/Value mm', f's:Tf/{n}Z/Function = "Step"', f'dv:Tf/{n}Z/Times = {Ntot} {times} s', f'dv:Tf/{n}Z/Values = {Ntot} {zs} mm',
              f'd:Ge/{n}/RotY = Tf/{n}R/Value deg', f's:Tf/{n}R/Function = "Step"', f'dv:Tf/{n}R/Times = {Ntot} {times} s', f'dv:Tf/{n}R/Values = {Ntot} {rs} deg']
    L += ['s:Gr/View/Type = "OpenGL"', 'd:Gr/View/Theta = 62 deg', 'd:Gr/View/Phi = 26 deg', 'u:Gr/View/Zoom = 1.1',
          'b:Ts/UseQt = "True"', 'b:Ts/PauseBeforeQuit = "True"']
    open(a.out, "w").write("\n".join(L) + "\n")
    print(f"{a.out}: {a.system}, {N} detectors, docked radii {min(Rc):.0f}-{max(Rc):.0f} mm "
          f"(contour {'ellipse' if a.ellipse else 'DICOM @axial-frac '+str(a.axial_frac)}), {Ntot} motion steps")

if __name__ == "__main__":
    main()
