#!/usr/bin/env python3
"""Generate a SPECT acquisition for a topas-spect phantom scenario, using clinical console parameters.

You prescribe the study the way a SPECT technologist would at the console: the number of projections,
the time per projection, the angular range (360 or 180 degrees), the number of detector heads, the
matrix size, the photopeak energy window, and the orbit radius. The tool derives the acquisition from
that prescription and couples, on one clock:
  - detector gantry MOTION : the head orbits the patient long axis (z), one stop per projection
  - isotope ACTIVITY decay : how the emitted activity falls over the study (and daughters build)
The study duration that drives the decay is (projections / heads) x time-per-projection.

Three engines:
  --decay  (recommended): ONE deck using the bundled SpectDecaySource. Each organ emits its parent ion
     and g4radioactivedecay produces the whole chain (e.g. 225Ac daughters) natively; histories per Tf
     bin follow the true decay, and the gantry rotates on the same Tf timeline. No external extension.
  native   (default): ONE deck per projection, histories scaled by exp(-ln2*t/half_life). Needs
     --half-life-h; simple exponential decay only, no daughter kinetics.
  --vr     : forced-detection fast path per projection (see variance_reduction).

Example (60 projections, 20 s each, dual-head 360 deg, 177Lu):
  make_dynamic_acquisition.py phantoms/scenarios/lu177_psma.txt --decay --isotope "71 177" \
       --projections 60 --time-per-projection 20 --angular-range 360 --heads 2 \
       --matrix 128 --photopeak 208 20 --radius 25 --out lu177_study
"""
import argparse, math, os, re

DETECTOR = """
# --- rotating detector head (orbits patient long axis z); bore faces the patient ---
s:Ge/Head/Type = "Group"
s:Ge/Head/Parent = "World"
{headrot}
s:Ge/Collim/Type = "TsParallelHoleCollimator"
s:Ge/Collim/Parent = "Head"
s:Ge/Collim/Material = "Lead"
d:Ge/Collim/HoleDiameter = 2.94 mm
d:Ge/Collim/SeptalThickness = 1.14 mm
d:Ge/Collim/CollimatorLength = 40.64 mm
i:Ge/Collim/NHolesX = 80
i:Ge/Collim/NHolesY = 100
s:Ge/Collim/Hole/Material = "Vacuum"
d:Ge/Collim/TransY = {rcol} mm
d:Ge/Collim/RotX = -90. deg
s:Ge/Crystal/Type = "TsBox"
s:Ge/Crystal/Parent = "Head"
s:Ge/Crystal/Material = "NaI"
d:Ge/Crystal/HLX = 200. mm
d:Ge/Crystal/HLY = 4.75 mm
d:Ge/Crystal/HLZ = 250. mm
d:Ge/Crystal/TransY = {rcry} mm
i:Ge/Crystal/XBins = {matrix}
i:Ge/Crystal/ZBins = {matrix}
"""
HEAD = """i:Ts/NumberOfThreads = {threads}
sv:Ph/Default/Modules = 1 "g4em-penelope"
sv:Ma/Lead/Components = 1 "Lead"
uv:Ma/Lead/Fractions = 1 1.0
d:Ma/Lead/Density = 11.35 g/cm3
sv:Ma/NaI/Components = 2 "Sodium" "Iodine"
uv:Ma/NaI/Fractions = 2 0.1534 0.8466
d:Ma/NaI/Density = 3.67 g/cm3
d:Ge/World/HLX = 500. mm
d:Ge/World/HLY = 500. mm
d:Ge/World/HLZ = 500. mm
s:Ge/World/Material = "Vacuum"
"""
SCORER = """s:Sc/Proj/Quantity = "EnergyDeposit"
s:Sc/Proj/Component = "Crystal"
s:Sc/Proj/OutputType = "CSV"
s:Sc/Proj/OutputFile = "{out}"
s:Sc/Proj/IfOutputFileAlreadyExists = "Overwrite"
b:Sc/Proj/OutputToConsole = "False"
b:Ts/PauseBeforeQuit = "False"
"""
# Forced-detection fast path (variance reduction): a fixed FD surface at +y (bore toward the virtual
# detector, RotX=90); each view rotates the phantom about its long axis (z) instead of moving the head.
# The scorer analytically projects every forward photon through the collimator - no full-MC collimator
# transport, so a view runs in seconds. The absolute scale is model-based (see docs/variance_reduction.md).
FD_DETECTOR = """
s:Ge/FDSurf/Type = "TsBox"
s:Ge/FDSurf/Parent = "World"
s:Ge/FDSurf/Material = "Vacuum"
d:Ge/FDSurf/HLX = 200. mm
d:Ge/FDSurf/HLY = 250. mm
d:Ge/FDSurf/HLZ = 0.05 mm
d:Ge/FDSurf/TransY = 280. mm
d:Ge/FDSurf/RotX = 90. deg
"""
FD_SCORER = """s:Sc/FD/Quantity = "ForcedDetectionProjection"
s:Sc/FD/Component = "FDSurf"
d:Sc/FD/HoleDiameter = 2.94 mm
d:Sc/FD/SeptalThickness = 1.14 mm
d:Sc/FD/CollimatorLength = 40.64 mm
d:Sc/FD/PixelSizeX = 4.0 mm
d:Sc/FD/PixelSizeY = 4.0 mm
i:Sc/FD/NPixelsX = 100
i:Sc/FD/NPixelsY = 64
d:Sc/FD/EnergyWindowLow = {wlo} keV
d:Sc/FD/EnergyWindowHigh = {whi} keV
s:Sc/FD/OutputType = "ASCII"
s:Sc/FD/OutputFile = "{out}"
s:Sc/FD/IfOutputFileAlreadyExists = "Overwrite"
b:Ts/PauseBeforeQuit = "False"
"""

def parse_sources(scen):
    """Return [(component, histories), ...] from the scenario's Volumetric sources, and the geometry
    text (scenario with all So/ lines removed)."""
    comps = {}   # source-name -> component
    hists = {}   # source-name -> histories
    for m in re.finditer(r's:So/(\w+)/Component\s*=\s*"(\w+)"', scen):
        comps[m.group(1)] = m.group(2)
    for m in re.finditer(r'i:So/(\w+)/NumberOfHistoriesInRun\s*=\s*(\d+)', scen):
        hists[m.group(1)] = int(m.group(2))
    srcs = [(comps[n], hists.get(n, 1000)) for n in comps]
    geom = "\n".join(l for l in scen.splitlines() if not re.match(r'\s*[a-z]+:So/', l))
    return srcs, geom

def tsrts_sources(srcs, Z, A, views):
    out = [f'# --- {len(srcs)} organs as SpectDecaySource (bundled with topas-spect; isotope Z={Z} A={A}).',
           '# Each emits the parent ion; g4radioactivedecay produces the whole chain, and the histories',
           '# per Tf time bin follow the true activity decay. Relative organ activity is the per-source',
           '# NumberOfHistoriesInRun (uptake x volume) ---']
    for comp, w in srcs:
        n = f"Src_{comp}"
        out += [f's:So/{n}/Type = "SpectDecaySource"',
                f's:So/{n}/Component = "{comp}"',
                f's:So/{n}/BeamParticle = "GenericIon({Z},{A})"',
                f'b:So/{n}/CorrectByNumberOfHistories = "True"',
                f'i:So/{n}/NumberOfHistoriesInRun = {w}']       # relative activity (uptake x volume)
    return "\n".join(out)

def timeline(T, views):
    return (f'd:Tf/TimelineStart = 0. s\nd:Tf/TimelineEnd = {T:.1f} s\n'
            f'i:Tf/NumberOfSequentialTimes = {views}\n')

def main():
    p = argparse.ArgumentParser(
        description="Generate a SPECT acquisition from a phantom scenario using clinical console parameters.")
    p.add_argument("scenario")
    # --- acquisition prescription (SPECT console terms) ---
    p.add_argument("--projections", "--views", dest="projections", type=int, required=True,
                   help="number of projections / angular samples (e.g. 60, 64, 120, 128)")
    p.add_argument("--time-per-projection", type=float, default=20.0,
                   help="dwell time per projection in seconds (default 20)")
    p.add_argument("--angular-range", "--orbit", dest="angular_range", type=float, default=360.0,
                   help="angular range in degrees: 360 (whole body) or 180 (cardiac)")
    p.add_argument("--heads", type=int, default=2, choices=[1, 2, 3],
                   help="number of detector heads acquiring in parallel (shortens the study; default 2)")
    p.add_argument("--matrix", type=int, default=128, help="projection matrix size N (N x N; default 128)")
    p.add_argument("--photopeak", nargs=2, type=float, default=[208.0, 20.0],
                   metavar=("CENTER_keV", "WIDTH_pct"),
                   help="photopeak energy window: center keV and total width percent (default 208 20 for 177Lu)")
    p.add_argument("--radius", type=float, default=30.0, help="orbit radius, detector-to-axis, in cm (default 30)")
    # --- engine / isotope ---
    p.add_argument("--isotope", help='isotope "Z A", e.g. "71 177" (177Lu), "89 225" (225Ac)')
    p.add_argument("--half-life-h", type=float, help="native backend: isotope half-life in hours")
    p.add_argument("--tsrts", "--decay", dest="tsrts", action="store_true",
                   help="native-decay backend: one deck using the bundled SpectDecaySource")
    p.add_argument("--vr", action="store_true",
                   help="forced-detection fast path per projection (rotate phantom about z, analytic collimator)")
    p.add_argument("--threads", type=int, default=4)
    p.add_argument("--out", default="acquisition")
    a = p.parse_args()

    scen = open(a.scenario).read()
    # clinical prescription -> internal quantities
    views = a.projections
    orbit = a.angular_range
    T = (a.projections / a.heads) * a.time_per_projection      # wall-clock acquisition time (s); drives decay
    rcol = a.radius * 10.0                                      # cm -> mm; collimator face at the orbit radius
    rcry = rcol + 25.5                                          # crystal just behind the collimator
    wlo = round(a.photopeak[0] * (1.0 - a.photopeak[1] / 200.0), 2)  # photopeak window low keV
    whi = round(a.photopeak[0] * (1.0 + a.photopeak[1] / 200.0), 2)  # photopeak window high keV
    det = dict(rcol=rcol, rcry=rcry, matrix=a.matrix)
    os.makedirs(a.out, exist_ok=True)

    if a.tsrts:
        if not a.isotope:
            p.error("--decay requires --isotope \"Z A\"")
        Z, A = a.isotope.split()
        srcs, geom = parse_sources(scen)
        # ONE deck: geometry + tsRTS sources + timeline + gantry motion (Linear) + scorer
        headrot = (f'd:Ge/Head/RotZ = Tf/Gantry/Value deg\n'
                   f's:Tf/Gantry/Function = "Linear deg"\n'
                   f'd:Tf/Gantry/StartValue = 0. deg\n'
                   f'd:Tf/Gantry/Rate = {orbit / T:.5f} deg/s')
        head = HEAD.format(threads=a.threads).replace(
            'sv:Ph/Default/Modules = 1 "g4em-penelope"',
            'sv:Ph/Default/Modules = 2 "g4em-penelope" "g4radioactivedecay"')  # decay the emitted ions
        deck = (head + timeline(T, views) + geom + "\n"
                + tsrts_sources(srcs, Z, A, views) + "\n"
                + DETECTOR.format(headrot=headrot, **det)
                + SCORER.format(out=f"{a.out}"))
        open(os.path.join(a.out, "acquisition.txt"), "w").write(deck)
        print(f"decay single deck in {a.out}/acquisition.txt | {views} projections, {orbit:.0f} deg range, "
              f"{a.heads} head(s), {a.time_per_projection:.0f} s/projection -> {T/60:.1f} min study, "
              f"isotope Z={Z} A={A} (decay + chain native, no external extension).")
        return

    # native backend: per-view decks with exp() decay
    if a.half_life_h is None:
        p.error("native backend requires --half-life-h (or use --decay)")
    lam = math.log(2) / (a.half_life_h * 3600.0)
    runner = []
    for k in range(views):
        t = k * T / views
        decay = math.exp(-lam * t)
        gantry = orbit * k / views
        body = re.sub(r'(NumberOfHistoriesInRun\s*=\s*)(\d+)',
                      lambda m: f"{m.group(1)}{max(1, round(int(m.group(2))*decay))}", scen)
        if a.vr:
            # forced detection: fixed FD detector at +y; rotate the phantom about its long axis (z) to
            # the projection angle. Analytic collimator projection -> seconds per projection, not full-MC.
            deck = (HEAD.format(threads=a.threads) + body
                    + f"\nd:Ge/Torso/RotZ = {gantry:.3f} deg\n"
                    + FD_DETECTOR
                    + FD_SCORER.format(wlo=wlo, whi=whi, out=f"{a.out}_view{k:03d}"))
        else:
            deck = (HEAD.format(threads=a.threads) + body + "\n"
                    + DETECTOR.format(headrot=f"d:Ge/Head/RotZ = {gantry:.3f} deg", **det)
                    + SCORER.format(out=f"{a.out}_view{k:03d}"))
        open(os.path.join(a.out, f"view{k:03d}.txt"), "w").write(deck)
        runner.append(f'"$TOPAS_BIN" view{k:03d}.txt   # gantry {gantry:.1f} deg, activity {decay*100:.2f}%'
                      + ("  [forced detection]" if a.vr else ""))
    open(os.path.join(a.out, "run_all.sh"), "w").write("#!/bin/bash\n" + "\n".join(runner) + "\n")
    print(f"{views} projection decks in {a.out}/ | {orbit:.0f} deg range, {a.heads} head(s), "
          f"{a.time_per_projection:.0f} s/projection -> {T/60:.1f} min study; "
          f"activity falls to {math.exp(-lam*T)*100:.2f}% by the last projection")

if __name__ == "__main__":
    main()
