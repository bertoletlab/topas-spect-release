#ifndef TsSlitSlatCollimator_hh
#define TsSlitSlatCollimator_hh

#include "TsVGeometryComponent.hh"

class TsSlitSlatCollimator : public TsVGeometryComponent
{
public:
	TsSlitSlatCollimator(TsParameterManager* pM, TsExtensionManager* eM, TsMaterialManager* mM,
						 TsGeometryManager* gM, TsVGeometryComponent* parentComponent,
						 G4VPhysicalVolume* parentVolume, G4String& name);
	~TsSlitSlatCollimator();

	G4VPhysicalVolume* Construct();

private:
	G4double fCollimatorLength;
	G4double fSlatThickness;
	G4double fSlatGap;
	G4int fNSlats;
	G4double fSlitWidth;
};

#endif
