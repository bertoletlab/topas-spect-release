#!/usr/bin/env python3
"""Value gate for the head-frame output of EDepSpectrum (RecordLocalPosition).

Reconstruction reference data come from detector heads that orbit the patient. The
scorer writes each event's energy-weighted centroid twice: in global coordinates
(X, Y, Z) and in the frame of the scored crystal (U, V, W). These checks assert
that the second frame is the crystal's own, at every head angle and on every
worker thread:

  default_output_unchanged   a deck without the new parameters writes exactly the
                             columns and values it wrote before (golden file)
  static_rotation            a head turned to 0, 90, 180 and 270 degrees: the
                             global-to-local map of every run is rigid, and the
                             photopeak centroid lands on the source's projection
  orbit_matches_static       the same four angles as four sequential time bins of
                             one deck: Run takes 0..3, and each bin matches its
                             static twin in pose, counts and centroid
  dual_head                  two opposed heads with their own DetectorID: equal
                             counts, mirrored U, shared V
  no_overlaps                no run reported an overlap, at any of the angles

Geometry of every check: Symbia LEHR (1.11 mm round holes on a square lattice,
0.16 mm septa, 24.05 mm lead), 9.5 mm NaI with 100 mm half-width, collimator face
150 mm from the gantry axis, an isotropic 140.5 keV point source in vacuum, a 20 %
window (126.45 to 154.55 keV) on unsmeared Edep, and Penelope physics with
fluorescence. The head rotates about global Y.

    TOPAS_BIN=/path/to/topas python3 tests/test_event_frame.py [--golden DIR]

--golden DIR holds deck.txt and run1.phsp captured with a binary built before
this feature (see docs/validation.md); without it the first check is skipped.
About ten minutes on eight threads.
"""

import argparse
import math
import os
import re
import shutil
import subprocess
import sys
import tempfile

import numpy as np

TOPAS = os.environ.get("TOPAS_BIN", shutil.which("topas"))
REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

R_FACE = 150.0       # collimator face to gantry axis [mm]
GAP = 0.25           # collimator back face to crystal front face [mm]
CRYSTAL_HALF = 100.0
MARGIN = 10.0
WINDOW = (126.45, 154.55)
ANGLES = (0.0, 90.0, 180.0, 270.0)
SOURCE = (25.0, 15.0, 40.0)
HISTORIES = 20000000
THREADS = 8

HEADER = """\
i:Ts/NumberOfThreads = {threads}
i:Ts/ShowHistoryCountAtInterval = 0
i:Ts/Seed = {seed}
b:Ts/PauseBeforeQuit = "False"
b:Ge/QuitIfOverlapDetected = "True"
sv:Ph/Default/Modules = 1 "g4em-penelope"
b:Ph/Default/Fluorescence = "True"
d:Ph/Default/CutForGamma = 0.05 mm
d:Ph/Default/CutForElectron = 0.05 mm
d:Ge/World/HLX = 300. mm
d:Ge/World/HLY = 300. mm
d:Ge/World/HLZ = 300. mm
s:Ge/World/Material = "Vacuum"
s:Ge/Gantry/Type = "Group"
s:Ge/Gantry/Parent = "World"
s:Ge/Src/Type = "TsBox"
s:Ge/Src/Parent = "World"
s:Ge/Src/Material = "Vacuum"
d:Ge/Src/HLX = 0.01 mm
d:Ge/Src/HLY = 0.01 mm
d:Ge/Src/HLZ = 0.01 mm
d:Ge/Src/TransX = {sx} mm
d:Ge/Src/TransY = {sy} mm
d:Ge/Src/TransZ = {sz} mm
s:So/Pt/Type = "Isotropic"
s:So/Pt/Component = "Src"
s:So/Pt/IsotropicParticle = "gamma"
d:So/Pt/IsotropicEnergy = 140.5 keV
u:So/Pt/IsotropicEnergySpread = 0.
i:So/Pt/NumberOfHistoriesInRun = {histories}
"""

SCORER = """\
s:Sc/{name}/Quantity = "EDepSpectrum"
s:Sc/{name}/Component = "{component}"
b:Sc/{name}/RecordLocalPosition = "True"
i:Sc/{name}/DetectorID = {det}
s:Sc/{name}/OutputType = "ASCII"
s:Sc/{name}/OutputFile = "{name}"
s:Sc/{name}/IfOutputFileAlreadyExists = "Overwrite"
"""


def preset():
    """Symbia LEHR preset text and its collimator length."""
    text = open(os.path.join(REPO, "systems", "symbia_lehr.txt")).read()
    length = float(re.search(r"Ge/Collim/CollimatorLength\s*=\s*([\d.]+)", text).group(1))
    pitch = sum(float(re.search(r"Ge/Collim/%s\s*=\s*([\d.]+)" % k, text).group(1))
                for k in ("HoleDiameter", "SeptalThickness"))
    hlz = float(re.search(r"Ge/Crystal/HLZ\s*=\s*([\d.]+)", text).group(1))
    return text, length, pitch, hlz


def head(suffix, sign):
    """Parameter lines for one head on the Gantry, at +z (sign=+1) or -z (sign=-1)."""
    text, length, pitch, hlz = preset()
    lines = [l for l in text.splitlines() if l.split("#", 1)[0].strip().startswith(("s:Ge/", "d:Ge/"))]
    if suffix:  # a second head: rename its components, keep the materials defined once
        lines = [l.replace("Ge/Collim/", "Ge/Collim%s/" % suffix)
                  .replace("Ge/Crystal/", "Ge/Crystal%s/" % suffix) for l in lines]
    else:
        lines = [l for l in text.splitlines() if l.split("#", 1)[0].strip()]
    body = re.sub(r'(Ge/(?:Collim|Crystal)\w*/Parent)\s*=\s*"World"', r'\1 = "Gantry"',
                  "\n".join(lines))
    n_holes = math.ceil(2 * (CRYSTAL_HALF + MARGIN) / (0.866 * pitch))
    c, k = "Collim" + suffix, "Crystal" + suffix
    rot = "0." if sign > 0 else "180."
    body += """
i:Ge/{c}/NHolesX = {n}
i:Ge/{c}/NHolesY = {n}
d:Ge/{c}/TransZ = {cz:.4f} mm
d:Ge/{c}/RotY = {rot} deg
d:Ge/{k}/HLX = {h} mm
d:Ge/{k}/HLY = {h} mm
d:Ge/{k}/TransZ = {kz:.4f} mm
d:Ge/{k}/RotY = {rot} deg
""".format(c=c, k=k, n=n_holes, h=CRYSTAL_HALF, rot=rot,
           cz=sign * (R_FACE + length / 2), kz=sign * (R_FACE + length + GAP + hlz))
    assert body.count('Parent = "Gantry"') == 2, "preset parent lines changed; update head()"
    return body


def deck(gantry, heads, seed, source=SOURCE, histories=HISTORIES, threads=THREADS):
    out = HEADER.format(threads=threads, seed=seed, sx=source[0], sy=source[1], sz=source[2],
                        histories=histories)
    out += gantry + "\n"
    for i, (suffix, sign) in enumerate(heads):
        out += head(suffix, sign) + "\n"
        out += SCORER.format(name="Head%d" % i, component="Crystal" + suffix, det=i)
    return out


def run(text):
    """Run one deck in a fresh space-free folder; returns (log, {scorer: rows dict})."""
    work = tempfile.mkdtemp(prefix="evframe_")
    open(os.path.join(work, "deck.txt"), "w").write(text)
    proc = subprocess.run([TOPAS, "deck.txt"], cwd=work, capture_output=True, text=True)
    log = proc.stdout + proc.stderr
    tables = {}
    for name in sorted(os.listdir(work)):
        if name.endswith(".header"):
            stem = name[:-7]
            cols = re.findall(r"^\s*\d+:\s*(\S+)", open(os.path.join(work, name)).read(), re.M)
            data = np.loadtxt(os.path.join(work, stem + ".phsp"), ndmin=2)
            tables[stem] = {c: data[:, i] for i, c in enumerate(cols)}
    shutil.rmtree(work, ignore_errors=True)
    if proc.returncode != 0:
        raise AssertionError("topas exited %d: %s" % (proc.returncode, log[-400:]))
    return log, tables


def rigid_fit(g, l):
    """Least-squares rotation R and translation t with l = R g + t (Kabsch)."""
    gc, lc = g.mean(0), l.mean(0)
    u, _, vt = np.linalg.svd((g - gc).T @ (l - lc))
    d = np.sign(np.linalg.det(vt.T @ u.T))
    rot = vt.T @ np.diag([1.0, 1.0, d]) @ u.T
    t = lc - rot @ gc
    resid = np.abs(l - (g @ rot.T + t)).max()
    return rot, t, resid


def summarize(tab, mask=None):
    """Pose, photopeak counts and photopeak centroid of one (Run, DetectorID) group."""
    sel = np.ones(len(tab["Edep"]), bool) if mask is None else mask
    g = np.c_[tab["X"], tab["Y"], tab["Z"]][sel]
    l = np.c_[tab["U"], tab["V"], tab["W"]][sel]
    rot, t, resid = rigid_fit(g, l)
    # rotation angle about Y of the global-to-local map, in degrees
    angle = math.degrees(math.atan2(rot[0, 2], rot[0, 0]))
    pk = sel & (tab["Edep"] >= WINDOW[0]) & (tab["Edep"] <= WINDOW[1])
    src_local = rot @ np.array(SOURCE) + t
    return dict(rot=rot, t=t, resid=resid, angle=angle, counts=int(pk.sum()),
                u=tab["U"][pk].mean(), v=tab["V"][pk].mean(),
                su=tab["U"][pk].std() / math.sqrt(pk.sum()),
                w_max=np.abs(tab["W"][sel]).max(), src_local=src_local)


def wrap(a):
    return (a + 180.0) % 360.0 - 180.0


LOGS = []
STATIC = {}


def check_default_output_unchanged(golden):
    if not golden:
        return "skipped (no --golden)"
    work = tempfile.mkdtemp(prefix="evframe_golden_")
    shutil.copy(os.path.join(golden, "deck.txt"), work)
    proc = subprocess.run([TOPAS, "deck.txt"], cwd=work, capture_output=True, text=True)
    new = open(os.path.join(work, "energy_response_NaI_140p5keV.phsp"), "rb").read()
    old = open(os.path.join(golden, "run1.phsp"), "rb").read()
    shutil.rmtree(work, ignore_errors=True)
    assert proc.returncode == 0, "golden deck exited %d" % proc.returncode
    assert new == old, "default-mode output differs from the golden file"
    return "%d rows byte-identical" % new.count(b"\n")


def check_static_rotation(_):
    lines = []
    for k, a in enumerate(ANGLES):
        gantry = "d:Ge/Gantry/RotY = %.1f deg" % a
        log, tabs = run(deck(gantry, [("", +1)], seed=101 + k))
        LOGS.append(log)
        tab = tabs["Head0"]
        assert set(np.unique(tab["Run"])) == {0}, "static deck wrote Run %s" % np.unique(tab["Run"])
        s = summarize(tab)
        STATIC[a] = s
        assert s["resid"] < 0.01, "angle %g: global-to-local map not rigid (%.3g mm)" % (a, s["resid"])
        assert s["w_max"] <= 4.75 + 1e-3, "angle %g: |W| %.3f mm outside the crystal" % (a, s["w_max"])
        du, dv = s["u"] - s["src_local"][0], s["v"] - s["src_local"][1]
        assert abs(du) < 0.3 and abs(dv) < 0.3, \
            "angle %g: centroid off the source projection by (%.2f, %.2f) mm" % (a, du, dv)
        lines.append("%g->%.3f deg, n=%d, dU=%.2f dV=%.2f mm (se %.2f)"
                     % (a, s["angle"], s["counts"], du, dv, s["su"]))
    # one rotation sign for all four angles
    errs = [min(abs(wrap(STATIC[a]["angle"] - a)), abs(wrap(STATIC[a]["angle"] + a))) for a in ANGLES]
    assert max(errs) < 0.1, "fitted angles off by %.3f deg" % max(errs)
    sign = +1 if all(abs(wrap(STATIC[a]["angle"] - a)) < 0.1 for a in ANGLES) else -1
    assert all(abs(wrap(STATIC[a]["angle"] - sign * a)) < 0.1 for a in ANGLES), \
        "fitted angles do not share one sign: %s" % [STATIC[a]["angle"] for a in ANGLES]
    return "global-to-local angle = %s RotY; " % ("+" if sign > 0 else "-") + "; ".join(lines)


def check_orbit_matches_static(_):
    gantry = """\
i:Tf/NumberOfSequentialTimes = 4
d:Tf/TimelineStart = 0. s
d:Tf/TimelineEnd = 4. s
s:Tf/Gantry/Function = "Step"
dv:Tf/Gantry/Times = 4 1. 2. 3. 4. s
dv:Tf/Gantry/Values = 4 0. 90. 180. 270. deg
d:Ge/Gantry/RotY = Tf/Gantry/Value deg"""
    log, tabs = run(deck(gantry, [("", +1)], seed=202))
    LOGS.append(log)
    tab = tabs["Head0"]
    runs = sorted(int(r) for r in np.unique(tab["Run"]))
    assert runs == [0, 1, 2, 3], "Run took %s" % runs
    lines = []
    for r, a in zip(runs, ANGLES):
        s = summarize(tab, tab["Run"] == r)
        ref = STATIC[a]
        assert s["resid"] < 0.01, "run %d: map not rigid" % r
        assert abs(wrap(s["angle"] - ref["angle"])) < 0.1, \
            "run %d: angle %.3f against static %.3f" % (r, s["angle"], ref["angle"])
        assert np.abs(s["t"] - ref["t"]).max() < 0.01, "run %d: translation differs" % r
        z = (s["counts"] - ref["counts"]) / math.sqrt(s["counts"] + ref["counts"])
        assert abs(z) < 4, "run %d: counts %d against %d (z=%.1f)" % (r, s["counts"], ref["counts"], z)
        assert abs(s["u"] - ref["u"]) < 0.3 and abs(s["v"] - ref["v"]) < 0.3, \
            "run %d: centroid (%.2f, %.2f) against static (%.2f, %.2f)" % (
                r, s["u"], s["v"], ref["u"], ref["v"])
        lines.append("run %d %.3f deg n=%d" % (r, s["angle"], s["counts"]))
    return "; ".join(lines)


def check_dual_head(_):
    log, tabs = run(deck("d:Ge/Gantry/RotY = 0. deg", [("", +1), ("B", -1)], seed=303))
    LOGS.append(log)
    a, b = summarize(tabs["Head0"]), summarize(tabs["Head1"])
    assert set(np.unique(tabs["Head0"]["DetectorID"])) == {0}
    assert set(np.unique(tabs["Head1"]["DetectorID"])) == {1}
    for name, s in (("A", a), ("B", b)):
        assert s["resid"] < 0.01, "head %s: map not rigid" % name
        du, dv = s["u"] - s["src_local"][0], s["v"] - s["src_local"][1]
        assert abs(du) < 0.3 and abs(dv) < 0.3, "head %s: off projection (%.2f, %.2f)" % (name, du, dv)
    z = (a["counts"] - b["counts"]) / math.sqrt(a["counts"] + b["counts"])
    assert abs(z) < 4, "counts %d against %d (z=%.1f)" % (a["counts"], b["counts"], z)
    assert abs(a["u"] + b["u"]) < 0.4, "U not mirrored: %.2f and %.2f" % (a["u"], b["u"])
    assert abs(a["v"] - b["v"]) < 0.4, "V differs: %.2f and %.2f" % (a["v"], b["v"])
    return "n=%d/%d, U=%.2f/%.2f, V=%.2f/%.2f mm" % (a["counts"], b["counts"], a["u"], b["u"],
                                                    a["v"], b["v"])


def check_no_overlaps(_):
    n = sum(log.count("Overlap is detected") for log in LOGS)
    assert LOGS, "no runs to inspect"
    assert n == 0, "%d overlap reports" % n
    return "%d runs, none reported an overlap" % len(LOGS)


CHECKS = [
    ("default_output_unchanged", check_default_output_unchanged),
    ("static_rotation", check_static_rotation),
    ("orbit_matches_static", check_orbit_matches_static),
    ("dual_head", check_dual_head),
    ("no_overlaps", check_no_overlaps),
]


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--golden", help="folder with deck.txt and run1.phsp from a pre-feature binary")
    args = ap.parse_args()
    if not TOPAS or not os.path.exists(TOPAS):
        print("set TOPAS_BIN to a topas binary with this extension built in")
        return 2
    print("binary: %s\n" % TOPAS)
    failures = 0
    for name, fn in CHECKS:
        try:
            print("  PASS  %-26s %s" % (name, fn(args.golden)), flush=True)
        except AssertionError as exc:
            print("  FAIL  %-26s %s" % (name, exc), flush=True)
            failures += 1
    print("\n%d of %d checks passed" % (len(CHECKS) - failures, len(CHECKS)))
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
