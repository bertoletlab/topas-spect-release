// Component for TsPinholeCollimator
//
// Pinhole and multi-pinhole SPECT collimator: an absorber plate of a user-specified material
// (e.g. Lead or Tungsten) perforated by one or more knife-edge pinhole apertures. Each aperture
// is a double cone (widest at both faces, narrowest at the pinhole in the mid-plane), subtracted
// from the plate so the aperture is filled by the parent (vacuum). A single aperture gives a
// pinhole collimator; an NxN grid gives a multi-pinhole collimator.
//
// Parameters (Ge/<name>/...):
//   Material         plate/absorber material (via TsVGeometryComponent, e.g. "Lead", "Tungsten")
//   PlateThickness   absorber thickness along the bore axis (z)
//   PinholeDiameter  aperture diameter at the knife edge (mid-plane)
//   AcceptanceAngle  full opening angle of the double cone (default 90 deg)
//   NPinholesX,      number of pinholes in each transverse direction (default 1 x 1 = single pinhole)
//   NPinholesY
//   PinholePitch     centre-to-centre pinhole spacing (multi-pinhole)
//   PlateHLX,        plate transverse half-sizes (default: cover the pinhole grid with a margin)
//   PlateHLY
//   FocalLength      0 (default) = axial pinholes; a finite value aims each pinhole at a focus at
//                    that distance on the object side (converging multi-pinhole)
//
// Pinhole sensitivity ~ (d_eff/(2h))^2 * sin^3(theta); resolution R = d_eff*(l+f)/f (Van
// Audenhaege 2015). Use a large focal/magnifying geometry with the detector well behind the plate.

#include "TsPinholeCollimator.hh"

#include "TsParameterManager.hh"
#include "G4Box.hh"
#include "G4Polycone.hh"
#include "G4SubtractionSolid.hh"
#include "G4VSolid.hh"
#include "G4RotationMatrix.hh"
#include "G4ThreeVector.hh"
#include "G4LogicalVolume.hh"
#include "G4VPhysicalVolume.hh"
#include "G4SystemOfUnits.hh"
#include <cmath>

TsPinholeCollimator::TsPinholeCollimator(TsParameterManager* pM, TsExtensionManager* eM,
										 TsMaterialManager* mM, TsGeometryManager* gM,
										 TsVGeometryComponent* parentComponent,
										 G4VPhysicalVolume* parentVolume, G4String& name)
: TsVGeometryComponent(pM, eM, mM, gM, parentComponent, parentVolume, name)
{
	fPlateThickness = 7.0 * mm;
	if (fPm->ParameterExists(GetFullParmName("PlateThickness")))
		fPlateThickness = fPm->GetDoubleParameter(GetFullParmName("PlateThickness"), "Length");

	fPinholeDiameter = 1.0 * mm;
	if (fPm->ParameterExists(GetFullParmName("PinholeDiameter")))
		fPinholeDiameter = fPm->GetDoubleParameter(GetFullParmName("PinholeDiameter"), "Length");

	fAcceptanceAngle = 90.0 * deg;
	if (fPm->ParameterExists(GetFullParmName("AcceptanceAngle")))
		fAcceptanceAngle = fPm->GetDoubleParameter(GetFullParmName("AcceptanceAngle"), "Angle");

	fNPinholesX = 1;
	if (fPm->ParameterExists(GetFullParmName("NPinholesX")))
		fNPinholesX = fPm->GetIntegerParameter(GetFullParmName("NPinholesX"));

	fNPinholesY = 1;
	if (fPm->ParameterExists(GetFullParmName("NPinholesY")))
		fNPinholesY = fPm->GetIntegerParameter(GetFullParmName("NPinholesY"));

	fPinholePitch = 20.0 * mm;
	if (fPm->ParameterExists(GetFullParmName("PinholePitch")))
		fPinholePitch = fPm->GetDoubleParameter(GetFullParmName("PinholePitch"), "Length");

	// Plate transverse half-size: default covers the pinhole grid plus a one-pitch margin.
	G4double defaultHLX = (fNPinholesX * fPinholePitch) / 2.0 + fPinholePitch / 2.0;
	G4double defaultHLY = (fNPinholesY * fPinholePitch) / 2.0 + fPinholePitch / 2.0;
	fPlateHLX = defaultHLX;
	if (fPm->ParameterExists(GetFullParmName("PlateHLX")))
		fPlateHLX = fPm->GetDoubleParameter(GetFullParmName("PlateHLX"), "Length");
	fPlateHLY = defaultHLY;
	if (fPm->ParameterExists(GetFullParmName("PlateHLY")))
		fPlateHLY = fPm->GetDoubleParameter(GetFullParmName("PlateHLY"), "Length");

	fFocalLength = 0.0;
	if (fPm->ParameterExists(GetFullParmName("FocalLength")))
		fFocalLength = fPm->GetDoubleParameter(GetFullParmName("FocalLength"), "Length");
}


TsPinholeCollimator::~TsPinholeCollimator()
{;}


G4VPhysicalVolume* TsPinholeCollimator::Construct()
{
	BeginConstruction();

	G4double t = fPlateThickness;
	G4double halfEdge = fPinholeDiameter / 2.0 + (t / 2.0) * std::tan(fAcceptanceAngle / 2.0);

	// Knife-edge double cone: widest (halfEdge) at the two faces, narrowest (d/2) at the mid-plane.
	// z-planes extend just beyond the plate so the subtraction cuts cleanly through both faces.
	G4double eps = 0.01 * mm;
	G4double zPlane[3] = {-t / 2.0 - eps, 0.0, t / 2.0 + eps};
	G4double rInner[3] = {0.0, 0.0, 0.0};
	G4double rOuter[3] = {halfEdge, fPinholeDiameter / 2.0, halfEdge};
	G4Polycone* aperture = new G4Polycone("Aperture", 0.0, 360.0 * deg, 3, zPlane, rInner, rOuter);

	// Absorber plate with the aperture(s) subtracted (subtracted region = parent material, vacuum).
	G4VSolid* solid = new G4Box("PinholePlate", fPlateHLX, fPlateHLY, t / 2.0);

	G4int nHoles = 0;
	for (G4int iy = 0; iy < fNPinholesY; iy++) {
		G4double yPos = (iy - (fNPinholesY - 1) / 2.0) * fPinholePitch;
		for (G4int ix = 0; ix < fNPinholesX; ix++) {
			G4double xPos = (ix - (fNPinholesX - 1) / 2.0) * fPinholePitch;

			// Aim the aperture at the object-side focus when FocalLength is set (else axial).
			G4RotationMatrix* rot = 0;
			if (fFocalLength != 0.0) {
				rot = new G4RotationMatrix();
				rot->rotateY(-std::atan(xPos / fFocalLength));
				rot->rotateX( std::atan(yPos / fFocalLength));
			}
			G4ThreeVector shift(xPos, yPos, 0.0);
			solid = new G4SubtractionSolid("Pinhole", solid, aperture, rot, shift);
			nHoles++;
		}
	}

	fEnvelopeLog = CreateLogicalVolume(solid);
	fEnvelopePhys = CreatePhysicalVolume(fEnvelopeLog);

	G4cout << "TsPinholeCollimator: Created " << nHoles << " pinhole(s) ("
		   << fNPinholesX << " x " << fNPinholesY << "), diameter = " << fPinholeDiameter / mm
		   << " mm, plate thickness = " << t / mm << " mm" << G4endl;

	InstantiateChildren(fEnvelopePhys);
	return fEnvelopePhys;
}
