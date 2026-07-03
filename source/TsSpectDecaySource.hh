// A simplified radioactive time source for SPECT acquisitions, bundled with topas-spect so no
// external extension is required. It emits a parent radionuclide (GenericIon) uniformly in a
// component's volume and, across a Tf timeline, scales the histories per time bin by the true
// activity decay. Geant4's own radioactive-decay physics (g4radioactivedecay) then produces the
// full decay chain from each emitted ion, so daughter photons appear natively. Chain-specific
// biological redistribution is modelled in the phantom scenario, not here.

#ifndef TsSpectDecaySource_hh
#define TsSpectDecaySource_hh

#include "TsSource.hh"

class TsSpectDecaySource : public TsSource
{
public:
    TsSpectDecaySource(TsParameterManager* pM, TsSourceManager* psM, G4String sourceName);
    ~TsSpectDecaySource();

    void SetNewNumberOfHistories(G4long n) { fNumberOfHistoriesInRun = n; }
    void ResolveParameters();
};
#endif
