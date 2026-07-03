// Physics Module for g4em-penelope-no-rayleigh
//
// TsPenelopeNoRayleigh.cc
// Identical to G4EmPenelopePhysics EXCEPT G4RayleighScattering is NOT
// registered for gamma.  The TOPAS interaction count filter will then
// treat InteractionCount==1 as exactly one Compton (or PE) with no
// Rayleigh contamination — directly comparable to our forward MC.
//
// B2.3f residual attenuation note:
//   Our PENELOPE tables include mu_Rayleigh in mu_tot for the exit path.
//   TOPAS-no-Rayleigh uses mu_tot - mu_Rayleigh.  At 112 keV (window mid)
//   mu_Rayleigh ~ 0.004 cm^-1, mean exit path ~10 cm => ~4% extra attenuation
//   in our MC. This reduces P_1_ours by ~4%, making the gap LARGER than
//   the observed ~8%, so exit-attenuation mismatch cannot explain the gap.

#include "TsPenelopeNoRayleigh.hh"
#include "G4SystemOfUnits.hh"
#include "G4ParticleDefinition.hh"

// gamma processes
#include "G4PhotoElectricEffect.hh"
#include "G4PenelopePhotoElectricModel.hh"
#include "G4ComptonScattering.hh"
#include "G4PenelopeComptonModel.hh"
#include "G4GammaConversion.hh"
#include "G4PenelopeGammaConversionModel.hh"
#include "G4PEEffectFluoModel.hh"
#include "G4KleinNishinaModel.hh"
// G4RayleighScattering INTENTIONALLY NOT INCLUDED

// e-/e+ processes
#include "G4eMultipleScattering.hh"
#include "G4eIonisation.hh"
#include "G4PenelopeIonisationModel.hh"
#include "G4eBremsstrahlung.hh"
#include "G4PenelopeBremsstrahlungModel.hh"
#include "G4eplusAnnihilation.hh"
#include "G4PenelopeAnnihilationModel.hh"
#include "G4ePairProduction.hh"
#include "G4hMultipleScattering.hh"
#include "G4hIonisation.hh"
#include "G4ionIonisation.hh"
#include "G4IonParametrisedLossModel.hh"
#include "G4NuclearStopping.hh"
#include "G4LindhardSorensenIonModel.hh"

// msc models
#include "G4GoudsmitSaundersonMscModel.hh"
#include "G4WentzelVIModel.hh"
#include "G4CoulombScattering.hh"
#include "G4eCoulombScatteringModel.hh"

// utilities
#include "G4EmBuilder.hh"
#include "G4EmStandUtil.hh"

// particles
#include "G4Gamma.hh"
#include "G4Electron.hh"
#include "G4Positron.hh"
#include "G4GenericIon.hh"

#include "G4PhysicsListHelper.hh"
#include "G4BuilderType.hh"
#include "G4EmModelActivator.hh"
#include "G4MscStepLimitType.hh"
#include "G4EmParameters.hh"

TsPenelopeNoRayleigh::TsPenelopeNoRayleigh(TsParameterManager* /*pM*/)
  : G4VPhysicsConstructor("TsPenelopeNoRayleigh")
{
    G4EmParameters* param = G4EmParameters::Instance();
    // Mirror G4EmPenelopePhysics constructor settings exactly:
    param->SetDefaults();
    param->SetMinEnergy(100 * CLHEP::eV);
    param->SetLowestElectronEnergy(100 * CLHEP::eV);
    param->SetNumberOfBinsPerDecade(20);
    param->ActivateAngularGeneratorForIonisation(true);
    param->SetStepFunction(0.2, 10 * CLHEP::um);
    param->SetStepFunctionMuHad(0.1, 50 * CLHEP::um);
    param->SetStepFunctionLightIons(0.1, 20 * CLHEP::um);
    param->SetStepFunctionIons(0.1, 1 * CLHEP::um);
    param->SetUseMottCorrection(true);
    param->SetMscStepLimitType(fUseSafetyPlus);
    param->SetMscSkin(3);
    param->SetMscRangeFactor(0.08);
    param->SetMuHadLateralDisplacement(true);
    param->SetFluo(true);
    param->SetUseICRU90Data(true);
    param->SetFluctuationType(fUrbanFluctuation);
    param->SetMaxNIELEnergy(1 * CLHEP::MeV);
    param->SetPIXEElectronCrossSectionModel("Penelope");
    SetPhysicsType(bElectromagnetic);
}

void TsPenelopeNoRayleigh::ConstructParticle() {
    G4EmBuilder::ConstructMinimalEmSet();
}

void TsPenelopeNoRayleigh::ConstructProcess() {
    G4EmBuilder::PrepareEMPhysics();
    G4PhysicsListHelper* ph = G4PhysicsListHelper::GetPhysicsListHelper();
    G4EmParameters* param = G4EmParameters::Instance();

    G4hMultipleScattering* hmsc = new G4hMultipleScattering("ionmsc");
    G4double highEnergyLimit = param->MscEnergyLimit();
    G4double nielEnergyLimit = param->MaxNIELEnergy();
    G4NuclearStopping* pnuc = nullptr;
    if (nielEnergyLimit > 0.0) {
        pnuc = new G4NuclearStopping();
        pnuc->SetMaxKinEnergy(nielEnergyLimit);
    }
    G4double PenelopeHighEnergyLimit = 1.0 * CLHEP::GeV;

    // ---- gamma: PE + Compton + GammaConversion only (NO Rayleigh) ----
    G4ParticleDefinition* particle = G4Gamma::Gamma();

    G4PhotoElectricEffect* pe = new G4PhotoElectricEffect();
    pe->SetEmModel(new G4PEEffectFluoModel());
    G4PenelopePhotoElectricModel* pePenelope = new G4PenelopePhotoElectricModel();
    pePenelope->SetHighEnergyLimit(PenelopeHighEnergyLimit);
    pe->AddEmModel(0, pePenelope);

    G4ComptonScattering* compt = new G4ComptonScattering();
    compt->SetEmModel(new G4KleinNishinaModel());
    G4PenelopeComptonModel* comptPenelope = new G4PenelopeComptonModel();
    comptPenelope->SetHighEnergyLimit(PenelopeHighEnergyLimit);
    compt->AddEmModel(0, comptPenelope);

    G4GammaConversion* gc = new G4GammaConversion();
    G4PenelopeGammaConversionModel* gcPenelope = new G4PenelopeGammaConversionModel();
    gcPenelope->SetHighEnergyLimit(PenelopeHighEnergyLimit);
    gc->AddEmModel(0, gcPenelope);

    ph->RegisterProcess(pe,    particle);
    ph->RegisterProcess(compt, particle);
    ph->RegisterProcess(gc,    particle);
    // ph->RegisterProcess(theRayleigh, particle) -- INTENTIONALLY OMITTED

    // ---- e- ----
    particle = G4Electron::Electron();
    G4GoudsmitSaundersonMscModel* msc1 = new G4GoudsmitSaundersonMscModel();
    G4WentzelVIModel* msc2 = new G4WentzelVIModel();
    msc1->SetHighEnergyLimit(highEnergyLimit);
    msc2->SetLowEnergyLimit(highEnergyLimit);
    G4EmBuilder::ConstructElectronMscProcess(msc1, msc2, particle);

    G4eCoulombScatteringModel* ssm = new G4eCoulombScatteringModel();
    G4CoulombScattering* ss = new G4CoulombScattering();
    ss->SetEmModel(ssm);
    ss->SetMinKinEnergy(highEnergyLimit);
    ssm->SetLowEnergyLimit(highEnergyLimit);
    ssm->SetActivationLowEnergyLimit(highEnergyLimit);

    G4eIonisation* eioni = new G4eIonisation();
    eioni->SetFluctModel(G4EmStandUtil::ModelOfFluctuations());
    G4PenelopeIonisationModel* ioniPen = new G4PenelopeIonisationModel();
    ioniPen->SetHighEnergyLimit(PenelopeHighEnergyLimit);
    eioni->AddEmModel(0, ioniPen);

    G4eBremsstrahlung* brem = new G4eBremsstrahlung();
    G4PenelopeBremsstrahlungModel* bremPen = new G4PenelopeBremsstrahlungModel();
    bremPen->SetHighEnergyLimit(PenelopeHighEnergyLimit);
    brem->SetEmModel(bremPen);

    G4ePairProduction* ee = new G4ePairProduction();

    ph->RegisterProcess(eioni, particle);
    ph->RegisterProcess(brem,  particle);
    ph->RegisterProcess(ee,    particle);
    ph->RegisterProcess(ss,    particle);

    // ---- e+ ----
    particle = G4Positron::Positron();
    msc1 = new G4GoudsmitSaundersonMscModel();
    msc2 = new G4WentzelVIModel();
    msc1->SetHighEnergyLimit(highEnergyLimit);
    msc2->SetLowEnergyLimit(highEnergyLimit);
    G4EmBuilder::ConstructElectronMscProcess(msc1, msc2, particle);

    ssm = new G4eCoulombScatteringModel();
    ss = new G4CoulombScattering();
    ss->SetEmModel(ssm);
    ss->SetMinKinEnergy(highEnergyLimit);
    ssm->SetLowEnergyLimit(highEnergyLimit);
    ssm->SetActivationLowEnergyLimit(highEnergyLimit);

    eioni = new G4eIonisation();
    eioni->SetFluctModel(G4EmStandUtil::ModelOfFluctuations());
    ioniPen = new G4PenelopeIonisationModel();
    ioniPen->SetHighEnergyLimit(PenelopeHighEnergyLimit);
    eioni->AddEmModel(0, ioniPen);

    brem = new G4eBremsstrahlung();
    bremPen = new G4PenelopeBremsstrahlungModel();
    bremPen->SetHighEnergyLimit(PenelopeHighEnergyLimit);
    brem->SetEmModel(bremPen);

    G4eplusAnnihilation* anni = new G4eplusAnnihilation();
    G4PenelopeAnnihilationModel* anniPen = new G4PenelopeAnnihilationModel();
    anniPen->SetHighEnergyLimit(PenelopeHighEnergyLimit);
    anni->AddEmModel(0, anniPen);

    ph->RegisterProcess(eioni, particle);
    ph->RegisterProcess(brem,  particle);
    ph->RegisterProcess(ee,    particle);
    ph->RegisterProcess(anni,  particle);
    ph->RegisterProcess(ss,    particle);

    // ---- generic ion ----
    particle = G4GenericIon::GenericIon();
    G4ionIonisation* ionIoni = new G4ionIonisation();
    ionIoni->SetEmModel(new G4LindhardSorensenIonModel());
    ph->RegisterProcess(hmsc, particle);
    ph->RegisterProcess(ionIoni, particle);
    if (pnuc) ph->RegisterProcess(pnuc, particle);

    // muons, hadrons, ions
    G4EmBuilder::ConstructCharged(hmsc, pnuc);

    // extra configuration
    G4EmModelActivator mact(GetPhysicsName());
}
