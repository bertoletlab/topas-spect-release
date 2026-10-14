// Particle Generator for Y90BremsSource.
//
// Y-90 is a pure beta emitter: its SPECT signal is the bremsstrahlung produced as the beta slows
// down in tissue (plus 511 keV annihilation photons from internal pair conversion of the Zr-90
// 1.76 MeV level, 6.37e-5/decay). That photon spectrum depends
// on the medium the decay happens in (radiative yield grows with Z, and low-energy photons are
// self-absorbed), so it cannot be written as a nuclear-data line table the way a gamma emitter's
// can. This generator instead samples a PRE-COMPUTED emission spectrum, tabulated per medium by
// a full Geant4 calibration (real Y-90 decay, PENELOPE EM physics, every photon tallied at its
// creation energy, i.e. un-attenuated -- attenuation is left to the geometry you place the
// source in). It emits ONE isotropic photon per history, drawn from the table, from a position
// sampled uniformly inside the source component (use a tiny component for a point source).
//
// Parameters:
//   s:So/X/Type         = "Y90BremsSource"
//   s:So/X/Component    = "<component the photons are emitted from>"
//   s:So/X/BeamParticle = "gamma"                 (required by the TOPAS base class; must be gamma)
//   s:So/X/Medium       = "Water" | "Tissue" | "Bone" | "Lung"
//       Water  = G4_WATER, Tissue = G4_TISSUE_SOFT_ICRP, Bone = G4_BONE_CORTICAL_ICRP,
//       Lung   = G4_LUNG_ICRP (lung TISSUE, 1.05 g/cm3 -- the local material a microsphere sits
//       in; spectra were shown to be insensitive to using the inflated-organ bulk density).
//   i:So/X/NumberOfHistoriesInRun = <number of PHOTONS>
//
// Normalization: one history = one emitted photon, NOT one decay. To convert a result per
// history into a result per Y-90 decay, multiply by the medium's yield (photons per decay over
// the tabulated 1-2300 keV range), printed at startup and available from GetYieldPerDecay().
// Shipped calibration (20M decays per medium): water 0.0826, tissue 0.0797, bone 0.1380,
// lung 0.0832 photons/decay in 1-2300 keV (exact values in TsY90BremsTables.hh). Roughly 21% of
// those lie in the 45-620 keV imaging range, i.e. ~1.8e-2 photons/decay for soft tissue.
//
// Internal bremsstrahlung (the QED radiative correction at the decay vertex, which Geant4's beta
// decay does not model) is included as a second, medium-independent table computed from KUB
// theory folded over the Geant4 Y-90 beta spectrum (kY90IBCumProb, ~2.1e-2 photons/decay in
// 1-2300 keV, ~8.8e-3 above 30 keV). In low-Z tissue it is roughly a quarter of the photons in the
// imaging range (Lim et al 2018 PMB 63:115001; Dewaraja et al 2018 J Nucl Med 59(S1):577).
//   b:So/X/IncludeInternalBremsstrahlung = "True" (default) | "False"  (external-only spectrum)
// The printed yield and GetYieldPerDecay() include it when enabled.

#ifndef TsY90BremsGenerator_hh
#define TsY90BremsGenerator_hh

#include "TsVGenerator.hh"
#include "G4ThreeVector.hh"
#include <vector>

class TsParameterManager;
class TsGeometryManager;
class TsGeneratorManager;
class G4Navigator;
class G4VPhysicalVolume;

class TsY90BremsGenerator : public TsVGenerator
{
public:
    TsY90BremsGenerator(TsParameterManager* pM, TsGeometryManager* gM, TsGeneratorManager* pgM, G4String sourceName);
    ~TsY90BremsGenerator();

    void ResolveParameters();
    void GeneratePrimaries(G4Event* anEvent);

    G4double GetYieldPerDecay() const { return fYieldPerDecay; }

private:
    void SampleVolumetricPosition();
    G4double SampleEnergy();

    G4String fMedium;
    const G4double* fCumProb;   // selected medium's cumulative bin probabilities (external brems)
    G4double fEBYield;          // external-brems photons per decay over the tabulated range
    G4bool   fIncludeIB;
    G4double fIBFraction;       // probability that a sampled photon comes from the IB table
    G4double fYieldPerDecay;    // total photons per decay (EB + IB if enabled)

    // uniform-in-volume sampling of the source component (same scheme as TsSpectDecayGenerator)
    G4bool fNeedExtent;
    G4double fXMin, fXMax, fYMin, fYMax, fZMin, fZMax;
    G4Navigator* fNavigator;
    std::vector<G4VPhysicalVolume*> fVolumes;
    G4ThreeVector fPos;
};
#endif
