// Component for TsStarGuideDetector
//
// Pixelated CZT detector slab for the GE StarGuide SPECT/CT.
// Creates a single CZT box. Pixel IDs are computed from hit position
// in the scorer (no daughter volumes needed).

#include "TsStarGuideDetector.hh"

#include "TsParameterManager.hh"
#include "G4Box.hh"
#include "G4LogicalVolume.hh"
#include "G4VPhysicalVolume.hh"
#include "G4SystemOfUnits.hh"

TsStarGuideDetector::TsStarGuideDetector(TsParameterManager* pM, TsExtensionManager* eM,
										 TsMaterialManager* mM, TsGeometryManager* gM,
										 TsVGeometryComponent* parentComponent,
										 G4VPhysicalVolume* parentVolume, G4String& name)
: TsVGeometryComponent(pM, eM, mM, gM, parentComponent, parentVolume, name)
{
	fCZTThickness = 5.0 * mm;
	if (fPm->ParameterExists(GetFullParmName("CZTThickness")))
		fCZTThickness = fPm->GetDoubleParameter(GetFullParmName("CZTThickness"), "Length");

	fPixelSizeX = 2.46 * mm;
	if (fPm->ParameterExists(GetFullParmName("PixelSizeX")))
		fPixelSizeX = fPm->GetDoubleParameter(GetFullParmName("PixelSizeX"), "Length");

	fPixelSizeY = 2.46 * mm;
	if (fPm->ParameterExists(GetFullParmName("PixelSizeY")))
		fPixelSizeY = fPm->GetDoubleParameter(GetFullParmName("PixelSizeY"), "Length");

	fNPixelsX = 32;
	if (fPm->ParameterExists(GetFullParmName("NPixelsX")))
		fNPixelsX = fPm->GetIntegerParameter(GetFullParmName("NPixelsX"));

	fNPixelsY = 32;
	if (fPm->ParameterExists(GetFullParmName("NPixelsY")))
		fNPixelsY = fPm->GetIntegerParameter(GetFullParmName("NPixelsY"));
}


TsStarGuideDetector::~TsStarGuideDetector()
{;}


G4VPhysicalVolume* TsStarGuideDetector::Construct()
{
	BeginConstruction();

	// Single CZT slab — pixel grid computed in scorer from hit position
	G4double envelopeHLX = fNPixelsX * fPixelSizeX / 2.0;
	G4double envelopeHLY = fNPixelsY * fPixelSizeY / 2.0;
	G4double envelopeHLZ = fCZTThickness / 2.0;

	G4Box* envelope = new G4Box("DetectorEnvelope", envelopeHLX, envelopeHLY, envelopeHLZ);
	fEnvelopeLog = CreateLogicalVolume(envelope);
	fEnvelopePhys = CreatePhysicalVolume(fEnvelopeLog);

	G4cout << "TsStarGuideDetector: CZT slab "
		   << 2.0 * envelopeHLX / mm << " x " << 2.0 * envelopeHLY / mm
		   << " x " << fCZTThickness / mm << " mm, pixel grid "
		   << fNPixelsX << " x " << fNPixelsY << G4endl;

	InstantiateChildren(fEnvelopePhys);
	return fEnvelopePhys;
}
