#!/usr/bin/env python3
"""Software tests for tools/make_dynamic_acquisition.py (no OpenTOPAS build needed).

Checks that each backend emits well-formed decks that couple motion and activity correctly:
  - native   : one deck per view; per-view gantry angle and exp() activity decay in the histories.
  - --decay  : one deck; every organ becomes a SpectDecaySource (parent ion; chain via g4radioactivedecay).
  - --vr     : per-view forced detection (phantom rotated to the view angle, FD scorer, decay applied).
"""
import math, os, re, subprocess, sys, tempfile

ROOT = os.path.join(os.path.dirname(__file__), "..")
GEN = os.path.join(ROOT, "tools", "make_dynamic_acquisition.py")
SCEN = os.path.join(ROOT, "phantoms", "scenarios", "lu177_psma.txt")

def run(args, out):
    subprocess.run([sys.executable, GEN, SCEN, *args, "--out", out], check=True,
                   capture_output=True, cwd=tempfile.gettempdir())

def test_native_motion_and_decay():
    d = os.path.join(tempfile.mkdtemp(), "acq")
    run(["--half-life-h", "2", "--projections", "8", "--time-per-projection", "450", "--heads", "1", "--angular-range", "360"], d)
    decks = sorted(f for f in os.listdir(d) if f.startswith("view"))
    assert len(decks) == 8
    # gantry angle increases per view; kidney histories decay across views
    g0 = float(re.search(r"Head/RotZ = ([\d.]+)", open(f"{d}/view000.txt").read()).group(1))
    g3 = float(re.search(r"Head/RotZ = ([\d.]+)", open(f"{d}/view003.txt").read()).group(1))
    assert abs(g0) < 1e-6 and abs(g3 - 135.0) < 1e-6, "gantry angle wrong"
    def kid(v):
        return int(re.search(r"KidneyLSrc/NumberOfHistoriesInRun = (\d+)", open(f"{d}/{v}").read()).group(1))
    assert kid("view000.txt") == 50000
    # 2 h half-life, view 7 at t=52.5 min -> exp(-ln2*52.5/120)=0.738
    assert abs(kid("view007.txt") / 50000 - math.exp(-math.log(2) * 52.5 / 120)) < 0.02, "decay wrong"

def test_tsrts_backend():
    d = os.path.join(tempfile.mkdtemp(), "acq")
    run(["--decay", "--isotope", "71 177", "--projections", "30", "--time-per-projection", "40", "--angular-range", "360"], d)
    txt = open(f"{d}/acquisition.txt").read()
    nsrc = len(re.findall(r's:So/\w+/Component', open(SCEN).read()))
    assert txt.count('Type = "SpectDecaySource"') == nsrc, "one decay source per organ"
    assert 'GenericIon(71,177)' in txt and 'g4radioactivedecay' in txt
    assert "Tf/Gantry/Value" in txt and "NumberOfSequentialTimes = 30" in txt   # motion on the same timeline
    assert "Volumetric" not in re.sub(r"#.*", "", txt), "native volumetric sources must be replaced"

def test_vr_forced_detection():
    d = os.path.join(tempfile.mkdtemp(), "acq")
    run(["--vr", "--half-life-h", "159.5", "--projections", "6", "--time-per-projection", "60", "--angular-range", "360"], d)
    txt = open(f"{d}/view003.txt").read()
    assert 'ForcedDetectionProjection' in txt and "Torso/RotZ = 180" in txt      # FD + phantom rotated to view
    assert "FDSurf/RotX = 90" in txt

if __name__ == "__main__":
    test_native_motion_and_decay(); test_tsrts_backend(); test_vr_forced_detection()
    print("PASS: native motion+decay, tsRTS backend, VR forced-detection decks all well-formed")
