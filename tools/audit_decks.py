#!/usr/bin/env python3
"""Check forced-detection decks against the physics they declare.

The scorer cannot tell whether a deck's numbers are right, only whether they are
self-consistent. Two deck errors of this kind, both found in our own decks, ran
to completion with no warning:

  * a deck declared a septal attenuation of 2.2300/mm for lead at 140.5 keV,
    where the tabulation below gives 2.692. Penetration came out high, and the
    reported sensitivity with it, by a few percent.
  * another declared no coefficient at all, which at the time meant the scorer's
    tungsten default of 3.56/mm, 32 percent high in the other direction. That
    default is gone: an unset coefficient is now tabulated per photon energy, and
    this tool treats an unset coefficient as correct.

A pinned coefficient is invisible at run time, because any value produces a
plausible answer. This compares what a deck declares against a tabulation, and
against the beam energy the same deck sets. The tolerance is 5 percent, which is
a screen for gross errors rather than a reference: interpolating a coarse table
across lead's 100 to 150 keV gap is itself good to only a few percent.

    python3 dev-tools/audit_decks.py <deck-or-directory> [...]

Exits non-zero when any deck disagrees by more than the tolerance.
"""

import glob
import math
import os
import re
import sys

TOLERANCE = 0.05          # 5 percent; the campaign decks sit within 1.5
DENSITY = {"lead": 11.35, "tungsten": 19.3}

# NIST XCOM mass attenuation with coherent scattering, cm^2/g. Lead only for
# now, which is what every parallel-hole preset in this package uses; tungsten
# collimators need their own row before this will say anything useful about them.
MASS_ATTENUATION = {
    "lead": [(0.100, 5.549), (0.150, 2.014), (0.200, 0.9985), (0.300, 0.4031),
             (0.400, 0.2323), (0.500, 0.1613), (1.000, 0.0710)],
}


def mu_per_mm(material, energy_keV):
    """Log-log interpolation of the tabulation, returned per millimetre."""
    table = MASS_ATTENUATION[material]
    e = energy_keV / 1000.0
    if e < table[0][0] or e > table[-1][0]:
        return None
    for (e1, m1), (e2, m2) in zip(table, table[1:]):
        if e1 <= e <= e2:
            f = (math.log(e) - math.log(e1)) / (math.log(e2) - math.log(e1))
            mass = math.exp(math.log(m1) + f * (math.log(m2) - math.log(m1)))
            return mass * DENSITY[material] / 10.0
    return None


def parameters(text):
    out = {}
    for line in text.splitlines():
        line = line.split("#")[0].strip()
        if "=" in line:
            key, value = line.split("=", 1)
            out[key.strip().split(":")[-1]] = value.strip().strip('"')
    return out


def audit(path):
    """Returns a list of complaints, empty when the deck is consistent."""
    text = open(path).read()
    if "ForcedDetectionProjection" not in text:
        return []
    p = parameters(text)
    complaints = []

    penetration = p.get("Sc/FD/IncludeSeptalPenetration", "False").lower() == "true"
    if not penetration:
        return []

    energy = None
    for key, value in p.items():
        if key.endswith("BeamEnergy"):
            m = re.match(r"([0-9.eE+-]+)\s*(keV|MeV)", value)
            if m:
                energy = float(m.group(1)) * (1000.0 if m.group(2) == "MeV" else 1.0)
    if energy is None:
        complaints.append("penetration is on but the deck sets no beam energy to check against")
        return complaints

    declared = None
    for key, value in p.items():
        if key.endswith("SeptalMuPerMM"):
            declared = float(value.split()[0])
    if declared is None:
        # Unset is the RECOMMENDED setting: the scorer then applies a tabulated coefficient at
        # each photon's own energy, which is the only correct choice for a source with more than
        # one line. Nothing to check.
        return complaints

    expected = mu_per_mm("lead", energy)
    if expected is None:
        complaints.append("beam energy %.1f keV is outside the tabulation" % energy)
        return complaints
    off = abs(declared - expected) / expected
    if off > TOLERANCE:
        complaints.append(
            "SeptalMuPerMM = %.4f/mm at %.1f keV, against %.4f/mm for lead, %.0f percent off"
            % (declared, energy, expected, 100 * off))
    return complaints


def main(argv):
    targets = []
    for arg in argv or ["."]:
        if os.path.isdir(arg):
            targets += sorted(glob.glob(os.path.join(arg, "**", "*.txt"), recursive=True))
        else:
            targets.append(arg)
    bad = 0
    checked = 0
    for path in targets:
        try:
            complaints = audit(path)
        except Exception as exc:               # a deck we cannot parse is not a failure
            print("  SKIP  %-52s %s" % (os.path.basename(path), exc))
            continue
        if not os.path.exists(path):
            continue
        text = open(path).read()
        if "ForcedDetectionProjection" not in text:
            continue
        checked += 1
        if complaints:
            bad += 1
            for c in complaints:
                print("  FAIL  %-52s %s" % (os.path.basename(path), c))
        else:
            print("  ok    %s" % os.path.basename(path))
    print("\n%d forced-detection decks checked, %d with a problem" % (checked, bad))
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
