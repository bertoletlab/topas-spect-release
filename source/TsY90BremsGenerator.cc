// Particle Generator for Y90BremsSource
//
// See TsY90BremsGenerator.hh. Samples the tabulated Y-90 photon emission spectrum of the chosen
// medium (TsY90BremsTables.hh): inverse-CDF over the log-spaced bins, uniform within a bin,
// isotropic direction, uniform-in-volume position inside the source component.

#include "TsY90BremsGenerator.hh"
#include "TsY90BremsTables.hh"
#include "TsParameterManager.hh"
#include "TsVGeometryComponent.hh"

#include "G4Event.hh"
#include "G4Gamma.hh"
#include "G4Navigator.hh"
#include "G4TransportationManager.hh"
#include "G4VisExtent.hh"
#include "G4RandomTools.hh"
#include "Randomize.hh"
#include "G4SystemOfUnits.hh"
#include <algorithm>
#include <cmath>

TsY90BremsGenerator::TsY90BremsGenerator(TsParameterManager* pM, TsGeometryManager* gM,
                                         TsGeneratorManager* pgM, G4String sourceName)
    : TsVGenerator(pM, gM, pgM, sourceName), fMedium("Water"), fCumProb(nullptr), fEBYield(0.),
      fIncludeIB(true), fIBFraction(0.), fYieldPerDecay(0.),
      fNeedExtent(true), fXMin(0), fXMax(0), fYMin(0), fYMax(0), fZMin(0), fZMax(0), fNavigator(nullptr)
{
    ResolveParameters();
}

TsY90BremsGenerator::~TsY90BremsGenerator() {}

void TsY90BremsGenerator::ResolveParameters()
{
    TsVGenerator::ResolveParameters();

    if (fParticleDefinition != G4Gamma::Definition()) {
        G4cerr << "Topas is exiting due to a serious error in source setup." << G4endl;
        G4cerr << "Source " << GetName() << ": Y90BremsSource emits photons; set BeamParticle = \"gamma\"." << G4endl;
        fPm->AbortSession(1);
    }

    fMedium = "Water";
    if (fPm->ParameterExists(GetFullParmName("Medium")))
        fMedium = fPm->GetStringParameter(GetFullParmName("Medium"));
    G4String m = fMedium;
    std::transform(m.begin(), m.end(), m.begin(), ::tolower);

    if      (m == "water")  { fCumProb = kY90BremsCumProbWater;  fEBYield = kY90BremsYieldWater;  }
    else if (m == "tissue") { fCumProb = kY90BremsCumProbTissue; fEBYield = kY90BremsYieldTissue; }
    else if (m == "bone")   { fCumProb = kY90BremsCumProbBone;   fEBYield = kY90BremsYieldBone;   }
    else if (m == "lung")   { fCumProb = kY90BremsCumProbLung;   fEBYield = kY90BremsYieldLung;   }
    else {
        G4cerr << "Topas is exiting due to a serious error in source setup." << G4endl;
        G4cerr << "Source " << GetName() << ": unknown Medium \"" << fMedium
               << "\". Choose Water, Tissue, Bone or Lung." << G4endl;
        fPm->AbortSession(1);
    }

    fIncludeIB = true;
    if (fPm->ParameterExists(GetFullParmName("IncludeInternalBremsstrahlung")))
        fIncludeIB = fPm->GetBooleanParameter(GetFullParmName("IncludeInternalBremsstrahlung"));
    fYieldPerDecay = fEBYield + (fIncludeIB ? kY90IBYield : 0.);
    fIBFraction = fIncludeIB ? kY90IBYield / fYieldPerDecay : 0.;

    G4cout << "Y90BremsSource " << GetName() << ": medium " << fMedium
           << ", external bremsstrahlung " << fEBYield << " photons/decay"
           << (fIncludeIB ? " + internal bremsstrahlung " : " (internal bremsstrahlung OFF, would add ")
           << kY90IBYield << (fIncludeIB ? "" : ")")
           << "; one photon per history; multiply per-history results by "
           << fYieldPerDecay << " photons/decay to get per-decay results." << G4endl;

    fNeedExtent = true;
    fVolumes = fComponent->GetAllPhysicalVolumes(true);
}

G4double TsY90BremsGenerator::SampleEnergy()
{
    // Mixture of the medium's external-brems table and the medium-independent IB table.
    const G4double* table = (fIBFraction > 0. && G4UniformRand() < fIBFraction) ? kY90IBCumProb : fCumProb;
    G4double u = G4UniformRand();
    const G4double* hi = std::lower_bound(table, table + kY90BremsNBins, u);
    G4int b = (G4int)(hi - table);
    if (b >= kY90BremsNBins) b = kY90BremsNBins - 1;
    G4double e = G4RandFlat::shoot(kY90BremsEdgesKeV[b], kY90BremsEdgesKeV[b + 1]);
    return e * keV;
}

void TsY90BremsGenerator::SampleVolumetricPosition()
{
    if (fNeedExtent) {
        G4VisExtent e = fComponent->GetExtent();
        fXMin = e.GetXmin(); fXMax = e.GetXmax();
        fYMin = e.GetYmin(); fYMax = e.GetYmax();
        fZMin = e.GetZmin(); fZMax = e.GetZmax();
        fNeedExtent = false;
        G4TransportationManager* tm = G4TransportationManager::GetTransportationManager();
        fNavigator = tm->GetNavigator(tm->GetParallelWorld(fComponent->GetWorldName()));
    }
    if (fXMax - fXMin == 0 && fYMax - fYMin == 0 && fZMax - fZMin == 0) {
        fPos = G4ThreeVector(fXMin, fYMin, fZMin);
        return;
    }
    G4double x = 0, y = 0, z = 0;
    G4VPhysicalVolume* found = nullptr;
    G4bool inside = false;
    while (!inside) {
        x = G4RandFlat::shoot(fXMin, fXMax);
        y = G4RandFlat::shoot(fYMin, fYMax);
        z = G4RandFlat::shoot(fZMin, fZMax);
        found = fNavigator->LocateGlobalPointAndSetup(G4ThreeVector(x, y, z));
        if (found)
            for (size_t t = 0; !inside && t < fVolumes.size(); t++)
                inside = (found == fVolumes[t]);
    }
    G4Point3D* c = fComponent->GetTransRelToWorld();
    fPos = G4ThreeVector(x - c[0].x(), y - c[0].y(), z - c[0].z());
}

void TsY90BremsGenerator::GeneratePrimaries(G4Event* anEvent)
{
    if (CurrentSourceHasGeneratedEnough()) return;

    SampleVolumetricPosition();

    G4double cosTheta = G4RandFlat::shoot(-1., 1.);
    G4double sinTheta = std::sqrt(1. - cosTheta * cosTheta);
    G4double phi = 2. * CLHEP::pi * G4UniformRand();

    TsPrimaryParticle p;
    p.posX = fPos.x(); p.posY = fPos.y(); p.posZ = fPos.z();
    p.dCos1 = sinTheta * std::cos(phi);
    p.dCos2 = sinTheta * std::sin(phi);
    p.dCos3 = cosTheta;
    p.kEnergy = SampleEnergy();
    p.particleDefinition = fParticleDefinition;
    p.weight = 1.0;
    p.isNewHistory = true;
    p.isOpticalPhoton = false;
    p.isGenericIon = false;
    p.ionCharge = 0;
    TransformPrimaryForComponent(&p);
    GenerateOnePrimary(anEvent, p);
    AddPrimariesToEvent(anEvent);
}
