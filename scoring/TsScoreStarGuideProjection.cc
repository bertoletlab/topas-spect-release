// Scorer for StarGuideProjection
//
// Ntuple scorer for StarGuide SPECT projections with Gaussian energy smearing.
// Two operating modes controlled by the OutputWeights parameter:
//
// OutputWeights = false (default, backward-compatible):
//   Accumulates edep*weight per pixel per event. One ntuple row per pixel.
//   4-column output: PixelID, TrueEnergy, SmearedEnergy, DetectorID.
//
// OutputWeights = true (for bremsstrahlung splitting / variance reduction):
//   Accumulates TRUE energy (no weight multiplication) per (pixel, track).
//   After each event, smears and windows each track's energy independently,
//   then writes one ntuple row per accepted track with the particle weight.
//   5-column output: PixelID, TrueEnergy, SmearedEnergy, DetectorID, Weight.
//
// ListMode = true (for post-hoc acquisition parameter sweeps):
//   Writes ALL detected events without energy smearing or window filtering.
//   Implies OutputWeights = true. SmearedEnergy is set equal to TrueEnergy.
//   Post-processing in Python applies configurable smearing + windowing,
//   enabling energy window / resolution sweeps from a single MC simulation.
//
// Why per-track in weighted mode? A single photon can deposit energy in
// multiple Geant4 steps (e.g. photoelectric + Auger cascades). These steps
// belong to the same track and must be summed before smearing (the detector
// sees total deposited energy). But split brem photons from the same event
// are separate tracks with weight 1/N and must NOT be mixed.

#include "TsScoreStarGuideProjection.hh"

#include "TsParameterManager.hh"
#include "TsVGeometryComponent.hh"
#include "G4Step.hh"
#include "G4TouchableHistory.hh"
#include "G4SystemOfUnits.hh"
#include "Randomize.hh"

#include <cmath>

TsScoreStarGuideProjection::TsScoreStarGuideProjection(TsParameterManager* pM, TsMaterialManager* mM,
													   TsGeometryManager* gM, TsScoringManager* scM,
													   TsExtensionManager* eM,
													   G4String scorerName, G4String quantity,
													   G4String outFileName, G4bool isSubScorer)
: TsVNtupleScorer(pM, mM, gM, scM, eM, scorerName, quantity, outFileName, isSubScorer)
{
	// Read pixel geometry from the scored component's parameters
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

	// Read energy resolution calibration vectors
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

	// Energy window (keV)
	fEnergyWindowLow = 126.5 * keV;
	if (fPm->ParameterExists(GetFullParmName("EnergyWindowLow")))
		fEnergyWindowLow = fPm->GetDoubleParameter(GetFullParmName("EnergyWindowLow"), "Energy");

	fEnergyWindowHigh = 154.6 * keV;
	if (fPm->ParameterExists(GetFullParmName("EnergyWindowHigh")))
		fEnergyWindowHigh = fPm->GetDoubleParameter(GetFullParmName("EnergyWindowHigh"), "Energy");

	// Detector ID for multi-head identification
	fDetectorIDValue = 0;
	if (fPm->ParameterExists(GetFullParmName("DetectorID")))
		fDetectorIDValue = fPm->GetIntegerParameter(GetFullParmName("DetectorID"));

	// OutputWeights mode for bremsstrahlung splitting / variance reduction
	fOutputWeights = false;
	if (fPm->ParameterExists(GetFullParmName("OutputWeights")))
		fOutputWeights = fPm->GetBooleanParameter(GetFullParmName("OutputWeights"));

	// ListMode: write all events without smearing or windowing
	fListMode = false;
	if (fPm->ParameterExists(GetFullParmName("ListMode")))
		fListMode = fPm->GetBooleanParameter(GetFullParmName("ListMode"));

	// ListMode implies weighted output (post-processing needs weights)
	if (fListMode)
		fOutputWeights = true;

	// Register ntuple columns
	fNtuple->RegisterColumnI(&fPixelID, "PixelID");
	fNtuple->RegisterColumnF(&fTrueEnergy, "TrueEnergy", "keV");
	fNtuple->RegisterColumnF(&fSmearedEnergy, "SmearedEnergy", "keV");
	fNtuple->RegisterColumnI(&fDetectorID, "DetectorID");
	if (fOutputWeights)
		fNtuple->RegisterColumnF(&fWeight, "Weight", "");
}


TsScoreStarGuideProjection::~TsScoreStarGuideProjection()
{;}


G4int TsScoreStarGuideProjection::ComputePixelID(G4double localX, G4double localY)
{
	// Convert local position to pixel indices
	G4int ix = (G4int)std::floor((localX + fDetectorHLX) / fPixelSizeX);
	G4int iy = (G4int)std::floor((localY + fDetectorHLY) / fPixelSizeY);

	// Clamp to valid range
	if (ix < 0) ix = 0;
	if (ix >= fNPixelsX) ix = fNPixelsX - 1;
	if (iy < 0) iy = 0;
	if (iy >= fNPixelsY) iy = fNPixelsY - 1;

	return ix * fNPixelsY + iy;
}


G4bool TsScoreStarGuideProjection::ProcessHits(G4Step* aStep, G4TouchableHistory*)
{
	if (!fIsActive) {
		fSkippedWhileInactive++;
		return false;
	}

	G4double edep = aStep->GetTotalEnergyDeposit();
	if (edep <= 0) return false;

	// Get hit position in local coordinates of the detector
	G4StepPoint* preStepPoint = aStep->GetPreStepPoint();
	G4ThreeVector worldPos = preStepPoint->GetPosition();
	G4ThreeVector localPos = preStepPoint->GetTouchableHandle()
		->GetHistory()->GetTopTransform().TransformPoint(worldPos);

	G4int pixelID = ComputePixelID(localPos.x(), localPos.y());

	if (fOutputWeights) {
		// Weighted mode: accumulate TRUE energy per (pixel, track).
		// Do NOT multiply by weight here — weight is recorded separately.
		G4int trackID = aStep->GetTrack()->GetTrackID();
		G4double weight = preStepPoint->GetWeight();
		auto key = std::make_pair(pixelID, trackID);
		auto& entry = fWeightedPixelTrack[key];
		entry.first += edep / keV;   // accumulate true energy in keV
		entry.second = weight;       // weight is constant per track
	} else {
		// Legacy mode: accumulate weighted energy per pixel
		G4double weight = preStepPoint->GetWeight();
		edep *= weight;
		fPixelEdep[pixelID] += edep;
	}

	return true;
}


void TsScoreStarGuideProjection::UserHookForEndOfEvent()
{
	G4double windowLow_keV = fEnergyWindowLow / keV;
	G4double windowHigh_keV = fEnergyWindowHigh / keV;

	if (fOutputWeights) {
		// Weighted mode: process each track independently, write with weight
		for (auto& entry : fWeightedPixelTrack) {
			G4int pixelCopyNo = entry.first.first;
			G4double trueEdep_keV = entry.second.first;
			G4double weight = entry.second.second;

			if (trueEdep_keV <= 0) continue;

			if (fListMode) {
				// List-mode: write raw event — no smearing, no windowing
				fPixelID = pixelCopyNo;
				fTrueEnergy = (G4float)(trueEdep_keV * keV);
				fSmearedEnergy = (G4float)(trueEdep_keV * keV);  // no smearing
				fDetectorID = fDetectorIDValue;
				fWeight = (G4float)weight;
				fNtuple->Fill();
			} else {
				// Apply Gaussian energy smearing to TRUE energy
				G4double fracFWHM = InterpolateFWHM(trueEdep_keV);
				G4double sigma_keV = fracFWHM * trueEdep_keV / (2.0 * std::sqrt(2.0 * std::log(2.0)));
				G4double smearedEdep_keV = G4RandGauss::shoot(trueEdep_keV, sigma_keV);

				// Apply energy window filter on smeared TRUE energy
				if (smearedEdep_keV >= windowLow_keV && smearedEdep_keV <= windowHigh_keV) {
					fPixelID = pixelCopyNo;
					fTrueEnergy = (G4float)(trueEdep_keV * keV);
					fSmearedEnergy = (G4float)(smearedEdep_keV * keV);
					fDetectorID = fDetectorIDValue;
					fWeight = (G4float)weight;
					fNtuple->Fill();
				}
			}
		}
		fWeightedPixelTrack.clear();
	} else {
		// Legacy mode: original behavior (backward-compatible)
		for (auto& pair : fPixelEdep) {
			G4int pixelCopyNo = pair.first;
			G4double trueEdep_keV = pair.second / keV;

			if (trueEdep_keV <= 0) continue;

			// Apply Gaussian energy smearing
			G4double fracFWHM = InterpolateFWHM(trueEdep_keV);
			G4double sigma_keV = fracFWHM * trueEdep_keV / (2.0 * std::sqrt(2.0 * std::log(2.0)));
			G4double smearedEdep_keV = G4RandGauss::shoot(trueEdep_keV, sigma_keV);

			// Apply energy window filter
			if (smearedEdep_keV >= windowLow_keV && smearedEdep_keV <= windowHigh_keV) {
				fPixelID = pixelCopyNo;
				// Store in Geant4 internal units (MeV); RegisterColumnF with "keV"
				// tells TOPAS to divide by the keV constant when writing to file.
				fTrueEnergy = (G4float)(trueEdep_keV * keV);
				fSmearedEnergy = (G4float)(smearedEdep_keV * keV);
				fDetectorID = fDetectorIDValue;
				fNtuple->Fill();
			}
		}
		fPixelEdep.clear();
	}
}


G4double TsScoreStarGuideProjection::InterpolateFWHM(G4double energy_keV)
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
