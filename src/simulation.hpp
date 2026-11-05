#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <string>
#include <vector>

namespace ghost {
constexpr float pi = 3.14159265358979323846f;
struct Vec { float x{}, y{}; };
inline Vec operator+(Vec a, Vec b) { return {a.x+b.x,a.y+b.y}; }
inline Vec operator-(Vec a, Vec b) { return {a.x-b.x,a.y-b.y}; }
inline Vec operator*(Vec a,float b) { return {a.x*b,a.y*b}; }
inline float length(Vec a) { return std::sqrt(a.x*a.x+a.y*a.y); }
inline float distance(Vec a,Vec b) { return length(a-b); }
inline float distance3(Vec a,float ah,Vec b,float bh) { const float d=distance(a,b);return std::sqrt(d*d+(ah-bh)*(ah-bh)); }
inline Vec unit(Vec a) { float d=length(a); return d>0.0001f?a*(1/d):Vec{}; }
inline float clamp(float v,float lo,float hi) { return std::clamp(v,lo,hi); }
inline float mix(float a,float b,float t) { return a+(b-a)*t; }

// Engine-free public mechanic API. Inputs are body lengths, Hz, and relative amplitudes.
float contamination(float selfAmplitude,float otherAmplitude,float distanceBL,
                    float referenceDistance=2,float minimumDistance=.25f,
                    float referenceConductivity=1,float waterConductivity=1);
float frequencyWindow(float delta,float critical=5,float safe=38);
float interference(float ownHz,float otherHz,float distanceBL,float otherAmplitude=1);
float jarTarget(float ownHz,float otherHz,float excursion,float low,float high);
enum class Band { Clear, Shimmer, Ghosting, Collapse };
Band band(float severity);
const char* bandName(Band value);
struct Phase {
    double angle{}, frequency{720}, target{720};
    void advance(double dt,double ramp=.035);
};

struct Tuning {
    float swimSpeed=180, acceleration=5, drag=3.4f;
    float frequencyMin=650, frequencyMax=1000, frequencyRate=48;
    float pulseCost=14, pulseCooldown=.75f, pulseSpeed=530, pulseRadius=440;
    float dashCost=20, dashSpeed=510, dashDuration=.26f, dashCooldown=1.3f;
    float chargeRecovery=7.0f, hushRecovery=16, jamDamage=14, integrityRecovery=3;
    float scanSeconds=1.4f, scanRange=72, scanMaxJam=.42f, bodyLength=12;
    float listenerSpeed=110, listenerHearing=900, listenerMemory=7;
    float metabolism=.08f, foodRecovery=22, foodRespawn=140, dayLength=720;
    bool load(const std::string& path,std::string& error);
};
struct Input {
    Vec move{}; float tune{}, wheel{}; bool ping{},dash{},hush{},scan{},autoTune{};
    float lift{}; bool feed{},rest{};
    // Multiplies normalized propulsion only. Keeping raw intent preserves the
    // distinction between slow directed swimming and quiet refuge resting.
    float propulsionScale=1;
};
enum class FishSpecies { BrownGhost, GlassKnifefish, PulseKnifefish, Catfish };
enum class FishBehavior { Resting, Foraging, Shoaling, Territorial, Avoiding };
const char* speciesName(FishSpecies species);
const char* behaviorName(FishBehavior behavior);
struct Source {
    Vec origin{}, position{}; float frequency{}, amplitude=1, phase{}, orbit=45, height=240;
    bool hunter{}; float alert{};
    FishSpecies species=FishSpecies::BrownGhost;
    FishBehavior behavior=FishBehavior::Foraging;
    Vec velocity{};float homeHeight=240,bodyLength=12,observation{},verticalVelocity{},satiety=75;
    int region{};bool observed{};
};
struct Relay { Vec position{}; float progress{}, reveal{}; bool found{}, complete{}; float height=240; int region{}; };
struct Rock { Vec position{}; float radius{}; float bottom=0,top=1320; };
struct Pulse { Vec origin{}; float radius{}, maximum{}, strength=1,height=240; };
struct Water {
    Vec current{};float conductivity=.01f,turbidity=.25f,temperature=27,oxygen=6.5f;
};
struct Region {
    std::string name,description;Vec minimum{},maximum{},center{};
    Water water{};float habitatDepth=720;bool discovered{};
};
struct Food {
    Vec position{};float height=100,nutrition=22,respawn{};int region{},kind{};
    bool available() const {return respawn<=0;}
};
struct Refuge { Vec position{};float height=240,radius=150;int region{};std::string name; };
enum class EventType { PulseSent, Dash, RelayRevealed, RelayScanned, ExtractionReady, Finished, Lost, ListenerAlert,
                       RegionDiscovered, FoodEaten, SpeciesObserved, Rested, ReturnedHome };
struct Event { EventType type; Vec position; int index=-1; };
enum class RunState { Playing, Won, Lost };
class Simulation {
public:
    static constexpr float width=14400,height=9600,regionSpan=4800;
    static constexpr float floorAltitude=50,ceilingAltitude=1320,entryAltitude=240;
    Tuning tuning;
    Vec player{350,780}, velocity{}, facing{1,0}, exit{350,780};
    float altitude=entryAltitude,verticalVelocity{},facingLift{};
    float frequency=720, targetFrequency=720, charge=100, integrity=100;
    float jam{}, elapsed{}, pulseTimer{}, dashTimer{}, dashRemaining{}, scanProgress{};
    bool hushed{};
    bool surveyMode=false,inRefuge{},resting{};
    std::uint32_t worldSeed=0x47485354u;
    float nourishment=85,fatigue{},dayPhase=.78f,feedingTimer{},travelled{},homeDistance{};
    int currentRegion{},nearestFood=-1,nearestRefuge=-1,discoveries{},observations{},homeVisits{};
    Water water{};
    int nearestSource=-1, nearestRelay=-1, scans{}, pings{};
    RunState state=RunState::Playing;
    std::vector<Source> sources;
    std::vector<Relay> relays;
    std::vector<Rock> rocks;
    std::vector<Pulse> pulses;
    std::vector<Event> events;
    std::vector<Region> regions;
    std::vector<Food> food;
    std::vector<Refuge> refuges;
    std::array<bool,4> speciesKnown{};
    Simulation();
    void reset();
    void step(float dt,const Input& input);
    float signalAt(Vec position,float ownFrequency,float atHeight=240) const;
    float spatialDistance(Vec position,float otherHeight) const;
    bool canScan() const;
    int regionAt(Vec position) const;
    Water waterAt(Vec position,float atHeight=240) const;
    float nightActivity() const;
    bool saveWorld(const std::string& path,std::string& error) const;
    bool loadWorld(const std::string& path,std::string& error);
    void returnHome();
private:
    Vec dashDirection{1,0};
    float dashLift{};
    Vec noiseOrigin{};
    float noiseHeight=entryAltitude,noiseFrequency=720,noiseRemaining{};
    bool wasHome=true,wasResting{};
    void generateWorld();
    void refreshNeighborhood();
    void updateEcology(float dt,const Input& input);
    void attractListener(Vec origin,float atHeight,float atFrequency,float loudness=1);
    float shiftedFrequency() const;
    void tick(float dt,const Input& input);
};
} // namespace ghost
