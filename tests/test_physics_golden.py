#!/usr/bin/env python3
"""Golden-value gate for the forced-detection scorer.

The existing gate proves that decks construct and run. Every defect found during
the PMB-122210 revision passed it: a transmission formula that was six times
low, an open-area fraction computed on the wrong lattice, a tungsten attenuation
coefficient applied to lead septa, and a transmission floor that discarded most
of the penetration tail. None of them stopped a deck from running, and none of
them changed an exit code.

These tests assert VALUES. Each one names the row in the manuscript campaign's
RESULTS_LEDGER.md that it protects, so a failure says which published number it
would have broken:

    research/manuscripts/2026-07-03_opentopas-spect-companion/RESULTS_LEDGER.md

Run with a TOPAS binary that has this extension built in:

    TOPAS_BIN=/path/to/topas python3 tests/test_physics_golden.py

The run is a few minutes. It is meant for a release gate, not for every commit.
"""

import os
import re
import shutil
import subprocess
import sys
import tempfile

TOPAS = os.environ.get("TOPAS_BIN", shutil.which("topas"))

# Symbia LEHR: round holes on a square lattice, lead septa at 140.5 keV.
SYMBIA_LEHR = dict(d=1.11, s=0.16, L=24.05, mu=2.6767, shape="Round", lattice="Square")

DECK = """\
i:Ts/NumberOfThreads = 0
i:Ts/ShowHistoryCountAtInterval = 100000000
sv:Ph/Default/Modules = 1 "g4em-penelope"
d:Ge/World/HLX = 300. mm
d:Ge/World/HLY = 300. mm
d:Ge/World/HLZ = 300. mm
s:Ge/World/Material = "Vacuum"
s:Ge/FDSurface/Type = "TsBox"
s:Ge/FDSurface/Parent = "World"
s:Ge/FDSurface/Material = "Vacuum"
d:Ge/FDSurface/HLX = 150. mm
d:Ge/FDSurface/HLY = 150. mm
d:Ge/FDSurface/HLZ = 0.05 mm
d:Ge/FDSurface/TransZ = 0. mm
s:Ge/Src/Type = "TsBox"
s:Ge/Src/Parent = "World"
s:Ge/Src/Material = "Vacuum"
d:Ge/Src/HLX = 0.01 mm
d:Ge/Src/HLY = 0.01 mm
d:Ge/Src/HLZ = 0.01 mm
d:Ge/Src/TransZ = -112.025 mm
s:So/Beam/Type = "Isotropic"
s:So/Beam/Component = "Src"
s:So/Beam/BeamParticle = "gamma"
d:So/Beam/BeamEnergy = 140.5 keV
i:So/Beam/NumberOfHistoriesInRun = {histories}
s:Sc/FD/Quantity = "ForcedDetectionProjection"
s:Sc/FD/Component = "FDSurface"
d:Sc/FD/HoleDiameter = {d} mm
d:Sc/FD/SeptalThickness = {s} mm
d:Sc/FD/CollimatorLength = {L} mm
u:Sc/FD/HoleFraction = {f}
s:Sc/FD/HoleShape = "{shape}"
s:Sc/FD/Lattice = "{lattice}"
b:Sc/FD/IncludeSeptalPenetration = "{pen}"
{mu_line}
d:Sc/FD/CZTThickness = 9.5 mm
b:Sc/FD/IncludeCrystalEfficiency = "True"
dv:Sc/FD/CrystalEfficiencyEnergies = 14 113.0 140.5 159.0 171.0 200.0 208.0 218.0 245.0 250.0 300.0 364.5 440.0 500.0 1000.0 keV
uv:Sc/FD/CrystalEfficiencyValues   = 14 0.944 0.862 0.7742 0.7137 0.5761 0.545 0.505 0.4147 0.400 0.2926 0.2065 0.149 0.1194 0.0417
dv:Sc/FD/ResolutionEnergies        = 6 100.0 140.5 208.0 300.0 440.0 1000.0 keV
uv:Sc/FD/ResolutionFractionalFWHM  = 6 0.0653 0.0550 0.0460 0.0383 0.0317 0.0210
d:Sc/FD/PixelSizeX = 4.0 mm
d:Sc/FD/PixelSizeY = 4.0 mm
i:Sc/FD/NPixelsX = 45
i:Sc/FD/NPixelsY = 45
d:Sc/FD/EnergyWindowLow  = 126.45 keV
d:Sc/FD/EnergyWindowHigh = 154.55 keV
s:Sc/FD/OutputType = "ASCII"
s:Sc/FD/OutputFile = "{out}"
s:Sc/FD/IfOutputFileAlreadyExists = "Overwrite"
b:Ts/PauseBeforeQuit = "False"
"""


def closed_form_fraction(shape, lattice, d, s):
    """Open-area fraction of the collimator face.

    Hexagonal holes tile a hexagonal lattice exactly, so the fraction is (d/p)^2
    with no packing factor; round holes leave the interstices, 0.9069 of a
    hexagonal cell and pi/4 of a square one.
    """
    p = d + s
    if shape == "Hex" and lattice == "Hex":
        return (d / p) ** 2
    if shape == "Round" and lattice == "Hex":
        return 0.9069 * (d / p) ** 2
    if shape == "Round" and lattice == "Square":
        return 0.7853981634 * (d / p) ** 2
    raise ValueError("no closed form for %s on %s" % (shape, lattice))


def run(histories=1000, fraction=None, pen="True", set_mu=True, geom=None, floor=None):
    """One short run; returns (exit code, log text, counts per decay or None)."""
    g = dict(geom or SYMBIA_LEHR)
    f = closed_form_fraction(g["shape"], g["lattice"], g["d"], g["s"]) \
        if fraction is None else fraction
    work = tempfile.mkdtemp(prefix="fdgold_")
    out = os.path.join(work, "out")
    deck = DECK.format(histories=histories, d=g["d"], s=g["s"], L=g["L"], f="%.6f" % f,
                       shape=g["shape"], lattice=g["lattice"], pen=pen, out=out,
                       mu_line=("u:Sc/FD/SeptalMuPerMM = %s" % g["mu"]) if set_mu else "")
    if floor is not None:
        deck += 'u:Sc/FD/MinTransmissionFraction = %s\n' % floor
    path = os.path.join(work, "deck.txt")
    open(path, "w").write(deck)
    proc = subprocess.run([TOPAS, path], capture_output=True, text=True)
    log = proc.stdout + proc.stderr
    value = None
    header = re.search(r"Original Histories:\s*(\d+)",
                       open(out + ".header").read()) if os.path.exists(out + ".header") else None
    # A run that aborts can still leave a header behind. Returning None rather than raising keeps
    # the failure legible: the caller gets the log that says why.
    if os.path.exists(out + ".phsp") and header:
        n = int(header.group(1))
        total = 0.0
        for line in open(out + ".phsp"):
            parts = line.split()
            if len(parts) >= 5:
                total += float(parts[4])
        value = total / n
    shutil.rmtree(work, ignore_errors=True)
    return proc.returncode, log, value


# --------------------------------------------------------------------------
# the checks
# --------------------------------------------------------------------------

def check_transmission_at_normal_incidence():
    """T(0) must equal the open-area fraction. Protects `fd-vs-geometric-reference`.

    A photon arriving along the bore sees exactly the open fraction of the face,
    with no angular term left over. The transmission bug fixed in 2c8c7f6 broke
    this identity and made the scorer about six times low, and nothing in the
    suite noticed.
    """
    rc, log, _ = run(histories=100)
    assert rc == 0, "run failed"
    m = re.search(r"T\(0\)=([0-9.eE+-]+)", log)
    assert m, "the scorer did not report T(0); the construction banner changed"
    t0 = float(m.group(1))
    f = closed_form_fraction(**{k: SYMBIA_LEHR[k] for k in ("shape", "lattice", "d", "s")})
    assert abs(t0 - f) / f < 1e-4, "T(0)=%.6f against an open fraction of %.6f" % (t0, f)
    return "T(0) = %.6f = open fraction" % t0


def check_open_fraction_closed_forms():
    """Each hole shape and lattice must trace back to its closed form.

    The scorer traces the aperture at construction and aborts when the traced
    fraction disagrees with the declared one by more than 2 percent, so
    declaring the closed form and surviving IS the test. Round holes were once
    evaluated on a hexagonal lattice with a nearest-row mapping error, which
    this catches in both directions.
    """
    out = []
    for shape, lattice in (("Round", "Square"), ("Round", "Hex"), ("Hex", "Hex")):
        g = dict(SYMBIA_LEHR, shape=shape, lattice=lattice)
        rc, log, _ = run(histories=100, geom=g)
        assert rc == 0, "%s on %s aborted: %s" % (
            shape, lattice, "\n".join(l for l in log.splitlines() if "MISMATCH" in l))
        out.append("%s/%s ok" % (shape, lattice))
    return ", ".join(out)


def check_geometry_guard_fires():
    """A wrong declared fraction must stop the run, not scale the answer quietly."""
    rc, log, _ = run(histories=100, fraction=0.75)
    assert rc != 0 or "GEOMETRY MISMATCH" in log, \
        "a 25 percent wrong HoleFraction ran to completion"
    return "guard fires on a 25 percent error"


def check_septal_attenuation_is_announced():
    """Which attenuation the septa get must be stated, whichever way it was chosen.

    Leaving SeptalMuPerMM unset is now the better option: the scorer applies a tabulated
    coefficient at each photon's own energy. Pinning one is still allowed and is right for a
    monoenergetic deck, but it is wrong by 82 percent if that deck then scores a second line, so
    neither choice may be silent.
    """
    _, pinned, _ = run(histories=100)
    assert "pinned at" in pinned, "a pinned coefficient was not announced"
    _, tabulated, _ = run(histories=100, set_mu=False)
    assert "tabulated for" in tabulated, "a tabulated coefficient was not announced"
    assert "tungsten default" not in tabulated, \
        "the scorer still claims a tungsten default, which no longer exists"
    return "both choices announced"


def check_transmission_floor_keeps_the_tail():
    """The shipped floor must not truncate the integral. Protects `ship-floor-truncation`.

    MinTransmissionFraction bounds the output file and costs no run time, but it
    discards weight. At the old default of 1e-4 it removed 0.66 percent of this
    geometry's response and most of its penetration tail.
    """
    rc, log, _ = run(histories=100000)
    assert rc == 0, "run failed"
    m = re.search(r"MinTransmissionFraction=([0-9.eE+-]+) discarded about ([0-9.eE+-]+) percent", log)
    assert m, "the scorer did not report what the floor discarded"
    floor, lost = float(m.group(1)), float(m.group(2))
    assert floor <= 1e-6, "the default floor has risen to %g" % floor
    assert lost < 0.05, "the floor discarded %.3f percent of the signal" % lost
    return "floor %g discards %.4f percent" % (floor, lost)


def check_sensitivity_against_transport():
    """Forced detection against the transported reference. Protects `ship-fig9-hi`.

    Full transport gives 7.5435e-05 counts per decay in this geometry at 2e9
    histories; forced detection gives 7.7353e-05, a ratio of 1.025. The arm here
    is short, so the tolerance covers its own counting error: the weights are
    heavy-tailed and 2e7 histories carry about 1.2 percent, which is why the
    gate runs that many rather than the 2e6 of the figure.
    """
    transport = 7.5435e-05
    rc, log, value = run(histories=20000000)
    assert rc == 0 and value is not None, "run failed"
    ratio = value / transport
    assert abs(ratio - 1.025) < 0.05, \
        "forced detection / transport = %.4f, outside 1.025 +- 0.05" % ratio
    return "%.4e, ratio to transport %.3f" % (value, ratio)


def check_penetration_changes_the_answer():
    """Penetration off must differ from penetration on, by the measured amount.

    Guards against a deck or default silently disabling the model: the geometric
    response alone is 6.93e-05 against 7.74e-05 with septa, 10 percent apart.
    """
    _, _, on = run(histories=5000000)
    _, _, off = run(histories=5000000, pen="False")
    assert on and off, "runs failed"
    gain = on / off
    assert 1.05 < gain < 1.20, "penetration changed the response by %.3f" % gain
    return "penetration adds %.1f percent" % (100 * (gain - 1))


def check_shipped_decks_declare_the_right_attenuation():
    """Every shipped deck's septal coefficient must match the material it models.

    The scorer cannot catch this: any coefficient produces a plausible answer.
    One of our own decks declared 2.2300/mm for lead at 140.5 keV against a
    tabulated 2.692, and another declared nothing and ran lead on the tungsten
    default. Both inflated or deflated penetration by tens of percent
    and neither changed an exit code.
    """
    import subprocess
    root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    audit = os.path.join(root, "tools", "audit_decks.py")
    proc = subprocess.run([sys.executable, audit,
                           os.path.join(root, "examples"),
                           os.path.join(root, "systems")],
                          capture_output=True, text=True)
    assert proc.returncode == 0, "\n" + "\n".join(
        l for l in proc.stdout.splitlines() if l.strip().startswith("FAIL"))
    return proc.stdout.strip().splitlines()[-1]


CHECKS = [
    ("transmission at normal incidence", check_transmission_at_normal_incidence),
    ("open-area closed forms", check_open_fraction_closed_forms),
    ("geometry guard", check_geometry_guard_fires),
    ("septal attenuation announced", check_septal_attenuation_is_announced),
    ("transmission floor", check_transmission_floor_keeps_the_tail),
    ("penetration is active", check_penetration_changes_the_answer),
    ("sensitivity against transport", check_sensitivity_against_transport),
    ("shipped decks declare lead", check_shipped_decks_declare_the_right_attenuation),
]


def scorer_is_registered():
    """Refuse to interpret anything from a binary that lacks the extension.

    A plain `topas` on PATH is very often a stock build with no extensions, and it fails every
    deck here with "Sc/FD/Quantity has unknown value" -- which reads like a broken build or a bad
    deck and is neither. Check once, loudly, before any value is asserted.
    """
    rc, log, _ = run(histories=10)
    return "unknown value" not in log


def main():
    if not TOPAS or not os.path.exists(TOPAS):
        print("set TOPAS_BIN to a topas binary with this extension built in")
        return 2
    print("binary: %s\n" % TOPAS)
    if not scorer_is_registered():
        print("  ABORT  that binary has no ForcedDetectionProjection scorer.")
        print("         Point TOPAS_BIN at a build that includes this extension;")
        print("         a stock topas on PATH will not do.")
        return 2
    failures = 0
    for name, fn in CHECKS:
        try:
            detail = fn()
            print("  PASS  %-32s %s" % (name, detail))
        except AssertionError as exc:
            print("  FAIL  %-32s %s" % (name, exc))
            failures += 1
    print("\n%d of %d checks passed" % (len(CHECKS) - failures, len(CHECKS)))
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
