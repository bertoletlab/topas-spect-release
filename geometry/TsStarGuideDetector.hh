#ifndef TsStarGuideDetector_hh
#define TsStarGuideDetector_hh

#include "TsVGeometryComponent.hh"

class TsStarGuideDetector : public TsVGeometryComponent
{
public:
	TsStarGuideDetector(TsParameterManager* pM, TsExtensionManager* eM, TsMaterialManager* mM,
						TsGeometryManager* gM, TsVGeometryComponent* parentComponent,
						G4VPhysicalVolume* parentVolume, G4String& name);
	~TsStarGuideDetector();

	G4VPhysicalVolume* Construct();

private:
	G4double fCZTThickness;
	G4double fPixelSizeX;
	G4double fPixelSizeY;
	G4int fNPixelsX;
	G4int fNPixelsY;
};

#endif
