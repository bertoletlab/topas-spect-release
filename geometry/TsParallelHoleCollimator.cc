// Component for TsParallelHoleCollimator
//
// General parallel-hole SPECT collimator: a septa slab of a user-specified material
// (e.g. Lead or Tungsten) perforated by a grid of vacuum channels. The hole shape is
// selectable (round or hexagonal) and the channels can be arranged on a square lattice
// or a hexagonal (offset-row) lattice, so the class covers the common low/medium/high-
// energy collimators of clinical SPECT systems.
//
// Parameters (Ge/<name>/...):
//   Material          slab material (via TsVGeometryComponent, e.g. "Lead", "Tungsten")
//   HoleDiameter      channel size; for Hex holes this is the across-flats dimension
//   SeptalThickness   wall thickness between channels (pitch = HoleDiameter + SeptalThickness)
//   CollimatorLength  slab thickness along the bore axis (z)
//   NHolesX, NHolesY  number of channels in each transverse direction
//   HoleShape         "Round" (default, G4Tubs), "Hex" (G4Polyhedra, pointy-top), or
//                     "Square" (G4Box; HoleDiameter is the side / flat-to-flat length,
//                     e.g. CZT square-hole collimators such as D-SPECT and GE StarGuide/WEHR)
//   Lattice           "Square" (default) or "Hex" (triangular centres, offset alternate
//                     rows, row spacing = pitch * sqrt(3)/2)
//   FocalLengthX,     0 (default) = parallel holes. A finite value tilts each channel so its axis
//   FocalLengthY      passes through a focus at that collimator-face-to-focus distance: a focus in
//                     BOTH axes = cone-beam (converging), one axis = fan-beam (focal line), and a
//                     negative value = diverging. Fan/cone magnify (m = f/(f - z)).
//   Hole/Material     channel fill material (e.g. "Vacuum")
//
// Defaults are Round holes on a Square lattice, which is the configuration used for the
// validated results in docs/validation.md. A hexagonal-hole, hexagonal-lattice collimator
// packs more open area (hex packing factor ~0.907 relative to a square lattice of the same
// pitch) and therefore has higher absolute sensitivity.

#include "TsParallelHoleCollimator.hh"

#include "TsParameterManager.hh"
#include "G4Box.hh"
#include "G4Tubs.hh"
#include "G4Polyhedra.hh"
#include "G4VSolid.hh"
#include "G4LogicalVolume.hh"
#include "G4VPhysicalVolume.hh"
#include "G4SystemOfUnits.hh"
#include "G4RotationMatrix.hh"
#include "G4String.hh"
#include <cmath>
#include <algorithm>

TsParallelHoleCollimator::TsParallelHoleCollimator(TsParameterManager* pM, TsExtensionManager* eM,
												   TsMaterialManager* mM, TsGeometryManager* gM,
												   TsVGeometryComponent* parentComponent,
												   G4VPhysicalVolume* parentVolume, G4String& name)
: TsVGeometryComponent(pM, eM, mM, gM, parentComponent, parentVolume, name)
{
	fHoleDiameter = 1.5 * mm;
	if (fPm->ParameterExists(GetFullParmName("HoleDiameter")))
		fHoleDiameter = fPm->GetDoubleParameter(GetFullParmName("HoleDiameter"), "Length");

	fSeptalThickness = 0.2 * mm;
	if (fPm->ParameterExists(GetFullParmName("SeptalThickness")))
		fSeptalThickness = fPm->GetDoubleParameter(GetFullParmName("SeptalThickness"), "Length");

	fCollimatorLength = 25.0 * mm;
	if (fPm->ParameterExists(GetFullParmName("CollimatorLength")))
		fCollimatorLength = fPm->GetDoubleParameter(GetFullParmName("CollimatorLength"), "Length");

	fNHolesX = 32;
	if (fPm->ParameterExists(GetFullParmName("NHolesX")))
		fNHolesX = fPm->GetIntegerParameter(GetFullParmName("NHolesX"));

	fNHolesY = 32;
	if (fPm->ParameterExists(GetFullParmName("NHolesY")))
		fNHolesY = fPm->GetIntegerParameter(GetFullParmName("NHolesY"));

	fHoleShape = "Round";
	if (fPm->ParameterExists(GetFullParmName("HoleShape")))
		fHoleShape = fPm->GetStringParameter(GetFullParmName("HoleShape"));

	fLattice = "Square";
	if (fPm->ParameterExists(GetFullParmName("Lattice")))
		fLattice = fPm->GetStringParameter(GetFullParmName("Lattice"));

	// Focal length: 0 (default) = parallel holes. A finite value focuses the holes toward a point
	// (cone-beam, both axes) or a line (fan-beam, one axis) in front of the collimator; negative
	// diverges. Value is the collimator-face-to-focus distance.
	fFocalLengthX = 0.0;
	if (fPm->ParameterExists(GetFullParmName("FocalLengthX")))
		fFocalLengthX = fPm->GetDoubleParameter(GetFullParmName("FocalLengthX"), "Length");

	fFocalLengthY = 0.0;
	if (fPm->ParameterExists(GetFullParmName("FocalLengthY")))
		fFocalLengthY = fPm->GetDoubleParameter(GetFullParmName("FocalLengthY"), "Length");
}


TsParallelHoleCollimator::~TsParallelHoleCollimator()
{;}


G4VPhysicalVolume* TsParallelHoleCollimator::Construct()
{
	BeginConstruction();

	G4String holeShape = G4StrUtil::to_lower_copy(fHoleShape);
	G4String lattice   = G4StrUtil::to_lower_copy(fLattice);
	G4bool hexHole    = (holeShape == "hex" || holeShape == "hexagon" || holeShape == "hexagonal");
	G4bool squareHole = (holeShape == "square" || holeShape == "rectangular");
	G4bool hexLattice = (lattice == "hex" || lattice == "hexagonal" || lattice == "triangular");

	// Nearest-neighbour centre-to-centre pitch = hole size across flats + septal wall.
	G4double pitch = fHoleDiameter + fSeptalThickness;
	// Hex lattice: rows are offset by pitch/2 and stacked at pitch*sqrt(3)/2.
	G4double rowSpacing = hexLattice ? pitch * std::sqrt(3.0) / 2.0 : pitch;

	// Transverse half-extent of a single hole (used to size the envelope so daughters fit).
	// Pointy-top hexagon: across-flats = HoleDiameter (x), point-to-point = 2*HoleDiameter/sqrt(3) (y).
	G4double holeHalfX = fHoleDiameter / 2.0;
	G4double holeHalfY = hexHole ? fHoleDiameter / std::sqrt(3.0) : fHoleDiameter / 2.0;

	// Largest hole-centre offset from the axis (odd rows shift by pitch/2 on a hex lattice).
	G4double maxCentreX = (fNHolesX - 1) / 2.0 * pitch + (hexLattice ? pitch / 2.0 : 0.0);
	G4double maxCentreY = (fNHolesY - 1) / 2.0 * rowSpacing;

	// Converging/diverging: each hole is tilted so its axis passes through the focal point/line.
	// The largest tilt is at the outermost hole; use it to keep the tilted holes inside the L-thick
	// slab (shorten the channels) and to widen the envelope transversely.
	G4bool converging   = (fFocalLengthX != 0.0 || fFocalLengthY != 0.0);
	G4double tiltXMax = (fFocalLengthX != 0.0) ? std::atan(maxCentreX / std::fabs(fFocalLengthX)) : 0.0;
	G4double tiltYMax = (fFocalLengthY != 0.0) ? std::atan(maxCentreY / std::fabs(fFocalLengthY)) : 0.0;
	G4double tiltMax  = std::sqrt(tiltXMax * tiltXMax + tiltYMax * tiltYMax);

	G4double envelopeHLZ = fCollimatorLength / 2.0;
	// Channel half-length along its own (tilted) axis, chosen so a tilted channel spans the slab
	// without poking through the faces. For parallel holes (tiltMax = 0) this is exactly L/2.
	G4double holeHalfExtent = std::max(holeHalfX, holeHalfY);
	G4double holeHalfZ = converging
		? (envelopeHLZ - holeHalfExtent * std::sin(tiltMax)) / std::cos(tiltMax)
		: envelopeHLZ;
	// Extra transverse room for the tilt of the channels.
	G4double tiltMargin = converging ? holeHalfZ * std::sin(tiltMax) : 0.0;

	// Envelope: septa slab covering the full hole grid with a half-septum margin. For the default
	// parallel Round/Square case this reduces exactly to NHoles*pitch/2 (validated geometry).
	G4double envelopeHLX = maxCentreX + holeHalfX + fSeptalThickness / 2.0 + tiltMargin;
	G4double envelopeHLY = maxCentreY + holeHalfY + fSeptalThickness / 2.0 + tiltMargin;

	G4Box* envelope = new G4Box("CollimatorEnvelope", envelopeHLX, envelopeHLY, envelopeHLZ);
	fEnvelopeLog = CreateLogicalVolume(envelope);
	fEnvelopePhys = CreatePhysicalVolume(fEnvelopeLog);

	// Single hole solid, reused for every channel (placed with a per-hole rotation when converging).
	G4VSolid* hole;
	if (hexHole) {
		G4double zPlane[2] = {-holeHalfZ, holeHalfZ};
		G4double rInner[2] = {0.0, 0.0};
		G4double rOuter[2] = {fHoleDiameter / 2.0, fHoleDiameter / 2.0};   // apothem = across-flats/2
		// phiStart = 30 deg -> pointy-top hexagon (vertical flats face the in-row neighbours).
		hole = new G4Polyhedra("Hole", 30.0 * deg, 360.0 * deg, 6, 2, zPlane, rInner, rOuter);
	} else if (squareHole) {
		// HoleDiameter is the square side (flat-to-flat); half-side in x and y.
		hole = new G4Box("Hole", fHoleDiameter / 2.0, fHoleDiameter / 2.0, holeHalfZ);
	} else {
		hole = new G4Tubs("Hole", 0.0, fHoleDiameter / 2.0, holeHalfZ, 0.0, 360.0 * deg);
	}
	G4LogicalVolume* holeLog = CreateLogicalVolume("Hole", hole);

	G4int copyNo = 0;
	for (G4int iy = 0; iy < fNHolesY; iy++) {
		G4double yPos = (iy - (fNHolesY - 1) / 2.0) * rowSpacing;
		G4double xOffset = (hexLattice && (iy % 2 != 0)) ? pitch / 2.0 : 0.0;
		for (G4int ix = 0; ix < fNHolesX; ix++) {
			G4double xPos = (ix - (fNHolesX - 1) / 2.0) * pitch + xOffset;

			G4ThreeVector* pos = new G4ThreeVector(xPos, yPos, 0.0);
			RegisterTranslation(pos);

			// Tilt each channel toward the focus (axis through the focal point/line). Negative
			// focal length diverges. G4 placement takes the inverse rotation of the daughter.
			G4RotationMatrix* rot = 0;
			if (converging) {
				rot = new G4RotationMatrix();
				if (fFocalLengthX != 0.0) rot->rotateY(-std::atan(xPos / fFocalLengthX));
				if (fFocalLengthY != 0.0) rot->rotateX( std::atan(yPos / fFocalLengthY));
				RegisterRotation(rot);
			}

			CreatePhysicalVolume("Hole", copyNo, true, holeLog, rot, pos, fEnvelopePhys);
			copyNo++;
		}
	}

	G4String shapeName = hexHole ? "hex" : (squareHole ? "square" : "round");
	G4String focusName = converging ? ((fFocalLengthX != 0.0 && fFocalLengthY != 0.0) ? "cone-beam"
									 : "fan-beam") : "parallel";
	G4cout << "TsParallelHoleCollimator: Created " << copyNo << " " << shapeName << " holes ("
		   << fNHolesX << " x " << fNHolesY << ") on a " << (hexLattice ? "hex" : "square")
		   << " lattice, " << focusName << ", pitch = " << pitch / mm << " mm" << G4endl;

	InstantiateChildren(fEnvelopePhys);
	return fEnvelopePhys;
}
