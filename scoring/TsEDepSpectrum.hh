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
//
// Optional head-frame output (b:Sc/<name>/RecordLocalPosition = "True"), for detectors that move
// or rotate (orbiting heads, one deck per view). Six columns are appended after the default four:
//   Z           global centroid Z [mm]
//   U, V, W     centroid in the scored component's frame [mm]: the frame of its envelope volume,
//               so daughters such as pixels do not change it
//   DetectorID  i:Sc/<name>/DetectorID (default 0); use one scorer per head in multi-head setups
//   Run         TOPAS run index, which advances once per sequential time bin
// Each row then carries its centroid in both frames, so the head pose of every (Run, DetectorID)
// can be recovered from the data by a rigid fit.
//
// The event sum mixes every deposit of the event, so split variance reduction (several tracks of
// different weight in one event) is outside its scope; events whose deposits carry differing
// weights are counted and reported at the end of each run.
#ifndef TsEDepSpectrum_hh
#define TsEDepSpectrum_hh
#include "TsVNtupleScorer.hh"
#include "G4ThreeVector.hh"

class TsEDepSpectrum : public TsVNtupleScorer
{
public:
    TsEDepSpectrum(TsParameterManager* pM, TsMaterialManager* mM, TsGeometryManager* gM,
                   TsScoringManager* scM, TsExtensionManager* eM,
                   G4String scorerName, G4String quantity, G4String outFileName, G4bool isSubScorer);
    virtual ~TsEDepSpectrum();
    G4bool ProcessHits(G4Step*, G4TouchableHistory*);
    void UserHookForEndOfEvent();
    void UserHookForEndOfRun();
    void AbsorbResultsFromWorkerScorer(TsVScorer* workerScorer);

private:
    G4ThreeVector ToComponentFrame(const G4Step* aStep, const G4ThreeVector& globalPos);

    G4double fEventEdep;   // per-event accumulator (Geant4 internal units)
    G4double fSumEx;       // sum of edep*x (for energy-weighted centroid)
    G4double fSumEy;
    G4double fSumEw;       // sum of edep*weight (for the energy-weighted event weight)
    G4float  fEdepKeV;     // ntuple column: deposited energy [keV]
    G4float  fXmm;         // ntuple column: centroid X [mm]
    G4float  fYmm;         // ntuple column: centroid Y [mm]
    G4float  fWeight;      // ntuple column: event track weight (1 without variance reduction)

    // Head-frame output (RecordLocalPosition)
    G4bool   fRecordLocal;
    G4int    fDetectorIDValue;
    G4double fSumEz;       // sum of edep*z (global)
    G4double fSumEu;       // sums of edep*(u, v, w), component frame
    G4double fSumEv;
    G4double fSumElocW;
    G4float  fZmm;         // ntuple column: centroid Z [mm]
    G4float  fUmm;         // ntuple columns: centroid U, V, W [mm]
    G4float  fVmm;
    G4float  fWmm;
    G4int    fDetectorID;  // ntuple column
    G4int    fRun;         // ntuple column

    // Mixed-weight guard
    G4double fFirstWeight;
    G4bool   fEventMixedWeight;
    G4long   fMixedWeightEvents;
};
#endif
