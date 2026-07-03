// Particle Generator for SpectDecaySource
//
// Emits the parent radionuclide (a GenericIon) uniformly inside a component and, over a Tf
// timeline, scales the number of primaries per bin by exp(-lambda * (t - t0)) so the emitted
// activity follows the true decay. The decay chain itself is produced by Geant4's
// g4radioactivedecay physics acting on each emitted ion, so the deck must enable it.

#include "TsSpectDecayGenerator.hh"
#include "TsSpectDecaySource.hh"
#include "TsParameterManager.hh"
#include "TsVGeometryComponent.hh"

#include "G4Event.hh"
#include "G4IonTable.hh"
#include "G4ParticleDefinition.hh"
#include "G4Navigator.hh"
#include "G4TransportationManager.hh"
#include "G4VisExtent.hh"
#include "G4RandomTools.hh"
#include "Randomize.hh"
#include "G4SystemOfUnits.hh"
#include <cmath>

TsSpectDecayGenerator::TsSpectDecayGenerator(TsParameterManager* pM, TsGeometryManager* gM,
                                             TsGeneratorManager* pgM, G4String sourceName)
    : TsVGenerator(pM, gM, pgM, sourceName), fIon(nullptr), fLambda(0.), fT0(0.),
      fBaseHistories(1), fCorrectByNumberOfHistories(true), fRunID(-1),
      fNeedExtent(true), fXMin(0), fXMax(0), fYMin(0), fYMax(0), fZMin(0), fZMax(0), fNavigator(nullptr)
{
    ResolveParameters();
}

TsSpectDecayGenerator::~TsSpectDecayGenerator() {}

void TsSpectDecayGenerator::ResolveParameters()
{
    TsVGenerator::ResolveParameters();

    // The base sets fParticleDefinition from BeamParticle = "GenericIon(Z,A)".
    fIon = fParticleDefinition;
    G4double meanlife = fIon->GetPDGLifeTime() / second;
    fLambda = (meanlife > 0.) ? 1.0 / meanlife : 0.0;

    fCorrectByNumberOfHistories = true;
    if (fPm->ParameterExists(GetFullParmName("CorrectByNumberOfHistories")))
        fCorrectByNumberOfHistories = fPm->GetBooleanParameter(GetFullParmName("CorrectByNumberOfHistories"));

    fT0 = 0.0;
    if (fPm->ParameterExists("Tf/TimelineStart"))
        fT0 = fPm->GetDoubleParameter("Tf/TimelineStart", "Time") / second;
    G4double tEnd = fT0;
    if (fPm->ParameterExists("Tf/TimelineEnd"))
        tEnd = fPm->GetDoubleParameter("Tf/TimelineEnd", "Time") / second;
    G4int nSeq = 1;
    if (fPm->ParameterExists("Tf/NumberOfSequentialTimes"))
        nSeq = fPm->GetIntegerParameter("Tf/NumberOfSequentialTimes");
    fTimeList.clear();
    G4double dt = (nSeq > 0) ? (tEnd - fT0) / nSeq : 0.0;
    for (G4int i = 0; i < nSeq; i++)
        fTimeList.push_back(fT0 + i * dt);

    fBaseHistories = GetSource()->GetNumberOfHistoriesInRun();
    fRunID = -1;
    fNeedExtent = true;
    fVolumes = fComponent->GetAllPhysicalVolumes(true);
}

void TsSpectDecayGenerator::UpdateForNewRun(G4bool force)
{
    TsVGenerator::UpdateForNewRun(force);
    fRunID++;
    // OpenTOPAS applies a SetNewNumberOfHistories change to the FOLLOWING run, so set the count for
    // the NEXT time bin here. Run 0 keeps the deck's NumberOfHistoriesInRun (the t0 activity).
    G4int next = fRunID + 1;
    if (fCorrectByNumberOfHistories && next >= 0 && next < (G4int)fTimeList.size()) {
        G4double t = fTimeList[next];
        G4double frac = (fLambda > 0.) ? std::exp(-fLambda * (t - fT0)) : 1.0;
        G4long n = (G4long)std::llround((G4double)fBaseHistories * frac);
        if (n < 1) n = 1;
        ((TsSpectDecaySource*)GetSource())->SetNewNumberOfHistories(n);
    }
}

void TsSpectDecayGenerator::SampleVolumetricPosition()
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

void TsSpectDecayGenerator::GeneratePrimaries(G4Event* anEvent)
{
    if (CurrentSourceHasGeneratedEnough()) return;

    SampleVolumetricPosition();

    TsPrimaryParticle p;
    p.posX = fPos.x(); p.posY = fPos.y(); p.posZ = fPos.z();
    p.dCos1 = 0.; p.dCos2 = 0.; p.dCos3 = 1.;
    p.kEnergy = 0.0;
    p.particleDefinition = fIon;
    p.weight = 1.0;
    p.isNewHistory = true;
    p.isOpticalPhoton = false;
    p.ionCharge = fIon->GetPDGCharge();
    TransformPrimaryForComponent(&p);
    GenerateOnePrimary(anEvent, p);
    AddPrimariesToEvent(anEvent);
}
