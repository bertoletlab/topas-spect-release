// Scorer for EDepSpectrum
//
// Pulse-height (energy-response) scorer: accumulates the TOTAL energy deposited
// in the sensitive component per EVENT (one incident primary) and writes one
// ntuple row per event with the deposited energy E_meas and the energy-weighted
// interaction centroid (global X,Y). With a mono-energetic primary this yields
// R(E_true -> E_meas): photopeak + Compton continuum + K-escape (+ CZT low-E tail).
// With a collimator upstream it also gives counts-in-window sensitivity.
//
// The row carries a Weight column (the event's track weight), so the scorer is correct under
// variance reduction: in post-processing, histogram Edep weighted by Weight (sum weights per bin),
// not raw row counts. Without VR every weight is 1, so summing weights == counting rows.
#ifndef TsEDepSpectrum_hh
#define TsEDepSpectrum_hh
#include "TsVNtupleScorer.hh"

class TsEDepSpectrum : public TsVNtupleScorer
{
public:
    TsEDepSpectrum(TsParameterManager* pM, TsMaterialManager* mM, TsGeometryManager* gM,
                   TsScoringManager* scM, TsExtensionManager* eM,
                   G4String scorerName, G4String quantity, G4String outFileName, G4bool isSubScorer);
    virtual ~TsEDepSpectrum();
    G4bool ProcessHits(G4Step*, G4TouchableHistory*);
    void UserHookForEndOfEvent();

private:
    G4double fEventEdep;   // per-event accumulator (Geant4 internal units)
    G4double fSumEx;       // sum of edep*x (for energy-weighted centroid)
    G4double fSumEy;
    G4double fSumEw;       // sum of edep*weight (for the energy-weighted event weight)
    G4float  fEdepKeV;     // ntuple column: deposited energy [keV]
    G4float  fXmm;         // ntuple column: centroid X [mm]
    G4float  fYmm;         // ntuple column: centroid Y [mm]
    G4float  fWeight;      // ntuple column: event track weight (1 without variance reduction)
};
#endif
