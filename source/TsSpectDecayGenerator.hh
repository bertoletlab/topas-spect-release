// Particle Generator for SpectDecaySource (see TsSpectDecaySource.hh for the overview).

#ifndef TsSpectDecayGenerator_hh
#define TsSpectDecayGenerator_hh

#include "TsVGenerator.hh"
#include "G4ThreeVector.hh"
#include <vector>

class TsParameterManager;
class TsGeometryManager;
class TsGeneratorManager;
class G4ParticleDefinition;
class G4Navigator;
class G4VPhysicalVolume;

class TsSpectDecayGenerator : public TsVGenerator
{
public:
    TsSpectDecayGenerator(TsParameterManager* pM, TsGeometryManager* gM, TsGeneratorManager* pgM, G4String sourceName);
    ~TsSpectDecayGenerator();

    void ResolveParameters();
    void GeneratePrimaries(G4Event* anEvent);
    void UpdateForNewRun(G4bool force);

private:
    void SampleVolumetricPosition();

    G4ParticleDefinition* fIon;
    G4double fLambda;                 // decay constant [1/s]
    G4double fT0;                     // timeline start [s]
    std::vector<G4double> fTimeList;  // left edge of each time bin [s]
    G4long fBaseHistories;            // NumberOfHistoriesInRun at the first bin
    G4bool fCorrectByNumberOfHistories;
    G4int fRunID;

    // uniform-in-volume sampling of the source component
    G4bool fNeedExtent;
    G4double fXMin, fXMax, fYMin, fYMax, fZMin, fZMax;
    G4Navigator* fNavigator;
    std::vector<G4VPhysicalVolume*> fVolumes;
    G4ThreeVector fPos;               // component-local
};
#endif
