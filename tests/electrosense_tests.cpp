#include "electrosense.hpp"
#include "sensory_frame.hpp"

#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>

namespace e = ghost::electro;
namespace {
int checks = 0;
void check(bool value, const char* message) {
    ++checks;
    if (!value) throw std::runtime_error(message);
}
bool near(double a, double b, double relative=1e-9, double absolute=1e-13) {
    return std::abs(a-b)<=absolute+relative*std::max(std::abs(a),std::abs(b));
}
bool near(e::Complex a, e::Complex b, double relative=1e-9, double absolute=1e-13) {
    return std::abs(a-b)<=absolute+relative*std::max(std::abs(a),std::abs(b));
}
bool near(e::CVec3 a, e::CVec3 b, double relative=1e-9, double absolute=1e-13) {
    return near(a.x,b.x,relative,absolute)&&near(a.y,b.y,relative,absolute)&&near(a.z,b.z,relative,absolute);
}
template<class Function> void rejects(Function function, const char* message) {
    bool rejected = false;
    try { function(); } catch (const std::invalid_argument&) { rejected = true; }
    check(rejected,message);
}
e::Vec3 rotate(e::Vec3 v) { return {-v.y,v.x,v.z}; }
e::CVec3 rotate(e::CVec3 v) { return {-v.y,v.x,v.z}; }

void dipoleTests() {
    const e::Medium water{0.01,0};
    e::Dipole source;
    const double p = source.momentAm.x;
    const double scale = p/(4*e::pi*water.conductivitySm);
    const auto axial = e::sampleDipole(source,{0.2,0,0},water,720);
    const auto equatorial = e::sampleDipole(source,{0,0.2,0},water,720);
    check(near(axial.potentialV,{scale/(0.2*0.2),0}),"Axial potential has SI units and analytic magnitude");
    check(near(axial.fieldVm.x,{2*scale/std::pow(0.2,3),0}),"Axial field matches the point-current dipole");
    check(near(equatorial.potentialV,{}),"Equatorial potential vanishes");
    check(near(equatorial.fieldVm.x,{-scale/std::pow(0.2,3),0}),"Equatorial field is opposite the moment");
    check(near(e::norm(axial.fieldVm),2*e::norm(equatorial.fieldVm)),"Dipole axial to equatorial field ratio is two");
    const auto twice = e::sampleDipole(source,{0.4,0,0},water,720);
    check(near(axial.potentialV,twice.potentialV*4.0),"Potential decays as inverse distance squared");
    check(near(axial.fieldVm,twice.fieldVm*8.0),"Field decays as inverse distance cubed");
    const e::Vec3 point{0.12,0.07,-0.05};
    const auto original = e::sampleDipole(source,point,water,720);
    const auto inverted = e::sampleDipole(source,point*(-1),water,720);
    check(near(inverted.potentialV,-original.potentialV),"Dipole potential is odd under inversion");
    check(near(inverted.fieldVm,original.fieldVm),"Dipole field is even under inversion");
    source.positionM={3,-2,7};
    const auto translated=e::sampleDipole(source,point+source.positionM,water,720);
    check(near(translated.potentialV,original.potentialV),"Potential is translation invariant");
    check(near(translated.fieldVm,original.fieldVm),"Field is translation invariant");
    source.positionM={};source.momentAm=rotate(source.momentAm);
    const auto rotated=e::sampleDipole(source,rotate(point),water,720);
    check(near(rotated.potentialV,original.potentialV),"Potential is rotation covariant");
    check(near(rotated.fieldVm,rotate(original.fieldVm)),"Field direction is rotation covariant");
    source=e::Dipole{};
    check(near(e::dipoleField(source,point,{0.02,0},720),original.fieldVm*0.5),"Constant-current field scales inversely with water conductivity");
    source.momentAm=source.momentAm*3;
    check(near(e::dipoleField(source,point,water,720),original.fieldVm*3.0),"Field is linear in source strength");
    source=e::Dipole{};source.phaseRad=e::pi/2;
    check(near(e::dipoleField(source,point,water,720),original.fieldVm*e::Complex{0,1}),"Source phase multiplies the complete field coherently");
    source=e::Dipole{};
    for (const auto samplePoint : {point,e::Vec3{0.0005,0.0003,-0.0001}}) {
        const auto field=e::dipoleField(source,samplePoint,water,720);
        const double h=1e-7;
        const auto gx=-(e::dipolePotential(source,samplePoint+e::Vec3{h,0,0},water,720)-e::dipolePotential(source,samplePoint-e::Vec3{h,0,0},water,720))/(2*h);
        const auto gy=-(e::dipolePotential(source,samplePoint+e::Vec3{0,h,0},water,720)-e::dipolePotential(source,samplePoint-e::Vec3{0,h,0},water,720))/(2*h);
        const auto gz=-(e::dipolePotential(source,samplePoint+e::Vec3{0,0,h},water,720)-e::dipolePotential(source,samplePoint-e::Vec3{0,0,h},water,720))/(2*h);
        check(near(field,{gx,gy,gz},1e-7),"Electric field equals negative potential gradient outside and inside numerical core");
    }
    const auto inside=e::sampleDipole(source,{source.coreRadiusM*(1-1e-8),0,0},water,720);
    const auto outside=e::sampleDipole(source,{source.coreRadiusM*(1+1e-8),0,0},water,720);
    check(inside.regularized&&!outside.regularized,"Numerical core is explicitly marked");
    check(near(inside.potentialV,outside.potentialV,1e-7),"Core potential is continuous");
    check(near(inside.fieldVm,outside.fieldVm,1e-7),"Core field is continuous");
    check(std::isfinite(e::norm(e::sampleDipole(source,{},water,720).fieldVm)),"Source coincidence is finite with an enabled core");
    source.coreRadiusM=0;
    rejects([&]{e::sampleDipole(source,{},water,720);},"Exact point dipole rejects its singular point");
}

void materialAndSphereTests() {
    const e::Medium water{0.01,80};
    check(near(e::conductivityFromMicroSiemensCm(100),0.01),"Conductivity conversion is 100 microSiemens per cm to 0.01 S per m");
    check(near(e::admittivity(water,0),{0.01,0}),"Dielectric displacement current vanishes at DC");
    check(near(e::admittivity(water,720).imag(),2*e::pi*720*e::epsilon0*80),"Admittivity uses angular frequency and absolute permittivity");
    check(near(e::sphereContrast(water,water,720),{}),"Matched complex material creates no electric image");
    check(near(e::sphereContrast({0,0},water,0),{-0.5,0}),"Insulating sphere contrast tends to minus one half");
    check(near(e::sphereContrast({1e10,80},water,720),{1,0},1e-10),"Conducting sphere contrast tends to one");
    check(near(e::sphereContrast({0.03,80},water,0),{0.4,0}),"Finite conductive contrast uses object plus twice water in denominator");
    const auto capacitive=e::sphereContrast({0.01,1e6},water,720);
    check(capacitive.imag()>0.4,"High effective permittivity gives a measurable quadrature component");
    check(std::abs(e::sphereContrast({0.01,1e6},water,1000))>
          std::abs(e::sphereContrast({0.01,1e6},water,100)),"Capacitive contrast depends on carrier frequency");
    e::Sphere object{{0.3,0,0},0.006,{0.03,0}};
    const e::Dipole source;
    const e::Medium resistiveWater{0.01,0};
    const auto response=e::sphereResponse(source,object,resistiveWater,720);
    const auto sample=e::sampleSphere(response,{0,0,0});
    const double expected=-2*source.momentAm.x*0.4*std::pow(object.radiusM,3)/
                           (4*e::pi*0.01*std::pow(0.3,5));
    check(near(sample.potentialV,{expected,0}),"Active return includes source-to-object and object-to-receiver geometry");
    object.positionM={0.6,0,0};
    const auto farther=e::sampleSphere(e::sphereResponse(source,object,resistiveWater,720),{});
    check(near(sample.potentialV,farther.potentialV*32.0),"Coincident source-receiver active potential scales as r^-5 in dipole approximation");
    object.positionM={0.3,0,0};object.radiusM*=2;
    const auto larger=e::sampleSphere(e::sphereResponse(source,object,resistiveWater,720),{});
    check(near(larger.potentialV,sample.potentialV*8.0),"Small-object perturbation scales with volume");
    object.radiusM=0.006;object.material=resistiveWater;
    check(near(e::sampleSphere(e::sphereResponse(source,object,resistiveWater,720),{}).potentialV,{}),"Matched object vanishes from complete response");
    const e::Vec3 boundary=response.object.positionM+e::Vec3{response.object.radiusM,0,0};
    const auto in=e::sampleSphere(response,boundary-e::Vec3{1e-10,0,0});
    const auto out=e::sampleSphere(response,boundary+e::Vec3{1e-10,0,0});
    check(near(in.potentialV,out.potentialV,1e-7),"Sphere surface potential is continuous");
    const auto internalNormal=(response.incidentFieldVm.x+in.fieldVm.x)*response.object.material.conductivitySm;
    const auto externalNormal=(response.incidentFieldVm.x+out.fieldVm.x)*resistiveWater.conductivitySm;
    check(near(internalNormal,externalNormal,1e-7),"Sphere boundary conserves normal current density");
    e::Dipole oblique=source;oblique.momentAm={2e-6,1e-6,0};
    const e::Sphere dielectric{response.object.positionM,response.object.radiusM,{0.01,1e6}};
    const auto capacitiveResponse=e::sphereResponse(oblique,dielectric,water,720);
    const auto complexIn=e::sampleSphere(capacitiveResponse,boundary-e::Vec3{1e-10,0,0});
    const auto complexOut=e::sampleSphere(capacitiveResponse,boundary+e::Vec3{1e-10,0,0});
    const auto interiorCurrent=(capacitiveResponse.incidentFieldVm.x+complexIn.fieldVm.x)*e::admittivity(dielectric.material,720);
    const auto exteriorCurrent=(capacitiveResponse.incidentFieldVm.x+complexOut.fieldVm.x)*e::admittivity(water,720);
    check(near(interiorCurrent,exteriorCurrent,1e-7),"Capacitive sphere conserves complex normal current including displacement");
    check(near(complexIn.fieldVm.y,complexOut.fieldVm.y,1e-7),"Capacitive sphere conserves tangential electric field");
    const auto center=e::sampleSphere(response,response.object.positionM);
    check(near(center.potentialV,{})&&std::isfinite(e::norm(center.fieldVm)),"Sphere interior has no dipole singularity");
    check(response.sizeDistanceRatio<0.03&&!response.sourceCoreOverlap,"Response exposes incident-uniformity diagnostic");
    const auto touching=e::sphereResponse(source,{{0.003,0,0},0.005,{0.03,80}},water,720);
    check(touching.sourceCoreOverlap,"Overlapping source/object approximation is flagged");
}

void spheroidTests() {
    const auto sphere=e::spheroidDepolarization(1,1);
    const auto rod=e::spheroidDepolarization(4,1);
    const auto disc=e::spheroidDepolarization(1,4);
    check(near(sphere.axial,1.0/3)&&near(sphere.transverse,1.0/3),"Sphere depolarization is one third in every direction");
    check(near(rod.axial+2*rod.transverse,1)&&near(disc.axial+2*disc.transverse,1),"Spheroid depolarization factors sum to one");
    check(rod.axial<sphere.axial&&disc.axial>sphere.axial,"Depolarization respects prolate and oblate geometry");
    for(const double aspect : {0.0001,0.1,0.999999,1.000001,10.0,10000.0}) {
        const auto n=e::spheroidDepolarization(aspect,1);
        check(std::isfinite(n.axial)&&n.axial>0&&n.axial<1,"Depolarization stays finite for needles, discs and almost spheres");
    }
    const e::Dipole source;
    const e::Medium water;
    const e::Sphere ball{{0.3,0.04,0},0.006,{0.03,1e5}};
    e::Spheroid shape{ball.positionM,{1,0,0},ball.radiusM,ball.radiusM,ball.material};
    const auto spherical=e::sampleSphere(e::sphereResponse(source,ball,water,720),{});
    const auto spheroidal=e::sampleSpheroid(e::spheroidResponse(source,shape,water,720),{});
    check(near(spherical.potentialV,spheroidal.potentialV),"Spheroid far-field model reduces exactly to sphere model");
    check(near(spherical.fieldVm,spheroidal.fieldVm),"Spherical limit preserves all field components");
    shape.positionM={0.3,0,0};shape.axialRadiusM=0.02;shape.transverseRadiusM=0.004;
    shape.material={1000,80};
    const auto aligned=e::spheroidResponse(source,shape,water,720);
    shape.axis={0,1,0};
    const auto across=e::spheroidResponse(source,shape,water,720);
    check(e::norm(aligned.inducedMomentAm)>e::norm(across.inducedMomentAm)*2,
          "Rotating an elongated conductive target changes its induced response");
    check(e::sampleSpheroid(across,shape.positionM).regularized,"Near spheroid dipole approximation is explicitly flagged");
}

void samplingAndMixingTests() {
    e::BodyPose pose;
    const auto receptors=e::makeBodyReceptors(pose);
    check(receptors.size()==96,"Default body sampling includes twelve rings and eight azimuths");
    for(const auto& r : receptors) {
        const auto p=r.positionM-pose.positionM;
        const double ellipsoid=std::pow(p.x/(pose.lengthM/2),2)+
                               std::pow(p.y/pose.lateralRadiusM,2)+std::pow(p.z/pose.verticalRadiusM,2);
        check(near(ellipsoid,1)&&near(e::norm(r.normal),1),"Every receptor lies on the three-dimensional body with unit outward normal");
    }
    pose.forward={0,0,1};pose.up={0,0,1};
    const auto vertical=e::makeBodyReceptors(pose,2,4);
    check(vertical.size()==8&&std::isfinite(vertical.front().positionM.z),"Vertical orientation constructs a nonsingular body frame");
    const e::Dipole source;
    const e::Medium water;
    const auto clear=e::sampleReceptors(source,water,720,{},receptors);
    for(const auto& s : clear) {
        check(near(s.fractionalAmplitudeChange,0)&&near(s.phaseShiftRad,0)&&s.carrierAvailable,
              "An empty scene has no amplitude or phase perturbation");
    }
    const e::Sphere object{{0.06,-0.05,0},0.004,{0.03,80}};
    const auto one=e::sampleReceptors(source,water,720,{object},receptors);
    const auto two=e::sampleReceptors(source,water,720,{object,object},receptors);
    check(std::abs(one.front().objectPerturbationV)>0,"Nearby contrast is represented on receptor samples");
    check(near(two.front().objectPerturbationV,one.front().objectPerturbationV*2.0),"Single-scattering contributions superpose coherently");
    const auto nullSamples=e::sampleReceptors(source,water,720,{object},{{{0,0.03,0},{0,1,0},0,0}});
    check(!nullSamples.front().carrierAvailable&&std::isfinite(nullSamples.front().fractionalAmplitudeChange),
          "Dipole nodal plane does not fabricate a huge normalized sensory image");
    const auto cancelled=e::sampleReceptors(source,water,720,{},receptors,
        {{{source.positionM,source.momentAm,e::pi,source.coreRadiusM},720}},0);
    check(std::abs(cancelled.front().totalBasebandV)<1e-12,"Opposite-phase identical external field cancels at receptors");
    check(!cancelled.front().phaseAvailable,"Phase is explicitly unavailable at a total-field null");
    const auto beat=e::sampleReceptors(source,water,720,{},receptors,{{source,724}},0.125);
    // Water permittivity gives a tiny frequency-dependent source response.
    check(std::abs(beat.front().totalBasebandV)<std::abs(beat.front().baselineV)*1e-5,
          "External receptor signals rotate at the difference frequency");
    check(near(e::beatFrequencyHz(720,724),4)&&near(e::beatFrequencyHz(724,720),4),"Beat frequency is unsigned EOD difference");
    const std::vector<e::Tone> tones{{{2,0},720},{{1,0},724}};
    const auto peak=e::mixSignals(tones,720,0);
    const auto trough=e::mixSignals(tones,720,0.125);
    const auto repeated=e::mixSignals(tones,720,0.25);
    check(near(peak.envelopeV,3)&&near(trough.envelopeV,1),"Beat envelope has correct constructive and destructive limits");
    check(near(repeated.envelopeV,peak.envelopeV),"Envelope period is inverse difference frequency");
    check(near(e::twoToneModulationDepth(2,1),0.5)&&near(e::twoToneModulationDepth(1,2),0.5),"Two-tone modulation depth is symmetric and bounded");
    check(e::twoToneModulationDepth(0,0)==0,"Zero-amplitude pair has zero modulation depth");
    check(near(e::mixSignals({{{1,0},720},{{1,0},724}},720,0.125).envelopeV,0),"Equal-amplitude beats can cancel completely");
    check(near(peak.narrowbandRmsV,3/std::sqrt(2.0)),"RMS and peak envelope conventions are distinct");
    const double time=0.173;
    const auto mixed=e::mixSignals(tones,720,time);
    const double direct=2*std::cos(2*e::pi*720*time)+std::cos(2*e::pi*724*time);
    check(near(mixed.instantaneousV,direct),"Baseband reconstruction equals direct time-domain signal sum");
    check(near(e::mixSignals(tones,701,time).instantaneousV,direct),"Physical voltage is independent of demodulation reference frequency");
    check(near(e::mixSignals(tones,720,10000.25).envelopeV,3),"Long-running beat evaluation retains phase accuracy");
    check(near(e::mixSignals({},720,0).envelopeV,0),"Empty signal sum is zero");
}

void validationTests() {
    rejects([]{e::admittivity({-1,80},720);},"Negative conductivity is rejected");
    rejects([]{e::admittivity({0.01,-1},720);},"Negative permittivity is rejected");
    rejects([]{e::admittivity({},-720);},"Negative frequency is rejected");
    rejects([]{e::sampleDipole({}, {1,0,0},{0,0},720);},"Nonconducting nondielectric water is rejected");
    rejects([]{e::sphereResponse({},{{1,0,0},0,{}},{},720);},"Zero-sized target is rejected");
    rejects([]{e::spheroidDepolarization(-1,1);},"Negative spheroid semi-axis is rejected");
    rejects([]{e::makeBodyReceptors({},0,4);},"Empty receptor dimension is rejected");
    rejects([]{e::mixSignals({},720,std::numeric_limits<double>::quiet_NaN());},"Nonfinite sample time is rejected");
    rejects([]{e::sampleReceptors({}, {},720,{}, {},{},0,0);},"Invalid receptor normalization floor is rejected");
}

void liveBridgeTests() {
    ghost::Simulation simulation;
    simulation.player={600,600};simulation.altitude=240;
    simulation.facing={1,0};simulation.facingLift=0;
    simulation.water.conductivity=0.01f;simulation.tuning.bodyLength=12;
    simulation.sources.clear();simulation.food.clear();simulation.rocks.clear();
    const auto clear=ghost::evaluateSenses(simulation);
    check(clear.skinCount==48&&near(clear.bodyLengthM,0.2),"Bridge samples a 20 cm body at bounded receptor resolution");
    check(near(clear.positionM.x,10)&&near(clear.positionM.y,4)&&near(clear.positionM.z,10),
          "Live bridge converts world units to metres with Y up");
    check(near(clear.distortion,0)&&clear.nearestTargetIndex==-1,"Empty live scene has no fabricated electric return");
    simulation.food.push_back({{612,603},240,22,0,0,0});
    const auto prey=ghost::evaluateSenses(simulation);
    check(prey.targetCount==1&&prey.objectRmsV>0&&prey.nearestSkinAmplitudeV>0,
          "A real available food entity produces physical body perturbations");
    check(near(prey.targets[0].distanceM,std::hypot(12.0,3.0)/60),"Live target range includes explicit scale conversion");
    const e::Dipole own{prey.positionM,{2e-6,0,0},0,0.002};
    const e::Sphere sphere{ghost::sensoryPosition(simulation.food[0].position,simulation.food[0].height),0.0025,{0.03,80}};
    const auto expected=e::sphereResponse(own,sphere,{simulation.water.conductivity,80},simulation.frequency);
    check(near(prey.skin[0].signal.objectPerturbationV,e::sampleSphere(expected,prey.skin[0].receptor.positionM).potentialV-
                                                     e::sampleSphere(expected,prey.positionM).potentialV),
          "Live bridge preserves differential analytical voltage without artistic gain or common-mode potential");
    simulation.food[0].respawn=1;
    check(ghost::evaluateSenses(simulation).targetCount==0,"Consumed prey is absent from active electrolocation");
    simulation.food[0].respawn=0;simulation.food[0].position=simulation.player;simulation.food[0].height=simulation.altitude;
    const auto overlapping=ghost::evaluateSenses(simulation);
    check(overlapping.targets[0].excluded&&overlapping.approximationLimited&&overlapping.objectRmsV==0,
          "Target-source overlap is flagged and excluded instead of generating a false singularity");
    simulation.food.clear();
    ghost::Source neighbor;
    neighbor.origin=neighbor.position={636,600};neighbor.height=240;neighbor.frequency=724;
    neighbor.velocity={20,0};neighbor.amplitude=1;neighbor.bodyLength=12;
    simulation.sources.push_back(neighbor);
    const auto wave=ghost::evaluateSenses(simulation);
    check(wave.emitterCount==1&&near(wave.emitters[0].beatHz,4)&&wave.externalRmsV>0,
          "Wave neighbors enter the model with physical amplitude and correct beat frequency");
    simulation.sources[0].position={840,600};
    const auto fourMetres=ghost::evaluateSenses(simulation);
    simulation.sources[0].position={1080,600};
    const auto eightMetres=ghost::evaluateSenses(simulation);
    check(near(fourMetres.externalRmsV/eightMetres.externalRmsV,8,0.005),
          "Distant differential body signals follow the field's inverse-cube limit rather than a common-mode inverse-square potential");
    simulation.sources[0].position=neighbor.position;
    simulation.sources[0].species=ghost::FishSpecies::PulseKnifefish;
    simulation.sources[0].frequency=45;
    const auto pulse=ghost::evaluateSenses(simulation);
    check(pulse.emitterCount==0&&pulse.externalRmsV==0&&pulse.objectRmsV>0,
          "Pulse repetition rate is not treated as a wave carrier while its body remains polarizable");
    simulation.sources[0].species=ghost::FishSpecies::Catfish;
    simulation.sources[0].amplitude=0;
    check(ghost::evaluateSenses(simulation).emitterCount==0,"Non-electric fish do not acquire a fictitious EOD");
    simulation.sources[0]=neighbor;
    ghost::SensoryClock clock;
    simulation.elapsed=0;
    ghost::evaluateSenses(simulation,&clock);
    simulation.elapsed=0.01f;simulation.frequency=730;
    const auto changing=ghost::evaluateSenses(simulation,&clock);
    check(changing.phaseIntegrated&&near(clock.ownPhaseRad,
          std::remainder(e::pi*(720+730)*static_cast<double>(simulation.elapsed),2*e::pi)),
          "Persistent clock integrates retuning rather than multiplying new frequency by lifetime");
    const double previous=clock.ownPhaseRad;
    ghost::evaluateSenses(simulation,&clock);
    check(clock.ownPhaseRad==previous,"Repeated evaluation at one simulation instant does not advance phase");
    simulation.elapsed=0;ghost::evaluateSenses(simulation,&clock);
    check(clock.ownPhaseRad==0,"Clock resets on a restarted simulation timeline");
    simulation.sources.clear();simulation.frequency=720;
    for(int i=0;i<24;++i) {
        neighbor.position={static_cast<float>(624+i),600};neighbor.origin=neighbor.position;
        simulation.sources.push_back(neighbor);
        simulation.food.push_back({{static_cast<float>(610+i),603},240,22,0,0,i%3});
    }
    const auto crowded=ghost::evaluateSenses(simulation);
    check(crowded.targetCount==ghost::sensoryMaxTargets&&crowded.emitterCount==ghost::sensoryMaxEmitters&&
          crowded.targetBudgetLimited&&crowded.emitterBudgetLimited,"Dense scenes respect bounded target and emitter budgets");
    for(std::size_t i=1;i<crowded.targetCount;++i)
        check(crowded.targets[i].distanceM>=crowded.targets[i-1].distanceM,"Bounded target selection retains nearest-first ordering");
    simulation.sources.clear();simulation.food.clear();
    simulation.rocks.push_back({{650,600},45,0,500});
    const auto terrain=ghost::evaluateSenses(simulation);
    check(terrain.nearbyTerrainOmitted&&terrain.targetCount==0&&terrain.objectRmsV==0,
          "Huge terrain is disclosed as omitted rather than approximated by a tiny sphere");
    const auto vertical=ghost::evaluateSenses(simulation,0.0,e::pi/2);
    check(near(vertical.forward.y,1)&&std::isfinite(vertical.baselineRmsV),"Bridge supports vertical fish orientation");
}
} // namespace

int main() {
    try {
        dipoleTests();materialAndSphereTests();spheroidTests();samplingAndMixingTests();validationTests();liveBridgeTests();
        std::cout << "Electrosense: " << checks << " analytical checks passed.\n";
        return 0;
    } catch(const std::exception& error) {
        std::cerr << "Electrosense check " << checks << " failed: " << error.what() << '\n';
        return 1;
    }
}
