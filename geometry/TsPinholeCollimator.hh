#ifndef TsPinholeCollimator_hh
#define TsPinholeCollimator_hh

#include "TsVGeometryComponent.hh"

class TsPinholeCollimator : public TsVGeometryComponent
{
public:
	TsPinholeCollimator(TsParameterManager* pM, TsExtensionManager* eM, TsMaterialManager* mM,
						TsGeometryManager* gM, TsVGeometryComponent* parentComponent,
						G4VPhysicalVolume* parentVolume, G4String& name);
	~TsPinholeCollimator();

	G4VPhysicalVolume* Construct();

private:
	G4double fPlateThickness;
	G4double fPinholeDiameter;
	G4double fAcceptanceAngle;   // full opening angle of the knife-edge double cone
	G4int fNPinholesX;
	G4int fNPinholesY;
	G4double fPinholePitch;
	G4double fPlateHLX;
	G4double fPlateHLY;
	G4double fFocalLength;       // 0 = axial pinholes; finite = aim each pinhole at a focus (object side)
};

#endif
