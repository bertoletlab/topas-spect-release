// Scorer for ForcedDetectionProjection
//
// Forced detection (variance reduction) scorer for StarGuide SPECT projections.
// Replaces physical collimator+detector MC transport with analytical collimator
// transmission computation. Produces ntuple output with 5 columns:
//   PixelID (int), TrueEnergy (keV), SmearedEnergy (keV), DetectorID (int), Weight (float)
//
// Each photon crossing the FD surface is processed individually:
// 1. Compute analytical collimator transmission T(theta) using Metz-Frey model
// 2. Apply Gaussian energy smearing to the REAL photon energy E_k
// 3. Apply energy window filter to the smeared energy
// 4. If accepted: record with Weight = T * upstream_weight
//
// ListMode = true: skip steps 2-3, record ALL photons with T > 0.
// Post-processing in Python applies configurable smearing + windowing.
//
// The Weight column encodes the transmission probability. Downstream parsers
// must SUM weights per pixel (not count rows) to build the projection.

#ifndef TsForcedDetectionProjection_hh
#define TsForcedDetectionProjection_hh

#include "TsVNtupleScorer.hh"
#include <vector>

class TsForcedDetectionProjection : public TsVNtupleScorer
{
public:
	TsForcedDetectionProjection(TsParameterManager* pM, TsMaterialManager* mM, TsGeometryManager* gM,
								TsScoringManager* scM, TsExtensionManager* eM,
								G4String scorerName, G4String quantity,
								G4String outFileName, G4bool isSubScorer);

	~TsForcedDetectionProjection();

	G4bool ProcessHits(G4Step*, G4TouchableHistory*);
	void UserHookForEndOfEvent();

private:
	// Collimator geometry parameters
	G4double fHoleDiameter;
	G4double fSeptalThickness;
	G4double fCollimatorLength;
	G4double fHoleFraction;

	// FD-specific parameters
	G4bool fIncludeSeptalPenetration;
	G4double fSeptalMuPerMM;

	// Virtual detector geometry
	G4double fDetectorOffset;  // distance from FD surface to virtual detector plane
	G4double fPixelSizeX;
	G4double fPixelSizeY;
	G4int fNPixelsX;
	G4int fNPixelsY;
	G4double fDetectorHLX;
	G4double fDetectorHLY;

	// Energy resolution model
	std::vector<G4double> fResolutionEnergies;
	std::vector<G4double> fResolutionFWHM;

	// Energy window
	G4double fEnergyWindowLow;
	G4double fEnergyWindowHigh;

	// ListMode: write ALL photons without smearing or windowing
	G4bool fListMode;

	// Detector ID
	G4int fDetectorIDValue;

	// Per-event hit buffer: each photon scored individually
	struct FDHitEntry {
		G4int pixelID;
		G4double trueEnergy_keV;
		G4double smearedEnergy_keV;
		G4double weight;  // transmission T * upstream weight
	};
	std::vector<FDHitEntry> fEventHits;

	// Ntuple output columns
	G4int fPixelID;
	G4float fTrueEnergy;
	G4float fSmearedEnergy;
	G4int fDetectorID;
	G4float fWeight;

	// Analytical collimator transmission (Metz-Frey model)
	G4double ComputeTransmission(G4double cosTheta);

	// Helpers (same interface as analog scorer)
	G4double InterpolateFWHM(G4double energy_keV);
	G4int ComputePixelID(G4double localX, G4double localY);
};

#endif
