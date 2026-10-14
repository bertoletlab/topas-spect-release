// Particle Source for Y90BremsSource
//
// Thin source that pairs with TsY90BremsGenerator; all sampling logic lives in the generator.

#include "TsY90BremsSource.hh"

TsY90BremsSource::TsY90BremsSource(TsParameterManager* pM, TsSourceManager* psM, G4String sourceName)
    : TsSource(pM, psM, sourceName)
{
    ResolveParameters();
}

TsY90BremsSource::~TsY90BremsSource() {}

void TsY90BremsSource::ResolveParameters()
{
    TsSource::ResolveParameters();
}
