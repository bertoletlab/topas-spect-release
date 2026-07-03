// Scorer for EDepSpectrum
//
// See TsEDepSpectrum.hh. Per-event total energy deposit -> ntuple row.

#include "TsEDepSpectrum.hh"
#include "G4Step.hh"
#include "G4SystemOfUnits.hh"

TsEDepSpectrum::TsEDepSpectrum(TsParameterManager* pM, TsMaterialManager* mM, TsGeometryManager* gM,
                               TsScoringManager* scM, TsExtensionManager* eM,
                               G4String scorerName, G4String quantity, G4String outFileName, G4bool isSubScorer)
: TsVNtupleScorer(pM, mM, gM, scM, eM, scorerName, quantity, outFileName, isSubScorer),
  fEventEdep(0.), fSumEx(0.), fSumEy(0.), fSumEw(0.), fEdepKeV(0.f), fXmm(0.f), fYmm(0.f), fWeight(1.f)
{
    fNtuple->RegisterColumnF(&fEdepKeV, "Edep",   "keV");
    fNtuple->RegisterColumnF(&fXmm,     "X",      "mm");
    fNtuple->RegisterColumnF(&fYmm,     "Y",      "mm");
    fNtuple->RegisterColumnF(&fWeight,  "Weight", "");
}

TsEDepSpectrum::~TsEDepSpectrum() {}

G4bool TsEDepSpectrum::ProcessHits(G4Step* aStep, G4TouchableHistory*)
{
    if (!fIsActive) { fSkippedWhileInactive++; return false; }

    G4double e = aStep->GetTotalEnergyDeposit();
    if (e <= 0.) return false;

    fEventEdep += e;
    // Energy-weighted global position of the deposit (midpoint of the step).
    G4ThreeVector p = 0.5 * (aStep->GetPreStepPoint()->GetPosition()
                           + aStep->GetPostStepPoint()->GetPosition());
    fSumEx += e * p.x();
    fSumEy += e * p.y();
    // Energy-weighted track weight, so the event's Weight column is well defined even if variance
    // reduction gives the deposits within an event different weights (uniform weight -> that weight).
    fSumEw += e * aStep->GetPreStepPoint()->GetWeight();
    return true;
}

void TsEDepSpectrum::UserHookForEndOfEvent()
{
    if (fEventEdep > 0.) {
        fEdepKeV = (G4float)(fEventEdep);        // internal (MeV); ntuple /keV on output
        fXmm     = (G4float)(fSumEx / fEventEdep); // internal length (=mm)
        fYmm     = (G4float)(fSumEy / fEventEdep);
        fWeight  = (G4float)(fSumEw / fEventEdep); // energy-weighted event weight (1 without VR)
        fNtuple->Fill();
    }
    fEventEdep = 0.;
    fSumEx = 0.;
    fSumEy = 0.;
    fSumEw = 0.;
}
