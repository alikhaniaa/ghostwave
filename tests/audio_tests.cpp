#include "audio.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <iomanip>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <vector>

// Only the device boundary is doubled. Every rendered sample comes from the
// production callback in audio.hpp, without a window, speaker, or filesystem.
struct rAudioBuffer {};
namespace device {
bool ready{}, failInit{}, failStream{};
int initializations{}, closes{}, loads{}, unloads{}, plays{}, stops{}, bufferSize{};
AudioCallback render{};
AudioStream lastStream{};
rAudioBuffer buffer;
void reset() {
    ready=failInit=failStream=false;
    initializations=closes=loads=unloads=plays=stops=bufferSize=0;
    render=nullptr;lastStream={};
}
}
extern "C" {
void InitAudioDevice(void) { ++device::initializations;device::ready=!device::failInit; }
void CloseAudioDevice(void) { ++device::closes;device::ready=false; }
bool IsAudioDeviceReady(void) { return device::ready; }
AudioStream LoadAudioStream(unsigned int rate,unsigned int bits,unsigned int channels) {
    ++device::loads;
    device::lastStream={device::failStream?nullptr:&device::buffer,nullptr,rate,bits,channels};
    return device::lastStream;
}
bool IsAudioStreamValid(AudioStream stream) { return stream.buffer!=nullptr; }
void UnloadAudioStream(AudioStream) { ++device::unloads; }
void PlayAudioStream(AudioStream) { ++device::plays; }
void StopAudioStream(AudioStream) { ++device::stops; }
void SetAudioStreamBufferSizeDefault(int size) { device::bufferSize=size; }
void SetAudioStreamCallback(AudioStream,AudioCallback callback) { device::render=callback; }
}

namespace a=ghost::audio_detail;
namespace {
int checks{};
void check(bool condition,const char* message) {
    ++checks;
    if(!condition) throw std::runtime_error(message);
}
bool near(double left,double right,double tolerance=1e-6) {
    return std::abs(left-right)<=tolerance;
}

void resetDSP(float environment=1,float emission=1,float master=.78f) {
    auto& p=a::parameters;
    p.master=master;p.environment=environment;p.ownHz=720;p.emission=emission;p.jam=0;p.flow=0;
    p.recording=0;p.progress=0;p.recordPan=0;p.exitLevel=0;p.exitPan=0;p.pending=0;p.epoch=0;
    for(auto& source:p.sources) {
        source.hz=720;source.level=0;source.pan=0;source.threat=0;source.pulsed=0;
    }
    for(auto& cue:p.cues) { cue.pan=0;cue.strength=1;cue.pitch=1;cue.epoch=0; }
    a::mixer=a::Mixer{};
}

std::vector<float> render(unsigned frames,const std::vector<unsigned>& chunks={1024}) {
    std::vector<float> output(std::size_t(frames)*2);
    unsigned offset=0;std::size_t chunk=0;
    while(offset<frames) {
        const unsigned count=std::min(chunks[chunk++%chunks.size()],frames-offset);
        a::callback(output.data()+std::size_t(offset)*2,count);offset+=count;
    }
    return output;
}
double peak(const std::vector<float>& samples) {
    double value=0;
    for(float sample:samples) value=std::max(value,std::abs(double(sample)));
    return value;
}
double power(const std::vector<float>& samples,int channel,unsigned first=0) {
    double sum=0;const std::size_t frames=samples.size()/2;
    for(std::size_t frame=first;frame<frames;++frame) {
        const double value=samples[frame*2+channel];sum+=value*value;
    }
    return sum/std::max<std::size_t>(1,frames-first);
}
double amplitude(const std::vector<float>& samples,double hz,int channel=0) {
    double real=0,imaginary=0;const std::size_t frames=samples.size()/2;
    for(std::size_t frame=0;frame<frames;++frame) {
        const double phase=a::tau*hz*frame/a::sampleRate;
        real+=samples[frame*2+channel]*std::cos(phase);
        imaginary-=samples[frame*2+channel]*std::sin(phase);
    }
    return 2*std::hypot(real,imaginary)/frames;
}
int activeCues() {
    return int(std::count_if(a::mixer.cues.begin(),a::mixer.cues.end(),
                            [](const a::CueVoice& cue){return cue.kind>=0;}));
}
void cue(int kind,float pan=0,float strength=1) {
    auto& p=a::parameters;
    p.cues[kind].pan=pan;p.cues[kind].strength=strength;p.cues[kind].pitch=1;
    p.cues[kind].epoch=p.epoch.load();p.pending.fetch_or(1u<<kind);
}

void carrierAndContinuity() {
    resetDSP();a::parameters.sources[0].hz=724;a::parameters.sources[0].level=.031f;
    render(24000);
    const auto sound=render(96000);
    const double own=amplitude(sound,720),neighbor=amplitude(sound,724);
    std::cout<<"Carrier amplitudes: 720 Hz="<<own<<", 724 Hz="<<neighbor
             <<"; adjacent="<<amplitude(sound,719)<<", "<<amplitude(sound,725)
             <<"; settled frequencies="<<a::mixer.hz<<", "<<a::mixer.sources[0].hz<<'\n';
    check(own>.011 && own<.018,"Continuous player carrier is present at 720 Hz");
    check(neighbor>.011 && neighbor<.018,"Continuous neighbor carrier is present at 724 Hz");
    check(near(a::mixer.hz,720,1e-7) && near(a::mixer.sources[0].hz,724,1e-7),
          "Carrier smoothing converges to the requested frequencies without float-rounding bias");
    check(amplitude(sound,719)<own*.025 && amplitude(sound,725)<neighbor*.025,
          "Near-frequency beating comes from distinct stable carriers");
    check(std::abs(amplitude(sound,720,1)-own)<.00015,"Player carrier is centered in stereo");

    resetDSP();a::parameters.ownHz=757;
    a::parameters.sources[0].hz=761;a::parameters.sources[0].level=.035f;
    a::parameters.sources[1].hz=53;a::parameters.sources[1].level=.022f;a::parameters.sources[1].pulsed=1;
    a::parameters.sources[2].hz=785;a::parameters.sources[2].threat=.8f;
    cue(a::RevealCue,.4f);
    const auto block=render(12000,{12000});
    resetDSP();a::parameters.ownHz=757;
    a::parameters.sources[0].hz=761;a::parameters.sources[0].level=.035f;
    a::parameters.sources[1].hz=53;a::parameters.sources[1].level=.022f;a::parameters.sources[1].pulsed=1;
    a::parameters.sources[2].hz=785;a::parameters.sources[2].threat=.8f;
    cue(a::RevealCue,.4f);
    const auto split=render(12000,{1,17,511,1024,73});
    check(block==split,"Active carriers, noise, filters and cues are independent of callback chunk size");

    render(11317);const auto before=a::mixer;
    const auto unchanged=render(1);
    a::mixer=before;a::parameters.ownHz=920;a::parameters.sources[0].hz=907;
    const auto retuned=render(1);
    check(std::abs(retuned[0]-unchanged[0])<.00005 && std::abs(retuned[1]-unchanged[1])<.00005,
          "Retuning starts with a continuous rendered waveform");
    check(a::mixer.ownPhase!=0 && a::mixer.sources[0].phase!=0,"Retuning does not reset oscillator phase");
    const auto ramp=render(16000);
    double jump=0;
    for(std::size_t i=2;i<ramp.size();++i) jump=std::max(jump,std::abs(double(ramp[i]-ramp[i-2])));
    check(jump<.024,"Frequency ramps do not introduce a click-sized sample discontinuity");
    check(near(a::mixer.hz,920,.1) && near(a::mixer.sources[0].hz,907,.15),
          "Retuning reaches the requested carrier frequencies");

    resetDSP(0,0);a::mixer.sources[3].phase=1.21;a::mixer.sources[3].hz=736;
    a::parameters.sources[3].hz=736;
    render(12613,{256,1024,63});
    const double expected=std::fmod(1.21+a::tau*736*12613/a::sampleRate,a::tau);
    check(near(a::mixer.sources[3].phase,expected,1e-9),"Distant silent carriers continue to advance");
    const double distantPhase=a::mixer.sources[3].phase;
    a::parameters.sources[3].level=.03f;render(1);
    check(near(a::mixer.sources[3].phase,std::fmod(distantPhase+a::tau*736/a::sampleRate,a::tau),1e-9),
          "An audible emitter resumes its retained phase");
    resetDSP(0,0);a::mixer.sources[3].phase=1.21;a::mixer.sources[3].hz=736;
    a::parameters.sources[3].hz=897;render(12613,{256,1024,63});
    const auto dormant=a::mixer.sources[3];
    resetDSP(0,0);a::mixer.sources[3].phase=1.21;a::mixer.sources[3].hz=736;
    a::parameters.sources[3].hz=897;a::parameters.sources[3].threat=1e-9f;
    render(12613,{1,17,511,1024,73});
    check(near(dormant.hz,a::mixer.sources[3].hz,1e-8) &&
          near(dormant.phase,a::mixer.sources[3].phase,1e-8) &&
          near(dormant.lowPhase,a::mixer.sources[3].lowPhase,1e-8),
          "A silent block ramp agrees with the per-sample carrier clock");
    std::cout<<"Retune maximum sample step="<<jump<<'\n';
}

void pulseAndStereo() {
    constexpr int samples=24000;constexpr float hz=50;
    double area=0,maximum=0,symmetry=0;int occupied=0;
    for(int i=0;i<samples;++i) {
        const double phase=a::tau*i/samples,value=a::biphasic(phase,hz);
        area+=value;maximum=std::max(maximum,std::abs(value));
        if(std::abs(value)>1e-9)++occupied;
        if(i>0) symmetry=std::max(symmetry,std::abs(value+a::biphasic(a::tau-phase,hz)));
    }
    check(std::abs(area/samples)<1e-9,"Pulse train has zero cycle-average signal");
    check(symmetry<1e-10,"Pulse train has balanced positive and negative lobes");
    check(maximum>.99 && maximum<1.01,"Pulse lobe normalization is bounded");
    check(occupied>samples*.06 && occupied<samples*.08,"A 50 Hz neighbor emits brief biphasic pulses");

    resetDSP(0,0);cue(a::RecordCue,.88f);const auto right=render(48000);
    const double ratio=power(right,1)/power(right,0);
    check(ratio>11 && ratio<17,"Head-relative cue panning favors the correct channel");
    resetDSP(0,0);cue(a::RecordCue,-.88f);const auto left=render(48000);
    double mirrorError=0;
    for(std::size_t frame=0;frame<right.size()/2;++frame) {
        mirrorError=std::max(mirrorError,std::abs(double(right[frame*2]-left[frame*2+1])));
        mirrorError=std::max(mirrorError,std::abs(double(right[frame*2+1]-left[frame*2])));
    }
    check(mirrorError<1e-7,"Mirrored cue positions swap stereo channels");
    resetDSP(0,0);cue(a::RecordCue);const auto center=render(16000);
    bool balanced=true;for(std::size_t i=0;i<center.size();i+=2) balanced&=center[i]==center[i+1];
    check(balanced,"A centered cue has equal left and right samples");

    resetDSP(1,0);a::parameters.sources[0].hz=777;
    a::parameters.sources[0].level=.055f;a::parameters.sources[0].pan=.88f;
    render(24000);const auto source=render(48000);
    check(amplitude(source,777,1)>amplitude(source,777,0)*3.4,
          "Continuous neighbor carriers also use spatial panning");
    std::cout<<"Right-panned cue power ratio="<<ratio<<"; pulse duty="<<double(occupied)/samples<<'\n';
}

void cuesAndReset() {
    constexpr std::array<double,a::cueCount> durations={.86,.48,1.3,1.9,2.75,3.7,2.4,1.6,2.8,.46,2.2};
    for(int kind=0;kind<a::cueCount;++kind) {
        resetDSP(0,0);cue(kind,.2f);
        const auto onset=render(4800);
        check(power(onset,0)>1e-9,"Each event cue has a non-silent attack");
        check(std::abs(onset[0])<1e-5,"Event cues start without an abrupt amplitude step");
        render(unsigned((durations[kind]+.15)*a::sampleRate));
        check(activeCues()==0,"Event cues release their finite voice slots");
        const auto tail=render(4800);
        check(peak(tail)<1e-8,"Expired cues leave no persistent audible tail");
    }
    resetDSP(0,0);cue(a::WinCue);render(4800);
    a::parameters.epoch=1;cue(a::RestCue);
    a::parameters.cues[a::RestCue].epoch=0;render(1);
    check(activeCues()==1 && a::mixer.cues[0].retiring,
          "A world reset fades existing cues and rejects stale pending events");
    render(12000);check(activeCues()==0,"Reset cues finish their brief release");
    cue(a::RestCue);render(1);
    check(activeCues()==1 && !a::mixer.cues[0].retiring,"Fresh cues play after the reset epoch");

    resetDSP(0,0);
    for(int kind=0;kind<a::cueCount;++kind) cue(kind);
    render(1);check(activeCues()==a::cueCount,"All event types can sound together");
    for(int kind=0;kind<a::cueCount;++kind) cue(kind);
    render(1);check(activeCues()==int(a::mixer.cues.size()),"Concurrent cues stay within the fixed voice pool");
    const double originalAge=a::mixer.cues[0].age;
    cue(a::PulseCue);render(1);
    check(a::mixer.cues[0].age>originalAge,"A full voice pool never steals a sounding cue");
    check(a::parameters.pending==0,"Dropped events do not remain queued indefinitely");
    render(192000);check(activeCues()==0,"A saturated cue pool drains fully");
}

void saturationAndMute() {
    resetDSP();a::parameters.jam=1;a::parameters.flow=1.5f;
    a::parameters.recording=1;a::parameters.progress=.8f;a::parameters.exitLevel=1;
    for(int index=0;index<a::sourceCount;++index) {
        auto& source=a::parameters.sources[index];
        source.hz=index%3==0?53:720+index;source.level=.06f;
        source.pan=index%2==0?.88f:-.88f;source.threat=1;source.pulsed=index%3==0?1.f:0.f;
    }
    for(int kind=0;kind<a::cueCount;++kind) cue(kind,float(kind%3-1)*.88f);
    const auto stressed=render(96000,{64,1024,337});
    bool finite=true;for(float value:stressed) finite&=std::isfinite(value);
    const double maximum=peak(stressed);
    check(finite,"The maximum authored mix produces only finite samples");
    check(maximum>.06 && maximum<.24,"Emitters and all cues stay below the soft output ceiling");
    check(power(stressed,0)>.00005 && power(stressed,1)>.00005,"The stress mix exercises both channels");
    const auto last=a::mixer;const auto sustained=render(1);
    a::mixer=last;a::parameters.master=0;constexpr unsigned muteFrames=24013;
    const auto fading=render(muteFrames);
    check(peak(std::vector<float>(fading.begin(),fading.begin()+2))>0,
          "Mute begins with a fade rather than an immediate hard cut");
    check(std::abs(fading[0]-sustained[0])<.0002 && std::abs(fading[1]-sustained[1])<.0002,
          "Mute begins continuously from the current signal");
    check(a::mixer.master==0 && power(fading,0,22000)==0 && power(fading,1,22000)==0,
          "Mute reaches exact digital silence");
    check(near(a::mixer.ownPhase,std::fmod(last.ownPhase+a::tau*720*muteFrames/a::sampleRate,a::tau),1e-9),
          "The carrier clock continues at the correct phase while muted");
    a::parameters.master=.78f;const auto rising=render(1);
    check(peak(rising)<.001,"Unmute begins with a smooth attack");
    const auto resumed=render(16000);check(power(resumed,0)>.00005,"Sound returns after unmuting");
    std::cout<<"Stress peak="<<maximum<<"; settled mute=0\n";
}

void deviceAndPublication() {
    device::reset();a::streamClaimed=false;
    {
        ghost::Audio audio;audio.init();audio.init();
        check(device::initializations==1 && device::loads==1 && device::plays==1,
              "Repeated init does not reopen a device or create a second stream");
        check(device::lastStream.sampleRate==48000 && device::lastStream.sampleSize==32 &&
              device::lastStream.channels==2 && device::bufferSize==1024,
              "The device boundary requests the expected floating-point stereo format");
        check(device::render==a::callback,"The opened stream uses the tested production callback");
        {ghost::Audio other;other.init();}
        check(device::loads==1 && device::closes==0,"A second Audio object cannot steal or close the live stream");

        ghost::Simulation sim;sim.sources.clear();sim.events.clear();sim.elapsed=10;
        ghost::Source source;source.position=sim.player+ghost::Vec{0,100};source.height=sim.altitude;
        source.frequency=777;source.species=ghost::FishSpecies::PulseKnifefish;sim.sources.push_back(source);
        sim.velocity={100,0};sim.water.current={0,0};audio.update(sim,false,false,0);
        const float ordinaryFlow=a::parameters.flow;
        check(near(a::parameters.sources[0].pan,.88) && a::parameters.sources[0].pulsed==1,
              "Camera yaw and neighbor species publish spatial pulse parameters");
        audio.update(sim,false,false,ghost::pi);
        check(near(a::parameters.sources[0].pan,-.88),"Turning around reverses head-relative source panning");
        sim.sources[0].height+=100;audio.update(sim,false,false,0);
        check(a::parameters.sources[0].pan>.70f && a::parameters.sources[0].pan<.71f,
              "Source depth contributes to spatial stereo distance");
        sim.hushed=true;audio.update(sim,false,false);
        check(a::parameters.emission==1 && near(a::parameters.flow,ordinaryFlow*.35),
              "Quiet free swimming retains the carrier and reduces water movement sound");
        sim.resting=true;audio.update(sim,false,false);
        check(a::parameters.emission==1 && near(a::parameters.flow,ordinaryFlow*.18),
              "Free-mode refuge rest retains the carrier");
        sim.surveyMode=true;audio.update(sim,false,false);
        check(near(a::parameters.emission,.035),"Challenge hush attenuation is separate from free exploration");
        audio.update(sim,true,true);
        check(a::parameters.master==0 && near(a::parameters.environment,.22),"Mute and pause publish independent sound levels");
        sim.surveyMode=false;sim.resting=sim.hushed=false;
        sim.frequency=std::numeric_limits<float>::quiet_NaN();
        sim.sources[0].frequency=std::numeric_limits<float>::infinity();audio.update(sim,false,false);
        check(a::parameters.ownHz==720 && a::parameters.sources[0].hz==720,
              "Non-finite carrier frequencies are replaced before reaching the callback");

        sim.events={{ghost::EventType::FoodEaten,sim.player}};audio.update(sim,false,false);
        check((a::parameters.pending.load()&(1u<<a::FoodCue))!=0,"A food action at unchanged simulation time publishes its cue");
        render(1);const int foodVoices=activeCues();audio.update(sim,false,false);render(1);
        check(activeCues()==foodVoices,"Reusing an event batch does not retrigger its cue");
        sim.events.clear();audio.update(sim,false,false);
        sim.events={{ghost::EventType::FoodEaten,sim.player}};audio.update(sim,false,false);render(1);
        check(activeCues()==foodVoices+1,"A new food event after consumption is audible even without a time advance");
        sim.events={{ghost::EventType::ReturnedHome,sim.player}};audio.update(sim,false,false);
        check((a::parameters.pending.load()&(1u<<a::WinCue))!=0,"Homecoming between ticks publishes its cue");
        const unsigned oldEpoch=a::parameters.epoch;
        sim.elapsed=0;sim.events.clear();audio.update(sim,false,false);render(1);
        check(a::parameters.epoch==oldEpoch+1 && a::parameters.pending==0,
              "A restarted timeline clears old pending cues and advances the audio epoch");
    }
    check(device::stops==1 && device::unloads==1 && device::closes==1 && !a::streamClaimed,
          "Owned streams and devices are released exactly once");
    device::reset();device::ready=true;
    {ghost::Audio audio;audio.init();}
    check(device::initializations==0 && device::closes==0 && device::unloads==1,
          "An externally owned device remains open after Audio destruction");
    device::reset();device::failInit=true;
    {ghost::Audio audio;audio.init();}
    check(device::loads==0 && device::closes==0 && !a::streamClaimed,"A failed device initialization releases the stream claim");
    device::reset();device::failStream=true;
    {ghost::Audio audio;audio.init();}
    check(device::loads==1 && device::closes==1 && !a::streamClaimed,"A failed stream releases its owned device and claim");
    device::reset();
    {ghost::Audio audio;audio.init();check(device::plays==1,"Audio can retry after earlier initialization failures");}
}
}

int main() {
    try {
        std::cout<<std::fixed<<std::setprecision(7);
        carrierAndContinuity();pulseAndStereo();cuesAndReset();saturationAndMute();deviceAndPublication();
        std::cout<<"PASS: "<<checks<<" offline audio checks (device boundary doubled; no hardware listening).\n";
        return 0;
    } catch(const std::exception& error) {
        std::cerr<<"FAIL after "<<checks<<" audio checks: "<<error.what()<<'\n';return 1;
    }
}
