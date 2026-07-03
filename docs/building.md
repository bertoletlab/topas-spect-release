# Building OpenTOPAS-SPECT

The extensions use the standard OpenTOPAS extension mechanism: OpenTOPAS scans the
directory (or directories) given in `TOPAS_EXTENSIONS_DIR`, compiles every `.cc`
found (recursively), and generates the factory that maps parameter-file names to
classes. No OpenTOPAS core files are modified.

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
