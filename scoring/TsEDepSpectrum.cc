// Scorer for EDepSpectrum
//
// See TsEDepSpectrum.hh. Per-event total energy deposit -> ntuple row.

#include "TsEDepSpectrum.hh"
#include "TsParameterManager.hh"
#include "TsVGeometryComponent.hh"
#include "G4Step.hh"
#include "G4NavigationHistory.hh"
#include "G4VTouchable.hh"
#include "G4SystemOfUnits.hh"
#include "G4ios.hh"

#include <cmath>

TsEDepSpectrum::TsEDepSpectrum(TsParameterManager* pM, TsMaterialManager* mM, TsGeometryManager* gM,
                               TsScoringManager* scM, TsExtensionManager* eM,
                               G4String scorerName, G4String quantity, G4String outFileName, G4bool isSubScorer)
: TsVNtupleScorer(pM, mM, gM, scM, eM, scorerName, quantity, outFileName, isSubScorer),
  fEventEdep(0.), fSumEx(0.), fSumEy(0.), fSumEw(0.), fEdepKeV(0.f), fXmm(0.f), fYmm(0.f), fWeight(1.f),
  fRecordLocal(false), fDetectorIDValue(0), fSumEz(0.), fSumEu(0.), fSumEv(0.), fSumElocW(0.),
  fZmm(0.f), fUmm(0.f), fVmm(0.f), fWmm(0.f), fDetectorID(0), fRun(0),
  fFirstWeight(0.), fEventMixedWeight(false), fMixedWeightEvents(0)
{
    if (fPm->ParameterExists(GetFullParmName("RecordLocalPosition")))
        fRecordLocal = fPm->GetBooleanParameter(GetFullParmName("RecordLocalPosition"));
    if (fPm->ParameterExists(GetFullParmName("DetectorID")))
        fDetectorIDValue = fPm->GetIntegerParameter(GetFullParmName("DetectorID"));

    fNtuple->RegisterColumnF(&fEdepKeV, "Edep",   "keV");
    fNtuple->RegisterColumnF(&fXmm,     "X",      "mm");
    fNtuple->RegisterColumnF(&fYmm,     "Y",      "mm");
    fNtuple->RegisterColumnF(&fWeight,  "Weight", "");
    if (fRecordLocal) {
        // Appended after the default columns, so readers of the four-column layout keep working.
        fNtuple->RegisterColumnF(&fZmm, "Z", "mm");
        fNtuple->RegisterColumnF(&fUmm, "U", "mm");
        fNtuple->RegisterColumnF(&fVmm, "V", "mm");
        fNtuple->RegisterColumnF(&fWmm, "W", "mm");
        fNtuple->RegisterColumnI(&fDetectorID, "DetectorID");
        fNtuple->RegisterColumnI(&fRun, "Run");
    }
}

TsEDepSpectrum::~TsEDepSpectrum() {}

// Transform a global point into the frame of the scored component's envelope volume. The envelope
// is looked up in the step's navigation history, not taken as the deepest volume, so a crystal with
// daughters (pixels, divisions) still reports in the crystal's frame. Read on every step, so the
// transform follows any time-feature motion of the component or its parents.
G4ThreeVector TsEDepSpectrum::ToComponentFrame(const G4Step* aStep, const G4ThreeVector& globalPos)
{
    const G4VPhysicalVolume* envelope = GetComponent()->GetEnvelopePhysicalVolume();
    const G4NavigationHistory* history = aStep->GetPreStepPoint()->GetTouchable()->GetHistory();
    for (G4int depth = (G4int)history->GetDepth(); depth >= 0; --depth) {
        if (history->GetVolume(depth) == envelope)
            return history->GetTransform(depth).TransformPoint(globalPos);
    }
    G4ExceptionDescription msg;
    msg << GetName() << ": the envelope of component " << GetComponent()->GetName()
        << " is not in the navigation history of a scored step, so its local frame is undefined.";
    G4Exception("TsEDepSpectrum::ToComponentFrame", "EDepSpectrum001", FatalException, msg);
    return globalPos;
}

G4bool TsEDepSpectrum::ProcessHits(G4Step* aStep, G4TouchableHistory*)
{
    if (!fIsActive) { fSkippedWhileInactive++; return false; }

    G4double e = aStep->GetTotalEnergyDeposit();
    if (e <= 0.) return false;

    const G4double w = aStep->GetPreStepPoint()->GetWeight();
    if (fEventEdep == 0.)
        fFirstWeight = w;
    else if (std::fabs(w - fFirstWeight) > 1e-9 * std::fabs(fFirstWeight))
        fEventMixedWeight = true;

    fEventEdep += e;
    // Energy-weighted global position of the deposit (midpoint of the step).
    G4ThreeVector p = 0.5 * (aStep->GetPreStepPoint()->GetPosition()
                           + aStep->GetPostStepPoint()->GetPosition());
    fSumEx += e * p.x();
    fSumEy += e * p.y();
    // Energy-weighted track weight, so the event's Weight column is well defined even if variance
    // reduction gives the deposits within an event different weights (uniform weight -> that weight).
    fSumEw += e * w;

    if (fRecordLocal) {
        fSumEz += e * p.z();
        G4ThreeVector q = ToComponentFrame(aStep, p);
        fSumEu += e * q.x();
        fSumEv += e * q.y();
        fSumElocW += e * q.z();
    }
    return true;
}

void TsEDepSpectrum::UserHookForEndOfEvent()
{
    if (fEventEdep > 0.) {
        fEdepKeV = (G4float)(fEventEdep);        // internal (MeV); ntuple /keV on output
        fXmm     = (G4float)(fSumEx / fEventEdep); // internal length (=mm)
        fYmm     = (G4float)(fSumEy / fEventEdep);
        fWeight  = (G4float)(fSumEw / fEventEdep); // energy-weighted event weight (1 without VR)
        if (fRecordLocal) {
            fZmm = (G4float)(fSumEz / fEventEdep);
            fUmm = (G4float)(fSumEu / fEventEdep);
            fVmm = (G4float)(fSumEv / fEventEdep);
            fWmm = (G4float)(fSumElocW / fEventEdep);
            fDetectorID = fDetectorIDValue;
            fRun = GetRunID();
        }
        if (fEventMixedWeight) fMixedWeightEvents++;
        fNtuple->Fill();
    }
    fEventEdep = 0.;
    fSumEx = 0.;
    fSumEy = 0.;
    fSumEw = 0.;
    fSumEz = 0.;
    fSumEu = 0.;
    fSumEv = 0.;
    fSumElocW = 0.;
    fEventMixedWeight = false;
}

void TsEDepSpectrum::AbsorbResultsFromWorkerScorer(TsVScorer* workerScorer)
{
    // Only workers run ProcessHits and only masters reach UserHookForEndOfRun, which TOPAS calls
    // after absorbing every worker, so the mixed-weight count travels across here.
    TsEDepSpectrum* worker = dynamic_cast<TsEDepSpectrum*>(workerScorer);
    if (worker) {
        fMixedWeightEvents += worker->fMixedWeightEvents;
        worker->fMixedWeightEvents = 0;
    }
    TsVNtupleScorer::AbsorbResultsFromWorkerScorer(workerScorer);
}

void TsEDepSpectrum::UserHookForEndOfRun()
{
    if (fMixedWeightEvents > 0)
        G4cout << "TsEDepSpectrum " << GetName() << ": " << fMixedWeightEvents
               << " events summed deposits carrying different weights. Event summation mixes"
               << " split tracks, so their Edep and centroid are not those of a single photon."
               << G4endl;
    fMixedWeightEvents = 0;
}
