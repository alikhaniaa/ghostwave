#pragma once

#include <complex>
#include <cstddef>
#include <vector>

// Engine-independent, electroquasistatic approximation. All coordinates are
// metres, frequencies Hz, currents A, moments A*m, potentials V, fields V/m.
// Phasors are PEAK amplitudes with the exp(+i*2*pi*f*t) convention.
// See docs/SCIENCE.md before interpreting these quantities as fish perception.
namespace ghost::electro {

constexpr double pi = 3.1415926535897932384626433832795;
constexpr double epsilon0 = 8.8541878128e-12; // F/m
constexpr double defaultFrequencyHz = 720.0;
constexpr double defaultBodyLengthM = 0.20;
constexpr const char* speciesName = "Apteronotus leptorhynchus";

using Complex = std::complex<double>;
struct Vec3 { double x{}, y{}, z{}; };
struct CVec3 { Complex x{}, y{}, z{}; };
Vec3 operator+(Vec3 a, Vec3 b);
Vec3 operator-(Vec3 a, Vec3 b);
Vec3 operator*(Vec3 a, double scale);
Vec3 operator/(Vec3 a, double scale);
double dot(Vec3 a, Vec3 b);
double norm(Vec3 a);
Vec3 normalized(Vec3 a);
Vec3 cross(Vec3 a, Vec3 b);
CVec3 operator+(CVec3 a, CVec3 b);
CVec3 operator-(CVec3 a, CVec3 b);
CVec3 operator*(CVec3 a, Complex scale);
CVec3 operator*(Vec3 a, Complex scale);
Complex dot(CVec3 a, Vec3 b);
double norm(CVec3 a);

struct Material {
    double conductivitySm = 0.01;      // 100 microSiemens/cm
    double relativePermittivity = 80;  // Effective bulk value; not capacitance F.
};
using Medium = Material;
double conductivityFromMicroSiemensCm(double value);
Complex admittivity(const Material& material, double frequencyHz);

struct Dipole {
    Vec3 positionM{};
    Vec3 momentAm{2e-6, 0, 0}; // Chosen scale, not a calibrated individual fish.
    double phaseRad{};
    double coreRadiusM = 0.002; // C1 potential continuation ONLY inside this core.
};
struct FieldSample {
    Complex potentialV{};
    CVec3 fieldVm{};
    bool regularized{};
};
FieldSample sampleDipole(const Dipole& source, Vec3 pointM,
                        const Medium& water, double frequencyHz);
Complex dipolePotential(const Dipole& source, Vec3 pointM,
                        const Medium& water, double frequencyHz);
CVec3 dipoleField(const Dipole& source, Vec3 pointM,
                 const Medium& water, double frequencyHz);

struct Sphere {
    Vec3 positionM{};
    double radiusM = 0.004;
    Material material{0.03, 80};
};
Complex sphereContrast(const Material& object, const Medium& water,
                       double frequencyHz);
struct SphereResponse {
    Sphere object;
    CVec3 incidentFieldVm{};
    Complex contrast{};
    // radius / centre-to-source distance: small is necessary, not sufficient,
    // for the incident-uniform-field approximation. No accuracy guarantee.
    double sizeDistanceRatio{};
    bool sourceCoreOverlap{};
};
SphereResponse sphereResponse(const Dipole& source, const Sphere& object,
                              const Medium& water, double frequencyHz);
// Exact sphere response to its locally uniform incident field, including the
// bounded interior continuation. Does not include the original source field.
FieldSample sampleSphere(const SphereResponse& response, Vec3 pointM);

// Optional orientation-sensitive, far-field ellipsoidal polarizability model.
// A spheroid has one symmetry semi-axis and two equal transverse semi-axes.
struct Spheroid {
    Vec3 positionM{};
    Vec3 axis{1, 0, 0};
    double axialRadiusM = 0.012;
    double transverseRadiusM = 0.004;
    Material material{0.03, 80};
};
struct Depolarization { double axial{}, transverse{}; };
Depolarization spheroidDepolarization(double axialRadiusM, double transverseRadiusM);
struct SpheroidResponse {
    Vec3 positionM{};
    CVec3 inducedMomentAm{};
    Complex waterAdmittivitySm{};
    double boundingRadiusM{};
    double sizeDistanceRatio{};
    bool sourceCoreOverlap{};
};
SpheroidResponse spheroidResponse(const Dipole& source, const Spheroid& object,
                                  const Medium& water, double frequencyHz);
// Dipole term only: use well outside boundingRadiusM. Near-field results are
// regularized for numerical safety and are NOT an ellipsoid boundary solution.
FieldSample sampleSpheroid(const SpheroidResponse& response, Vec3 pointM);

struct BodyPose {
    Vec3 positionM{};
    Vec3 forward{1, 0, 0};
    Vec3 up{0, 0, 1};
    double lengthM = defaultBodyLengthM;
    double lateralRadiusM = 0.012;
    double verticalRadiusM = 0.025;
};
struct Receptor {
    Vec3 positionM{};
    Vec3 normal{};
    double longitudinal{}; // -1 tail to +1 nose
    double azimuthRad{};   // Around body, zero on the right flank.
};
// Synthetic ellipsoid samples, not an anatomical receptor-density map.
std::vector<Receptor> makeBodyReceptors(const BodyPose& pose,
                                      std::size_t longitudinalCount = 12,
                                      std::size_t radialCount = 8);
struct Emitter { Dipole source; double frequencyHz = defaultFrequencyHz; };
struct ReceptorSample {
    Complex baselineV{};
    Complex objectPerturbationV{};
    Complex externalBasebandV{};
    Complex totalBasebandV{};
    double amplitudeChangeV{};
    double fractionalAmplitudeChange{};
    double phaseShiftRad{};
    double rmsV{};
    bool carrierAvailable{};
    bool phaseAvailable{}; // False at either a baseline or total-field null.
    bool approximateNearField{};
};
// Surface extracellular potential is a proxy for transdermal perturbation.
// It omits the fish/skin boundary problem, spike encoding, and scattering of
// other emitters by objects. Multiple spheres use single scattering.
// baselineFloorV is a numerical normalization floor, NOT a sensory threshold.
std::vector<ReceptorSample> sampleReceptors(
    const Dipole& ownSource, const Medium& water, double ownFrequencyHz,
    const std::vector<Sphere>& objects, const std::vector<Receptor>& receptors,
    const std::vector<Emitter>& otherFish = {}, double timeSeconds = 0,
    double baselineFloorV = 1e-6);

struct Tone { Complex peakV{}; double frequencyHz{}; };
struct SignalMix {
    Complex basebandV{};
    double instantaneousV{};
    double envelopeV{};
    double narrowbandRmsV{};
    double phaseRad{};
};
// Exact analytic signal summation; the envelope-RMS interpretation assumes
// a carrier faster than envelope changes. Frequencies must be nonnegative.
SignalMix mixSignals(const std::vector<Tone>& tones, double referenceFrequencyHz,
                     double timeSeconds);
double beatFrequencyHz(double firstHz, double secondHz);
double twoToneModulationDepth(double firstPeakV, double secondPeakV);

} // namespace ghost::electro
