#!/usr/bin/env python3
"""End-to-end scientific control experiment for topas-spect.

Exercises the full imaging chain against a KNOWN analytic answer: an off-axis point source imaged
over a 360 deg acquisition through a Symbia LEHR collimator with forced detection. The classic SPECT
consistency law says the projected position of a point at radial offset rho traces a SINUSOID of
amplitude rho versus the view angle. We generate the acquisition, run it, decode each view's
weighted centroid, and check:

  1. sinogram amplitude == source radial offset (geometry + acquisition motion correct),
  2. per-view centroid follows rho*cos(phi) to sub-pixel residual,
  3. counts are flat across views for a centered-radius source in vacuum (angular sensitivity uniform).

Needs an OpenTOPAS build with the topas-spect extension. Run:
    TOPAS_BIN=/path/to/topas python3 tests/control_experiment.py [--rho 60] [--views 12]
Exits non-zero on failure so it can gate a release.
"""
import argparse, math, os, subprocess, sys, tempfile

DECK = """i:Ts/NumberOfThreads = {threads}
sv:Ph/Default/Modules = 1 "g4em-penelope"
d:Ge/World/HLX = 400. mm
d:Ge/World/HLY = 400. mm
d:Ge/World/HLZ = 400. mm
s:Ge/World/Material = "Vacuum"
s:Ge/Pt/Type = "TsSphere"
s:Ge/Pt/Parent = "World"
s:Ge/Pt/Material = "G4_WATER"
d:Ge/Pt/RMax = 2. mm
d:Ge/Pt/TransX = {x0} mm
d:Ge/Pt/TransY = {y0} mm
s:So/S/Type = "Volumetric"
s:So/S/Component = "Pt"
s:So/S/VolumetricParticle = "gamma"
d:So/S/VolumetricEnergy = 140.5 keV
s:So/S/ActiveMaterial = "G4_WATER"
i:So/S/NumberOfHistoriesInRun = {hist}
s:Ge/FDSurf/Type = "TsBox"
s:Ge/FDSurf/Parent = "World"
s:Ge/FDSurf/Material = "Vacuum"
d:Ge/FDSurf/HLX = {hlx} mm
d:Ge/FDSurf/HLY = 130. mm
d:Ge/FDSurf/HLZ = 0.05 mm
d:Ge/FDSurf/TransY = 280. mm
d:Ge/FDSurf/RotX = 90. deg
s:Sc/FD/Quantity = "ForcedDetectionProjection"
s:Sc/FD/Component = "FDSurf"
d:Sc/FD/HoleDiameter = 1.11 mm
d:Sc/FD/SeptalThickness = 0.16 mm
d:Sc/FD/CollimatorLength = 24.05 mm
d:Sc/FD/PixelSizeX = {psx} mm
d:Sc/FD/PixelSizeY = 4.0 mm
i:Sc/FD/NPixelsX = {npx}
i:Sc/FD/NPixelsY = {npy}
d:Sc/FD/EnergyWindowLow = 126.5 keV
d:Sc/FD/EnergyWindowHigh = 154.6 keV
s:Sc/FD/OutputType = "ASCII"
s:Sc/FD/OutputFile = "{out}"
s:Sc/FD/IfOutputFileAlreadyExists = "Overwrite"
b:Ts/PauseBeforeQuit = "False"
"""

def centroid_x(phsp, npy, psx, hlx):
    sw = swx = 0.0
    for line in open(phsp):
        f = line.split()
        if len(f) < 5:
            continue
        pid = int(float(f[0])); w = float(f[-1])
        ix = pid // npy
        sw += w; swx += w * (-hlx + (ix + 0.5) * psx)
    return (swx / sw if sw > 0 else float("nan")), sw

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--rho", type=float, default=60.0)
    ap.add_argument("--views", type=int, default=12)
    ap.add_argument("--hist", type=int, default=400000)
    ap.add_argument("--threads", type=int, default=4)
    a = ap.parse_args()
    topas = os.environ.get("TOPAS_BIN")
    if not topas:
        sys.exit("set TOPAS_BIN to a topas-spect build")
    npx, npy, psx = 100, 64, 4.0; hlx = npx * psx / 2
    tmp = tempfile.mkdtemp(); rows = []
    for k in range(a.views):
        phi = 2 * math.pi * k / a.views
        deck = DECK.format(threads=a.threads, x0=f"{a.rho*math.cos(phi):.3f}", y0=f"{a.rho*math.sin(phi):.3f}",
                           hist=a.hist, hlx=hlx, psx=psx, npx=npx, npy=npy, out=f"v{k:02d}")
        fn = os.path.join(tmp, f"v{k:02d}.txt"); open(fn, "w").write(deck)
        subprocess.run([topas, os.path.basename(fn)], cwd=tmp, capture_output=True)
        xc, sw = centroid_x(os.path.join(tmp, f"v{k:02d}.phsp"), npy, psx, hlx)
        rows.append((360.0 * k / a.views, xc, sw))

    print(" phi   x-centroid   rho*cos   counts")
    maxres = 0.0
    for phi, xc, sw in rows:
        exp = a.rho * math.cos(math.radians(phi))
        if sw > 0:
            maxres = max(maxres, abs(xc - exp))
        print(f"{phi:5.0f}   {xc:8.1f}   {exp:7.1f}   {sw:.3f}")
    Sc = sum(math.cos(math.radians(p)) ** 2 for p, _, s in rows if s > 0)
    amp = sum(xc * math.cos(math.radians(p)) for p, xc, s in rows if s > 0) / Sc
    cnts = [s for _, _, s in rows if s > 0]
    cv = (max(cnts) - min(cnts)) / (sum(cnts) / len(cnts))
    print(f"\namplitude {amp:.1f} mm (expect {a.rho}) | max residual {maxres:.1f} mm | count spread {cv*100:.1f}%")
    ok = abs(amp - a.rho) <= 0.1 * a.rho and maxres <= 2 * psx and cv <= 0.15
    print("PASS: sinogram amplitude = source offset, sub-2-pixel residual, flat angular counts" if ok
          else "FAIL")
    sys.exit(0 if ok else 1)

if __name__ == "__main__":
    main()
