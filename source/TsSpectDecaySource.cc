// Particle Source for SpectDecaySource
//
// Thin source that pairs with TsSpectDecayGenerator. All of the decay/time logic lives in the
// generator; this class only exposes SetNewNumberOfHistories so the generator can set the number
// of primaries for each time bin.

#include "TsSpectDecaySource.hh"

TsSpectDecaySource::TsSpectDecaySource(TsParameterManager* pM, TsSourceManager* psM, G4String sourceName)
    : TsSource(pM, psM, sourceName)
{
    ResolveParameters();
}

TsSpectDecaySource::~TsSpectDecaySource() {}

void TsSpectDecaySource::ResolveParameters()
{
    TsSource::ResolveParameters();
}
