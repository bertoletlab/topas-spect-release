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
//
// RecordScatterOrder = true: append a 6th column, ScatterOrder (int), that
// separates primary (0, unscattered) from scattered (1, scatter-order >= 1)
// photons so the two can be summed into separate tallies. A photon is flagged
// scattered iff its momentum direction at the FD surface differs from its
// emission (vertex) direction — i.e. it underwent a Compton or Rayleigh
// deflection. Default false keeps the original 5-column format.

#ifndef TsForcedDetectionProjection_hh
#define TsForcedDetectionProjection_hh

#include "TsVNtupleScorer.hh"
#include <map>
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
	void UserHookForEndOfRun();
	void AbsorbResultsFromWorkerScorer(TsVScorer* workerScorer);

private:
	// Collimator geometry parameters
	G4double fHoleDiameter;
	G4double fSeptalThickness;
	G4double fCollimatorLength;
	G4double fHoleFraction;

	// FD-specific parameters
	G4bool fIncludeSeptalPenetration;
	G4double fSeptalMuPerMM;
	G4bool fSeptalMuWasSet;
	G4String fSeptalMaterialName;
	std::vector<G4double> fMuTableEnergy;
	std::vector<G4double> fMuTableValue;
	G4double fCurrentEnergy;
	std::vector<G4double> fMuGrid;
	std::vector<std::vector<G4double> > fTransTable;
	std::map<G4int, G4double> fMuByEnergyBin;

	// Septal-penetration model. "EffectiveLength" (default) keeps the shipped behaviour,
	// L_eff = L - 2/mu inside the Metz overlap, which is accurate where penetration is a
	// near-cutoff grazing effect and returns exactly zero past the cutoff, so it can carry no
	// tail. "RayTrace" instead measures the lead a ray actually crosses by walking it through
	// the hole array, which produces the tail; see BuildTransmissionTable.
	G4String fPenetrationModel;
	G4bool fPenetrationModelWasSet;
	G4double fWeightKept;
	G4double fWeightCut;
	G4String fLattice;              // "Square" or "Hex", matching TsParallelHoleCollimator

	// Tabulated transmission for the ray-trace model, built once at construction and read by
	// interpolation per photon. Empty under "EffectiveLength".
	std::vector<G4double> fTransTanTheta;
	G4double fMinTransmissionFraction;   // floor as a fraction of T(0); 0 disables
	G4double fTransFloor;                // absolute floor, set once the table exists

	// Hole shape (Round, Hex, Square -- see TsParallelHoleCollimator). Only
	// "Round" gets the exact Metz aperture-autocorrelation closed form;
	// non-round shapes fall back to the round-hole formula as a documented
	// approximation (a loud warning is printed at construction).
	G4String fHoleShape;

	// Crystal photopeak-window detection efficiency: multiplicative factor
	// applied on top of geometric transmission, representing the probability
	// that a photon which geometrically reaches the crystal is actually
	// counted in the photopeak energy window (accounts for Compton escape,
	// K-escape, incomplete absorption, etc. -- NOT the same as the Gaussian
	// energy-resolution smearing already applied below, which only blurs the
	// recorded energy of events assumed to deposit their full energy).
	G4bool fApplyCrystalEfficiency;
	std::vector<G4double> fCrystalEffEnergies;   // keV
	std::vector<G4double> fCrystalEffValues;     // unitless, 0-1
	// Warn once when a photon lands outside the tabulated range. Beyond the last point the
	// interpolation CLAMPS, which is silent and can be large: a table ending at 440 keV used at
	// 1 MeV returns 0.149 where 9.5 mm of NaI actually gives 0.042, a factor of 3.6.
	mutable G4bool fWarnedEffClamp;

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

	// RecordScatterOrder: emit the extra ScatterOrder column (primary vs scatter)
	G4bool fRecordScatterOrder;

	// Detector ID
	G4int fDetectorIDValue;

	// Per-event hit buffer: each photon scored individually
	struct FDHitEntry {
		G4int pixelID;
		G4double trueEnergy_keV;
		G4double smearedEnergy_keV;
		G4double weight;  // transmission T * upstream weight
		G4int scatterOrder;  // 0 = primary (unscattered), 1 = scattered (order >= 1)
	};
	std::vector<FDHitEntry> fEventHits;

	// Ntuple output columns
	G4int fPixelID;
	G4float fTrueEnergy;
	G4float fSmearedEnergy;
	G4int fDetectorID;
	G4float fWeight;
	G4int fScatterOrder;

	// Analytical collimator transmission (Metz-Frey model)
	G4double ComputeTransmission(G4double cosTheta);

	// Ray-trace septal-penetration model
	void BuildTransmissionTable();
	G4bool PointInAir(G4double x, G4double y) const;
	void     TracedTransmissionRow(G4double tanTheta, const std::vector<G4double>& mus,
								   std::vector<G4double>& row) const;
	G4double InterpolateMu(const std::vector<G4double>& row, G4double mu) const;
	G4double LookupTransmission(G4double tanTheta, G4double mu) const;
	G4double SeptalMuAt(G4double energy);
	void     BuildSeptalMuTable();

	// Helpers (same interface as analog scorer)
	G4double InterpolateFWHM(G4double energy_keV);
	G4double InterpolateCrystalEfficiency(G4double energy_keV);
	G4int ComputePixelID(G4double localX, G4double localY);
};

#endif
