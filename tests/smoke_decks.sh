#!/bin/bash
# Smoke test: every shipped example deck builds with zero overlaps and exits 0 at low histories.
# Catches deck-level regressions (param typos, geometry overlaps, renamed components).
# Each example is run FROM ITS OWN FOLDER, so its `../../systems`, `../../phantoms` includes resolve.
# Needs an OpenTOPAS build with topas-spect. Usage: TOPAS_BIN=/path/to/topas bash tests/smoke_decks.sh
set -u
: "${TOPAS_BIN:?set TOPAS_BIN to a topas-spect build}"
REPO="$(cd "$(dirname "$0")/.." && pwd)"
WORK="$(mktemp -d)/topas-spect"        # space-free copy (OpenTOPAS IncludeFile can't handle spaces)
cp -R "$REPO" "$WORK"; cd "$WORK"
pass=0; fail=0
for deck in examples/*/*.txt; do
  [ -f "$deck" ] || continue
  dir="$(dirname "$deck")"
  # low histories, no interactive pause / Qt; write the probe next to the deck so relative includes resolve
  sed -e 's/NumberOfHistoriesInRun = [0-9]*/NumberOfHistoriesInRun = 2000/g' \
      -e 's/UseQt = "True"/UseQt = "False"/' \
      -e 's/PauseBeforeQuit = "True"/PauseBeforeQuit = "False"/' \
      -e '/Gr\/View/d' \
      -e 's/NumberOfSequentialTimes = [0-9]*/NumberOfSequentialTimes = 3/' "$deck" > "$dir/_smoke.txt"
  ( cd "$dir" && "$TOPAS_BIN" _smoke.txt ) > "$dir/_smoke.log" 2>&1
  ex=$?; ov=$(grep -c "Overlap is detected" "$dir/_smoke.log")
  if [ "$ex" -eq 0 ] && [ "$ov" -eq 0 ]; then echo "  PASS  $deck"; pass=$((pass+1))
  else echo "  FAIL  $deck (exit $ex, overlaps $ov)"; grep -iE "serious error|Parameter name|unable to open" "$dir/_smoke.log" | grep -vi tolerance | head -1 | sed 's/^/         /'; fail=$((fail+1)); fi
  rm -f "$dir/_smoke.txt" "$dir/_smoke.log"
done
echo "smoke: $pass passed, $fail failed"
exit $((fail>0))
