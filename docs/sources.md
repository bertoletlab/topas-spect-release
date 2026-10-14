# Radionuclide sources

OpenTOPAS already gives you beams, isotropic point sources and volumetric sources. This package adds
two sources for the cases a SPECT study runs into: emitting a **decaying parent and its chain**, and
emitting the **photon spectrum of a pure beta emitter** without transporting the betas.

| Source | Use it when |
|---|---|
| `SpectDecaySource` | the isotope's own decay physics matters: imaging lines, daughter products, activity falling across an acquisition |
| `Y90BremsSource` | you need the photons a 90Y distribution produces and the beta transport itself is not the question |

Everything else, including the uniform-activity organ sources used by the phantom scenarios, is
ordinary OpenTOPAS. See [Patient-like phantoms](phantoms.md).

## SpectDecaySource: a decaying parent and its chain

```
s:So/Src/Type         = "SpectDecaySource"
s:So/Src/Component    = "Organ"
s:So/Src/BeamParticle = "GenericIon(71,177)"    # 177Lu; (89,225) for 225Ac, (39,90) for 90Y
b:So/Src/CorrectByNumberOfHistories = "True"
i:So/Src/NumberOfHistoriesInRun = 20000         # the count at the FIRST time bin
```

The source emits the parent as a generic ion, uniformly within its component, and Geant4's
`g4radioactivedecay` produces the chain from there, so the imaging lines and any daughter lines come
out of the decay physics rather than from a declaration in the deck. Add
`"g4radioactivedecay"` to `Ph/Default/Modules`.

**Activity across a timeline.** With `CorrectByNumberOfHistories = "True"` the source scales the
histories in each `Tf` time bin by the parent's decay, using the Geant4 half-life, so
`NumberOfHistoriesInRun` sets the count at the first bin and later bins fall off on their own. This is
what makes a multi-view acquisition over hours of real decay a single deck. Daughters stay at the
parent's position, which is right for a static source; a study of biological redistribution needs
separate source regions per species.

!!! warning "arm64 builds need OpenTOPAS 404fe33 or newer"
    Older arm64 builds evaluate an at-rest ion's momentum in a way that can return NaN, after which
    the decay direction silently degrades. See [Building details](building.md).

A worked deck is
[`examples/source/decay_source.txt`](https://github.com/bertoletlab/topas-spect-release/blob/main/examples/source/decay_source.txt),
which also carries a 2 mm aluminium entrance window in front of the crystal: a bare crystal in vacuum
scores the charged particles that escape the source volume, which is not what a camera sees.

## Y90BremsSource: 90Y photons without beta transport

90Y has no gamma line. What a camera sees is bremsstrahlung, produced along the beta tracks in the
tissue around each decay, so a full simulation spends nearly all of its time transporting betas that
never reach the detector. `Y90BremsSource` samples the photon spectrum directly instead.

```
s:So/Src/Type         = "Y90BremsSource"
s:So/Src/Component    = "Liver"
s:So/Src/BeamParticle = "gamma"      # required
s:So/Src/Medium       = "Tissue"     # Water | Tissue | Bone | Lung
b:So/Src/IncludeInternalBremsstrahlung = "True"
i:So/Src/NumberOfHistoriesInRun = 100000
```

**One photon per history.** The source emits a single photon per history, sampled from the
tabulated spectrum of the chosen medium, isotropically, uniformly within the component. Per-history
results are therefore per emitted photon, not per decay: **multiply by the startup-printed yield** to
get per-decay results. The source prints the external-bremsstrahlung yield for the medium and the
internal-bremsstrahlung yield it is adding, for that purpose.

**Where the tables come from.** `source/TsY90BremsTables.hh` is generated, not hand-written: a real
90Y decay simulated with `g4radioactivedecay` and PENELOPE inside a sphere of each medium large enough
to contain the beta range, tallying every photon at its creation energy. The tables are therefore
**un-attenuated at the source**: attenuation on the way out is left to the transport in your own
geometry, which is where it belongs. Binning is 150 log-spaced bins from 1 to 2300 keV.

Internal bremsstrahlung, the photons from the beta decay itself rather than from slowing down in
matter, is medium-independent and ships as its own table. `IncludeInternalBremsstrahlung` defaults to
true; turning it off is a diagnostic, not a physical choice.

!!! note "Medium is the emitting medium, not the attenuator"
    Pick `Medium` to match the tissue holding the activity, since the bremsstrahlung yield scales with
    the atomic number of the material the betas slow down in: bone produces noticeably more photons per
    decay than soft tissue. It does not describe what the photons cross on their way out.

### What this source is and is not

It reproduces the photon field a 90Y distribution emits. It does not reproduce beta dosimetry, and it
cannot: the betas are never created. For absorbed dose from 90Y, transport the betas, for example with
the monoenergetic 0.9337 MeV electron source the `y90_sirt.txt` scenario uses, or a full decay with
`SpectDecaySource`. Use `Y90BremsSource` for the imaging half of the problem.

The one quirk worth knowing: Geant4 11.2 and earlier emit a spurious 1760.71 keV gamma for the
Zr-90 0+ to 0+ transition, whose internal conversion coefficient is zero in PhotonEvaporation5.7. The
table generator removed it and substituted the physical product of that level, two 511 keV
internal-pair-conversion photons. A full `g4radioactivedecay` 90Y simulation on an affected Geant4
still shows the spurious line; this source does not.
