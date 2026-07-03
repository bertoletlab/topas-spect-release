#ifndef TsScoreStarGuideProjection_hh
#define TsScoreStarGuideProjection_hh

#include "TsVNtupleScorer.hh"
#include <map>
#include <utility>

class TsScoreStarGuideProjection : public TsVNtupleScorer
{
public:
	TsScoreStarGuideProjection(TsParameterManager* pM, TsMaterialManager* mM, TsGeometryManager* gM,
							   TsScoringManager* scM, TsExtensionManager* eM,
							   G4String scorerName, G4String quantity,
							   G4String outFileName, G4bool isSubScorer);

	~TsScoreStarGuideProjection();

	G4bool ProcessHits(G4Step*, G4TouchableHistory*);
	void UserHookForEndOfEvent();

private:
	// Per-event energy accumulation (legacy mode): pixelID -> deposited energy
	std::map<G4int, G4double> fPixelEdep;

	// Per-track accumulation for weighted particles (brem splitting).
	// Key: (pixelID, trackID), Value: (accumulated_edep_keV, weight)
	// A single photon can produce multiple Geant4 steps in CZT (e.g.
	// photoelectric + Auger), which must be summed before smearing.
	// Split photons from the same event are separate tracks and must NOT mix.
	std::map<std::pair<G4int, G4int>, std::pair<G4double, G4double>> fWeightedPixelTrack;

	// OutputWeights mode: true when b:Sc/.../OutputWeights = "True"
	G4bool fOutputWeights;

	// ListMode: write ALL events without smearing or windowing.
	// Enables post-hoc energy window / resolution sweeps in Python.
	G4bool fListMode;

	// Pixel geometry for position-based ID computation
	G4double fPixelSizeX;
	G4double fPixelSizeY;
	G4int fNPixelsX;
	G4int fNPixelsY;
	G4double fDetectorHLX;
	G4double fDetectorHLY;

	// Energy resolution model: piecewise from parameter vectors
	std::vector<G4double> fResolutionEnergies;
	std::vector<G4double> fResolutionFWHM;

	// Energy window
	G4double fEnergyWindowLow;
	G4double fEnergyWindowHigh;

	// Detector ID (for multi-head setups)
	G4int fDetectorIDValue;

	// Ntuple output columns
	G4int fPixelID;
	G4float fTrueEnergy;
	G4float fSmearedEnergy;
	G4int fDetectorID;
	G4float fWeight;  // 5th column, only registered when fOutputWeights=true

	// Helper: interpolate fractional FWHM at given energy
	G4double InterpolateFWHM(G4double energy_keV);

	// Helper: compute pixel ID from local position
	G4int ComputePixelID(G4double localX, G4double localY);
};

#endif
