// Scorer for ForcedDetectionProjection
//
// Forced detection variance reduction scorer for SPECT projections.
// Instead of tracking photons through the collimator (where >99.99% are
// absorbed), this scorer sits on a thin vacuum surface at the collimator
// face position. For each photon crossing the surface, it:
//   1. Computes analytical Metz-Frey collimator transmission T(theta)
//   2. Applies Gaussian energy smearing to the REAL photon energy E_k
//   3. Checks the energy window on the smeared energy
//   4. Records accepted photons with Weight = T (transmission probability)
//
// Output ntuple: PixelID, TrueEnergy, SmearedEnergy, DetectorID, Weight
//
// Downstream parsers must SUM the Weight column per pixel to build projections.
// This gives ~1,000-5,000x efficiency gain over analog tracking.
//
// Reference: Metz CE, Frey EC. "Geometric collimator efficiency..."

#include "TsForcedDetectionProjection.hh"

#include "TsParameterManager.hh"
#include "TsVGeometryComponent.hh"
#include "G4Step.hh"
#include "G4StepPoint.hh"
#include "G4Track.hh"
#include "G4Gamma.hh"
#include "G4SystemOfUnits.hh"
#include "Randomize.hh"

#include <cmath>

TsForcedDetectionProjection::TsForcedDetectionProjection(
	TsParameterManager* pM, TsMaterialManager* mM,
	TsGeometryManager* gM, TsScoringManager* scM,
	TsExtensionManager* eM,
	G4String scorerName, G4String quantity,
	G4String outFileName, G4bool isSubScorer)
: TsVNtupleScorer(pM, mM, gM, scM, eM, scorerName, quantity, outFileName, isSubScorer)
{
	// --- Collimator geometry ---
	fHoleDiameter = 1.08 * mm;
	if (fPm->ParameterExists(GetFullParmName("HoleDiameter")))
		fHoleDiameter = fPm->GetDoubleParameter(GetFullParmName("HoleDiameter"), "Length");

	fSeptalThickness = 1.38 * mm;
	if (fPm->ParameterExists(GetFullParmName("SeptalThickness")))
		fSeptalThickness = fPm->GetDoubleParameter(GetFullParmName("SeptalThickness"), "Length");

	fCollimatorLength = 25.0 * mm;
	if (fPm->ParameterExists(GetFullParmName("CollimatorLength")))
		fCollimatorLength = fPm->GetDoubleParameter(GetFullParmName("CollimatorLength"), "Length");

	// Geometric hole fraction: f = pi*(d/2)^2 / (d+t)^2
	G4double pitch = fHoleDiameter + fSeptalThickness;
	fHoleFraction = M_PI * (fHoleDiameter / 2.0) * (fHoleDiameter / 2.0) / (pitch * pitch);

	if (fPm->ParameterExists(GetFullParmName("HoleFraction")))
		fHoleFraction = fPm->GetUnitlessParameter(GetFullParmName("HoleFraction"));

	// --- FD-specific parameters ---
	fIncludeSeptalPenetration = false;
	if (fPm->ParameterExists(GetFullParmName("IncludeSeptalPenetration")))
		fIncludeSeptalPenetration = fPm->GetBooleanParameter(GetFullParmName("IncludeSeptalPenetration"));

	fSeptalMuPerMM = 3.56 / mm;  // Tungsten attenuation at 140.5 keV
	if (fPm->ParameterExists(GetFullParmName("SeptalMuPerMM")))
		fSeptalMuPerMM = fPm->GetUnitlessParameter(GetFullParmName("SeptalMuPerMM")) / mm;

	// --- Virtual detector plane offset ---
	// Distance from FD surface center to virtual detector = collimator half-length + CZT half-thickness
	G4double cztThickness = 5.0 * mm;
	if (fPm->ParameterExists(GetFullParmName("CZTThickness")))
		cztThickness = fPm->GetDoubleParameter(GetFullParmName("CZTThickness"), "Length");
	fDetectorOffset = fCollimatorLength / 2.0 + cztThickness / 2.0;

	// --- Pixel geometry (identical parameters to analog scorer) ---
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

	fDetectorHLX = fNPixelsX * fPixelSizeX / 2.0;
	fDetectorHLY = fNPixelsY * fPixelSizeY / 2.0;

	// --- Energy resolution calibration vectors ---
	G4int nResPoints = 0;
	if (fPm->ParameterExists(GetFullParmName("ResolutionEnergies"))) {
		G4double* energies = fPm->GetDoubleVector(GetFullParmName("ResolutionEnergies"), "Energy");
		nResPoints = fPm->GetVectorLength(GetFullParmName("ResolutionEnergies"));
		for (G4int i = 0; i < nResPoints; i++)
			fResolutionEnergies.push_back(energies[i] / keV);
	} else {
		fResolutionEnergies.push_back(140.5);
		fResolutionEnergies.push_back(208.0);
	}

	if (fPm->ParameterExists(GetFullParmName("ResolutionFractionalFWHM"))) {
		G4double* fwhms = fPm->GetUnitlessVector(GetFullParmName("ResolutionFractionalFWHM"));
		G4int nFWHM = fPm->GetVectorLength(GetFullParmName("ResolutionFractionalFWHM"));
		for (G4int i = 0; i < nFWHM; i++)
			fResolutionFWHM.push_back(fwhms[i]);
	} else {
		fResolutionFWHM.push_back(0.055);
		fResolutionFWHM.push_back(0.046);
	}

	// --- Energy window ---
	fEnergyWindowLow = 126.5 * keV;
	if (fPm->ParameterExists(GetFullParmName("EnergyWindowLow")))
		fEnergyWindowLow = fPm->GetDoubleParameter(GetFullParmName("EnergyWindowLow"), "Energy");

	fEnergyWindowHigh = 154.6 * keV;
	if (fPm->ParameterExists(GetFullParmName("EnergyWindowHigh")))
		fEnergyWindowHigh = fPm->GetDoubleParameter(GetFullParmName("EnergyWindowHigh"), "Energy");

	// --- Detector ID ---
	fDetectorIDValue = 0;
	if (fPm->ParameterExists(GetFullParmName("DetectorID")))
		fDetectorIDValue = fPm->GetIntegerParameter(GetFullParmName("DetectorID"));

	// ListMode: write all photons without smearing or windowing
	fListMode = false;
	if (fPm->ParameterExists(GetFullParmName("ListMode")))
		fListMode = fPm->GetBooleanParameter(GetFullParmName("ListMode"));

	// Register ntuple columns: 5 columns (4 standard + Weight)
	fNtuple->RegisterColumnI(&fPixelID, "PixelID");
	fNtuple->RegisterColumnF(&fTrueEnergy, "TrueEnergy", "keV");
	fNtuple->RegisterColumnF(&fSmearedEnergy, "SmearedEnergy", "keV");
	fNtuple->RegisterColumnI(&fDetectorID, "DetectorID");
	fNtuple->RegisterColumnF(&fWeight, "Weight", "");

	G4cout << "TsForcedDetectionProjection: HoleDiameter=" << fHoleDiameter/mm
		   << "mm, CollimatorLength=" << fCollimatorLength/mm
		   << "mm, HoleFraction=" << fHoleFraction
		   << ", SeptalPenetration=" << (fIncludeSeptalPenetration ? "ON" : "OFF")
		   << G4endl;
}


TsForcedDetectionProjection::~TsForcedDetectionProjection()
{;}


G4double TsForcedDetectionProjection::ComputeTransmission(G4double cosTheta)
{
	// Metz-Frey geometric transmission model for parallel-hole collimator.
	//
	// T_geo(theta) = f * [max(0, d*cos(theta) - 2L*sin(theta)) / d]^2
	//
	// where f is the geometric hole fraction, d is hole diameter,
	// L is collimator length, theta is angle to collimator axis.

	if (cosTheta <= 0.0) return 0.0;

	G4double sinTheta = std::sqrt(1.0 - cosTheta * cosTheta);

	// Geometric (open-hole) transmission
	G4double numerator = fHoleDiameter * cosTheta - 2.0 * fCollimatorLength * sinTheta;
	G4double T_geo = 0.0;

	if (numerator > 0.0) {
		G4double ratio = numerator / fHoleDiameter;
		T_geo = fHoleFraction * ratio * ratio;
	}

	if (!fIncludeSeptalPenetration)
		return T_geo;

	// Optional septal penetration correction:
	// T_pen = f * (1 - geo_fraction) * exp(-mu * t / sin(theta))
	// Adds transmission through septa for photons outside the geometric cone.
	if (sinTheta < 1e-10) return T_geo;  // near-normal: no penetration path

	G4double geo_fraction = (numerator > 0.0) ?
		(numerator / fHoleDiameter) * (numerator / fHoleDiameter) : 0.0;

	G4double T_pen = fHoleFraction * (1.0 - geo_fraction) *
		std::exp(-fSeptalMuPerMM * fSeptalThickness / sinTheta);

	return T_geo + T_pen;
}


G4int TsForcedDetectionProjection::ComputePixelID(G4double localX, G4double localY)
{
	G4int ix = (G4int)std::floor((localX + fDetectorHLX) / fPixelSizeX);
	G4int iy = (G4int)std::floor((localY + fDetectorHLY) / fPixelSizeY);

	if (ix < 0) ix = 0;
	if (ix >= fNPixelsX) ix = fNPixelsX - 1;
	if (iy < 0) iy = 0;
	if (iy >= fNPixelsY) iy = fNPixelsY - 1;

	return ix * fNPixelsY + iy;
}


G4bool TsForcedDetectionProjection::ProcessHits(G4Step* aStep, G4TouchableHistory*)
{
	if (!fIsActive) {
		fSkippedWhileInactive++;
		return false;
	}

	// Only score boundary crossings (photons entering the FD surface)
	G4StepPoint* preStepPoint = aStep->GetPreStepPoint();
	if (preStepPoint->GetStepStatus() != fGeomBoundary) return false;

	// Only gammas
	if (aStep->GetTrack()->GetDefinition() != G4Gamma::GammaDefinition()) return false;

	// Get kinetic energy and weight
	G4double E_k = preStepPoint->GetKineticEnergy();
	if (E_k <= 0.0) return false;

	G4double weight = preStepPoint->GetWeight();

	// Transform momentum direction to local coordinates
	// Local Z axis points along the collimator bore (radially outward)
	G4ThreeVector worldDir = preStepPoint->GetMomentumDirection();
	G4AffineTransform transform = preStepPoint->GetTouchableHandle()
		->GetHistory()->GetTopTransform();
	G4ThreeVector localDir = transform.TransformAxis(worldDir);

	// cosTheta is the component along local Z (collimator axis)
	// Positive localZ means photon is traveling INTO the collimator (toward detector)
	G4double cosTheta = localDir.z();

	if (cosTheta <= 0.0) {
		// Photon going away from detector — kill it, it can't contribute
		aStep->GetTrack()->SetTrackStatus(fStopAndKill);
		return false;
	}

	// Compute analytical collimator transmission
	G4double T = ComputeTransmission(cosTheta);
	if (T <= 0.0) {
		aStep->GetTrack()->SetTrackStatus(fStopAndKill);
		return false;
	}

	// Project hit position through collimator to virtual detector plane
	G4ThreeVector worldPos = preStepPoint->GetPosition();
	G4ThreeVector localPos = transform.TransformPoint(worldPos);

	// Project position from FD surface to detector plane
	// The FD surface is at the collimator center; the detector plane is
	// fDetectorOffset further along +Z (local)
	G4double projX = localPos.x() + localDir.x() / localDir.z() * fDetectorOffset;
	G4double projY = localPos.y() + localDir.y() / localDir.z() * fDetectorOffset;

	// Check if projected position falls within detector active area.
	// Use >= to reject photons exactly at the boundary (prevents clamping
	// artifacts where boundary photons accumulate on edge pixels).
	if (std::abs(projX) >= fDetectorHLX || std::abs(projY) >= fDetectorHLY) {
		aStep->GetTrack()->SetTrackStatus(fStopAndKill);
		return false;
	}

	G4int pixelID = ComputePixelID(projX, projY);

	G4double E_keV = E_k / keV;

	if (fListMode) {
		// List-mode: record raw hit — no smearing, no windowing
		FDHitEntry entry;
		entry.pixelID = pixelID;
		entry.trueEnergy_keV = E_keV;
		entry.smearedEnergy_keV = E_keV;  // no smearing
		entry.weight = weight * T;
		fEventHits.push_back(entry);
	} else {
		// Apply energy smearing to the REAL photon energy (not T*E_k)
		G4double fracFWHM = InterpolateFWHM(E_keV);
		G4double sigma_keV = fracFWHM * E_keV / (2.0 * std::sqrt(2.0 * std::log(2.0)));
		G4double smearedE_keV = G4RandGauss::shoot(E_keV, sigma_keV);

		// Apply energy window to the smeared PHOTON energy
		G4double windowLow_keV = fEnergyWindowLow / keV;
		G4double windowHigh_keV = fEnergyWindowHigh / keV;

		if (smearedE_keV >= windowLow_keV && smearedE_keV <= windowHigh_keV) {
			// Buffer this hit for end-of-event writing
			FDHitEntry entry;
			entry.pixelID = pixelID;
			entry.trueEnergy_keV = E_keV;
			entry.smearedEnergy_keV = smearedE_keV;
			entry.weight = weight * T;
			fEventHits.push_back(entry);
		}
	}

	// Kill the track — this photon has been scored
	aStep->GetTrack()->SetTrackStatus(fStopAndKill);

	return true;
}


void TsForcedDetectionProjection::UserHookForEndOfEvent()
{
	// Write one ntuple row per accepted photon hit
	for (const auto& hit : fEventHits) {
		fPixelID = hit.pixelID;
		fTrueEnergy = (G4float)(hit.trueEnergy_keV * keV);
		fSmearedEnergy = (G4float)(hit.smearedEnergy_keV * keV);
		fDetectorID = fDetectorIDValue;
		fWeight = (G4float)(hit.weight);
		fNtuple->Fill();
	}

	fEventHits.clear();
}


G4double TsForcedDetectionProjection::InterpolateFWHM(G4double energy_keV)
{
	if (fResolutionEnergies.size() < 2)
		return fResolutionFWHM.empty() ? 0.05 : fResolutionFWHM[0];

	if (energy_keV <= fResolutionEnergies[0])
		return fResolutionFWHM[0];

	if (energy_keV >= fResolutionEnergies.back())
		return fResolutionFWHM.back();

	for (size_t i = 0; i < fResolutionEnergies.size() - 1; i++) {
		if (energy_keV >= fResolutionEnergies[i] && energy_keV <= fResolutionEnergies[i + 1]) {
			G4double frac = (energy_keV - fResolutionEnergies[i]) /
							(fResolutionEnergies[i + 1] - fResolutionEnergies[i]);
			return fResolutionFWHM[i] + frac * (fResolutionFWHM[i + 1] - fResolutionFWHM[i]);
		}
	}

	return fResolutionFWHM.back();
}
