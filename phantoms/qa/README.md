# QA phantoms

Standard nuclear-medicine quality-assurance phantoms, encoded from **manufacturer-datasheet geometry**
(no invented dimensions). They let you assess an OpenTOPAS-SPECT system the same way you would a real
camera: tomographic uniformity, spatial resolution, and contrast recovery.

Each file is an `IncludeFile` fragment (defines `Ge/*` geometry and `So/*` sources, Parent `World`). An
example deck in [`examples/qa/`](../../examples/qa/) supplies the world, physics, a collimator+detector,
and a scorer, and is run from its own folder so the `../../` include resolves.

| Fragment | Phantom | What it tests |
|---|---|---|
| `jaszczak_deluxe.txt` | Jaszczak Deluxe (Data Spectrum) | tomographic uniformity + spatial resolution (cold rods) + contrast (cold spheres) |
| `nema_iec_body.txt` | NEMA IEC body (IEC 61675-1 / NEMA NU 2-2018) | contrast recovery of hot spheres in a warm body with a cold lung |
| `resolution_sources.txt` | NEMA NU-1 line/point sources | system spatial resolution (FWHM) |

## Geometry (all traceable, see `research/knowledge/spect-qa-phantom-geometry/`)

- **Jaszczak Deluxe** (model ECT/DLX/P): PMMA cylinder 216 mm ID x 186 mm, 3.2 mm wall. Six cold-rod
  sectors, rod diameters 4.8/6.4/7.9/9.5/11.1/12.7 mm, hex lattice **pitch = 2x diameter**, 88 mm tall.
  Six cold spheres 9.5-31.8 mm, centers 127 mm from base. Cold PMMA rods/spheres in a uniformly-active
  water background read as voids.
- **NEMA IEC body**: interior 290 (lateral) x 221 (AP) x 193 mm, 3.2 mm PMMA wall (~9.7 L). Six hot
  fillable spheres 10/13/17/22/28/37 mm (centers 70 mm from the lid) at a selectable sphere:background
  concentration ratio (default 4:1). Cold 45 mm foam lung insert (0.3 g/cm3).
- **Resolution sources**: three line sources (capillary bore < 1 mm) at 0 and +/-75 mm (7.5 cm NEMA
  spacing), in air (or add the 20 cm water cylinder for the with-scatter measurement).

## Regenerating / customizing

The fragments are produced by [`tools/make_qa_phantom.py`](../../tools/make_qa_phantom.py):

```bash
python3 tools/make_qa_phantom.py jaszczak --model ultradeluxe    # other Jaszczak models
python3 tools/make_qa_phantom.py nema-iec --ratio 8              # 8:1 sphere:background
python3 tools/make_qa_phantom.py resolution --in-water           # with-scatter variant
python3 tools/make_qa_phantom.py all                             # regenerate the shipped set
```

## Notes
- Default emission is Tc-99m 140.5 keV (the standard QA isotope). Edit `So/*/VolumetricEnergy` for
  another isotope, and swap the collimator preset to match its energy.
- Sphere walls (~1 mm PMMA) are not modeled (a documented assumption; small attenuation impact).
- Small NEMA spheres carry few histories by design (their concentration x volume is small); scale all
  histories up together for statistics, keeping the ratio fixed.
