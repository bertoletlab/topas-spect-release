// Scorer for ForcedDetectionProjection
//
// Forced detection variance reduction scorer for SPECT projections.
// Instead of tracking photons through the collimator (where >99.99% are
// absorbed), this scorer sits on a thin vacuum surface at the collimator
// face position. For each photon crossing the surface, it:
//   1. Computes analytical Metz-Frey collimator transmission T(theta)
//   2. Applies Gaussian energy smearing to the REAL photon energy E_k
//   3. Checks the energy window on the smeared energy
//   4. Records accepted photons with Weight = T (transmission probability)
//
// Output ntuple: PixelID, TrueEnergy, SmearedEnergy, DetectorID, Weight
//
// Downstream parsers must SUM the Weight column per pixel to build projections.
// This gives ~1,000-5,000x efficiency gain over analog tracking.
//
// Reference: Metz CE, Atkins FB, Beck RN. "The geometric transfer function
// component for scintillation camera collimators with straight parallel
// holes." Phys Med Biol 25(6):1059-1070, 1980. doi:10.1088/0031-9155/25/6/003
//
// T(theta) is the exact aperture-autocorrelation closed form for circular
// holes (Metz Eq. 11); see ComputeTransmission() for the full derivation and
// the history of two bugs (angular-cutoff factor of 2, wrong functional
// shape) that this replaced. Weight also includes an energy-dependent
// crystal photopeak-window detection efficiency by default (see
// docs/validation.md section 1); see ProcessHits() and
// InterpolateCrystalEfficiency().

#include "TsForcedDetectionProjection.hh"
#include <utility>
#include <string>
#include <sstream>
#include <map>
#include <mutex>

#include "TsParameterManager.hh"
#include "TsVGeometryComponent.hh"
#include "G4Step.hh"
#include "G4StepPoint.hh"
#include "G4Track.hh"
#include "G4Gamma.hh"
#include "G4Exception.hh"
#include "G4SystemOfUnits.hh"
#include "Randomize.hh"

#include <cmath>
#include <mutex>
#include <cctype>

// How much signal MinTransmissionFraction actually throws away, summed across worker
// threads and reported once. An estimate from the transmission table alone overstates it,
// because the widest angles miss the detector and would be rejected anyway.
static std::mutex gCutMutex;
static G4int gWorkersDone = 0;

TsForcedDetectionProjection::TsForcedDetectionProjection(
	TsParameterManager* pM, TsMaterialManager* mM,
	TsGeometryManager* gM, TsScoringManager* scM,
	TsExtensionManager* eM,
	G4String scorerName, G4String quantity,
	G4String outFileName, G4bool isSubScorer)
: TsVNtupleScorer(pM, mM, gM, scM, eM, scorerName, quantity, outFileName, isSubScorer)
{
	// --- Parallel-hole only: refuse a converging or diverging collimator ---
	// The model below (Metz aperture autocorrelation and the ray-traced septal penetration) assumes
	// every hole axis is parallel to the surface normal. The scorer has no focal-length concept, so
	// paired with a converging, diverging or fan-beam collimator it would quietly model parallel
	// holes. Two ways to be caught: the scorer is given FocalLengthX/FocalLengthY (the names the
	// collimator uses, so a deck that copies its collimator parameters across brings them along),
	// or the same deck defines a TsParallelHoleCollimator with a nonzero focal length.
	{
		G4String offender;
		for (const char* axis : {"FocalLengthX", "FocalLengthY"}) {
			if (fPm->ParameterExists(GetFullParmName(axis)) &&
				fPm->GetDoubleParameter(GetFullParmName(axis), "Length") != 0.0)
				offender = GetFullParmName(axis);
		}
		if (offender.empty()) {
			std::vector<G4String> geoTypes;
			fPm->GetParameterNamesBracketedBy("Ge/", "/Type", &geoTypes);
			for (const G4String& typeParm : geoTypes) {
				G4String type = fPm->GetStringParameter(typeParm);
				G4StrUtil::to_lower(type);
				if (type != "tsparallelholecollimator") continue;
				G4String base = typeParm.substr(0, typeParm.size() - 4);  // strip "Type"
				for (const char* axis : {"FocalLengthX", "FocalLengthY"}) {
					G4String name = base + axis;
					if (fPm->ParameterExists(name) && fPm->GetDoubleParameter(name, "Length") != 0.0)
						offender = name;
				}
			}
		}
		if (!offender.empty()) {
			G4ExceptionDescription ed;
			ed << "Forced detection supports parallel-hole geometry only, and " << offender
			   << " is nonzero (a converging, diverging or fan-beam collimator).\n"
			   << "TsForcedDetectionProjection has no focal-length model and would silently score "
			   << "parallel holes, giving the wrong geometric acceptance, field of view and "
			   << "septal path.\n"
			   << "Use full transport (the TsEDepSpectrum scorer behind the physical collimator) "
			   << "for converging or diverging collimators.";
			G4Exception("TsForcedDetectionProjection::TsForcedDetectionProjection",
						"SPECT_FD_NonParallelCollimator", FatalException, ed);
		}
	}

	// --- Collimator geometry ---
	fHoleDiameter = 1.08 * mm;
	if (fPm->ParameterExists(GetFullParmName("HoleDiameter")))
		fHoleDiameter = fPm->GetDoubleParameter(GetFullParmName("HoleDiameter"), "Length");

	fSeptalThickness = 1.38 * mm;
	if (fPm->ParameterExists(GetFullParmName("SeptalThickness")))
		fSeptalThickness = fPm->GetDoubleParameter(GetFullParmName("SeptalThickness"), "Length");

	fCollimatorLength = 25.0 * mm;
	if (fPm->ParameterExists(GetFullParmName("CollimatorLength")))
		fCollimatorLength = fPm->GetDoubleParameter(GetFullParmName("CollimatorLength"), "Length");

	// Geometric hole fraction: f = pi*(d/2)^2 / (d+t)^2
	G4double pitch = fHoleDiameter + fSeptalThickness;
	fHoleFraction = M_PI * (fHoleDiameter / 2.0) * (fHoleDiameter / 2.0) / (pitch * pitch);

	if (fPm->ParameterExists(GetFullParmName("HoleFraction")))
		fHoleFraction = fPm->GetUnitlessParameter(GetFullParmName("HoleFraction"));

	// --- FD-specific parameters ---
	fIncludeSeptalPenetration = false;
	if (fPm->ParameterExists(GetFullParmName("IncludeSeptalPenetration")))
		fIncludeSeptalPenetration = fPm->GetBooleanParameter(GetFullParmName("IncludeSeptalPenetration"));

	fSeptalMuPerMM = 3.56 / mm;  // Tungsten attenuation at 140.5 keV
	fSeptalMuWasSet = fPm->ParameterExists(GetFullParmName("SeptalMuPerMM"));
	if (fSeptalMuWasSet)
		fSeptalMuPerMM = fPm->GetUnitlessParameter(GetFullParmName("SeptalMuPerMM")) / mm;

	// The septum material, used when the deck does not pin a coefficient. Default lead, which is
	// what every parallel-hole preset in this package uses; tungsten collimators set it.
	fSeptalMaterialName = "G4_Pb";
	if (fPm->ParameterExists(GetFullParmName("SeptalMaterial")))
		fSeptalMaterialName = fPm->GetStringParameter(GetFullParmName("SeptalMaterial"));
	fCurrentEnergy = 0.0;
	BuildSeptalMuTable();

	// --- Hole shape (drives which closed-form transmission is used) ---
	// --- Septal-penetration model ---
	// Default preserves the shipped behaviour exactly, so existing decks reproduce bit for bit.
	// Transmission floor, as a fraction of the on-axis value: a hit is recorded only above it.
	//
	// A model that produces a penetration tail also produces a long list of photons carrying
	// almost no weight, and the floor is what keeps the output file bounded. It does NOT save run
	// time: measured at 2e7 histories, 108.7 s of CPU at 1e-4 against 110.2 s at 1e-9. What it
	// costs is the tail itself. On the Symbia LEHR geometry behind the companion paper's figure 9,
	// a floor of 1e-4 removes 0.66 percent of the integral, cuts the modelled support from 118.2 mm
	// to 18.7 mm, and drops the signal beyond r = 12.3 mm from 0.723 percent to 0.069 percent
	// against 0.881 percent under full transport. In other words it discards most of what the ray
	// trace was built to produce.
	//
	// The default is therefore 1e-6, where the integral has converged: 7.7850e-05 against
	// 7.7857e-05 at 1e-9 on that geometry, a difference of 0.009 percent. The cost is output size,
	// 1,559,729 entries against 56,809 for a 2e7-history run. Raise it when file size matters more
	// than the tail, and read the discarded fraction the scorer reports at the end of each run.
	// Set to 0 to disable. Ignored by the effective-length model, which cuts off sharply on its own.
	fMinTransmissionFraction = 1.0e-6;
	if (fPm->ParameterExists(GetFullParmName("MinTransmissionFraction")))
		fMinTransmissionFraction = fPm->GetUnitlessParameter(GetFullParmName("MinTransmissionFraction"));

	// Ray tracing is the default: it measures the lead a ray crosses instead of widening the
	// acceptance cutoff, and across five collimators from 113 keV to 1 MeV it holds the
	// end-to-end ratio against full transport between 0.75 and 1.10, where the effective-length
	// correction ranged from 0.014 to 1.02. "EffectiveLength" remains available.
	fWeightKept = 0.0;
	fWeightCut = 0.0;
	fPenetrationModel = "RayTrace";
	fPenetrationModelWasSet = fPm->ParameterExists(GetFullParmName("PenetrationModel"));
	if (fPenetrationModelWasSet)
		fPenetrationModel = fPm->GetStringParameter(GetFullParmName("PenetrationModel"));
	fLattice = "Square";
	if (fPm->ParameterExists(GetFullParmName("Lattice")))
		fLattice = fPm->GetStringParameter(GetFullParmName("Lattice"));

	fHoleShape = "Round";
	if (fPm->ParameterExists(GetFullParmName("HoleShape")))
		fHoleShape = fPm->GetStringParameter(GetFullParmName("HoleShape"));

	{
		G4String shapeLower = fHoleShape;
		for (auto& c : shapeLower) c = std::tolower(c);
		if (shapeLower != "round") {
			G4cerr << G4endl
				   << "*******************************************************************" << G4endl
				   << "* TsForcedDetectionProjection WARNING: HoleShape=\"" << fHoleShape << "\"" << G4endl
				   << "* This scorer's analytical transmission T(theta) is the EXACT Metz" << G4endl
				   << "* (1980) aperture-autocorrelation closed form for CIRCULAR holes" << G4endl
				   << "* (Eq. 11). Hexagonal and square holes have distinct, anisotropic" << G4endl
				   << "* closed forms (Metz Eq. 12) that depend on azimuth as well as theta" << G4endl
				   << "* and are NOT implemented here. The scorer is falling back to the" << G4endl
				   << "* round-hole formula as an approximation (same cutoff angle, same" << G4endl
				   << "* f-scaling, but not the exact hex/square shape function). This is" << G4endl
				   << "* a known, documented limitation -- do not treat results for non-" << G4endl
				   << "* round holes as exact." << G4endl
				   << "*******************************************************************" << G4endl
				   << G4endl;
		}
	}

	// --- Virtual detector plane offset ---
	// Distance from FD surface center to virtual detector = collimator half-length + CZT half-thickness
	G4double cztThickness = 5.0 * mm;
	if (fPm->ParameterExists(GetFullParmName("CZTThickness")))
		cztThickness = fPm->GetDoubleParameter(GetFullParmName("CZTThickness"), "Length");
	fDetectorOffset = fCollimatorLength / 2.0 + cztThickness / 2.0;

	// --- Pixel geometry (identical parameters to analog scorer) ---
	fPixelSizeX = 2.46 * mm;
	if (fPm->ParameterExists(GetFullParmName("PixelSizeX")))
		fPixelSizeX = fPm->GetDoubleParameter(GetFullParmName("PixelSizeX"), "Length");

	fPixelSizeY = 2.46 * mm;
	if (fPm->ParameterExists(GetFullParmName("PixelSizeY")))
		fPixelSizeY = fPm->GetDoubleParameter(GetFullParmName("PixelSizeY"), "Length");

	fNPixelsX = 32;
	if (fPm->ParameterExists(GetFullParmName("NPixelsX")))
		fNPixelsX = fPm->GetIntegerParameter(GetFullParmName("NPixelsX"));

	fNPixelsY = 32;
	if (fPm->ParameterExists(GetFullParmName("NPixelsY")))
		fNPixelsY = fPm->GetIntegerParameter(GetFullParmName("NPixelsY"));

	fDetectorHLX = fNPixelsX * fPixelSizeX / 2.0;
	fDetectorHLY = fNPixelsY * fPixelSizeY / 2.0;

	// --- Energy resolution calibration vectors ---
	G4int nResPoints = 0;
	if (fPm->ParameterExists(GetFullParmName("ResolutionEnergies"))) {
		G4double* energies = fPm->GetDoubleVector(GetFullParmName("ResolutionEnergies"), "Energy");
		nResPoints = fPm->GetVectorLength(GetFullParmName("ResolutionEnergies"));
		for (G4int i = 0; i < nResPoints; i++)
			fResolutionEnergies.push_back(energies[i] / keV);
	} else {
		fResolutionEnergies.push_back(140.5);
		fResolutionEnergies.push_back(208.0);
	}

	if (fPm->ParameterExists(GetFullParmName("ResolutionFractionalFWHM"))) {
		G4double* fwhms = fPm->GetUnitlessVector(GetFullParmName("ResolutionFractionalFWHM"));
		G4int nFWHM = fPm->GetVectorLength(GetFullParmName("ResolutionFractionalFWHM"));
		for (G4int i = 0; i < nFWHM; i++)
			fResolutionFWHM.push_back(fwhms[i]);
	} else {
		fResolutionFWHM.push_back(0.055);
		fResolutionFWHM.push_back(0.046);
	}

	// --- Energy window ---
	fEnergyWindowLow = 126.5 * keV;
	if (fPm->ParameterExists(GetFullParmName("EnergyWindowLow")))
		fEnergyWindowLow = fPm->GetDoubleParameter(GetFullParmName("EnergyWindowLow"), "Energy");

	fEnergyWindowHigh = 154.6 * keV;
	if (fPm->ParameterExists(GetFullParmName("EnergyWindowHigh")))
		fEnergyWindowHigh = fPm->GetDoubleParameter(GetFullParmName("EnergyWindowHigh"), "Energy");

	// --- Crystal photopeak-window detection efficiency ---
	// Default table is this package's own tabulated NaI 9.5 mm photopeak-window
	// efficiency (docs/validation.md section 1), from full Geant4 transport of
	// mono-energetic photons into a bare crystal slab. Applying this by default
	// is a deliberate, non-backward-compatible change: without it, the scorer
	// implicitly assumed 100% detection of every geometrically-transmitted,
	// in-window photon, which is not correct (Compton/K-escape reduce the
	// counted fraction well below 1). Users needing the old (incorrect)
	// behavior can set IncludeCrystalEfficiency=False.
	fWarnedEffClamp = false;
	fApplyCrystalEfficiency = true;
	if (fPm->ParameterExists(GetFullParmName("IncludeCrystalEfficiency")))
		fApplyCrystalEfficiency = fPm->GetBooleanParameter(GetFullParmName("IncludeCrystalEfficiency"));

	if (fPm->ParameterExists(GetFullParmName("CrystalEfficiencyEnergies"))) {
		G4double* energies = fPm->GetDoubleVector(GetFullParmName("CrystalEfficiencyEnergies"), "Energy");
		G4int nEffPoints = fPm->GetVectorLength(GetFullParmName("CrystalEfficiencyEnergies"));
		for (G4int i = 0; i < nEffPoints; i++)
			fCrystalEffEnergies.push_back(energies[i] / keV);
	} else {
		// DEFAULT TABLE: 9.5 mm NaI ONLY (normal incidence, 20% photopeak window, unsmeared;
		// full Geant4 transport of mono-energetic photons into a bare crystal slab). A deck with
		// another crystal (thickness, CZT, LaBr3, ...) MUST declare its own
		// CrystalEfficiencyEnergies/CrystalEfficiencyValues; nothing here checks the crystal.
		// 14 points, measured 2026-10-26. The earlier 6-point table (113/140.5/208/218/250/440 keV)
		// had nothing between 250 and 440 keV, and linear interpolation across that gap read 14.1%
		// high at 300 keV and 20.5% high at 364.5 keV. Outside 113-1000 keV the end values are held.
		fCrystalEffEnergies = {113.0, 140.5, 159.0, 171.0, 200.0, 208.0, 218.0,
							   245.0, 250.0, 300.0, 364.5, 440.0, 500.0, 1000.0};
	}

	if (fPm->ParameterExists(GetFullParmName("CrystalEfficiencyValues"))) {
		G4double* effs = fPm->GetUnitlessVector(GetFullParmName("CrystalEfficiencyValues"));
		G4int nEffVals = fPm->GetVectorLength(GetFullParmName("CrystalEfficiencyValues"));
		for (G4int i = 0; i < nEffVals; i++)
			fCrystalEffValues.push_back(effs[i]);
	} else {
		fCrystalEffValues = {0.944, 0.862, 0.7742, 0.7137, 0.5761, 0.545, 0.505,
							 0.4147, 0.400, 0.2926, 0.2065, 0.149, 0.1194, 0.0417};
	}

	if (fCrystalEffEnergies.size() != fCrystalEffValues.size()) {
		G4cerr << "TsForcedDetectionProjection: CrystalEfficiencyEnergies and "
			   << "CrystalEfficiencyValues must have the same length." << G4endl;
		exit(1);
	}

	// --- Detector ID ---
	fDetectorIDValue = 0;
	if (fPm->ParameterExists(GetFullParmName("DetectorID")))
		fDetectorIDValue = fPm->GetIntegerParameter(GetFullParmName("DetectorID"));

	// ListMode: write all photons without smearing or windowing
	fListMode = false;
	if (fPm->ParameterExists(GetFullParmName("ListMode")))
		fListMode = fPm->GetBooleanParameter(GetFullParmName("ListMode"));

	// RecordScatterOrder: append the ScatterOrder column separating primary from scatter
	fRecordScatterOrder = false;
	if (fPm->ParameterExists(GetFullParmName("RecordScatterOrder")))
		fRecordScatterOrder = fPm->GetBooleanParameter(GetFullParmName("RecordScatterOrder"));

	// Register ntuple columns: 5 columns (4 standard + Weight), +1 optional ScatterOrder
	fNtuple->RegisterColumnI(&fPixelID, "PixelID");
	fNtuple->RegisterColumnF(&fTrueEnergy, "TrueEnergy", "keV");
	fNtuple->RegisterColumnF(&fSmearedEnergy, "SmearedEnergy", "keV");
	fNtuple->RegisterColumnI(&fDetectorID, "DetectorID");
	fNtuple->RegisterColumnF(&fWeight, "Weight", "");
	if (fRecordScatterOrder)
		fNtuple->RegisterColumnI(&fScatterOrder, "ScatterOrder");

	G4cout << "TsForcedDetectionProjection: HoleDiameter=" << fHoleDiameter/mm
		   << "mm, CollimatorLength=" << fCollimatorLength/mm
		   << "mm, HoleShape=" << fHoleShape
		   << ", HoleFraction=" << fHoleFraction
		   << ", SeptalPenetration=" << (fIncludeSeptalPenetration ? "ON" : "OFF")
		   << ", CrystalEfficiency=" << (fApplyCrystalEfficiency ? "ON" : "OFF")
		   << ", ScatterOrder=" << (fRecordScatterOrder ? "ON" : "OFF")
		   << G4endl;

	// Say which attenuation the septa are getting. A deck that pins SeptalMuPerMM owns that
	// number and nothing checks it against the material; a deck that does not gets Geant4's own
	// coefficient for the named material at each photon's energy, which is the same data the
	// transport path uses.
	if (fIncludeSeptalPenetration && !fSeptalMuWasSet)
		G4cout << "TsForcedDetectionProjection: septal attenuation tabulated for "
			   << fSeptalMaterialName << ", applied at each photon's own energy. Set"
			   << " Sc/.../SeptalMuPerMM to pin a single coefficient instead, Sc/.../SeptalMaterial"
			   << " for a different septum, or SeptalMuEnergies and SeptalMuValues for your own"
			   << " tabulation."
			   << G4endl;
	else if (fIncludeSeptalPenetration)
		G4cout << "TsForcedDetectionProjection: septal attenuation pinned at "
			   << fSeptalMuPerMM * mm << "/mm for every photon energy. Unset Sc/.../SeptalMuPerMM"
			   << " to use the tabulation for " << fSeptalMaterialName << " instead." << G4endl;

	// Where this scorer puts its modelled collimator, stated loudly because getting it wrong is
	// invisible in the integral and shows up only in the point-spread shape. The surface the
	// scorer is attached to is taken as the collimator MID-PLANE, so the modelled front face sits
	// half a collimator length UPSTREAM of it. A transport deck that places its source relative
	// to the collimator FRONT FACE and a forced-detection deck that places it at the same z are
	// therefore NOT at the same source-to-collimator distance: the fast path ends up
	// CollimatorLength/2 closer, and its point-spread function comes out narrow by construction
	// (measured 0.794 of transport at 100 mm, against 0.924 once registered; PMB-122210 Phase 5b).
	// Sensitivity is unaffected, since parallel-hole sensitivity does not depend on that distance,
	// which is exactly why the error survived a campaign of integral cross-checks.
	G4cout << "TsForcedDetectionProjection: scoring surface is treated as the collimator MID-PLANE."
		   << " Modelled front face is " << (fCollimatorLength / 2.0)/mm << "mm upstream of it, and"
		   << " the virtual detector plane " << fDetectorOffset/mm << "mm downstream."
		   << " To match a transport deck that measures source distance from the collimator FRONT"
		   << " FACE, place the source " << (fCollimatorLength / 2.0)/mm << "mm further away."
		   << G4endl;

	G4String penLower = fPenetrationModel;
	penLower.toLower();
	if (penLower == "raytrace") {
		// Asking for a penetration model while penetration is off is a contradiction worth
		// stopping for, but only when the user asked: RayTrace is the default, so a deck that
		// never mentions penetration at all simply runs without it.
		if (!fIncludeSeptalPenetration && fPenetrationModelWasSet) {
			G4cerr << "TsForcedDetectionProjection: PenetrationModel=\"RayTrace\" requires "
				   << "IncludeSeptalPenetration=\"True\"." << G4endl;
			fPm->AbortSession(1);
		}
		if (fIncludeSeptalPenetration)
			BuildTransmissionTable();
	}
}


TsForcedDetectionProjection::~TsForcedDetectionProjection()
{;}


// ---------------------------------------------------------------------------
// Ray-trace septal-penetration model
//
// WHY A TRACE RATHER THAN A CORRECTION
//   Transmission through lead is exp(-mu * l). The only hard part is l, the lead a ray actually
//   crosses, and that is geometry. The shipped effective-length correction, L_eff = L - 2/mu,
//   sidesteps l by widening the acceptance cutoff instead; it is accurate where penetration is a
//   grazing effect near the cutoff, and it returns exactly zero beyond that cutoff, so the
//   modelled response has compact support and can carry no tail. Measured against transport, it
//   recovers the penetration multiplier to about 1 percent while penetration adds little, and
//   misses by 48 percent at Symbia LEHR with 208 keV photons, where 56 percent of the detected
//   signal crossed lead.
//
//   Two closed-form approximations to l were tried first and both failed, in opposite directions:
//   a one-dimensional slit charges roughly half the lead a ray crosses in a two-dimensional array
//   and over-predicted by a factor of 17; substituting squares of equal area for round holes
//   under-predicted by 16 to 32 percent. Walking the ray and testing membership in the real
//   aperture removes the approximation entirely.
//
// WHAT IS AVERAGED
//   For a given polar angle the transmission is averaged over where rays enter the collimator
//   face, across one lattice cell in both transverse directions, and over azimuth. Azimuthal
//   averaging is what carries the model to a hexagonal lattice, whose two periods differ.
//
// VALIDATION (PMB-122210 revision, resim/pentail_prototype/)
//   Penetration multiplier against full transport, 8 configurations, 5 collimators, 113 keV to
//   440 keV: mean error 3.6 percent, 0.4 to 1.5 percent in regime, and the two most
//   penetration-dominated configurations improve from -47.6 and -35.4 percent to -11.1 and
//   -11.2 percent. Absolute check, which a ratio cannot provide: the opaque-limit geometric
//   efficiency times the NaI photopeak efficiency reproduces the measured opaque-septa
//   sensitivity to 1.009, 0.969 and 0.988 across two collimators and two energies, nothing
//   fitted. Converged: doubling every discretization below moves the hardest case by 0.03 percent.
//
// WHAT IT STILL OMITS
//   Photons that Compton-scatter in the septa and still land inside the photopeak window.
//   Attenuation removes them from the primary beam without delivering them anywhere, so this term
//   is a LOWER BOUND on the tail by construction. Both penetration-dominated configurations miss
//   LOW by the same 11 percent, which is the size of that bound's shortfall and the signature of
//   one missing channel rather than a geometry error.
// ---------------------------------------------------------------------------

namespace {
	// Discretization verified by convergence: the settings below and a grid with 40 percent more
	// samples in every dimension agree to 0.03 percent on the most penetration-dominated case,
	// while halving them costs 7.5 percent.
	// Entry sampling is ADAPTIVE in angle. Near the axis the average IS the open fraction and
	// needs a fine grid to resolve the aperture edge; past about tan(theta) = d/L every ray
	// already crosses many cells and self-averages, where an 8x8 grid matches 40x40 to under
	// 1 percent (measured). Without this the table costs about 40 s to build; with it, a few.
	const G4int kNEntryFine    = 40;
	const G4int kNEntryCoarse  = 12;
	const G4int kNAzimuth      = 24;
	const G4int kNDepth        = 220;
	const G4int kNTableAngles  = 96;

	// The attenuation axis, used only when the deck leaves SeptalMuPerMM unset. The range spans
	// lead from roughly 1 MeV at the bottom to 40 keV at the top, which covers every SPECT line
	// and every scattered photon that can still land in a photopeak window.
	const G4int kNMuGrid       = 96;
	const G4double kMuGridLow  = 0.02;     // per mm
	const G4double kMuGridHigh = 100.0;    // per mm

	// Photon energies are binned logarithmically before asking Geant4 for a coefficient, so a
	// continuous scattered spectrum costs a bounded number of lookups. 0.5 percent per bin.
	const G4double kMuEnergyLogStep = 0.005;
}

G4bool TsForcedDetectionProjection::PointInAir(G4double x, G4double y) const
{
	// HOLE SHAPE AND LATTICE ARE SEPARATE CHOICES, and getting either wrong moves the open-area
	// fraction by several percent. Checked against the closed forms:
	//   hexagonal holes on a hexagonal lattice   f = (d/p)^2
	//   circular holes on a hexagonal lattice    f = 0.9069 (d/p)^2
	//   circular holes on a square lattice       f = (pi/4) (d/p)^2
	// with d the flat-to-flat width for a hexagon and p = d + s. A regular hexagon of apothem r
	// has area 2*sqrt(3)*r^2, and the Voronoi cell of a triangular lattice of pitch p is itself a
	// hexagon of apothem p/2, which is why the hexagonal ratio is exactly (d/p)^2.
	const G4double p = fHoleDiameter + fSeptalThickness;
	const G4double r = fHoleDiameter / 2.0;
	const G4double s3 = std::sqrt(3.0) / 2.0;

	G4String shape = fHoleShape;  shape.toLower();
	G4String lat   = fLattice;    lat.toLower();
	const G4bool hexHole = (shape == "hex" || shape == "hexagon" || shape == "hexagonal");

	if (lat == "hex") {
		// Express the point in the lattice basis a1 = (p, 0), a2 = (p/2, p*sqrt(3)/2) and test
		// the nine nearest centres. Mapping to the nearest ROW and then taking x modulo the pitch
		// is NOT equivalent: a point's nearest hole is often in an adjacent row, and that error
		// cost 3 percent of the open-area fraction.
		const G4double v = y / (p * s3);
		const G4double u = (x - v * p / 2.0) / p;
		const G4double u0 = std::floor(u + 0.5), v0 = std::floor(v + 0.5);
		for (G4int du = -1; du <= 1; ++du) {
			for (G4int dv = -1; dv <= 1; ++dv) {
				const G4double cx = (u0 + du) * p + (v0 + dv) * p / 2.0;
				const G4double cy = (v0 + dv) * p * s3;
				const G4double ax = x - cx, ay = y - cy;
				const G4bool inside = hexHole
					? (std::fabs(ax) <= r && std::fabs(0.5 * ax + s3 * ay) <= r
					                      && std::fabs(-0.5 * ax + s3 * ay) <= r)
					: (ax * ax + ay * ay < r * r);
				if (inside) return true;
			}
		}
		return false;
	}

	const G4double dx = std::fmod(std::fmod(x, p) + p, p) - p / 2.0;
	const G4double dy = std::fmod(std::fmod(y, p) + p, p) - p / 2.0;
	if (hexHole)
		return std::fabs(dx) <= r && std::fabs(0.5 * dx + s3 * dy) <= r
		                          && std::fabs(-0.5 * dx + s3 * dy) <= r;
	return (dx * dx + dy * dy) < r * r;
}


void TsForcedDetectionProjection::TracedTransmissionRow(G4double tanTheta,
														const std::vector<G4double>& mus,
														std::vector<G4double>& row) const
{
	// THE GEOMETRY DOES NOT DEPEND ON THE PHOTON ENERGY. What the ray trace measures is how much
	// lead a ray crosses, and that is fixed by the hole array and the angle alone; the energy
	// enters only through exp(-mu * l) afterwards. Tracing once and evaluating every coefficient
	// on the way is what makes a per-energy coefficient affordable, because the expensive part,
	// walking kNDepth steps for every entry position and azimuth, happens once instead of once
	// per energy. With a single coefficient this reduces to exactly the original loop.
	const G4double p = fHoleDiameter + fSeptalThickness;
	const G4double cosTheta = 1.0 / std::sqrt(1.0 + tanTheta * tanTheta);
	const G4double dz = fCollimatorLength / kNDepth;

	row.assign(mus.size(), 0.0);

	if (tanTheta <= 0.0) {
		// At normal incidence a ray either runs clean down a channel or sits in lead for the
		// whole collimator length, so the average is the open fraction plus whatever survives a
		// full-length traverse. The deck carries the open fraction exactly, so take it from there.
		for (size_t j = 0; j < mus.size(); ++j)
			row[j] = fHoleFraction
				   + (1.0 - fHoleFraction) * std::exp(-mus[j] * fCollimatorLength);
		return;
	}

	const G4double cellsCrossed = fCollimatorLength * tanTheta / p;
	const G4int nEntry = (cellsCrossed < 2.0) ? kNEntryFine : kNEntryCoarse;

	for (G4int ia = 0; ia < kNAzimuth; ++ia) {
		const G4double phi = (ia + 0.5) / kNAzimuth * (M_PI / 2.0);
		const G4double ux = tanTheta * std::cos(phi);
		const G4double uy = tanTheta * std::sin(phi);
		std::vector<G4double> acc(mus.size(), 0.0);
		for (G4int ix = 0; ix < nEntry; ++ix) {
			const G4double x0 = (ix + 0.5) / nEntry * p;
			for (G4int iy = 0; iy < nEntry; ++iy) {
				const G4double y0 = (iy + 0.5) / nEntry * p;
				G4int leadSteps = 0;
				for (G4int iz = 0; iz < kNDepth; ++iz) {
					const G4double z = (iz + 0.5) * dz;
					if (!PointInAir(x0 + ux * z, y0 + uy * z)) ++leadSteps;
				}
				if (leadSteps == 0) {
					for (size_t j = 0; j < mus.size(); ++j) acc[j] += 1.0;
				} else {
					const G4double leadPath = leadSteps * dz / cosTheta;
					for (size_t j = 0; j < mus.size(); ++j)
						acc[j] += std::exp(-mus[j] * leadPath);
				}
			}
		}
		for (size_t j = 0; j < mus.size(); ++j) row[j] += acc[j] / (nEntry * nEntry);
	}
	for (size_t j = 0; j < mus.size(); ++j) row[j] /= kNAzimuth;
}


void TsForcedDetectionProjection::BuildTransmissionTable()
{
	// SHARED ACROSS WORKER THREADS. Geant4 instantiates one scorer per thread, so building the
	// table in each constructor duplicates the work N times AND serialises it: measured at 12
	// threads, execution went from 1.4 s to 63 s while CPU utilisation fell from 785 to 116
	// percent, and the 60 s difference in user time was almost exactly 12 builds. The table
	// depends only on the collimator geometry and mu, so one build per distinct configuration is
	// enough. Keyed on those parameters because a deck may define several collimators.
	static std::mutex cacheMutex;
	static std::map<std::string, std::pair<std::vector<G4double>, std::vector<std::vector<G4double> > > > cache;

	// The attenuation axis, built BEFORE the cache lookup: a thread that hits the cache skips
	// the table build but still needs this grid to interpolate on, and leaving it empty there
	// read past the end of an empty vector.
	// The attenuation axis. A deck that declares SeptalMuPerMM gets exactly that one coefficient,
	// so its results are unchanged to the last digit; a deck that does not gets a logarithmic grid
	// spanning lead from about 1 MeV to about 40 keV, and the coefficient for each photon's own
	// energy is interpolated on it. Transmission is a Laplace transform of the path-length
	// distribution, so it is smooth and log-convex in mu and interpolates well.
	fMuGrid.clear();
	if (fSeptalMuWasSet) {
		fMuGrid.push_back(fSeptalMuPerMM);
	} else {
		for (G4int j = 0; j < kNMuGrid; ++j)
			fMuGrid.push_back(kMuGridLow
							  * std::pow(kMuGridHigh / kMuGridLow, j / (G4double)(kNMuGrid - 1)));
	}

	std::ostringstream keyStream;
	keyStream << fHoleDiameter << '|' << fSeptalThickness << '|' << fCollimatorLength << '|'
			  << (fSeptalMuWasSet ? fSeptalMuPerMM : -1.0) << '|'
			  << fHoleFraction << '|' << fHoleShape << '|' << fLattice;
	const std::string key = keyStream.str();
	{
		std::lock_guard<std::mutex> guard(cacheMutex);
		std::map<std::string, std::pair<std::vector<G4double>,
										std::vector<std::vector<G4double> > > >::iterator
			it = cache.find(key);
		if (it != cache.end()) {
			fTransTanTheta = it->second.first;
			fTransTable = it->second.second;
			fTransFloor = fMinTransmissionFraction * fTransTable.front().front();
			return;
		}
	}

	// Tabulate out to the angle at which even a clear diagonal run through the array is
	// attenuated to nothing, bounded so the table stays finite for very transparent septa.
	G4double tanMax = 20.0 * fHoleDiameter / fCollimatorLength;
	if (tanMax < 0.25) tanMax = 0.25;
	if (tanMax > 1.50) tanMax = 1.50;

	fTransTanTheta.clear();
	fTransTable.clear();
	fTransTanTheta.reserve(kNTableAngles);
	fTransTable.reserve(kNTableAngles);

	std::vector<G4double> row;
	for (G4int i = 0; i < kNTableAngles; ++i) {
		const G4double t = tanMax * i / (kNTableAngles - 1);
		TracedTransmissionRow(t, fMuGrid, row);
		fTransTanTheta.push_back(t);
		fTransTable.push_back(row);
	}

	// GEOMETRY SELF-CHECK. T(0) is the open-area fraction the traced geometry actually has, and
	// the deck declares its own in HoleFraction. They must agree; when they do not, the hole
	// shape or the lattice is wrong. This is the failure this model is most exposed to, because
	// Lattice defaults to Square: running a hexagonal collimator without setting it silently
	// trades f = (d/p)^2 for (pi/4)(d/p)^2, about 10 percent, with nothing to see in the output.
	// Sample the aperture directly over one lattice cell. NOT T(0): TracedTransmission short
	// circuits at normal incidence and returns fHoleFraction, so comparing against it would be
	// comparing the declared value with itself. An earlier version of this guard did exactly
	// that and could never fire.
	G4double tracedFraction = 0.0;
	{
		const G4int nSample = 400;
		const G4double cellX = fHoleDiameter + fSeptalThickness;
		G4String latLower = fLattice; latLower.toLower();
		const G4double cellY = (latLower == "hex")
			? cellX * std::sqrt(3.0)          // two rows: the y period of an offset lattice
			: cellX;
		G4int inAir = 0;
		for (G4int i = 0; i < nSample; ++i) {
			const G4double xx = (i + 0.5) / nSample * cellX;
			for (G4int j = 0; j < nSample; ++j) {
				const G4double yy = (j + 0.5) / nSample * cellY;
				if (PointInAir(xx, yy)) ++inAir;
			}
		}
		tracedFraction = (G4double)inAir / (nSample * (G4double)nSample);
	}
	const G4double mismatch = std::fabs(tracedFraction - fHoleFraction)
							  / std::max(fHoleFraction, 1.0e-9);
	if (mismatch > 0.02) {
		G4cerr << "" << G4endl
			   << "TsForcedDetectionProjection: GEOMETRY MISMATCH." << G4endl
			   << "  The ray-traced open-area fraction is " << tracedFraction
			   << ", while HoleFraction declares " << fHoleFraction
			   << " (" << 100.0 * mismatch << " percent apart)." << G4endl
			   << "  HoleShape=\"" << fHoleShape << "\", Lattice=\"" << fLattice
			   << "\", d=" << fHoleDiameter/mm << "mm, septum=" << fSeptalThickness/mm
			   << "mm." << G4endl
			   << "  Closed forms: hexagonal holes on a hexagonal lattice give (d/p)^2;"
			   << " circular on hexagonal 0.9069(d/p)^2; circular on square (pi/4)(d/p)^2,"
			   << " with p = d + septum." << G4endl
			   << "  Set HoleShape and Lattice to match the collimator, or correct HoleFraction."
			   << G4endl;
		fPm->AbortSession(1);
	}

	fTransFloor = fMinTransmissionFraction * fTransTable.front().front();

	{
		std::lock_guard<std::mutex> guard(cacheMutex);
		cache[key] = std::make_pair(fTransTanTheta, fTransTable);
	}

	G4cout << "TsForcedDetectionProjection: ray-traced septal transmission tabulated over "
		   << kNTableAngles << " angles to tan(theta)=" << tanMax
		   << " (" << fLattice << " lattice, " << fHoleShape << " holes, mu="
		   << (fSeptalMuWasSet ? "pinned" : "per energy")
		   << "). T(0)=" << fTransTable.front().front()
		   << ", T(max)=" << fTransTable.back().front() << "." << G4endl;

}


G4double TsForcedDetectionProjection::InterpolateMu(const std::vector<G4double>& row,
												   G4double mu) const
{
	// One coefficient on the grid means the deck declared it, so there is nothing to interpolate.
	if (fMuGrid.size() == 1) return row[0];
	if (mu <= fMuGrid.front()) return row.front();
	if (mu >= fMuGrid.back()) return row.back();
	// Logarithmic in both, because the grid is logarithmic and transmission falls exponentially.
	const G4double lstep = std::log(fMuGrid[1] / fMuGrid[0]);
	const G4int j = (G4int)(std::log(mu / fMuGrid[0]) / lstep);
	const G4double f = std::log(mu / fMuGrid[j]) / lstep;
	const G4double a = row[j] > 0.0 ? std::log(row[j]) : -700.0;
	const G4double b = row[j + 1] > 0.0 ? std::log(row[j + 1]) : -700.0;
	return std::exp(a * (1.0 - f) + b * f);
}


G4double TsForcedDetectionProjection::LookupTransmission(G4double tanTheta, G4double mu) const
{
	if (fTransTable.empty()) return 0.0;
	if (tanTheta <= fTransTanTheta.front()) return InterpolateMu(fTransTable.front(), mu);
	if (tanTheta >= fTransTanTheta.back()) return 0.0;   // beyond the table the tail is negligible
	const G4double step = fTransTanTheta[1] - fTransTanTheta[0];
	const G4int i = (G4int)(tanTheta / step);
	const G4double f = (tanTheta - fTransTanTheta[i]) / step;
	return InterpolateMu(fTransTable[i], mu) * (1.0 - f)
		 + InterpolateMu(fTransTable[i + 1], mu) * f;
}


G4double TsForcedDetectionProjection::SeptalMuAt(G4double energy)
{
	// WHY A TABLE AND NOT GEANT4. Asking G4EmCalculator for the coefficient would put this on the
	// same data the transport path uses, which is the right instinct, but it cannot be done here:
	// called from a worker thread it forces G4PenelopePhotoElectricModel::ReadDataFile(), which is
	// master-thread-only, and Geant4 aborts the run. So the coefficients are tabulated, the same
	// way this scorer already tabulates crystal efficiency and energy resolution, and a deck can
	// replace them with SeptalMuEnergies and SeptalMuValues for a septum material not shipped here.
	//
	// The point of tabulating at all is that ONE coefficient cannot serve a real source. Lead
	// attenuates 4.5 times more strongly at 113 keV than at 208 keV, and 177Lu emits both; across
	// a single 20 percent photopeak window the coefficient still varies by 65 percent.
	if (fSeptalMuWasSet) return fSeptalMuPerMM;
	if (fMuTableEnergy.empty()) return 0.0;

	const G4double e = energy / keV;
	if (e <= fMuTableEnergy.front()) return fMuTableValue.front();
	if (e >= fMuTableEnergy.back()) return fMuTableValue.back();
	size_t i = 0;
	while (i + 2 < fMuTableEnergy.size() && fMuTableEnergy[i + 1] < e) ++i;
	// Log-log, because attenuation falls as a power of energy between absorption edges and a
	// linear chord across a decade would be badly wrong.
	const G4double f = (std::log(e) - std::log(fMuTableEnergy[i]))
					 / (std::log(fMuTableEnergy[i + 1]) - std::log(fMuTableEnergy[i]));
	return std::exp(std::log(fMuTableValue[i]) * (1.0 - f)
					+ std::log(fMuTableValue[i + 1]) * f);
}


void TsForcedDetectionProjection::BuildSeptalMuTable()
{
	// NIST XCOM mass attenuation with coherent scattering, converted to a linear coefficient at
	// the material's nominal density. Points are dense where SPECT lines live and where lead's
	// K edge at 88 keV makes the curve turn over; a deck working below the lowest point or above
	// the highest gets the end value rather than an extrapolation.
	static const G4double leadE[] = {40, 50, 60, 70, 80, 88, 88.005, 100, 113, 122, 140.5, 150,
									 171, 200, 208, 245, 300, 364.5, 400, 440, 500, 662, 1000};
	static const G4double leadMu[] = {16.62, 9.206, 5.600, 3.654, 2.516, 1.910, 8.161, 6.298,
									  4.692, 3.930, 2.692, 2.286, 1.620, 1.133, 1.038, 0.7446,
									  0.4576, 0.3150, 0.2636, 0.2256, 0.1831, 0.1278, 0.0806};
	static const G4double tungstenE[] = {40, 50, 60, 69.5, 69.525, 80, 100, 113, 140.5, 150, 200,
										 208, 300, 364.5, 400, 500, 662, 1000};
	static const G4double tungstenMu[] = {20.49, 11.46, 7.059, 4.733, 10.26, 7.198, 4.138, 3.051,
										  1.785, 1.546, 0.8292, 0.7628, 0.3762, 0.2743, 0.2345,
										  0.1702, 0.1232, 0.0790};

	fMuTableEnergy.clear();
	fMuTableValue.clear();

	if (fPm->ParameterExists(GetFullParmName("SeptalMuEnergies"))
		&& fPm->ParameterExists(GetFullParmName("SeptalMuValues"))) {
		const G4int nE = fPm->GetVectorLength(GetFullParmName("SeptalMuEnergies"));
		G4double* es = fPm->GetDoubleVector(GetFullParmName("SeptalMuEnergies"), "Energy");
		G4double* vs = fPm->GetUnitlessVector(GetFullParmName("SeptalMuValues"));
		for (G4int i = 0; i < nE; ++i) {
			fMuTableEnergy.push_back(es[i] / keV);
			fMuTableValue.push_back(vs[i]);
		}
		return;
	}

	G4String mat = fSeptalMaterialName; mat.toLower();
	const G4double* es = 0; const G4double* vs = 0; size_t n = 0;
	if (mat.find("pb") != std::string::npos || mat.find("lead") != std::string::npos) {
		es = leadE; vs = leadMu; n = sizeof(leadE) / sizeof(leadE[0]);
	} else if (mat.find("w") != std::string::npos || mat.find("tungsten") != std::string::npos) {
		es = tungstenE; vs = tungstenMu; n = sizeof(tungstenE) / sizeof(tungstenE[0]);
	} else if (!fSeptalMuWasSet && fIncludeSeptalPenetration) {
		G4cerr << "TsForcedDetectionProjection: no built-in septal attenuation for \""
			   << fSeptalMaterialName << "\". Set Sc/.../SeptalMuPerMM for a single coefficient,"
			   << " or Sc/.../SeptalMuEnergies and SeptalMuValues for a tabulation." << G4endl;
		fPm->AbortSession(1);
		return;
	}
	for (size_t i = 0; i < n; ++i) {
		fMuTableEnergy.push_back(es[i]);
		fMuTableValue.push_back(vs[i]);
	}
}


G4double TsForcedDetectionProjection::ComputeTransmission(G4double cosTheta)
{
	// Metz-Frey geometric transmission model for parallel-hole collimator
	// (Metz, Atkins, Beck, Phys Med Biol 25(6):1059-1070, 1980, doi:10.1088/
	// 0031-9155/25/6/003). Section p1062-1064: the response is the two-
	// dimensional convolution/autocorrelation of the hole aperture function
	// with its own reflection (their Eq. 5) -- photon fluence at the image
	// plane is nonzero only if a ray passes through BOTH the front and back
	// hole apertures. For a circular aperture of diameter d, this
	// autocorrelation is exactly the classic circle-circle overlap area: two
	// coaxial-axis-separated disks of diameter d, front and back aperture
	// centers offset laterally by s = L*tan(theta), overlap only while
	// s < d, i.e. tan(theta) < d/L. Normalizing the overlap area by the
	// single-disk area pi*(d/2)^2 gives, with u = L*tan(theta)/d:
	//
	//   T_geo(theta) = f * (2/pi) * [ arccos(u) - u*sqrt(1 - u^2) ],  u < 1
	//   T_geo(theta) = 0,                                             u >= 1
	//
	// where f is the geometric hole fraction. This replaces a PREVIOUS,
	// INCORRECT implementation,
	//   T_geo(theta) = f * [max(0, d*cos(theta) - 2*L*sin(theta)) / d]^2,
	// which had two independent errors, both confirmed against Metz directly
	// (see PMB-122210 referee-response notes):
	//   (1) a spurious factor of 2 in the angular cutoff -- the "2*L" term
	//       put the cutoff at tan(theta) = d/(2L) instead of the correct
	//       tan(theta) = d/L. This was NOT a half-length convention artifact:
	//       fCollimatorLength is documented and used elsewhere in this file
	//       as the FULL collimator length.
	//   (2) the wrong functional shape -- a squared linear ramp instead of
	//       the true circle-circle overlap (arccos - u*sqrt(1-u^2)) form.
	// Together these underestimated sensitivity by ~6x for a representative
	// clinical LEHR collimator (Symbia LEHR, 140.5 keV): semi-analytic
	// integration gives 1.33e-5 (old, buggy) vs 7.98e-5 (this formula)
	// counts/decay at 100 mm, vs 6.96e-5 from full opaque-septa Geant4
	// transport (agreement to ~1% once the crystal photopeak efficiency
	// factor below is included).
	//
	// Hole shape: this closed form is EXACT only for circular holes (Metz
	// Eq. 11). Hexagonal and square holes have distinct, azimuth-dependent
	// closed forms (Metz Eq. 12) that this scorer does not implement (see
	// the loud constructor-time warning for HoleShape != "Round"); for those
	// shapes this formula is used as a documented approximation that at
	// least gets the correct cutoff angle and disk-normalized magnitude.
	//
	// Optional septal penetration correction (IncludeSeptalPenetration=True):
	// folded in via the standard "effective hole length" correction
	// (Anger; see e.g. Cherry, Sorenson & Phelps, Physics in Nuclear Medicine),
	// L_eff = L - 2/mu, used in place of L above. This is the treatment already
	// described for this scorer in the manuscript text ("corrected for septal
	// penetration by an effective hole length").
	//
	// This replaced an earlier additive term,
	//   T_pen = f * (1 - geo_fraction) * exp(-mu*t/sin(theta)),
	// that modeled a photon crossing exactly one nominal septal thickness t
	// regardless of incidence angle. That is only defensible for theta just
	// past the geometric cutoff theta_c; for theta -> 90 deg (grazing to the
	// bore axis) it wrongly asymptotes to a constant f*exp(-mu*t) instead of
	// continuing to fall. Physically, a photon that misses the direct
	// line-of-sight through a hole must still advance the full axial length L
	// to reach the detector; as cos(theta) -> 0 the total path needed to cover
	// that length grows without bound, so it must cross progressively more
	// septal material, not a single fixed thickness. The L_eff substitution
	// keeps the whole transmission function in the single bounded T_geo shape:
	// it still falls smoothly to exactly zero at a (slightly enlarged)
	// critical angle theta_c_eff = atan(d / L_eff), and never plateaus at
	// a nonzero value for any theta, including grazing incidence.
	//
	// Validity note: L_eff = L - 2/mu assumes mu*t >> 1 (septa thick/opaque
	// enough that penetration is a near-tip grazing effect, not full-thickness
	// tunneling). For very thin or low-mu septa where 2/mu approaches or
	// exceeds L, L_eff is clamped to zero (degenerate limit: septa contribute
	// negligible blocking, so transmission approaches f*cos^2(theta) across the
	// full forward hemisphere -- itself a physically sensible bound, not a
	// divergence).

	if (cosTheta <= 0.0) return 0.0;

	G4double sinTheta = std::sqrt(1.0 - cosTheta * cosTheta);

	// Ray-trace model: the tabulated average already carries both clean passage through a channel
	// and attenuated passage through septa, so it replaces the Metz term rather than adding to it.
	if (!fTransTable.empty()) {
		const G4double t = LookupTransmission(sinTheta / cosTheta, SeptalMuAt(fCurrentEnergy));
		// The floor keeps the output file from carrying millions of negligible entries and costs
		// nothing in run time, but the weight below it never reaches the projection. How much
		// that is depends on the geometry, so count it rather than predicting it.
		if (t < fTransFloor) { fWeightCut += t; return 0.0; }
		fWeightKept += t;
		return t;
	}

	G4double L = fCollimatorLength;
	if (fIncludeSeptalPenetration && fSeptalMuPerMM > 0.0) {
		L = fCollimatorLength - 2.0 / fSeptalMuPerMM;
		if (L < 0.0) L = 0.0;
	}

	if (L <= 0.0) {
		// Degenerate limit (see validity note above): septa contribute no
		// blocking at all, so every forward-going photon reaches the
		// detector through some hole -- bounded only by the hole fraction.
		return fHoleFraction;
	}

	// u = L*tan(theta)/d; cutoff (front/back aperture projections stop
	// overlapping) is exactly u = 1, i.e. tan(theta) = d/L.
	G4double tanTheta = sinTheta / cosTheta;
	G4double u = L * tanTheta / fHoleDiameter;

	if (u >= 1.0) return 0.0;

	G4double shapeFactor = (2.0 / M_PI) * (std::acos(u) - u * std::sqrt(1.0 - u * u));

	return fHoleFraction * shapeFactor;
}


G4int TsForcedDetectionProjection::ComputePixelID(G4double localX, G4double localY)
{
	G4int ix = (G4int)std::floor((localX + fDetectorHLX) / fPixelSizeX);
	G4int iy = (G4int)std::floor((localY + fDetectorHLY) / fPixelSizeY);

	if (ix < 0) ix = 0;
	if (ix >= fNPixelsX) ix = fNPixelsX - 1;
	if (iy < 0) iy = 0;
	if (iy >= fNPixelsY) iy = fNPixelsY - 1;

	return ix * fNPixelsY + iy;
}


G4bool TsForcedDetectionProjection::ProcessHits(G4Step* aStep, G4TouchableHistory*)
{
	if (!fIsActive) {
		fSkippedWhileInactive++;
		return false;
	}

	// Only score boundary crossings (photons entering the FD surface)
	G4StepPoint* preStepPoint = aStep->GetPreStepPoint();
	if (preStepPoint->GetStepStatus() != fGeomBoundary) return false;

	// Only gammas
	if (aStep->GetTrack()->GetDefinition() != G4Gamma::GammaDefinition()) return false;

	// Get kinetic energy and weight
	G4double E_k = preStepPoint->GetKineticEnergy();
	if (E_k <= 0.0) return false;

	G4double weight = preStepPoint->GetWeight();

	// Transform momentum direction to local coordinates
	// Local Z axis points along the collimator bore (radially outward)
	G4ThreeVector worldDir = preStepPoint->GetMomentumDirection();

	// Scatter-order flag: a primary photon travels in a straight line from its emission
	// point, so its direction is unchanged from the vertex; any Compton or Rayleigh
	// deflection changes it. dot < 1 => the photon scattered at least once.
	const G4ThreeVector& vertexDir = aStep->GetTrack()->GetVertexMomentumDirection();
	G4int scatterOrder = (vertexDir.dot(worldDir) < 1.0 - 1e-9) ? 1 : 0;

	G4AffineTransform transform = preStepPoint->GetTouchableHandle()
		->GetHistory()->GetTopTransform();
	G4ThreeVector localDir = transform.TransformAxis(worldDir);

	// cosTheta is the component along local Z (collimator axis)
	// Positive localZ means photon is traveling INTO the collimator (toward detector)
	G4double cosTheta = localDir.z();

	if (cosTheta <= 0.0) {
		// Photon going away from detector — kill it, it can't contribute
		aStep->GetTrack()->SetTrackStatus(fStopAndKill);
		return false;
	}

	// Compute analytical collimator transmission
	fCurrentEnergy = E_k;
	G4double T = ComputeTransmission(cosTheta);
	if (T <= 0.0) {
		aStep->GetTrack()->SetTrackStatus(fStopAndKill);
		return false;
	}

	// Project hit position through collimator to virtual detector plane
	G4ThreeVector worldPos = preStepPoint->GetPosition();
	G4ThreeVector localPos = transform.TransformPoint(worldPos);

	// Project position from FD surface to detector plane
	// The FD surface is at the collimator center; the detector plane is
	// fDetectorOffset further along +Z (local)
	G4double projX = localPos.x() + localDir.x() / localDir.z() * fDetectorOffset;
	G4double projY = localPos.y() + localDir.y() / localDir.z() * fDetectorOffset;

	// Check if projected position falls within detector active area.
	// Use >= to reject photons exactly at the boundary (prevents clamping
	// artifacts where boundary photons accumulate on edge pixels).
	if (std::abs(projX) >= fDetectorHLX || std::abs(projY) >= fDetectorHLY) {
		aStep->GetTrack()->SetTrackStatus(fStopAndKill);
		return false;
	}

	G4int pixelID = ComputePixelID(projX, projY);

	G4double E_keV = E_k / keV;

	if (fListMode) {
		// List-mode: record raw hit — no smearing, no windowing, and (deliberately)
		// no crystal photopeak-window efficiency factor either. That efficiency is
		// specific to a particular energy window (docs/validation.md's 20% window),
		// and ListMode exists precisely so that smearing/windowing choices are made
		// downstream in Python; baking in a window-specific efficiency here would
		// undercut that. If IncludeCrystalEfficiency is desired for list-mode
		// output, apply it in post-processing alongside the chosen window.
		FDHitEntry entry;
		entry.pixelID = pixelID;
		entry.trueEnergy_keV = E_keV;
		entry.smearedEnergy_keV = E_keV;  // no smearing
		entry.weight = weight * T;
		entry.scatterOrder = scatterOrder;
		fEventHits.push_back(entry);
	} else {
		// Apply energy smearing to the REAL photon energy (not T*E_k)
		G4double fracFWHM = InterpolateFWHM(E_keV);
		G4double sigma_keV = fracFWHM * E_keV / (2.0 * std::sqrt(2.0 * std::log(2.0)));
		G4double smearedE_keV = G4RandGauss::shoot(E_keV, sigma_keV);

		// Apply energy window to the smeared PHOTON energy
		G4double windowLow_keV = fEnergyWindowLow / keV;
		G4double windowHigh_keV = fEnergyWindowHigh / keV;

		if (smearedE_keV >= windowLow_keV && smearedE_keV <= windowHigh_keV) {
			// Crystal photopeak-window detection efficiency: the probability that
			// a photon which geometrically transmits the collimator and reaches
			// the crystal is actually counted in this energy window (Compton
			// escape, K-escape, incomplete absorption, etc.). This is INDEPENDENT
			// of the Gaussian smearing above, which only blurs the recorded energy
			// of events already assumed to deposit full energy; without this
			// factor the scorer implicitly assumed 100% detection of every
			// in-window photon, which is not correct (see docs/validation.md
			// section 1 for the tabulated NaI/CZT values this defaults to).
			G4double crystalEff = fApplyCrystalEfficiency ? InterpolateCrystalEfficiency(E_keV) : 1.0;

			// Buffer this hit for end-of-event writing
			FDHitEntry entry;
			entry.pixelID = pixelID;
			entry.trueEnergy_keV = E_keV;
			entry.smearedEnergy_keV = smearedE_keV;
			entry.weight = weight * T * crystalEff;
			entry.scatterOrder = scatterOrder;
			fEventHits.push_back(entry);
		}
	}

	// Kill the track — this photon has been scored
	aStep->GetTrack()->SetTrackStatus(fStopAndKill);

	return true;
}


void TsForcedDetectionProjection::AbsorbResultsFromWorkerScorer(TsVScorer* workerScorer)
{
	// Only masters reach UserHookForEndOfRun, and only workers run ProcessHits, so the discarded
	// weight has to travel across here with the ntuple rows.
	TsForcedDetectionProjection* worker =
		dynamic_cast<TsForcedDetectionProjection*>(workerScorer);
	if (worker) {
		fWeightKept += worker->fWeightKept;
		fWeightCut  += worker->fWeightCut;
		worker->fWeightKept = 0.0;
		worker->fWeightCut  = 0.0;
	}
	TsVNtupleScorer::AbsorbResultsFromWorkerScorer(workerScorer);
}


void TsForcedDetectionProjection::UserHookForEndOfRun()
{
	if (fTransTable.empty()) return;
	std::lock_guard<std::mutex> guard(gCutMutex);
	if (gWorkersDone++ > 0) return;
	const G4double total = fWeightKept + fWeightCut;
	if (total <= 0.0) return;
	G4cout << "TsForcedDetectionProjection: MinTransmissionFraction="
		   << fMinTransmissionFraction << " discarded about "
		   << 100.0 * fWeightCut / total
		   << " percent of the modelled signal, the widest-angle part of it."
		   << " The default of 1e-6 converges the integral; a higher value trades the"
		   << " penetration tail for a smaller output file." << G4endl;
}


void TsForcedDetectionProjection::UserHookForEndOfEvent()
{
	// Write one ntuple row per accepted photon hit
	for (const auto& hit : fEventHits) {
		fPixelID = hit.pixelID;
		fTrueEnergy = (G4float)(hit.trueEnergy_keV * keV);
		fSmearedEnergy = (G4float)(hit.smearedEnergy_keV * keV);
		fDetectorID = fDetectorIDValue;
		fWeight = (G4float)(hit.weight);
		fScatterOrder = hit.scatterOrder;
		fNtuple->Fill();
	}

	fEventHits.clear();
}


G4double TsForcedDetectionProjection::InterpolateFWHM(G4double energy_keV)
{
	if (fResolutionEnergies.size() < 2)
		return fResolutionFWHM.empty() ? 0.05 : fResolutionFWHM[0];

	if (energy_keV <= fResolutionEnergies[0])
		return fResolutionFWHM[0];

	if (energy_keV >= fResolutionEnergies.back())
		return fResolutionFWHM.back();

	for (size_t i = 0; i < fResolutionEnergies.size() - 1; i++) {
		if (energy_keV >= fResolutionEnergies[i] && energy_keV <= fResolutionEnergies[i + 1]) {
			G4double frac = (energy_keV - fResolutionEnergies[i]) /
							(fResolutionEnergies[i + 1] - fResolutionEnergies[i]);
			return fResolutionFWHM[i] + frac * (fResolutionFWHM[i + 1] - fResolutionFWHM[i]);
		}
	}

	return fResolutionFWHM.back();
}


G4double TsForcedDetectionProjection::InterpolateCrystalEfficiency(G4double energy_keV)
{
	// Linear interpolation over the tabulated photopeak-window efficiency
	// (docs/validation.md section 1, or user-supplied CrystalEfficiencyEnergies/
	// CrystalEfficiencyValues), with the endpoints held constant outside the
	// tabulated range -- same clamping convention as InterpolateFWHM above. Above the last
	// point the clamp warns once per thread; below the first it is silent. The compiled default
	// spans 113-1000 keV (9.5 mm NaI only).
	if (fCrystalEffEnergies.size() < 2)
		return fCrystalEffValues.empty() ? 1.0 : fCrystalEffValues[0];

	if (energy_keV <= fCrystalEffEnergies[0])
		return fCrystalEffValues[0];

	if (energy_keV >= fCrystalEffEnergies.back()) {
		// Warn only when the photon is genuinely ABOVE the table. A photon sitting exactly on
		// the last point is interpolated correctly, and warning there cries wolf: a 1 MeV
		// monoenergetic run against a table ending at 1 MeV raised the alarm on every thread.
		const G4double beyond = energy_keV - fCrystalEffEnergies.back();
		if (!fWarnedEffClamp && beyond > 1.0e-6 * fCrystalEffEnergies.back()) {
			fWarnedEffClamp = true;
			G4cerr << "" << G4endl
				   << "TsForcedDetectionProjection WARNING: crystal-efficiency table CLAMPED."
				   << G4endl
				   << "  A photon at " << energy_keV << " keV is above the table's last point at "
				   << fCrystalEffEnergies.back() << " keV, so the efficiency is held at "
				   << fCrystalEffValues.back() << " instead of falling." << G4endl
				   << "  Photopeak efficiency drops steeply with energy: for 9.5 mm NaI a table"
				   << " ending at 440 keV would return 0.149 at 1 MeV, where the measured value is"
				   << " 0.042, so sensitivity would come out 3.6 times too high." << G4endl
				   << "  Extend CrystalEfficiencyEnergies and CrystalEfficiencyValues to cover"
				   << " the photons in this run." << G4endl;
		}
		return fCrystalEffValues.back();
	}

	for (size_t i = 0; i < fCrystalEffEnergies.size() - 1; i++) {
		if (energy_keV >= fCrystalEffEnergies[i] && energy_keV <= fCrystalEffEnergies[i + 1]) {
			G4double frac = (energy_keV - fCrystalEffEnergies[i]) /
							(fCrystalEffEnergies[i + 1] - fCrystalEffEnergies[i]);
			return fCrystalEffValues[i] + frac * (fCrystalEffValues[i + 1] - fCrystalEffValues[i]);
		}
	}

	return fCrystalEffValues.back();
}
