<!-- Thanks for contributing to OpenTOPAS-SPECT. -->

## What and why
<!-- What does this change do, and what problem does it solve? -->

## How verified
<!-- e.g. tests/smoke_decks.sh passes; python tests pass; which decks build; mkdocs build --strict clean. -->
- [ ] `tests/smoke_decks.sh` passes (all examples build, zero overlaps)
- [ ] Python tests pass (if `tools/` changed)
- [ ] `mkdocs build --strict` is clean (if docs changed)

## Provenance (for new presets or phantoms)
<!-- Cite the source for every new geometry value. Delete this section if not applicable. -->

## Checklist
- [ ] `CHANGELOG.md` updated under `[Unreleased]`
- [ ] New examples run from their own folder with `../../` includes
- [ ] Any moving geometry checked for overlaps at the worst-case time step
