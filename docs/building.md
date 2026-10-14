# Building OpenTOPAS-SPECT

The extensions use the standard OpenTOPAS extension mechanism: OpenTOPAS scans the
directory (or directories) given in `TOPAS_EXTENSIONS_DIR`, compiles every `.cc`
found (recursively), and generates the factory that maps parameter-file names to
classes. No OpenTOPAS core files are modified.

## Required OpenTOPAS version

Build against OpenTOPAS 4.2.2 with Geant4 11.2.2. On **arm64 (Apple Silicon)** you additionally
need OpenTOPAS commit `404fe33` or newer if you use any radioactive-decay source
(`SpectDecaySource`, or a built-in source with `BeamEnergy = 0`).

Older arm64 builds compute the primary momentum of an at-rest ion as `sqrt(E*E - m*m)`. Clang
contracts that into a fused multiply-add, and for some nuclides the rounding residual comes out
negative, so the momentum evaluates to NaN. Geant4 then falls back to direction (0,0,1), does not
treat the ion as at rest, and transports it to the source volume's boundary before it decays.
Every decay then happens on the volume's `+z` surface instead of throughout its volume, and roughly
half the decay products leave the source without ever traversing it.

Whether a given nuclide is affected depends on the sign of that residual, which depends on the exact
double value of its mass, so it is per-nuclide and per-build. An approximate mass is not enough to
predict it: the sign flips over mass differences well below 1 MeV/c². Measured on one arm64 build,
by rebuilding across the fix and comparing output: **90Y affected; 177Lu and 225Ac not affected.**
x86-64 builds are unaffected.

Self-test for any nuclide, on any build:

```
s:So/Src/Type         = "SpectDecaySource"
s:So/Src/BeamParticle = "GenericIon(39,90)"   # the nuclide you intend to use
s:Sc/PS/Quantity      = "PhaseSpace"
b:Sc/PS/IncludeVertexInfo = "True"
```

Histogram the vertex positions. They must fill the source volume. If they all sit on one face, the
build needs the fix.

## Configure and build

```bash
cmake -S /path/to/OpenTOPAS \
      -B /path/to/OpenTOPAS-build \
      -DTOPAS_EXTENSIONS_DIR="/path/to/topas-spect" \
      -DGeant4_DIR=/path/to/geant4/lib/cmake/Geant4 \
      -DCMAKE_BUILD_TYPE=Release

cmake --build /path/to/OpenTOPAS-build --parallel 8
```

Combine with other extension sets by separating paths with a semicolon:

```bash
-DTOPAS_EXTENSIONS_DIR="/path/to/topas-spect;/path/to/another-extension-set"
```

## Adding to an existing build

If the build tree already exists, re-run cmake in it to pick up new or changed
extension files, then rebuild:

```bash
cd /path/to/OpenTOPAS-build
cmake -DTOPAS_EXTENSIONS_DIR="/path/to/topas-spect" .
make -j8
```

## Naming convention

Each extension `.cc` begins with a single comment line declaring its category and
name, which OpenTOPAS uses to register it:

```
// <Category> for <Name>
```

where `<Category>` is one of `Component`, `Scorer`, `Physics Module`, etc. A class
`TsFoo` registered as `// Scorer for Foo` is then selected in a parameter file with
`s:Sc/<name>/Quantity = "Foo"`.
