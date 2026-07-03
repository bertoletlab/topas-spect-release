// Physics Module for g4em-penelope-no-rayleigh
//
// TsPenelopeNoRayleigh.hh
// Custom TOPAS physics module: G4EmPenelopePhysics WITHOUT Rayleigh scattering.
// Registered as "g4em-penelope-no-rayleigh" in TOPAS.
//
// Purpose: apples-to-apples single-Compton comparison with our forward MC,
// which treats Rayleigh as a null collision (DECISIONS.md Q2).
// With Rayleigh disabled here, OnlyIncludeParticlesWithInteractionCount==1
// means exactly one Compton — directly comparable to our MC.
//
// The only difference from g4em-penelope is the line:
//   ph->RegisterProcess(theRayleigh, particle)  IS OMITTED for gamma.
//
// Known residual mismatch vs our MC:
//   Our mu_tot includes mu_Rayleigh (from PENELOPE tables), TOPAS-no-Rayleigh
//   does not. This creates ~1.8% less survival in our MC over mean exit path.
//   Effect direction: our MC < TOPAS-no-Rayleigh by ~1.8% from attenuation.
//   This is documented in B2.3f and is the opposite direction from the ~8%
//   we are investigating.
//
// Author: TOPAS/G4 Expert, B2.3f root-cause investigation, 2026-06-22

#pragma once
#include "G4VPhysicsConstructor.hh"
#include "TsParameterManager.hh"

class TsPenelopeNoRayleigh : public G4VPhysicsConstructor {
public:
    explicit TsPenelopeNoRayleigh(TsParameterManager* pM);
    ~TsPenelopeNoRayleigh() override = default;
    void ConstructParticle() override;
    void ConstructProcess()  override;
};
