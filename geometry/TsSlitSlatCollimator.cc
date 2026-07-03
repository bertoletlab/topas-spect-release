// Component for TsSlitSlatCollimator
//
// Slit-slat SPECT collimator (first-order model): an absorber slab of a user-specified material
// perforated by a stack of rectangular through-slots. The slots model crossed 1-D collimation —
// a slit aperture in the transaxial (x) direction crossed with parallel slats in the axial (y)
// direction. Each open slot spans the slit width in x and one slat gap in y; adjacent slots are
// separated in y by the slat foils.
//
// Parameters (Ge/<name>/...):
//   Material         absorber (slat) material (via TsVGeometryComponent, e.g. "Lead", "Tungsten")
//   CollimatorLength slab thickness along the bore axis (z)
//   SlatThickness    absorber foil thickness between adjacent slots (y)
//   SlatGap          open slot height (y); slat pitch = SlatThickness + SlatGap
//   NSlats           number of open slots (stacked in y)
//   SlitWidth        slot width in the transaxial direction (x) = the slit aperture
//   Slot/Material    slot fill material (e.g. "Vacuum")
//
// This straight (non-focusing) slit-slat gives axial resolution from the slats and transaxial
// resolution from the slit. A focusing (magnifying) slit is a future refinement.

#include "TsSlitSlatCollimator.hh"

#include "TsParameterManager.hh"
#include "G4Box.hh"
#include "G4LogicalVolume.hh"
#include "G4VPhysicalVolume.hh"
#include "G4ThreeVector.hh"
#include "G4SystemOfUnits.hh"

TsSlitSlatCollimator::TsSlitSlatCollimator(TsParameterManager* pM, TsExtensionManager* eM,
										   TsMaterialManager* mM, TsGeometryManager* gM,
										   TsVGeometryComponent* parentComponent,
										   G4VPhysicalVolume* parentVolume, G4String& name)
: TsVGeometryComponent(pM, eM, mM, gM, parentComponent, parentVolume, name)
{
	fCollimatorLength = 30.0 * mm;
	if (fPm->ParameterExists(GetFullParmName("CollimatorLength")))
		fCollimatorLength = fPm->GetDoubleParameter(GetFullParmName("CollimatorLength"), "Length");

	fSlatThickness = 0.3 * mm;
	if (fPm->ParameterExists(GetFullParmName("SlatThickness")))
		fSlatThickness = fPm->GetDoubleParameter(GetFullParmName("SlatThickness"), "Length");

	fSlatGap = 1.5 * mm;
	if (fPm->ParameterExists(GetFullParmName("SlatGap")))
		fSlatGap = fPm->GetDoubleParameter(GetFullParmName("SlatGap"), "Length");

	fNSlats = 64;
	if (fPm->ParameterExists(GetFullParmName("NSlats")))
		fNSlats = fPm->GetIntegerParameter(GetFullParmName("NSlats"));

	fSlitWidth = 40.0 * mm;
	if (fPm->ParameterExists(GetFullParmName("SlitWidth")))
		fSlitWidth = fPm->GetDoubleParameter(GetFullParmName("SlitWidth"), "Length");
}


TsSlitSlatCollimator::~TsSlitSlatCollimator()
{;}


G4VPhysicalVolume* TsSlitSlatCollimator::Construct()
{
	BeginConstruction();

	G4double pitch = fSlatThickness + fSlatGap;   // slat centre-to-centre in y
	G4double envelopeHLX = fSlitWidth / 2.0 + fSlatThickness;                 // slit + margin
	G4double envelopeHLY = fNSlats * pitch / 2.0 + fSlatThickness / 2.0;      // slat stack + half foil
	G4double envelopeHLZ = fCollimatorLength / 2.0;

	G4Box* envelope = new G4Box("SlitSlatEnvelope", envelopeHLX, envelopeHLY, envelopeHLZ);
	fEnvelopeLog = CreateLogicalVolume(envelope);
	fEnvelopePhys = CreatePhysicalVolume(fEnvelopeLog);

	// Open slot (slit width x, slat gap y, full depth z), reused for every slat gap.
	G4Box* slot = new G4Box("Slot", fSlitWidth / 2.0, fSlatGap / 2.0, envelopeHLZ);
	G4LogicalVolume* slotLog = CreateLogicalVolume("Slot", slot);

	for (G4int iy = 0; iy < fNSlats; iy++) {
		G4double yPos = (iy - (fNSlats - 1) / 2.0) * pitch;
		G4ThreeVector* pos = new G4ThreeVector(0.0, yPos, 0.0);
		RegisterTranslation(pos);
		CreatePhysicalVolume("Slot", iy, true, slotLog, 0, pos, fEnvelopePhys);
	}

	G4cout << "TsSlitSlatCollimator: Created " << fNSlats << " slots, slit width = "
		   << fSlitWidth / mm << " mm, slat gap = " << fSlatGap / mm << " mm, pitch = "
		   << pitch / mm << " mm" << G4endl;

	InstantiateChildren(fEnvelopePhys);
	return fEnvelopePhys;
}
