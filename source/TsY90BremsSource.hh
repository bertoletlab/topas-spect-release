// Y-90 photon source for SPECT: emits the bremsstrahlung (+ 1.76 MeV line) photon spectrum a
// Y-90 decay produces in a chosen tissue, from a pre-computed table, without simulating the
// beta. Pairs with TsY90BremsGenerator, where the sampling lives. See that header for the
// physics, the medium options and the per-decay normalization.

#ifndef TsY90BremsSource_hh
#define TsY90BremsSource_hh

#include "TsSource.hh"

class TsY90BremsSource : public TsSource
{
public:
    TsY90BremsSource(TsParameterManager* pM, TsSourceManager* psM, G4String sourceName);
    ~TsY90BremsSource();

    void ResolveParameters();
};
#endif
