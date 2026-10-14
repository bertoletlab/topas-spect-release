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
#include <map>
#include <vector>
#include <limits>
#include <sstream>

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
	G4bool converging = (fFocalLengthX != 0.0 || fFocalLengthY != 0.0);

	G4double envelopeHLZ = fCollimatorLength / 2.0;

	// Circumscribed radius of a single hole's 2D cross-section: the worst-case (i.e. safe/
	// conservative, never an underestimate) distance from the channel axis to any point on its
	// cross-section, regardless of the azimuthal direction of the tilt. Used below to guarantee a
	// tilted channel's end-cap rim never protrudes past the envelope face.
	//   Round:  radius itself.
	//   Square: half-diagonal (corner), not the half-side (flat) -- the corner is what can poke
	//           through the face when the tilt axis is not aligned with a flat.
	//   Hex:    point-to-point distance (already the shape's circumradius).
	G4double holeCircumRadius = hexHole ? (fHoleDiameter / std::sqrt(3.0))
					   : squareHole ? (fHoleDiameter / 2.0) * std::sqrt(2.0)
					   : (fHoleDiameter / 2.0);

	// Exact channel half-length for a channel whose axis is tilted by (thetaX, thetaY) -- rotation
	// about Y by thetaX composed with rotation about X by thetaY, matching the placement rotation
	// built below. The composed tilt of the channel axis away from the mother's z-axis has
	// cos(thetaEff) = cos(thetaX)*cos(thetaY) EXACTLY (this is not the same as, and for finite
	// angles is smaller than, the small-angle Pythagorean approximation cos(sqrt(thetaX^2+thetaY^2))
	// that this code used before). The channel's end-cap rim reaches envelopeHLZ exactly when
	//   holeHalfZ * cos(thetaEff) + holeCircumRadius * sin(thetaEff) = envelopeHLZ.
	// For thetaX = thetaY = 0 this reduces exactly to envelopeHLZ (parallel-hole geometry untouched).
	auto SafeHoleHalfZ = [&](G4double thetaX, G4double thetaY) -> G4double {
		G4double cosEff = std::cos(thetaX) * std::cos(thetaY);
		G4double sinEff = std::sqrt(std::max(0.0, 1.0 - cosEff * cosEff));
		return (envelopeHLZ - holeCircumRadius * sinEff) / cosEff;
	};

	// Worst-case (largest) tilt occurs at the outermost hole (largest |xPos|, |yPos| simultaneously);
	// used only to size the envelope's transverse half-extents so every tilted channel's footprint
	// still fits inside the envelope box (a separate concern from the per-channel z-length below).
	G4double cornerThetaX = (fFocalLengthX != 0.0) ? std::atan(maxCentreX / fFocalLengthX) : 0.0;
	G4double cornerThetaY = (fFocalLengthY != 0.0) ? std::atan(maxCentreY / fFocalLengthY) : 0.0;
	G4double cornerCosEff = std::cos(cornerThetaX) * std::cos(cornerThetaY);
	G4double cornerSinEff = std::sqrt(std::max(0.0, 1.0 - cornerCosEff * cornerCosEff));
	G4double cornerHoleHalfZ = SafeHoleHalfZ(cornerThetaX, cornerThetaY);
	G4double tiltMargin = converging ? cornerHoleHalfZ * cornerSinEff : 0.0;

	// Envelope: septa slab covering the full hole grid with a half-septum margin. For the default
	// parallel Round/Square case this reduces exactly to NHoles*pitch/2 (validated geometry).
	G4double envelopeHLX = maxCentreX + holeHalfX + fSeptalThickness / 2.0 + tiltMargin;
	G4double envelopeHLY = maxCentreY + holeHalfY + fSeptalThickness / 2.0 + tiltMargin;

	G4Box* envelope = new G4Box("CollimatorEnvelope", envelopeHLX, envelopeHLY, envelopeHLZ);
	fEnvelopeLog = CreateLogicalVolume(envelope);
	fEnvelopePhys = CreatePhysicalVolume(fEnvelopeLog);

	// Hole solids are cached by EXACT channel half-length (no rounding/quantization at all): for
	// parallel holes every channel computes the identical bit-for-bit safeHalfZ (thetaX=thetaY=0
	// exactly), so they all land in the one cache entry and share the one solid -- bit-for-bit the
	// same construction as before this fix (a strict no-op for the validated, published parallel-
	// hole geometry). For converging/diverging geometries, every channel gets its own exact safe
	// half-length (the fix for the protrusion bug); channels that happen to compute the identical
	// value (guaranteed by lattice mirror symmetry: cos()/atan() are exactly even/odd, so mirrored
	// holes recompute the same double bit-for-bit) share a cached solid, and every other channel
	// gets its own. No approximation is introduced anywhere: the length used for a given hole is
	// always exactly its own computed safe value, never rounded up (so it can never protrude) and
	// never rounded down beyond what it exactly is (so no artificial shortening is introduced).
	std::map<G4double, G4LogicalVolume*> holeLogCache;
	auto GetHoleLogical = [&](G4double safeHalfZ) -> G4LogicalVolume* {
		auto it = holeLogCache.find(safeHalfZ);
		if (it != holeLogCache.end()) return it->second;
		G4double halfZ = safeHalfZ;
		G4VSolid* hole;
		if (hexHole) {
			G4double zPlane[2] = {-halfZ, halfZ};
			G4double rInner[2] = {0.0, 0.0};
			G4double rOuter[2] = {fHoleDiameter / 2.0, fHoleDiameter / 2.0};   // apothem = across-flats/2
			// phiStart = 30 deg -> pointy-top hexagon (vertical flats face the in-row neighbours).
			hole = new G4Polyhedra("Hole", 30.0 * deg, 360.0 * deg, 6, 2, zPlane, rInner, rOuter);
		} else if (squareHole) {
			// HoleDiameter is the square side (flat-to-flat); half-side in x and y.
			hole = new G4Box("Hole", fHoleDiameter / 2.0, fHoleDiameter / 2.0, halfZ);
		} else {
			hole = new G4Tubs("Hole", 0.0, fHoleDiameter / 2.0, halfZ, 0.0, 360.0 * deg);
		}
		G4LogicalVolume* holeLog = CreateLogicalVolume("Hole", hole);
		holeLogCache[safeHalfZ] = holeLog;
		return holeLog;
	};

	// Per-hole axis position at the front (object-side, local z = -envelopeHLZ) and back (detector-
	// side, local z = +envelopeHLZ) mother faces, in global (x,y). Derived in closed form from the
	// exact same rotation this class applies at placement (rotateY(thetaX) then rotateX(thetaY),
	// G4's daughter-rotation-is-the-inverse convention): X(z) = xPos + z*tan(thetaX) exactly;
	// Y(z) = yPos + z*tan(thetaY)/cos(thetaX) (the 1/cos(thetaX) cross-term is a real, if usually
	// small, consequence of composing the two tilts in this order -- it is the existing, unchanged
	// convention, not something introduced by this fix). These are reported below as an intuitive
	// "septal thickness at the face" number, but they are NOT by themselves the enforced safety
	// check -- see the axis-segment clearance below for why.
	std::vector<std::vector<G4double>> frontX(fNHolesY, std::vector<G4double>(fNHolesX));
	std::vector<std::vector<G4double>> frontY(fNHolesY, std::vector<G4double>(fNHolesX));
	std::vector<std::vector<G4double>> backX(fNHolesY, std::vector<G4double>(fNHolesX));
	std::vector<std::vector<G4double>> backY(fNHolesY, std::vector<G4double>(fNHolesX));

	// Per-hole central axis, as a 3D point (its untilted centre, always at local/global z = 0) and a
	// unit direction (the local z-axis after the exact placement rotation, same closed form as
	// above generalized off-axis: direction = (cos(thetaY)*sin(thetaX), sin(thetaY),
	// cos(thetaX)*cos(thetaY))), plus the exact per-hole half-length used to build its solid. Used
	// below to find the true minimum clearance between neighbouring channels ANYWHERE along their
	// tilted length, not just at the two face planes -- a tilted channel and its similarly-tilted
	// neighbour run nearly parallel over their full length, so their closest approach is not
	// guaranteed to be at either face (confirmed by direct G4 overlap testing during development:
	// the actual intersection this check is designed to catch occurred mid-channel, near one rim,
	// not exactly at a face plane).
	std::vector<std::vector<G4ThreeVector>> axisPos(fNHolesY, std::vector<G4ThreeVector>(fNHolesX));
	std::vector<std::vector<G4ThreeVector>> axisDir(fNHolesY, std::vector<G4ThreeVector>(fNHolesX));
	std::vector<std::vector<G4double>> axisHalfLen(fNHolesY, std::vector<G4double>(fNHolesX));

	G4int copyNo = 0;
	for (G4int iy = 0; iy < fNHolesY; iy++) {
		G4double yPos = (iy - (fNHolesY - 1) / 2.0) * rowSpacing;
		G4double xOffset = (hexLattice && (iy % 2 != 0)) ? pitch / 2.0 : 0.0;
		for (G4int ix = 0; ix < fNHolesX; ix++) {
			G4double xPos = (ix - (fNHolesX - 1) / 2.0) * pitch + xOffset;

			G4double thetaX = (fFocalLengthX != 0.0) ? std::atan(xPos / fFocalLengthX) : 0.0;
			G4double thetaY = (fFocalLengthY != 0.0) ? std::atan(yPos / fFocalLengthY) : 0.0;

			frontX[iy][ix] = xPos + (-envelopeHLZ) * std::tan(thetaX);
			backX[iy][ix]  = xPos + envelopeHLZ * std::tan(thetaX);
			frontY[iy][ix] = yPos + (-envelopeHLZ) * std::tan(thetaY) / std::cos(thetaX);
			backY[iy][ix]  = yPos + envelopeHLZ * std::tan(thetaY) / std::cos(thetaX);

			G4double safeHalfZ = SafeHoleHalfZ(thetaX, thetaY);
			axisPos[iy][ix] = G4ThreeVector(xPos, yPos, 0.0);
			axisDir[iy][ix] = G4ThreeVector(std::cos(thetaY) * std::sin(thetaX),
											 std::sin(thetaY),
											 std::cos(thetaX) * std::cos(thetaY));
			axisHalfLen[iy][ix] = safeHalfZ;

			G4ThreeVector* pos = new G4ThreeVector(xPos, yPos, 0.0);
			RegisterTranslation(pos);

			// Tilt each channel toward the focus (axis through the focal point/line). Negative
			// focal length diverges. G4 placement takes the inverse rotation of the daughter.
			G4RotationMatrix* rot = 0;
			if (converging) {
				rot = new G4RotationMatrix();
				if (fFocalLengthX != 0.0) rot->rotateY(-thetaX);
				if (fFocalLengthY != 0.0) rot->rotateX( thetaY);
				RegisterRotation(rot);
			}

			// Each channel gets ITS OWN exact safe half-length -- the dominant fix. Previously every
			// channel reused a single length sized from the single worst-case (outermost) hole,
			// which made every less-tilted channel protrude through the envelope faces.
			G4LogicalVolume* holeLog = GetHoleLogical(safeHalfZ);

			CreatePhysicalVolume("Hole", copyNo, true, holeLog, rot, pos, fEnvelopePhys);
			copyNo++;
		}
	}

	// Guard-rail diagnostic. The fix above guarantees (by construction) that no channel's end-cap
	// rim can protrude past the envelope's z-faces. It does NOT, by itself, guarantee that adjacent
	// channels don't intersect EACH OTHER -- and, importantly, a tilted channel and its similarly-
	// tilted neighbour run nearly parallel over their full length, so their closest approach is not
	// guaranteed to fall at either face; it can fall anywhere along the shared length (confirmed by
	// direct G4 overlap testing during development). So two complementary checks are done, over
	// every nearest-neighbour pair on the lattice (same row, same column, and both diagonals -- a
	// superset that covers the true nearest neighbours of both the square and the hex/triangular
	// lattice):
	//  (1) an intuitive, face-only "local septal thickness" estimate (centre-to-centre spacing at
	//      each face minus the nominal HoleDiameter) -- informational, reported for both faces.
	//  (2) the ENFORCED check: model each channel as a "capsule" (its central axis segment, from
	//      rim to rim, thickened by the hole's nominal across-flats width) and compute the true
	//      minimum distance between the two finite axis SEGMENTS (not infinite lines, and not
	//      evaluated only at the two faces) using the standard closest-point-between-two-segments
	//      construction. Two channels cannot intersect if this axis-segment distance exceeds the sum
	//      of their footprint half-widths, so fHoleDiameter (== 2x half-width) is subtracted to get
	//      an inter-channel clearance. fHoleDiameter, not the circumscribed radius, is the correct
	//      reference here: the lattice is built so every nearest-neighbour direction is flat-to-flat
	//      (that is the entire point of a hex-on-hex or square-on-square lattice), so the
	//      circumscribed radius (corner-to-corner) would UNDER-estimate clearance and false-positive
	//      even on the untilted, validated parallel-hole geometry (caught empirically: a first
	//      version of this check used the circumradius and incorrectly flagged the default,
	//      non-converging case). This is what is actually enforced below.
	G4double minSpacingFront = std::numeric_limits<G4double>::max();
	G4double minSpacingBack  = std::numeric_limits<G4double>::max();
	G4double minAxisClearance = std::numeric_limits<G4double>::max();
	const G4int dxs[4] = {1, 0, 1, -1};
	const G4int dys[4] = {0, 1, 1,  1};

	// Closest squared distance between two finite 3D segments [p1,q1] and [p2,q2] (Ericson,
	// "Real-Time Collision Detection" Sec. 5.1.9).
	auto ClosestSegSegDistSq = [](const G4ThreeVector& p1, const G4ThreeVector& q1,
								   const G4ThreeVector& p2, const G4ThreeVector& q2) -> G4double {
		const G4double eps = 1e-15;
		G4ThreeVector d1 = q1 - p1;
		G4ThreeVector d2 = q2 - p2;
		G4ThreeVector r  = p1 - p2;
		G4double a = d1.dot(d1);
		G4double e = d2.dot(d2);
		G4double f = d2.dot(r);
		G4double s, t;
		if (a <= eps && e <= eps) {
			s = t = 0.0;
		} else if (a <= eps) {
			s = 0.0;
			t = std::clamp(f / e, 0.0, 1.0);
		} else {
			G4double c = d1.dot(r);
			if (e <= eps) {
				t = 0.0;
				s = std::clamp(-c / a, 0.0, 1.0);
			} else {
				G4double b = d1.dot(d2);
				G4double denom = a * e - b * b;
				s = (denom != 0.0) ? std::clamp((b * f - c * e) / denom, 0.0, 1.0) : 0.0;
				t = (b * s + f) / e;
				if (t < 0.0) { t = 0.0; s = std::clamp(-c / a, 0.0, 1.0); }
				else if (t > 1.0) { t = 1.0; s = std::clamp((b - c) / a, 0.0, 1.0); }
			}
		}
		G4ThreeVector c1 = p1 + d1 * s;
		G4ThreeVector c2 = p2 + d2 * t;
		return (c1 - c2).mag2();
	};

	for (G4int iy = 0; iy < fNHolesY; iy++) {
		for (G4int ix = 0; ix < fNHolesX; ix++) {
			for (G4int k = 0; k < 4; k++) {
				G4int jx = ix + dxs[k];
				G4int jy = iy + dys[k];
				if (jx < 0 || jx >= fNHolesX || jy < 0 || jy >= fNHolesY) continue;

				G4double dFrontX = frontX[jy][jx] - frontX[iy][ix];
				G4double dFrontY = frontY[jy][jx] - frontY[iy][ix];
				G4double dBackX  = backX[jy][jx]  - backX[iy][ix];
				G4double dBackY  = backY[jy][jx]  - backY[iy][ix];
				minSpacingFront = std::min(minSpacingFront, std::sqrt(dFrontX * dFrontX + dFrontY * dFrontY));
				minSpacingBack  = std::min(minSpacingBack,  std::sqrt(dBackX * dBackX + dBackY * dBackY));

				G4ThreeVector p1 = axisPos[iy][ix] - axisHalfLen[iy][ix] * axisDir[iy][ix];
				G4ThreeVector q1 = axisPos[iy][ix] + axisHalfLen[iy][ix] * axisDir[iy][ix];
				G4ThreeVector p2 = axisPos[jy][jx] - axisHalfLen[jy][jx] * axisDir[jy][jx];
				G4ThreeVector q2 = axisPos[jy][jx] + axisHalfLen[jy][jx] * axisDir[jy][jx];
				G4double axisDist = std::sqrt(ClosestSegSegDistSq(p1, q1, p2, q2));
				minAxisClearance = std::min(minAxisClearance, axisDist - fHoleDiameter);
			}
		}
	}
	G4double minSeptalFront = minSpacingFront - fHoleDiameter;
	G4double minSeptalBack  = minSpacingBack  - fHoleDiameter;

	if (minAxisClearance <= 0.0) {
		std::ostringstream msg;
		msg << "TsParallelHoleCollimator: converging geometry makes adjacent channels intersect.\n"
			<< "  Minimum inter-channel clearance (axis-segment, enforced check) = " << minAxisClearance / mm << " mm\n"
			<< "  Minimum estimated septal thickness at front (object-side) face  = " << minSeptalFront / mm << " mm\n"
			<< "  Minimum estimated septal thickness at back (detector-side) face = " << minSeptalBack / mm << " mm\n"
			<< "  Nominal SeptalThickness = " << fSeptalThickness / mm << " mm\n"
			<< "Reduce NHolesX/NHolesY, increase SeptalThickness, or use a longer FocalLengthX/FocalLengthY.";
		OutOfRange(GetFullParmName("SeptalThickness"), msg.str());
	}

	G4String shapeName = hexHole ? "hex" : (squareHole ? "square" : "round");
	G4String focusName = converging ? ((fFocalLengthX != 0.0 && fFocalLengthY != 0.0) ? "cone-beam"
									 : "fan-beam") : "parallel";
	G4cout << "TsParallelHoleCollimator: Created " << copyNo << " " << shapeName << " holes ("
		   << fNHolesX << " x " << fNHolesY << ") on a " << (hexLattice ? "hex" : "square")
		   << " lattice, " << focusName << ", pitch = " << pitch / mm << " mm, "
		   << holeLogCache.size() << " unique channel length(s)" << G4endl;
	if (converging) {
		G4cout << "TsParallelHoleCollimator: minimum inter-channel clearance (enforced) = "
			   << minAxisClearance / mm << " mm; minimum estimated septal thickness (informational) = "
			   << minSeptalFront / mm << " mm (front/object-side face), "
			   << minSeptalBack / mm << " mm (back/detector-side face); nominal = "
			   << fSeptalThickness / mm << " mm" << G4endl;
	}

	InstantiateChildren(fEnvelopePhys);
	return fEnvelopePhys;
}
