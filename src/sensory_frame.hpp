#pragma once

#include "electrosense.hpp"
#include "simulation.hpp"

#include <array>
#include <cmath>
#include <cstddef>
#include <limits>
#include <vector>

// Renderer-free bridge. Electrical coordinates use Y up:
// (simulation.position.x, simulation.altitude, simulation.position.y) / 60.
// Output voltages are physical model values, before any artistic display gain.
// This bridge uses a virtual reference at the body centre for perturbations;
// a spatially constant potential therefore cannot masquerade as a skin signal.
namespace ghost {
constexpr double sensoryWorldUnitsPerMetre = 60.0;
constexpr std::size_t sensoryReceptorCount = 48;
constexpr std::size_t sensoryMaxTargets = 12;
constexpr std::size_t sensoryMaxEmitters = 8;
constexpr std::size_t sensoryTrackedSources = 64;

inline electro::Vec3 sensoryPosition(Vec position, double altitude) {
    return {position.x/sensoryWorldUnitsPerMetre,altitude/sensoryWorldUnitsPerMetre,
            position.y/sensoryWorldUnitsPerMetre};
}
enum class SensedKind { Food, FishBody };
struct SensedTarget {
    SensedKind kind = SensedKind::Food;
    int index = -1; // Index in Simulation.food or Simulation.sources.
    electro::Vec3 positionM{};
    double boundingRadiusM{};
    double distanceM{}; // Player-centre to target-centre distance.
    double peakSkinAmplitudeV{}; // Absolute complex perturbation magnitude.
    double rmsSkinAmplitudeV{};  // RMS over receptors and carrier cycles.
    double fractionalAmplitudeChange{}; // At peak perturbation receptor.
    double phaseShiftRad{};              // Relative to its local baseline.
    std::size_t peakReceptor{};
    bool phaseAvailable{};
    bool approximationLimited{};
    bool excluded{}; // Overlaps source/body; no trustworthy dipole estimate.
};
struct SensedEmitter {
    int sourceIndex = -1;
    electro::Vec3 positionM{};
    double frequencyHz{},beatHz{},distanceM{},rmsSkinAmplitudeV{};
    bool approximationLimited{};
};
struct SkinSignal {
    electro::Receptor receptor;
    electro::ReceptorSample signal; // Surface-minus-centre voltage proxy.
};
struct SensoryFrame {
    double carrierHz{},waterSm{},bodyLengthM{};
    electro::Vec3 positionM{},forward{};
    std::array<SkinSignal,sensoryReceptorCount> skin{};
    std::size_t skinCount{};
    std::array<SensedTarget,sensoryMaxTargets> targets{};
    std::size_t targetCount{};
    std::array<SensedEmitter,sensoryMaxEmitters> emitters{};
    std::size_t emitterCount{};
    double baselineRmsV{},objectRmsV{},externalRmsV{},totalRmsV{};
    double distortion{}; // L2 norm of complex (object+external) / baseline.
    double amplitudeDistortion{}; // L2 amplitude-envelope change / baseline.
    double phaseRmsRad{};         // Baseline-power weighted, valid sites only.
    double peakSkinAmplitudeV{};  // Combined perturbation, maximum receptor.
    double nearestSkinAmplitudeV{},nearestSkinPhaseRad{};
    double nearestTargetDistanceM = std::numeric_limits<double>::infinity();
    int nearestTargetIndex = -1;  // Index into targets, not Simulation vectors.
    bool nearestPhaseAvailable{};
    bool approximationLimited{};
    bool nearbyTerrainOmitted{}; // Large cylinders need a boundary solver.
    bool targetBudgetLimited{},emitterBudgetLimited{};
    bool phaseIntegrated{};      // False for stateless snapshot evaluation.
};

// Keep one clock per running simulation for phase-continuous rendering.
// Frequencies between observations are integrated by the trapezoid rule;
// this is not an independently calibrated oscillator or audio synchronization.
struct SensoryClock {
    bool initialized{};
    std::uint32_t seed{};
    double previousTime{},ownPhaseRad{},previousOwnHz{};
    std::size_t sourceCount{};
    std::array<double,sensoryTrackedSources> sourcePhaseRad{},previousHz{};
    std::array<Vec,sensoryTrackedSources> origin{};
    std::array<FishSpecies,sensoryTrackedSources> species{};
    static double wrap(double value) { return std::remainder(value,2.0*electro::pi); }
    void update(const Simulation& simulation) {
        const double now=simulation.elapsed;
        const bool reset=!initialized || simulation.worldSeed!=seed || now<previousTime;
        const double dt=reset?0:std::max(0.0,now-previousTime);
        if(reset)ownPhaseRad=0;
        else ownPhaseRad=wrap(ownPhaseRad+electro::pi*(previousOwnHz+simulation.frequency)*dt);
        const std::size_t count=std::min(simulation.sources.size(),sensoryTrackedSources);
        for(std::size_t i=0;i<count;++i) {
            const auto& source=simulation.sources[i];
            const bool changed=reset || i>=sourceCount || source.origin.x!=origin[i].x ||
                               source.origin.y!=origin[i].y || source.species!=species[i];
            if(changed)sourcePhaseRad[i]=source.phase;
            else sourcePhaseRad[i]=wrap(sourcePhaseRad[i]+electro::pi*(previousHz[i]+source.frequency)*dt);
            previousHz[i]=source.frequency;origin[i]=source.origin;species[i]=source.species;
        }
        sourceCount=count;previousOwnHz=simulation.frequency;previousTime=now;
        seed=simulation.worldSeed;initialized=true;
    }
};

namespace sensory_detail {
inline electro::Vec3 direction(electro::Vec3 value, electro::Vec3 fallback={1,0,0}) {
    const double length=electro::norm(value);
    return std::isfinite(length)&&length>1e-8 ? value/length : fallback;
}
inline electro::Vec3 sourceDirection(const Source& source) {
    return direction({source.velocity.x,source.verticalVelocity,source.velocity.y},
                     direction({std::cos(source.phase),0,std::sin(source.phase)}));
}
struct Candidate {
    SensedKind kind{};
    int index{};
    double distanceM{};
};
template<std::size_t Count>
inline void insertNearest(std::array<Candidate,Count>& values,std::size_t& count,
                          Candidate value,bool& truncated) {
    std::size_t slot=0;
    while(slot<count && values[slot].distanceM<=value.distanceM)++slot;
    if(count==Count)truncated=true;
    if(slot>=Count)return;
    const std::size_t end=std::min(count,Count-1);
    for(std::size_t i=end;i>slot;--i)values[i]=values[i-1];
    values[slot]=value;
    count=std::min(count+1,Count);
}
inline void updateDerived(electro::ReceptorSample& sample) {
    constexpr double floorV=1e-6;
    sample.totalBasebandV=sample.baselineV+sample.objectPerturbationV+sample.externalBasebandV;
    const double baseline=std::abs(sample.baselineV),total=std::abs(sample.totalBasebandV);
    sample.amplitudeChangeV=total-baseline;
    sample.carrierAvailable=baseline>=floorV;
    sample.phaseAvailable=sample.carrierAvailable&&total>=floorV;
    sample.fractionalAmplitudeChange=sample.carrierAvailable?sample.amplitudeChangeV/baseline:0;
    sample.phaseShiftRad=sample.phaseAvailable?std::arg(sample.totalBasebandV/sample.baselineV):0;
    sample.rmsV=total/std::sqrt(2.0);
}
} // namespace sensory_detail

// At most 48 samples per selected target/emitter, plus baseline/reference
// samples, after linear scene selection. Targets beyond 2 m and emitters beyond 8 m are culled
// for cost, NOT because these are biological detection thresholds.
// Giant terrain cylinders are intentionally excluded from the sphere model.
inline SensoryFrame evaluateSenses(const Simulation& simulation,electro::Vec3 forward,
                                  SensoryClock* clock=nullptr) {
    namespace e=electro;
    using namespace sensory_detail;
    SensoryFrame frame;
    frame.carrierHz=simulation.frequency;
    frame.waterSm=simulation.water.conductivity;
    frame.bodyLengthM=simulation.tuning.bodyLength/sensoryWorldUnitsPerMetre;
    frame.positionM=sensoryPosition(simulation.player,simulation.altitude);
    frame.forward=direction(forward);
    if(clock)clock->update(simulation);
    frame.phaseIntegrated=clock!=nullptr;
    const double ownPhase=clock?clock->ownPhaseRad:0;
    const e::Medium water{frame.waterSm,80};
    const e::Dipole own{frame.positionM,frame.forward*2e-6,ownPhase,0.002};
    const double bodyScale=frame.bodyLengthM/e::defaultBodyLengthM;
    const auto receptors=e::makeBodyReceptors({frame.positionM,frame.forward,{0,1,0},
                                               frame.bodyLengthM,0.012*bodyScale,0.025*bodyScale},8,6);
    frame.skinCount=receptors.size();

    std::array<Candidate,sensoryMaxTargets> selectedTargets{};
    std::array<Candidate,sensoryMaxEmitters> selectedEmitters{};
    std::size_t targetCount=0,emitterCount=0;
    for(std::size_t i=0;i<simulation.food.size();++i) {
        const auto& food=simulation.food[i];
        if(!food.available())continue;
        const double distance=e::norm(sensoryPosition(food.position,food.height)-frame.positionM);
        if(distance<=2.0)insertNearest(selectedTargets,targetCount,{SensedKind::Food,static_cast<int>(i),distance},frame.targetBudgetLimited);
    }
    for(std::size_t i=0;i<simulation.sources.size();++i) {
        const auto& source=simulation.sources[i];
        const double distance=e::norm(sensoryPosition(source.position,source.height)-frame.positionM);
        if(distance<=2.0)insertNearest(selectedTargets,targetCount,{SensedKind::FishBody,static_cast<int>(i),distance},frame.targetBudgetLimited);
        const bool wave=source.species==FishSpecies::BrownGhost || source.species==FishSpecies::GlassKnifefish;
        if(wave && source.amplitude>0 && source.frequency>0 && distance<=8.0)
            insertNearest(selectedEmitters,emitterCount,{SensedKind::FishBody,static_cast<int>(i),distance},frame.emitterBudgetLimited);
    }
    std::vector<e::Emitter> external;
    external.reserve(emitterCount);
    for(std::size_t i=0;i<emitterCount;++i) {
        const int index=selectedEmitters[i].index;
        const auto& source=simulation.sources[static_cast<std::size_t>(index)];
        const auto position=sensoryPosition(source.position,source.height);
        // Relative amplitude is interpreted as a chosen current-moment scale.
        // No unsupported body-size-to-current law is imposed.
        double phase=source.phase;
        if(clock && static_cast<std::size_t>(index)<clock->sourceCount)phase=clock->sourcePhaseRad[static_cast<std::size_t>(index)];
        else phase+=ownPhase+2*e::pi*std::remainder((source.frequency-frame.carrierHz)*simulation.elapsed,1.0);
        external.push_back({{position,sourceDirection(source)*(2e-6*source.amplitude),phase,0.002},source.frequency});
        auto& emitter=frame.emitters[i];
        emitter.sourceIndex=index;emitter.positionM=position;emitter.frequencyHz=source.frequency;
        emitter.beatHz=e::beatFrequencyHz(source.frequency,frame.carrierHz);
        emitter.distanceM=selectedEmitters[i].distanceM;
        emitter.approximationLimited=emitter.distanceM<(source.bodyLength+simulation.tuning.bodyLength)/sensoryWorldUnitsPerMetre;
    }
    frame.emitterCount=emitterCount;
    // Clock phases are instantaneous; time zero avoids advancing them twice.
    const auto samples=e::sampleReceptors(own,water,frame.carrierHz,{},receptors);
    for(std::size_t i=0;i<frame.skinCount;++i)frame.skin[i]={receptors[i],samples[i]};
    for(std::size_t i=0;i<emitterCount;++i) {
        double sum=0;
        const auto reference=e::sampleDipole(external[i].source,frame.positionM,water,external[i].frequencyHz);
        for(std::size_t r=0;r<frame.skinCount;++r) {
            const auto other=e::sampleDipole(external[i].source,receptors[r].positionM,water,external[i].frequencyHz);
            const auto voltage=other.potentialV-reference.potentialV;
            frame.skin[r].signal.externalBasebandV+=voltage;
            frame.skin[r].signal.approximateNearField=frame.skin[r].signal.approximateNearField||other.regularized||reference.regularized;
            sum+=std::norm(voltage);
        }
        frame.emitters[i].rmsSkinAmplitudeV=std::sqrt(sum/(2*frame.skinCount));
        frame.approximationLimited=frame.approximationLimited||frame.emitters[i].approximationLimited;
    }

    for(std::size_t i=0;i<targetCount;++i) {
        const auto& selected=selectedTargets[i];
        auto& target=frame.targets[i];
        target.kind=selected.kind;target.index=selected.index;target.distanceM=selected.distanceM;
        e::SphereResponse sphere;
        e::SpheroidResponse spheroid;
        if(target.kind==SensedKind::Food) {
            const auto& food=simulation.food[static_cast<std::size_t>(target.index)];
            target.positionM=sensoryPosition(food.position,food.height);
            const double radii[]={0.0025,0.004,0.003};
            const int kind=std::clamp(food.kind,0,2);
            target.boundingRadiusM=radii[kind];
            // Chosen illustrative effective bulk tissue, not measured prey.
            const e::Material material{0.03,kind==2?1e5:80.0};
            sphere=e::sphereResponse(own,{target.positionM,target.boundingRadiusM,material},water,frame.carrierHz);
            target.approximationLimited=sphere.sizeDistanceRatio>0.2;
            target.excluded=sphere.sourceCoreOverlap;
        } else {
            const auto& source=simulation.sources[static_cast<std::size_t>(target.index)];
            target.positionM=sensoryPosition(source.position,source.height);
            target.boundingRadiusM=source.bodyLength/(2*sensoryWorldUnitsPerMetre);
            const e::Spheroid body{target.positionM,sourceDirection(source),target.boundingRadiusM,
                                    target.boundingRadiusM*0.12,{0.03,1e5}};
            spheroid=e::spheroidResponse(own,body,water,frame.carrierHz);
            target.approximationLimited=spheroid.sizeDistanceRatio>0.2;
            target.excluded=spheroid.sourceCoreOverlap;
        }
        // Reject intersecting model bodies rather than displaying a singular
        // or polynomial source-interior value as an environmental reading.
        for(const auto& receptor:receptors)
            if(e::norm(receptor.positionM-target.positionM)<=target.boundingRadiusM)
                target.excluded=true;
        target.approximationLimited=target.approximationLimited||target.excluded;
        double sum=0;
        const auto reference=target.excluded?e::Complex{}:
            target.kind==SensedKind::Food?e::sampleSphere(sphere,frame.positionM).potentialV:
                                          e::sampleSpheroid(spheroid,frame.positionM).potentialV;
        if(!target.excluded)for(std::size_t r=0;r<frame.skinCount;++r) {
            const auto response=target.kind==SensedKind::Food ? e::sampleSphere(sphere,receptors[r].positionM)
                                                             : e::sampleSpheroid(spheroid,receptors[r].positionM);
            auto& sample=frame.skin[r].signal;
            const auto voltage=response.potentialV-reference;
            sample.objectPerturbationV+=voltage;
            sample.approximateNearField=sample.approximateNearField||target.approximationLimited||response.regularized;
            const double amplitude=std::abs(voltage);
            sum+=amplitude*amplitude;
            if(amplitude>target.peakSkinAmplitudeV) {
                target.peakSkinAmplitudeV=amplitude;target.peakReceptor=r;
                const double baseline=std::abs(sample.baselineV);
                const auto total=sample.baselineV+voltage;
                target.phaseAvailable=baseline>=1e-6&&std::abs(total)>=1e-6;
                target.fractionalAmplitudeChange=baseline>=1e-6?(std::abs(total)-baseline)/baseline:0;
                target.phaseShiftRad=target.phaseAvailable?std::arg(total/sample.baselineV):0;
            }
        }
        target.rmsSkinAmplitudeV=std::sqrt(sum/(2*frame.skinCount));
        frame.approximationLimited=frame.approximationLimited||target.approximationLimited;
    }
    frame.targetCount=targetCount;
    for(std::size_t i=0;i<targetCount;++i)if(!frame.targets[i].excluded) {
        frame.nearestTargetIndex=static_cast<int>(i);
        frame.nearestTargetDistanceM=frame.targets[i].distanceM;
        frame.nearestSkinAmplitudeV=frame.targets[i].peakSkinAmplitudeV;
        frame.nearestSkinPhaseRad=frame.targets[i].phaseShiftRad;
        frame.nearestPhaseAvailable=frame.targets[i].phaseAvailable;
        break;
    }
    double baselinePower=0,objectPower=0,externalPower=0,totalPower=0;
    double perturbationPower=0,amplitudePower=0,phasePower=0,phaseWeight=0;
    for(std::size_t i=0;i<frame.skinCount;++i) {
        auto& sample=frame.skin[i].signal;
        updateDerived(sample);
        const double base=std::norm(sample.baselineV);
        baselinePower+=base;objectPower+=std::norm(sample.objectPerturbationV);
        externalPower+=std::norm(sample.externalBasebandV);totalPower+=std::norm(sample.totalBasebandV);
        const double perturbation=std::norm(sample.objectPerturbationV+sample.externalBasebandV);
        perturbationPower+=perturbation;amplitudePower+=sample.amplitudeChangeV*sample.amplitudeChangeV;
        frame.peakSkinAmplitudeV=std::max(frame.peakSkinAmplitudeV,std::sqrt(perturbation));
        if(sample.phaseAvailable) {phasePower+=base*sample.phaseShiftRad*sample.phaseShiftRad;phaseWeight+=base;}
        frame.approximationLimited=frame.approximationLimited||sample.approximateNearField;
    }
    const double normalization=2.0*frame.skinCount;
    frame.baselineRmsV=std::sqrt(baselinePower/normalization);
    frame.objectRmsV=std::sqrt(objectPower/normalization);
    frame.externalRmsV=std::sqrt(externalPower/normalization);
    frame.totalRmsV=std::sqrt(totalPower/normalization);
    if(baselinePower>1e-18) {
        frame.distortion=std::sqrt(perturbationPower/baselinePower);
        frame.amplitudeDistortion=std::sqrt(amplitudePower/baselinePower);
    }
    if(phaseWeight>0)frame.phaseRmsRad=std::sqrt(phasePower/phaseWeight);
    for(const auto& rock:simulation.rocks) {
        const double horizontal=std::max(0.0,static_cast<double>(distance(simulation.player,rock.position)-rock.radius));
        const double vertical=std::max({0.0,static_cast<double>(rock.bottom-simulation.altitude),
                                           static_cast<double>(simulation.altitude-rock.top)});
        if(std::hypot(horizontal,vertical)<sensoryWorldUnitsPerMetre) {
            frame.nearbyTerrainOmitted=true;break;
        }
    }
    return frame;
}
inline SensoryFrame evaluateSenses(const Simulation& simulation,SensoryClock* clock=nullptr) {
    return evaluateSenses(simulation,{simulation.facing.x,simulation.facingLift,simulation.facing.y},clock);
}
inline SensoryFrame evaluateSenses(const Simulation& simulation,double yawRad,double pitchRad,
                                  SensoryClock* clock=nullptr) {
    return evaluateSenses(simulation,{std::cos(yawRad)*std::cos(pitchRad),std::sin(pitchRad),
                                     std::sin(yawRad)*std::cos(pitchRad)},clock);
}
} // namespace ghost
