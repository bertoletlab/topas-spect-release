#ifndef TsParallelHoleCollimator_hh
#define TsParallelHoleCollimator_hh

#include "TsVGeometryComponent.hh"

class TsParallelHoleCollimator : public TsVGeometryComponent
{
public:
	TsParallelHoleCollimator(TsParameterManager* pM, TsExtensionManager* eM, TsMaterialManager* mM,
							 TsGeometryManager* gM, TsVGeometryComponent* parentComponent,
							 G4VPhysicalVolume* parentVolume, G4String& name);
	~TsParallelHoleCollimator();

	G4VPhysicalVolume* Construct();

private:
	G4double fHoleDiameter;
	G4double fSeptalThickness;
	G4double fCollimatorLength;
	G4int fNHolesX;
	G4int fNHolesY;
	G4String fHoleShape;   // "Round" (default) or "Hex" or "Square"
	G4String fLattice;     // "Square" (default) or "Hex"
	G4double fFocalLengthX; // 0 (default) = parallel; finite = converging (fan/cone); negative = diverging
	G4double fFocalLengthY;
};

#endif
