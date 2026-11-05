#pragma once
#include "simulation.hpp"
#include "raylib.h"
#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstring>

namespace ghost {
namespace audio_detail {
// Authored audification of electric signals and water movement, not fish calls.
constexpr double tau=6.28318530717958647692, sampleRate=48000.0;
constexpr int sourceCount=24, cueCount=11;
enum CueKind { PulseCue, DashCue, RevealCue, RecordCue, ExitCue, WinCue, LossCue, ListenerCue,
               DiscoveryCue, FoodCue, RestCue };
static_assert(std::atomic<float>::is_always_lock_free && std::atomic<unsigned>::is_always_lock_free,
              "The audio callback needs lock-free parameter publication.");

struct SourceTarget {
    std::atomic<float> hz{720}, level{0}, pan{0}, threat{0}, pulsed{0};
};
struct CueTarget {
    std::atomic<float> pan{0}, strength{1}, pitch{1};
    std::atomic<unsigned> epoch{0};
};
struct Parameters {
    std::atomic<float> master{0}, environment{.2f}, ownHz{720}, emission{1}, jam{0}, flow{0};
    std::atomic<float> recording{0}, progress{0}, recordPan{0}, exitLevel{0}, exitPan{0};
    std::atomic<unsigned> pending{0}, epoch{0};
    std::array<SourceTarget,sourceCount> sources;
    std::array<CueTarget,cueCount> cues;
};
struct SourceVoice {
    double phase{}, lowPhase{}, flutter{}, hz=720;
    float level{}, left=.7071f, right=.7071f, threat{}, pulsed{};
};
struct CueVoice {
    int kind=-1;
    double age{}, a{}, b{}, c{};
    float left=.7071f, right=.7071f, strength=1, pitch=1, release=1;
    bool retiring{};
};
struct Mixer {
    double ownPhase{}, bassA{}, bassB{}, drift{}, scanPhase{}, scanBeat{}, exitPhase{}, exitBeat{}, hz=720;
    float master{}, environment{}, emission{}, jam{}, flow{}, recording{}, progress{}, exitLevel{};
    float recordLeft=.7071f, recordRight=.7071f, exitLeft=.7071f, exitRight=.7071f;
    float waterL{}, waterR{}, grainL{}, grainR{}, lowpassL{}, lowpassR{};
    float previousL{}, previousR{}, dcL{}, dcR{};
    std::uint32_t random=712367;
    unsigned epoch{};
    std::array<SourceVoice,sourceCount> sources;
    std::array<CueVoice,16> cues;
};

// Only atomics cross threads. All phases, envelopes, filters and noise live in
// the callback-owned mixer; it never dereferences a Simulation or allocates.
inline Parameters parameters;
inline Mixer mixer;
inline std::atomic<bool> streamClaimed{false};
// Linear interpolation keeps oscillator error below -115 dB at negligible cost.
// This table is built before main; the real-time path does no trigonometric work.
inline const std::array<float,2048> sineTable=[] {
    std::array<float,2048> table{};
    for(unsigned i=0;i<table.size();++i) table[i]=float(std::sin(tau*i/table.size()));
    return table;
}();
inline float sine(double phase) {
    const double index=phase*(2048/tau);
    const unsigned whole=unsigned(index);
    const float a=sineTable[whole&2047],b=sineTable[(whole+1)&2047];
    return a+(b-a)*float(index-whole);
}
inline double biphasic(double phase,float hz) {
    const double x=(phase<tau*.5?phase:phase-tau)/(tau*hz*.0007);
    const double edge=std::max(0.0,1-std::abs(x));
    return 9.48148*x*edge*edge*edge;
}
inline float smooth(float& value,float target,float rate=.0006f) {
    value+=(target-value)*rate;
    if(target==0 && std::abs(value)<.0000001f) value=0; // Avoid denormals during long mutes.
    return value;
}
// Float state can stall a 724 Hz ramp at 723.939 Hz as its increment rounds away.
// Keep carrier smoothing as precise as phase integration for accurate beating.
inline double smooth(double& value,double target,double rate=.0006) {
    value+=(target-value)*rate;
    return value;
}
inline double advance(double& phase,double hz) {
    phase+=tau*hz/sampleRate;
    if(phase>=tau) phase-=tau;
    return phase;
}
inline float whiteNoise() {
    mixer.random=mixer.random*1664525u+1013904223u;
    return float(mixer.random>>8)*(1.f/8388608.f)-1.f;
}
inline float leftGain(float pan) { return std::sqrt(.5f*(1-clamp(pan,-.9f,.9f))); }
inline float rightGain(float pan) { return std::sqrt(.5f*(1+clamp(pan,-.9f,.9f))); }
inline float envelope(double age,double delay,double duration,double attack=.028) {
    const double t=age-delay;
    if(t<=0 || t>=duration) return 0;
    const double a=std::min(1.0,t/attack), tail=1-t/duration;
    return float(a*a*(3-2*a)*tail*tail);
}
inline float cueSample(CueVoice& v,float grain) {
    v.age+=1/sampleRate;
    const double t=v.age;
    double length=1, sample=0;
    switch(v.kind) {
        case PulseCue: {
            length=.86;
            const double fall=std::max(0.0,1-t/length);
            advance(v.a,(215+390*fall*fall)*v.pitch);
            sample=.112*envelope(t,0,length,.018)*(sine(v.a)+.12*sine(v.a*2));
            break;
        }
        case DashCue:
            length=.48; advance(v.a,52+54*std::max(0.0,1-t/length));
            sample=envelope(t,0,length,.03)*(.035*sine(v.a)+.12*grain);
            break;
        case RevealCue:
            length=1.3; advance(v.a,438*v.pitch);advance(v.b,657*v.pitch);
            sample=.059*(envelope(t,0,.95)*sine(v.a)+.55*envelope(t,.19,1.1)*sine(v.b));
            break;
        case RecordCue:
            length=1.9;advance(v.a,330*v.pitch);advance(v.b,495*v.pitch);
            sample=.072*(envelope(t,0,1.5,.04)*sine(v.a)+.7*envelope(t,.22,1.65,.04)*sine(v.b));
            break;
        case ExitCue:
            length=2.75;advance(v.a,261.63);advance(v.b,329.63);advance(v.c,392);
            sample=.058*(envelope(t,0,2.1,.06)*sine(v.a)+.75*envelope(t,.28,2.1,.06)*sine(v.b)+
                         .6*envelope(t,.56,2.15,.06)*sine(v.c));
            break;
        case WinCue:
            length=3.7;advance(v.a,220);advance(v.b,330);advance(v.c,440);
            sample=.056*(envelope(t,0,3.5,.09)*sine(v.a)+.7*envelope(t,.22,3.4,.09)*sine(v.b)+
                         .45*envelope(t,.45,3.2,.09)*sine(v.c));
            break;
        case LossCue:
            length=2.4;advance(v.a,76+92*std::max(0.0,1-t/length));advance(v.b,83+88*std::max(0.0,1-t/length));
            sample=.073*envelope(t,0,length,.06)*(sine(v.a)+.35*sine(v.b));
            break;
        case ListenerCue:
            length=1.6;advance(v.a,91+4*sine(t*3));advance(v.b,143);
            sample=.052*envelope(t,0,length,.11)*(sine(v.a)+.3*sine(v.b)+grain*.4);
            break;
        case DiscoveryCue:
            length=2.8;advance(v.a,146.83*v.pitch);advance(v.b,220*v.pitch);
            sample=.047*(envelope(t,0,2.7,.15)*sine(v.a)+.55*envelope(t,.24,2.5,.16)*sine(v.b));
            break;
        case FoodCue:
            length=.46;advance(v.a,115+120*std::max(0.0,1-t/length));
            sample=envelope(t,0,length,.015)*(.041*sine(v.a)+.17*grain);
            break;
        case RestCue:
            length=2.2;advance(v.a,110);advance(v.b,165);
            sample=.041*envelope(t,0,length,.18)*(sine(v.a)+.35*sine(v.b));
            break;
        default: v.kind=-1;return 0;
    }
    if(v.retiring) v.release*=.998f;
    if(t>=length || v.release<.0001f) v.kind=-1;
    return float(sample)*v.strength*v.release;
}

inline void callback(void* buffer,unsigned frames) {
    auto* out=static_cast<float*>(buffer);
    auto load=[](const std::atomic<float>& v) { return v.load(std::memory_order_relaxed); };
    const float master=load(parameters.master), environment=load(parameters.environment);
    const float ownHz=load(parameters.ownHz), emission=load(parameters.emission);
    const float jam=load(parameters.jam), flow=load(parameters.flow), recording=load(parameters.recording);
    const float progress=load(parameters.progress), exitLevel=load(parameters.exitLevel);
    const float recordLeft=leftGain(load(parameters.recordPan)), recordRight=rightGain(load(parameters.recordPan));
    const float exitLeft=leftGain(load(parameters.exitPan)), exitRight=rightGain(load(parameters.exitPan));
    struct Target { float hz,level,left,right,threat,pulsed; bool active; };
    std::array<Target,sourceCount> targets;
    const double hzDamping=std::pow(1-.0005,double(frames)),panDamping=std::pow(1-.0006,double(frames));
    for(int j=0;j<sourceCount;++j) {
        const auto& s=parameters.sources[j];const float pan=load(s.pan);
        auto& voice=mixer.sources[j];auto& target=targets[j];
        target={load(s.hz),load(s.level),leftGain(pan),rightGain(pan),load(s.threat),load(s.pulsed),true};
        target.active=voice.level!=0 || voice.threat!=0 || target.level!=0 || target.threat!=0;
        if(!target.active) {
            // Advance distant carriers in one exact ramp integral; their phase is
            // retained when a different habitat comes within hearing distance.
            const double delta=voice.hz-target.hz;
            const double phaseStep=tau/sampleRate*(target.hz*frames+delta*.9995*(1-hzDamping)/.0005);
            voice.phase=std::fmod(voice.phase+phaseStep,tau);
            voice.lowPhase=std::fmod(voice.lowPhase+phaseStep*.125,tau);
            voice.flutter=std::fmod(voice.flutter+tau*.65*frames/sampleRate,tau);
            voice.hz=target.hz+delta*hzDamping;
            voice.left=float(target.left+(voice.left-target.left)*panDamping);
            voice.right=float(target.right+(voice.right-target.right)*panDamping);
            voice.pulsed=float(target.pulsed+(voice.pulsed-target.pulsed)*panDamping);
        }
    }
    const unsigned pending=parameters.pending.exchange(0,std::memory_order_acquire);
    const unsigned epoch=parameters.epoch.load(std::memory_order_acquire);
    if(epoch!=mixer.epoch) {
        for(auto& cue:mixer.cues) cue.retiring=true;
        mixer.epoch=epoch;
    }
    for(int k=0;k<cueCount;++k) if(pending&(1u<<k)) {
        if(parameters.cues[k].epoch.load(std::memory_order_relaxed)!=epoch) continue;
        for(auto& cue:mixer.cues) if(cue.kind<0) {
            const auto& target=parameters.cues[k];const float pan=load(target.pan);
            cue=CueVoice{};cue.kind=k;cue.left=leftGain(pan);cue.right=rightGain(pan);
            cue.strength=load(target.strength);cue.pitch=load(target.pitch);
            break; // Coalesce simultaneous identical returns; never steal a sounding voice.
        }
    }
    for(unsigned i=0;i<frames;++i) {
        smooth(mixer.master,master,.0009f);smooth(mixer.environment,environment);
        smooth(mixer.hz,ownHz,.0007);smooth(mixer.emission,emission,.0008f);
        smooth(mixer.jam,jam);smooth(mixer.flow,flow,.0003f);smooth(mixer.recording,recording,.001f);
        smooth(mixer.progress,progress);smooth(mixer.exitLevel,exitLevel);
        smooth(mixer.recordLeft,recordLeft);smooth(mixer.recordRight,recordRight);
        smooth(mixer.exitLeft,exitLeft);smooth(mixer.exitRight,exitRight);
        const float noiseL=whiteNoise(),noiseR=whiteNoise();
        smooth(mixer.waterL,noiseL,.012f);smooth(mixer.waterR,noiseR,.012f);
        smooth(mixer.grainL,noiseL,.075f);smooth(mixer.grainR,noiseR,.075f);
        const float grain=(mixer.grainL+mixer.grainR-mixer.waterL-mixer.waterR)*.5f;
        advance(mixer.bassA,47);advance(mixer.bassB,63.17);advance(mixer.drift,.073);
        const double deep=.012*(sine(mixer.bassA)+.43*sine(mixer.bassB))*(.85+.15*sine(mixer.drift));
        const float water=.12f+.08f*mixer.flow, texture=.018f+.055f*mixer.flow+.10f*mixer.jam;
        double left=deep+water*mixer.waterL+texture*(mixer.grainL-mixer.waterL);
        double right=deep+water*mixer.waterR+texture*(mixer.grainR-mixer.waterR);
        advance(mixer.ownPhase,mixer.hz);
        const double self=.031*mixer.emission*(sine(mixer.ownPhase)+.075*sine(mixer.ownPhase*2));
        left+=self*.7071;right+=self*.7071;
        for(int j=0;j<sourceCount;++j) {
            auto& s=mixer.sources[j];const auto& target=targets[j];
            if(!target.active) continue;
            smooth(s.hz,target.hz,.0005);smooth(s.level,target.level,.00035f);
            smooth(s.left,target.left);smooth(s.right,target.right);smooth(s.threat,target.threat,.0003f);
            smooth(s.pulsed,target.pulsed);
            advance(s.phase,s.hz);advance(s.lowPhase,s.hz*.125);advance(s.flutter,.65+1.6*s.threat);
            if(s.level<.000001f && s.threat<.00001f) continue;
            // The actual continuous carrier makes near-frequency beating audible.
            const double wave=sine(s.phase)+.06*sine(s.phase*2);
            const double pulse=s.pulsed>.00001f?biphasic(s.phase,s.hz):0;
            const double electric=s.level*((1-s.pulsed)*wave+2.3*s.pulsed*pulse);
            const double flutter=.5+.5*sine(s.flutter);
            const double breath=.022*s.threat*(.45+.55*flutter*flutter)*sine(s.lowPhase);
            left+=(electric+breath)*s.left;right+=(electric+breath)*s.right;
        }
        advance(mixer.scanPhase,225+145*mixer.progress);advance(mixer.scanBeat,5+3*mixer.progress);
        const double scan=.024*mixer.recording*(.6+.4*sine(mixer.scanBeat))*sine(mixer.scanPhase);
        left+=scan*mixer.recordLeft;right+=scan*mixer.recordRight;
        advance(mixer.exitPhase,196);advance(mixer.exitBeat,.42);
        const double swell=std::max(0.f,sine(mixer.exitBeat)),squared=swell*swell;
        const double beacon=.029*mixer.exitLevel*squared*squared*squared*
                            (sine(mixer.exitPhase)+.18*sine(mixer.exitPhase*2));
        left+=beacon*mixer.exitLeft;right+=beacon*mixer.exitRight;
        left*=mixer.environment;right*=mixer.environment;
        for(auto& cue:mixer.cues) if(cue.kind>=0) {
            const float sample=cueSample(cue,grain);left+=sample*cue.left;right+=sample*cue.right;
        }
        // Underwater roll-off, DC removal, and a soft ceiling below -12 dBFS.
        smooth(mixer.lowpassL,float(left),.19f);smooth(mixer.lowpassR,float(right),.19f);
        mixer.dcL=mixer.lowpassL-mixer.previousL+.997f*mixer.dcL;mixer.previousL=mixer.lowpassL;
        mixer.dcR=mixer.lowpassR-mixer.previousR+.997f*mixer.dcR;mixer.previousR=mixer.lowpassR;
        const float l=mixer.dcL*mixer.master,r=mixer.dcR*mixer.master;
        out[i*2]=l/(1+std::abs(l)/.24f);out[i*2+1]=r/(1+std::abs(r)/.24f);
    }
}
} // namespace audio_detail

class Audio {
    AudioStream stream{};
    bool ready{}, ownsDevice{}, ownsClaim{};
    float previousTime=-1, previousProgress{};
    std::uint64_t previousEvents{};
    RunState previousState=RunState::Playing;
    static float panAt(Vec position,const Simulation& sim,float yaw,float altitude) {
        const Vec offset=position-sim.player;
        const float d=std::max(1.f,distance3(position,altitude,sim.player,sim.altitude));
        return clamp((-std::sin(yaw)*offset.x+std::cos(yaw)*offset.y)/d,-.88f,.88f);
    }
    static void cue(int kind,float pan=0,float strength=1,float pitch=1) {
        auto& p=audio_detail::parameters;auto& target=p.cues[kind];
        target.pan.store(pan,std::memory_order_relaxed);target.strength.store(strength,std::memory_order_relaxed);
        target.pitch.store(pitch,std::memory_order_relaxed);
        target.epoch.store(p.epoch.load(std::memory_order_relaxed),std::memory_order_relaxed);
        p.pending.fetch_or(1u<<kind,std::memory_order_release);
    }
    void release() {
        if(ready) {StopAudioStream(stream);UnloadAudioStream(stream);ready=false;}
        if(ownsDevice && IsAudioDeviceReady()) CloseAudioDevice();
        ownsDevice=false;
        if(ownsClaim) audio_detail::streamClaimed.store(false,std::memory_order_release);
        ownsClaim=false;
    }
public:
    Audio()=default;
    Audio(const Audio&)=delete;
    Audio& operator=(const Audio&)=delete;
    void init() {
        if(ready) return;
        bool unclaimed=false;
        if(!audio_detail::streamClaimed.compare_exchange_strong(unclaimed,true)) return;
        ownsClaim=true;ownsDevice=!IsAudioDeviceReady();
        if(ownsDevice) InitAudioDevice();
        if(!IsAudioDeviceReady()) {release();return;}
        auto& p=audio_detail::parameters;
        p.master=0;p.environment=.2f;p.ownHz=720;p.emission=0;p.jam=0;p.flow=0;
        p.recording=0;p.progress=0;p.exitLevel=0;p.pending=0;p.epoch=0;
        audio_detail::mixer=audio_detail::Mixer{};
        for(int i=0;i<audio_detail::sourceCount;++i) {
            p.sources[i].level=0;p.sources[i].threat=0;p.sources[i].hz=720;p.sources[i].pan=0;p.sources[i].pulsed=0;
            audio_detail::mixer.sources[i].phase=std::fmod(i*2.399963,audio_detail::tau);
        }
        previousTime=-1;previousProgress=0;previousEvents=0;previousState=RunState::Playing;
        SetAudioStreamBufferSizeDefault(1024);stream=LoadAudioStream(48000,32,2);
        if(!IsAudioStreamValid(stream)) {release();return;}
        ready=true;SetAudioStreamCallback(stream,audio_detail::callback);PlayAudioStream(stream);
    }
    // Yaw follows the camera: +X is forward at zero and +Y is to the right.
    // Existing callers can omit it; first-person callers should publish camera yaw.
    void update(const Simulation& sim,bool muted,bool paused,float listenerYaw=0) {
        if(!ready) return;
        using namespace audio_detail;
        auto& p=parameters;
        const float yaw=std::isfinite(listenerYaw)?listenerYaw:0;
        const bool playing=sim.state==RunState::Playing;
        p.master=muted?0.f:.78f;
        p.environment=paused?.22f:playing?1.f:.28f;
        p.ownHz=std::isfinite(sim.frequency)?clamp(sim.frequency,80,4000):720;
        // Quiet free swimming changes movement sound, never the continuous EOD.
        p.emission=sim.surveyMode && sim.hushed?.035f:playing?1.f:.2f;p.jam=clamp(sim.jam,0,1);
        const Vec relativeFlow=sim.velocity-sim.water.current;
        p.flow=clamp(std::sqrt(relativeFlow.x*relativeFlow.x+relativeFlow.y*relativeFlow.y+
                            sim.verticalVelocity*sim.verticalVelocity)/std::max(1.f,sim.tuning.swimSpeed)+
                            length(sim.water.current)/300,0,1.5f)*(sim.resting?.18f:sim.hushed?.35f:1.f);
        for(int i=0;i<sourceCount;++i) {
            auto& target=p.sources[i];
            if(i>=int(sim.sources.size())) {target.level=0;target.threat=0;continue;}
            const auto& source=sim.sources[i];const float d=sim.spatialDistance(source.position,source.height);
            const float falloff=clamp((1000-d)/250,0,1)/(1+(d/190)*(d/190));
            target.hz=std::isfinite(source.frequency)?clamp(source.frequency,5,4000):720;
            target.level=.030f*clamp(source.amplitude,0,2)*falloff;
            target.pulsed=source.species==FishSpecies::PulseKnifefish?1.f:0.f;
            target.pan=panAt(source.position,sim,yaw,source.height);
            const float close=clamp(1-d/640,0,1);
            target.threat=source.hunter?close*close*(.15f+.85f*clamp(source.alert,0,1)):0;
        }
        std::uint64_t eventSignature=sim.events.size();
        for(const auto& event:sim.events) {
            std::uint32_t x{},y{};
            std::memcpy(&x,&event.position.x,sizeof(x));std::memcpy(&y,&event.position.y,sizeof(y));
            eventSignature=(eventSignature*1099511628211ull)^unsigned(event.type);
            eventSignature=(eventSignature*1099511628211ull)^std::uint32_t(event.index);
            eventSignature=(eventSignature*1099511628211ull)^x;
            eventSignature=(eventSignature*1099511628211ull)^y;
        }
        const bool newFrame=sim.elapsed!=previousTime || sim.state!=previousState || eventSignature!=previousEvents;
        if(sim.elapsed<previousTime) {p.epoch.fetch_add(1,std::memory_order_relaxed);p.pending=0;previousProgress=0;}
        p.recording=!paused && playing && sim.canScan() && sim.scanProgress>previousProgress+.000001f?1.f:0.f;
        p.progress=clamp(sim.scanProgress,0,1);
        if(sim.nearestRelay>=0 && sim.nearestRelay<int(sim.relays.size())) {
            const auto& relay=sim.relays[sim.nearestRelay];p.recordPan=panAt(relay.position,sim,yaw,relay.height);
        }
        const bool exitReady=playing && sim.surveyMode && !sim.relays.empty() && sim.scans>=int(sim.relays.size());
        const float exitDistance=sim.spatialDistance(sim.exit,Simulation::entryAltitude);
        p.exitLevel=exitReady?(.16f+.84f*clamp(1-exitDistance/1600,0,1)):0;
        p.exitPan=panAt(sim.exit,sim,yaw,Simulation::entryAltitude);
        // Pausing and end screens may reuse the same event vector. Consume each
        // published event set once, including actions issued between simulation ticks.
        if(newFrame) for(const auto& event:sim.events) {
            float altitude=sim.altitude;
            if(event.index>=0) switch(event.type) {
                case EventType::RelayRevealed:case EventType::RelayScanned:
                    if(event.index<int(sim.relays.size())) altitude=sim.relays[event.index].height;
                    break;
                case EventType::FoodEaten:
                    if(event.index<int(sim.food.size())) altitude=sim.food[event.index].height;
                    break;
                case EventType::SpeciesObserved:
                    if(event.index<int(sim.sources.size())) altitude=sim.sources[event.index].height;
                    break;
                case EventType::Rested:
                    if(event.index<int(sim.refuges.size())) altitude=sim.refuges[event.index].height;
                    break;
                default:break;
            }
            if(event.type==EventType::ListenerAlert) {
                float closest=1e9f;
                for(const auto& source:sim.sources) if(source.hunter) {
                    const float d=distance(event.position,source.position);
                    if(d<closest) {closest=d;altitude=source.height;}
                }
            }
            const float pan=panAt(event.position,sim,yaw,altitude);
            switch(event.type) {
                case EventType::PulseSent:cue(PulseCue,0,1,clamp(sim.frequency/720,.8f,1.25f));break;
                case EventType::Dash:cue(DashCue);break;
                case EventType::RelayRevealed:cue(RevealCue,pan,1,1+std::max(0,event.index)*.055f);break;
                case EventType::RelayScanned:cue(RecordCue,pan,1,1+std::max(0,event.index)*.055f);break;
                case EventType::ExtractionReady:cue(ExitCue,panAt(sim.exit,sim,yaw,Simulation::entryAltitude));break;
                case EventType::Finished:cue(WinCue);break;
                case EventType::Lost:cue(LossCue);break;
                case EventType::RegionDiscovered:cue(DiscoveryCue,pan,1,1+std::max(0,event.index)*.025f);break;
                case EventType::FoodEaten:cue(FoodCue,pan);break;
                case EventType::SpeciesObserved:cue(RecordCue,pan,.55f);break;
                case EventType::Rested:cue(RestCue,0,.7f);break;
                case EventType::ReturnedHome:cue(WinCue,0,.55f);break;
                case EventType::ListenerAlert: {
                    const float proximity=clamp(1-sim.spatialDistance(event.position,altitude)/950,0,1);
                    cue(ListenerCue,pan,proximity*.8f);break;
                }
            }
        }
        previousTime=sim.elapsed;previousProgress=sim.scanProgress;previousState=sim.state;previousEvents=eventSignature;
    }
    ~Audio() {release();}
};
} // namespace ghost
