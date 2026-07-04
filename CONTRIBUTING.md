# Contributing to OpenTOPAS-SPECT

Thanks for your interest in improving OpenTOPAS-SPECT. Bug reports, new camera presets, phantoms, and
documentation fixes are all welcome.

## Reporting bugs and requesting features

Open an issue using the templates. For a bug, always include the **OpenTOPAS and Geant4 versions** you
built against, the deck (or a minimal version of it), the command you ran, and the full error output.
Most issues come down to a version or environment detail, so those fields save a lot of back-and-forth.

## Building

OpenTOPAS-SPECT is a set of OpenTOPAS extensions compiled into an OpenTOPAS build. Point a fresh build
at this directory with `TOPAS_EXTENSIONS_DIR`:

```bash
cmake -S /path/to/OpenTOPAS -B /path/to/build \
      -DTOPAS_EXTENSIONS_DIR="/path/to/topas-spect" \
      -DGeant4_DIR=/path/to/geant4/lib/cmake/Geant4 -DCMAKE_BUILD_TYPE=Release
cmake --build /path/to/build --parallel 8
```

Use a **separate build directory per extension set**, and add `-DTOPAS_USE_QT=ON` if you want the
viewer. See [`docs/installation.md`](docs/installation.md), including the runtime Geant4-data
environment (and the `TOPAS_G4_DATA_DIR` pitfall).

## Running the tests

Before opening a pull request, run:

```bash
# Python tools (no OpenTOPAS needed)
python3 tests/test_ring_motion.py
python3 tests/test_dynamic_acquisition.py

# every shipped example builds with zero overlaps (needs an OpenTOPAS-SPECT build)
TOPAS_BIN=/path/to/build/topas bash tests/smoke_decks.sh
```

All 20+ example decks must build cleanly. If you add an example, it must pass the smoke test, and it
must be **run from its own folder** (`cd examples/<category> && topas deck.txt`) because OpenTOPAS
resolves `IncludeFile` relative to the launch directory; the shipped decks use `../../systems`,
`../../phantoms`.

## Conventions

- **Match the surrounding code.** New C++ extensions follow the source/generator registration comments
  (`// Particle Source for X`) and the existing class style.
- **No invented numbers.** Every camera-geometry or phantom dimension must trace to a cited source
  (see `systems/README.md` and the phantom headers). New presets need provenance and a reliability grade.
- **Moving geometry:** OpenTOPAS only overlap-checks the first time bin, so verify the worst-case state
  (widest footprint) of any swiveling/rotating geometry by hand.
- **Docs:** user-facing changes update the relevant `docs/*.md` chapter; `mkdocs build --strict` must pass.

## Pull requests

Keep changes focused, describe what and why, and note how you verified it (tests run, decks that build).
Update `CHANGELOG.md` under `[Unreleased]`.

## License

By contributing, you agree that your contributions are licensed under the [MIT License](LICENSE).
