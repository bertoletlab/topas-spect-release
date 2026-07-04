# Installation

OpenTOPAS-SPECT is a set of OpenTOPAS *extensions*: self-contained C++ classes that OpenTOPAS compiles into
its own binary. You install it by pointing an OpenTOPAS build at the source directory. There is nothing
to install separately, and the OpenTOPAS core is never modified.

## Before you start

Make sure you have:

- **OpenTOPAS** 4.2.p2, the version this package is built and tested against; see the
  [OpenTOPAS install guide](https://opentopas.readthedocs.io/).
- **Geant4** 11.2.2 (geant4-11-02-patch-02) with its data files, built as OpenTOPAS requires.
- **CMake** ≥ 3.16 and a C++17 compiler.
- For the Python helper scripts only: Python 3.9+ with the packages in `requirements.txt`
  (`pip install -r requirements.txt`). The simulation itself needs none of these.

## Get the source

```bash
git clone https://github.com/bertoletlab/topas-spect-release.git
```

## Build

Point a fresh OpenTOPAS build at the extension directory with `TOPAS_EXTENSIONS_DIR`. OpenTOPAS compiles
every `.cc` it finds there and registers the new components and scorers automatically.

```bash
cmake -S /path/to/OpenTOPAS \
      -B /path/to/OpenTOPAS-build \
      -DTOPAS_EXTENSIONS_DIR="/path/to/topas-spect" \
      -DGeant4_DIR=/path/to/geant4/lib/cmake/Geant4 \
      -DCMAKE_BUILD_TYPE=Release

cmake --build /path/to/OpenTOPAS-build --parallel 8
```

The build produces a `topas` binary in the build directory that includes all the SPECT components.

!!! tip "Keep separate build directories"
    Use a **fresh build directory per extension set**. Reusing a directory that was configured for a
    different `TOPAS_EXTENSIONS_DIR` can carry stale paths and cause confusing link errors. If you
    only changed or added extension files, re-run `cmake .` in the existing build directory to pick
    them up, then rebuild; see [Building details](building.md).

### Combining with other extensions

`TOPAS_EXTENSIONS_DIR` is a semicolon-separated list, so you can build OpenTOPAS-SPECT together with
other extension sets in one binary:

```bash
-DTOPAS_EXTENSIONS_DIR="/path/to/topas-spect;/path/to/another-extension"
```

Native isotope decay for [dynamic acquisitions](dynamic_acquisition.md) needs no extra extension: the
`SpectDecaySource` decay source is bundled with OpenTOPAS-SPECT. If any extension set you combine uses the
Qt viewer, add `-DTOPAS_USE_QT=ON -DQt5_DIR=<qt5>/lib/cmake/Qt5` to the **same** configure step, or the
link step will fail.

## Runtime environment

Before running (or opening the Qt viewer), OpenTOPAS needs the Geant4 **data-file** environment variables
set, so it can find the `G4EMLOW`, `PhotonEvaporation`, `RadioactiveDecay`, and other data directories for
your Geant4 version. The usual way is to source your Geant4 environment script once per shell:

```bash
source /path/to/geant4-install/bin/geant4.sh
```

!!! warning "The `TOPAS_G4_DATA_DIR` pitfall"
    If OpenTOPAS aborts at startup with *"TOPAS does not know the set of data files needed for this Geant4
    release"*, `TOPAS_G4_DATA_DIR` is set (often from your shell profile) to a data directory whose
    versions do not match your Geant4 build. **Unset it** and rely on the individual `G4*DATA` variables,
    or point it at the data directory that matches your Geant4 version:

    ```bash
    unset TOPAS_G4_DATA_DIR          # then re-run; OpenTOPAS uses the individual G4*DATA variables
    ```

    A quick check: `echo "$TOPAS_G4_DATA_DIR"` should be empty, and `which topas` should be the build you
    intend to run. This is an OpenTOPAS/Geant4 setup issue, not a deck issue.

## Verify the install

Run a shipped example from its own folder. OpenTOPAS resolves `IncludeFile` paths relative to the
directory you launch from, and each example's includes point to `../../systems`, `../../phantoms`, so
run from the example's folder:

```bash
cd topas-spect/examples/system
/path/to/OpenTOPAS-build/topas symbia_lehr_sensitivity.txt
```

You should see OpenTOPAS build the geometry with no overlap warnings and report a sensitivity in
counts per decay. To check every shipped example at once, run the smoke test:

```bash
TOPAS_BIN=/path/to/OpenTOPAS-build/topas bash tests/smoke_decks.sh
```

It runs each example at low history counts and confirms that all build cleanly and exit successfully.

!!! warning "Paths with spaces"
    OpenTOPAS's `IncludeFile` cannot handle spaces in directory paths. If your checkout lives under a
    path containing spaces, copy it to a space-free location before running.
