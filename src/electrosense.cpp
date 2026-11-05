#include "electrosense.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace ghost::electro {
namespace {
void require(bool valid, const char* message) {
    if (!valid) throw std::invalid_argument(message);
}
bool finite(Vec3 v) { return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z); }
void frequencyValid(double hz) {
    require(std::isfinite(hz) && hz >= 0, "Frequency must be finite and nonnegative");
}
Complex waterGamma(const Medium& water, double hz) {
    const auto gamma = admittivity(water, hz);
    require(std::abs(gamma) > 0, "Water must have nonzero admittivity");
    return gamma;
}
Complex rotation(double frequencyHz, double seconds) {
    // Reduce cycles before trig to retain precision during long sessions.
    const double cycles = std::remainder(frequencyHz * seconds, 1.0);
    return std::polar(1.0, 2.0 * pi * cycles);
}
FieldSample dipoleKernel(CVec3 momentAm, Vec3 displacementM,
                        Complex gamma, double coreRadiusM) {
    const double r2 = dot(displacementM, displacementM);
    const double radius = std::sqrt(r2);
    const Complex projection = dot(momentAm, displacementM);
    double potentialFactor{}, gradientFactor{};
    const bool regularized = radius < coreRadiusM;
    if (regularized) {
        // q(r)=(5-3r^2/a^2)/(2a^3) matches q=r^-3 and its first
        // derivative at a. E=-grad(q*p.r), including inside the core.
        const double a2 = coreRadiusM * coreRadiusM;
        const double a3 = a2 * coreRadiusM;
        potentialFactor = (2.5 - 1.5 * r2 / a2) / a3;
        gradientFactor = 3.0 / (a3 * a2);
    } else {
        require(radius > 0, "An unregularized dipole is singular at its source");
        potentialFactor = 1.0 / (r2 * radius);
        gradientFactor = 3.0 * potentialFactor / r2;
    }
    const Complex scale = 1.0 / (4.0 * pi * gamma);
    return {projection * potentialFactor * scale,
            (displacementM * (projection * gradientFactor) -
             momentAm * potentialFactor) * scale, regularized};
}
void sphereValid(const Sphere& object) {
    require(finite(object.positionM) && std::isfinite(object.radiusM) && object.radiusM > 0,
            "Sphere position must be finite and radius positive");
}
} // namespace

Vec3 operator+(Vec3 a, Vec3 b) { return {a.x+b.x, a.y+b.y, a.z+b.z}; }
Vec3 operator-(Vec3 a, Vec3 b) { return {a.x-b.x, a.y-b.y, a.z-b.z}; }
Vec3 operator*(Vec3 a, double s) { return {a.x*s, a.y*s, a.z*s}; }
Vec3 operator/(Vec3 a, double s) { return a*(1.0/s); }
double dot(Vec3 a, Vec3 b) { return a.x*b.x+a.y*b.y+a.z*b.z; }
double norm(Vec3 a) { return std::sqrt(dot(a,a)); }
Vec3 normalized(Vec3 a) {
    require(finite(a) && norm(a) > 0, "Direction must be finite and nonzero");
    return a/norm(a);
}
Vec3 cross(Vec3 a, Vec3 b) { return {a.y*b.z-a.z*b.y,a.z*b.x-a.x*b.z,a.x*b.y-a.y*b.x}; }
CVec3 operator+(CVec3 a, CVec3 b) { return {a.x+b.x,a.y+b.y,a.z+b.z}; }
CVec3 operator-(CVec3 a, CVec3 b) { return {a.x-b.x,a.y-b.y,a.z-b.z}; }
CVec3 operator*(CVec3 a, Complex s) { return {a.x*s,a.y*s,a.z*s}; }
CVec3 operator*(Vec3 a, Complex s) { return {a.x*s,a.y*s,a.z*s}; }
Complex dot(CVec3 a, Vec3 b) { return a.x*b.x+a.y*b.y+a.z*b.z; }
double norm(CVec3 a) { return std::sqrt(std::norm(a.x)+std::norm(a.y)+std::norm(a.z)); }

double conductivityFromMicroSiemensCm(double value) {
    require(std::isfinite(value) && value >= 0, "Conductivity must be finite and nonnegative");
    return value * 1e-4;
}
Complex admittivity(const Material& material, double frequencyHz) {
    frequencyValid(frequencyHz);
    require(std::isfinite(material.conductivitySm) && material.conductivitySm >= 0 &&
            std::isfinite(material.relativePermittivity) && material.relativePermittivity >= 0,
            "Material conductivity and permittivity must be finite and nonnegative");
    return {material.conductivitySm, 2.0*pi*frequencyHz*epsilon0*material.relativePermittivity};
}
FieldSample sampleDipole(const Dipole& source, Vec3 pointM,
                        const Medium& water, double frequencyHz) {
    require(finite(source.positionM) && finite(source.momentAm) && finite(pointM) &&
            std::isfinite(source.phaseRad) && std::isfinite(source.coreRadiusM) &&
            source.coreRadiusM >= 0, "Dipole parameters must be finite with a nonnegative core");
    return dipoleKernel(source.momentAm * std::polar(1.0, source.phaseRad),
                        pointM-source.positionM, waterGamma(water,frequencyHz), source.coreRadiusM);
}
Complex dipolePotential(const Dipole& source, Vec3 pointM,
                        const Medium& water, double frequencyHz) {
    return sampleDipole(source,pointM,water,frequencyHz).potentialV;
}
CVec3 dipoleField(const Dipole& source, Vec3 pointM,
                 const Medium& water, double frequencyHz) {
    return sampleDipole(source,pointM,water,frequencyHz).fieldVm;
}
Complex sphereContrast(const Material& object, const Medium& water, double frequencyHz) {
    const auto outside = waterGamma(water,frequencyHz);
    const auto inside = admittivity(object,frequencyHz);
    return (inside-outside)/(inside+2.0*outside);
}
SphereResponse sphereResponse(const Dipole& source, const Sphere& object,
                              const Medium& water, double frequencyHz) {
    sphereValid(object);
    const double distance = norm(object.positionM-source.positionM);
    return {object, dipoleField(source,object.positionM,water,frequencyHz),
            sphereContrast(object.material,water,frequencyHz),
            object.radiusM/std::max(distance,1e-30),
            distance < object.radiusM+source.coreRadiusM};
}
FieldSample sampleSphere(const SphereResponse& response, Vec3 pointM) {
    sphereValid(response.object);
    require(finite(pointM), "Sample position must be finite");
    const Vec3 r = pointM-response.object.positionM;
    const double r2 = dot(r,r);
    const double a = response.object.radiusM;
    const Complex projection = dot(response.incidentFieldVm,r);
    if (r2 < a*a) {
        // Uniform-field sphere interior: delta(phi)=K E0.r,
        // E_inside=E0-K E0=3*gamma_water/(gamma_object+2*gamma_water)*E0.
        return {response.contrast*projection,
                response.incidentFieldVm*(-response.contrast), true};
    }
    const double a3OverR3 = a*a*a/(r2*std::sqrt(r2));
    return {response.contrast*projection*a3OverR3,
            (r*(projection*(3.0/r2))-response.incidentFieldVm)*
                (response.contrast*a3OverR3), false};
}

Depolarization spheroidDepolarization(double a, double b) {
    require(std::isfinite(a) && std::isfinite(b) && a>0 && b>0,
            "Spheroid semi-axes must be finite and positive");
    const double ratio = b/a;
    double axial{};
    if (std::abs(ratio-1.0) < 1e-5) {
        // Expansion in q=1-b^2/a^2 avoids cancellation at the sphere.
        const double q = 1.0-ratio*ratio;
        axial = 1.0/3.0-2.0*q/15.0-2.0*q*q/35.0-2.0*q*q*q/63.0;
    } else if (a>b) {
        const double q = ratio*ratio;
        const double e = std::sqrt(1.0-q);
        // log((1+e)/(1-e)) = 2*log((1+e)/ratio) remains stable for long rods.
        axial = q/(e*e*e)*(std::log((1.0+e)/ratio)-e);
    } else {
        const double e = std::sqrt(ratio*ratio-1.0);
        axial = (1.0+1.0/(e*e))*(1.0-std::atan(e)/e);
    }
    return {axial,(1.0-axial)/2.0};
}
SpheroidResponse spheroidResponse(const Dipole& source, const Spheroid& object,
                                  const Medium& water, double frequencyHz) {
    require(finite(object.positionM), "Spheroid position must be finite");
    const Vec3 axis = normalized(object.axis);
    const auto n = spheroidDepolarization(object.axialRadiusM,object.transverseRadiusM);
    const auto outside = waterGamma(water,frequencyHz);
    const auto contrast = admittivity(object.material,frequencyHz)-outside;
    const auto incident = dipoleField(source,object.positionM,water,frequencyHz);
    const CVec3 parallel = axis*dot(incident,axis);
    const CVec3 transverse = incident-parallel;
    const double volume = (4.0*pi/3.0)*object.axialRadiusM*
                           object.transverseRadiusM*object.transverseRadiusM;
    const auto induced = (parallel*(contrast/(outside+n.axial*contrast))+
                          transverse*(contrast/(outside+n.transverse*contrast)))*(volume*outside);
    const double bound = std::max(object.axialRadiusM,object.transverseRadiusM);
    const double distance = norm(object.positionM-source.positionM);
    return {object.positionM,induced,outside,bound,bound/std::max(distance,1e-30),
            distance < bound+source.coreRadiusM};
}
FieldSample sampleSpheroid(const SpheroidResponse& response, Vec3 pointM) {
    require(finite(pointM) && response.boundingRadiusM>0 && std::abs(response.waterAdmittivitySm)>0,
            "Invalid spheroid response");
    return dipoleKernel(response.inducedMomentAm,pointM-response.positionM,
                        response.waterAdmittivitySm,response.boundingRadiusM);
}

std::vector<Receptor> makeBodyReceptors(const BodyPose& pose,
                                      std::size_t longitudinalCount, std::size_t radialCount) {
    require(finite(pose.positionM) && std::isfinite(pose.lengthM) && pose.lengthM>0 &&
            std::isfinite(pose.lateralRadiusM) && pose.lateralRadiusM>0 &&
            std::isfinite(pose.verticalRadiusM) && pose.verticalRadiusM>0,
            "Body dimensions must be finite and positive");
    require(longitudinalCount>0 && radialCount>0 &&
            longitudinalCount<=4096 && radialCount<=4096,
            "Receptor sampling dimensions must lie between 1 and 4096");
    const Vec3 forward = normalized(pose.forward);
    // Gram-Schmidt; fall back deterministically if up is parallel to forward.
    Vec3 up = pose.up-forward*dot(pose.up,forward);
    if (!finite(up) || norm(up)<1e-10) {
        const Vec3 seed = std::abs(forward.z)<0.9 ? Vec3{0,0,1} : Vec3{0,1,0};
        up = seed-forward*dot(seed,forward);
    }
    up = normalized(up);
    const Vec3 right = normalized(cross(forward,up));
    const double halfLength = pose.lengthM*0.5;
    std::vector<Receptor> receptors;
    receptors.reserve(longitudinalCount*radialCount);
    for (std::size_t i=0; i<longitudinalCount; ++i) {
        const double longitudinal = -1.0+2.0*(static_cast<double>(i)+0.5)/
                                                   static_cast<double>(longitudinalCount);
        const double radialScale = std::sqrt(1.0-longitudinal*longitudinal);
        for (std::size_t j=0; j<radialCount; ++j) {
            const double angle = 2.0*pi*static_cast<double>(j)/static_cast<double>(radialCount);
            const double y = radialScale*std::cos(angle);
            const double z = radialScale*std::sin(angle);
            receptors.push_back({pose.positionM+forward*(halfLength*longitudinal)+
                                 right*(pose.lateralRadiusM*y)+up*(pose.verticalRadiusM*z),
                                 normalized(forward*(longitudinal/halfLength)+
                                            right*(y/pose.lateralRadiusM)+up*(z/pose.verticalRadiusM)),
                                 longitudinal,angle});
        }
    }
    return receptors;
}

std::vector<ReceptorSample> sampleReceptors(
    const Dipole& ownSource, const Medium& water, double ownFrequencyHz,
    const std::vector<Sphere>& objects, const std::vector<Receptor>& receptors,
    const std::vector<Emitter>& otherFish, double timeSeconds, double baselineFloorV) {
    frequencyValid(ownFrequencyHz);
    require(std::isfinite(timeSeconds) && std::isfinite(baselineFloorV) && baselineFloorV>0,
            "Time must be finite and baseline floor finite and positive");
    std::vector<SphereResponse> responses;
    responses.reserve(objects.size());
    for (const auto& object : objects) responses.push_back(sphereResponse(ownSource,object,water,ownFrequencyHz));
    std::vector<Complex> rotations;
    rotations.reserve(otherFish.size());
    for (const auto& other : otherFish) {
        frequencyValid(other.frequencyHz);
        rotations.push_back(rotation(other.frequencyHz-ownFrequencyHz,timeSeconds));
    }
    std::vector<ReceptorSample> result;
    result.reserve(receptors.size());
    for (const auto& receptor : receptors) {
        ReceptorSample sample;
        const auto own = sampleDipole(ownSource,receptor.positionM,water,ownFrequencyHz);
        sample.baselineV = own.potentialV;
        sample.approximateNearField = own.regularized;
        for (const auto& response : responses) {
            const auto scattered = sampleSphere(response,receptor.positionM);
            sample.objectPerturbationV += scattered.potentialV;
            sample.approximateNearField = sample.approximateNearField || scattered.regularized ||
                                         response.sourceCoreOverlap || response.sizeDistanceRatio>0.2;
        }
        for (std::size_t i=0; i<otherFish.size(); ++i) {
            const auto other = sampleDipole(otherFish[i].source,receptor.positionM,water,otherFish[i].frequencyHz);
            sample.externalBasebandV += other.potentialV*rotations[i];
            sample.approximateNearField = sample.approximateNearField || other.regularized;
        }
        sample.totalBasebandV = sample.baselineV+sample.objectPerturbationV+sample.externalBasebandV;
        const double baseline = std::abs(sample.baselineV);
        const double total = std::abs(sample.totalBasebandV);
        sample.amplitudeChangeV = total-baseline;
        sample.carrierAvailable = baseline>=baselineFloorV;
        sample.phaseAvailable = sample.carrierAvailable && total>=baselineFloorV;
        if (sample.carrierAvailable) {
            sample.fractionalAmplitudeChange = sample.amplitudeChangeV/baseline;
            // At a total-field null its phase is undefined. Return a stable
            // zero, while totalBasebandV still exposes that cancellation.
            if (sample.phaseAvailable) sample.phaseShiftRad = std::arg(sample.totalBasebandV/sample.baselineV);
        }
        sample.rmsV = total/std::sqrt(2.0);
        result.push_back(sample);
    }
    return result;
}

SignalMix mixSignals(const std::vector<Tone>& tones, double referenceFrequencyHz,
                     double timeSeconds) {
    frequencyValid(referenceFrequencyHz);
    require(std::isfinite(timeSeconds), "Time must be finite");
    SignalMix result;
    for (const auto& tone : tones) {
        frequencyValid(tone.frequencyHz);
        require(std::isfinite(tone.peakV.real()) && std::isfinite(tone.peakV.imag()),
                "Tone phasor must be finite");
        result.basebandV += tone.peakV*rotation(tone.frequencyHz-referenceFrequencyHz,timeSeconds);
    }
    result.instantaneousV = (result.basebandV*rotation(referenceFrequencyHz,timeSeconds)).real();
    result.envelopeV = std::abs(result.basebandV);
    result.narrowbandRmsV = result.envelopeV/std::sqrt(2.0);
    result.phaseRad = std::arg(result.basebandV);
    return result;
}
double beatFrequencyHz(double firstHz, double secondHz) {
    frequencyValid(firstHz); frequencyValid(secondHz);
    return std::abs(firstHz-secondHz);
}
double twoToneModulationDepth(double firstPeakV, double secondPeakV) {
    require(std::isfinite(firstPeakV) && std::isfinite(secondPeakV) && firstPeakV>=0 && secondPeakV>=0,
            "Tone amplitudes must be finite and nonnegative");
    const double larger = std::max(firstPeakV,secondPeakV);
    return larger>0 ? std::min(firstPeakV,secondPeakV)/larger : 0;
}
} // namespace ghost::electro
