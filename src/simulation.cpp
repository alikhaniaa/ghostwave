#include "simulation.hpp"
#include <fstream>
#include <sstream>
#include <unordered_map>
#include <iomanip>
#include <filesystem>
#include <limits>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

namespace ghost {
const char* speciesName(FishSpecies species) {
    switch(species) {
        case FishSpecies::GlassKnifefish:return "Glass knifefish";
        case FishSpecies::PulseKnifefish:return "Pulse knifefish";
        case FishSpecies::Catfish:return "River catfish";
        default:return "Brown ghost knifefish";
    }
}
const char* behaviorName(FishBehavior behavior) {
    switch(behavior) {
        case FishBehavior::Resting:return "Sheltering";
        case FishBehavior::Shoaling:return "Shoaling";
        case FishBehavior::Territorial:return "Territorial approach";
        case FishBehavior::Avoiding:return "Avoiding disturbance";
        default:return "Foraging";
    }
}
float contamination(float self,float other,float d,float d0,float dmin,float ref,float water) {
    if(self<=0 || other<=0 || water<=0 || ref<=0 || d0<=0) return 0;
    const float ratio=d0/std::max(d,std::max(.0001f,dmin));
    return (other/self)*ratio*ratio*ratio*(ref/water);
}
float frequencyWindow(float delta,float critical,float safe) {
    delta=std::abs(delta);
    if(safe<=critical) return delta>=safe?0.f:1.f;
    const float t=clamp((delta-critical)/(safe-critical),0,1);
    return 1-t*t*(3-2*t);
}
float interference(float own,float other,float d,float amp) {
    return clamp(contamination(1,amp,d)*frequencyWindow(own-other),0,1);
}
float jarTarget(float own,float other,float excursion,float low,float high) {
    const float direction=own==other?((high-own>=own-low)?1.f:-1.f):(own>other?1.f:-1.f);
    return clamp(own+direction*excursion,low,high);
}
Band band(float j) { return j>=.7f?Band::Collapse:j>=.38f?Band::Ghosting:j>=.12f?Band::Shimmer:Band::Clear; }
const char* bandName(Band b) {
    switch(b) { case Band::Shimmer:return "SHIMMER"; case Band::Ghosting:return "GHOSTING";
        case Band::Collapse:return "COLLAPSE"; default:return "CLEAR"; }
}
void Phase::advance(double dt,double ramp) {
    if(dt<=0) return;
    // Exact integral of the exponential frequency ramp: no phase jumps when tuning.
    const double a=ramp>0?std::exp(-dt/ramp):0;
    const double integral=target*dt+(ramp>0?(frequency-target)*ramp*(1-a):0);
    frequency=target+(frequency-target)*a;
    angle=std::fmod(angle+2.0*3.14159265358979323846*integral,2.0*3.14159265358979323846);
    if(angle<0) angle+=2.0*3.14159265358979323846;
}
bool Tuning::load(const std::string& path,std::string& error) {
    std::ifstream file(path);
    if(!file) { error="Cannot read "+path; return false; }
    Tuning next=*this;
    std::unordered_map<std::string,float*> fields={
        {"swim_speed",&next.swimSpeed},{"acceleration",&next.acceleration},{"drag",&next.drag},
        {"frequency_min",&next.frequencyMin},{"frequency_max",&next.frequencyMax},{"frequency_rate",&next.frequencyRate},
        {"pulse_cost",&next.pulseCost},{"pulse_cooldown",&next.pulseCooldown},{"pulse_speed",&next.pulseSpeed},{"pulse_radius",&next.pulseRadius},
        {"dash_cost",&next.dashCost},{"dash_speed",&next.dashSpeed},{"dash_duration",&next.dashDuration},{"dash_cooldown",&next.dashCooldown},
        {"charge_recovery",&next.chargeRecovery},{"hush_recovery",&next.hushRecovery},{"jam_damage",&next.jamDamage},
        {"integrity_recovery",&next.integrityRecovery},{"scan_seconds",&next.scanSeconds},{"scan_range",&next.scanRange},
        {"scan_max_jam",&next.scanMaxJam},{"body_length",&next.bodyLength},
        {"listener_speed",&next.listenerSpeed},{"listener_hearing",&next.listenerHearing},{"listener_memory",&next.listenerMemory},
        {"metabolism",&next.metabolism},{"food_recovery",&next.foodRecovery},{"food_respawn",&next.foodRespawn},{"day_length",&next.dayLength}};
    std::string line; int n=0;
    while(std::getline(file,line)) {
        ++n; line=line.substr(0,line.find('#'));
        if(line.find_first_not_of(" \t\r")==std::string::npos) continue;
        const auto eq=line.find('=');
        if(eq==std::string::npos) { error="Missing '=' at line "+std::to_string(n); return false; }
        std::string key=line.substr(0,eq);
        key.erase(0,key.find_first_not_of(" \t")); key.erase(key.find_last_not_of(" \t")+1);
        std::istringstream val(line.substr(eq+1)); float v; std::string extra;
        if(!fields.count(key) || !(val>>v) || (val>>extra) || !std::isfinite(v) || v<=0) {
            error="Invalid tuning at line "+std::to_string(n); return false;
        }
        *fields.at(key)=v;
    }
    if(next.frequencyMin>=next.frequencyMax || next.scanMaxJam>1 || next.pulseCost>100 || next.dashCost>100 ||
       next.bodyLength<1 || next.swimSpeed>2000 || next.pulseRadius>5000 || next.scanSeconds<.05f ||
       next.listenerSpeed>1000 || next.listenerHearing>5000 || next.listenerMemory>60 ||
       next.metabolism>10 || next.foodRecovery>100 || next.foodRespawn<5 || next.dayLength<60) {
        error="Tuning values outside valid range"; return false;
    }
    *this=next; error.clear(); return true;
}
Simulation::Simulation() { reset(); }
void Simulation::reset() {
    player=exit={350,780}; velocity={}; facing=dashDirection={1,0};altitude=entryAltitude;verticalVelocity=facingLift=dashLift=0; frequency=targetFrequency=clamp(720,tuning.frequencyMin,tuning.frequencyMax);
    charge=integrity=100; jam=elapsed=pulseTimer=dashTimer=dashRemaining=scanProgress=0;
    noiseOrigin=player;noiseHeight=altitude;noiseFrequency=frequency;noiseRemaining=0;
    nourishment=85;fatigue=feedingTimer=travelled=homeDistance=0;dayPhase=.78f;
    currentRegion=discoveries=observations=homeVisits=0;nearestFood=nearestRefuge=-1;
    inRefuge=resting=wasResting=false;wasHome=true;speciesKnown.fill(false);
    hushed=false; scans=pings=0; nearestRelay=nearestSource=-1; state=RunState::Playing;
    generateWorld();refreshNeighborhood();
    pulses.clear(); events.clear();
}
int Simulation::regionAt(Vec p) const {
    return std::clamp((int)(p.x/regionSpan),0,2)+3*std::clamp((int)(p.y/regionSpan),0,1);
}
Water Simulation::waterAt(Vec p,float h) const {
    if(regions.empty()) return {};
    if(regions.size()!=6) return regions.front().water;
    const float gx=clamp(p.x/regionSpan-.5f,0,2),gy=clamp(p.y/regionSpan-.5f,0,1);
    const int x0=(int)gx,x1=std::min(2,x0+1),y0=(int)gy,y1=std::min(1,y0+1);
    const float tx=gx-x0,ty=gy-y0;
    auto blend=[](const Water& a,const Water& b,float t) {
        Water w;w.current=a.current+(b.current-a.current)*t;
        w.conductivity=mix(a.conductivity,b.conductivity,t);w.turbidity=mix(a.turbidity,b.turbidity,t);
        w.temperature=mix(a.temperature,b.temperature,t);w.oxygen=mix(a.oxygen,b.oxygen,t);return w;
    };
    Water w=blend(blend(regions[y0*3+x0].water,regions[y0*3+x1].water,tx),
                  blend(regions[y1*3+x0].water,regions[y1*3+x1].water,tx),ty);
    const float bed=clamp(h/250,0,1);
    w.current=w.current*(.35f+.65f*bed);
    w.current.y+=std::sin(p.x*.0013f+p.y*.0007f)*length(w.current)*.16f;
    // Root cover is represented by a smooth sheltered-current factor, not by a
    // fluid solver. The exposed channel still retains its full current.
    for(const auto& shelter:refuges) {
        const float t=clamp(distance3(p,h,shelter.position,shelter.height)/(shelter.radius*1.35f),0,1);
        const float smooth=t*t*(3-2*t);
        w.current=w.current*(.08f+.92f*smooth);
    }
    w.temperature-=clamp((ceilingAltitude-h)/ceilingAltitude,0,1)*1.2f;
    return w;
}
float Simulation::nightActivity() const {return .5f+.5f*std::cos((dayPhase-.85f)*2*pi);}
void Simulation::generateWorld() {
    sources.clear();relays.clear();rocks.clear();food.clear();refuges.clear();regions.clear();
    std::uint32_t rng=worldSeed;
    auto random=[&]() {rng=rng*1664525u+1013904223u;return (rng>>8)*(1.f/16777216.f);};
    const char* names[]={"Rootwater Refuge","Silt Forest","Basalt Narrows","Flooded Gallery","Open Channel","Deep Basin"};
    const char* descriptions[]={"Sheltered roots and slow blackwater. A place to feed and rest.",
        "Submerged timber, soft sediment and abundant benthic prey.","A mineral-rich reach with stronger current and stone cover.",
        "A shaded floodplain among drowned tree roots.","Moving water, drifting invertebrates and loose fish shoals.",
        "Cool, deep water with scattered shelter and quiet electrical returns."};
    const Water conditions[]={{{3,1},.006f,.24f,28,6.1f},{{8,3},.010f,.72f,28,5.5f},{{23,-4},.024f,.19f,26.5f,7.3f},
        {{4,6},.004f,.58f,28.5f,5.2f},{{31,5},.014f,.38f,27,7.5f},{{9,-3},.018f,.12f,25.5f,5.8f}};
    for(int r=0;r<6;++r) {
        const Vec minimum{(r%3)*regionSpan,(r/3)*regionSpan},center=minimum+Vec{2400,2400};
        regions.push_back({names[r],descriptions[r],minimum,minimum+Vec{regionSpan,regionSpan},center,conditions[r],480.f+r*140,false});
        const Vec shelter=r==0?exit:center+Vec{-480,390};const float shelterHeight=r==0?entryAltitude:160.f+r*45;
        refuges.push_back({shelter,shelterHeight,170,r,std::string(names[r])+" shelter"});
        for(int j=0;j<2;++j) {
            const Vec p=r==0&&j==0?Vec{1040,680}:minimum+(j==0?Vec{1320,1540}:Vec{3600,3240});
            relays.push_back({p,0,0,false,false,190.f+((r*2+j)%5)*170,r});
        }
    }
    // Corridors through region centers and clear objective/refuge space make every
    // generated habitat connected. Rocks are bounded vertical cylinders.
    for(int r=0;r<6;++r) for(int n=0;n<32;++n) {
        for(int attempt=0;attempt<90;++attempt) {
            const Vec p=regions[r].minimum+Vec{280+random()*4240,280+random()*4240};const float radius=45+random()*115;
            if(std::abs(p.x-regions[r].center.x)<radius+230 || std::abs(p.y-regions[r].center.y)<radius+230) continue;
            if(r==0 && std::abs(p.y-exit.y)<radius+180)continue;
            bool blocked=distance(p,exit)<radius+400;
            for(const auto& site:relays)if(distance(p,site.position)<radius+180)blocked=true;
            for(const auto& shelter:refuges)if(distance(p,shelter.position)<radius+240)blocked=true;
            for(const auto& rock:rocks)if(distance(p,rock.position)<radius+rock.radius+45)blocked=true;
            if(blocked)continue;
            const float top=n%8==0?ceilingAltitude:140+random()*(r==2?900:580);
            rocks.push_back({p,radius,0,top});break;
        }
    }
    for(int r=0;r<6;++r) {
        for(int j=0;j<12;++j) {
            Vec p;float h;
            if(j<4) {
                const float angle=j*pi*.5f+.35f,radius=80+j*22.f;p=refuges[r].position+Vec{std::cos(angle)*radius,std::sin(angle)*radius};h=refuges[r].height-20+j*10;
                if(r==0) {
                    const Vec closePrey[]={{368,786},{409,812},{327,748},{388,715}};
                    const float closeHeight[]={235,246,228,252};p=closePrey[j];h=closeHeight[j];
                }
            } else {
                const auto& rock=rocks[(r*32+j*2)%rocks.size()];const float angle=random()*2*pi;
                p=rock.position+Vec{std::cos(angle)*(rock.radius+38),std::sin(angle)*(rock.radius+38)};h=80+random()*130;
            }
            food.push_back({p,h,tuning.foodRecovery,0,regionAt(p),j%3});
        }
        for(int j=0;j<4;++j) {
            Source s;s.region=r;s.species=(FishSpecies)j;s.origin=regions[r].minimum+Vec{950+random()*2800,700+random()*3200};
            if(r==0) {const Vec start[]={{970,610},{1770,1150},{2170,500},{1460,730}};s.origin=start[j];}
            s.position=s.origin;s.height=s.homeHeight=120+random()*500;s.phase=random()*2*pi;s.orbit=100+random()*150;
            s.frequency=j==0?680+random()*270:j==1?300+random()*100:j==2?35+random()*25:0;
            s.amplitude=j==3?0:j==0?1.f:.65f;s.hunter=j==0;s.bodyLength=j==0?12:j==1?15:j==2?18:22;
            s.satiety=50+std::fmod(s.phase*13,35.f);
            if(j==1) {
                // Neighboring habitat edges host small loose pairs. Without
                // these shared ranges, one glass fish per region never actually
                // encountered a conspecific and the shoaling rule was dormant.
                const Vec shoalHomes[]={{4700,2600},{4900,2660},{11400,4700},{4700,7500},{4900,7540},{11460,4900}};
                const float shoalHeights[]={320,320,520,410,410,520};
                s.origin=s.position=shoalHomes[r];s.height=s.homeHeight=shoalHeights[r];s.orbit=90;
            }
            if(r==0 && j==0) {
                s.origin=s.position={430,756};s.height=s.homeHeight=246;s.frequency=728;s.phase=.4f;s.orbit=38;s.satiety=78;
            }
            // A generated animal must begin outside the same finite volumes it
            // will collide with, rather than being ejected on its first tick.
            for(int pass=0;pass<3;++pass)for(const auto& obstacle:rocks) {
                if(s.height<obstacle.bottom-12 || s.height>obstacle.top+12)continue;
                const Vec offset=s.position-obstacle.position;const float d=length(offset),limit=obstacle.radius+14;
                if(d<limit)s.position=obstacle.position+(d>.001f?offset*(1/d):Vec{1,0})*limit;
            }
            s.origin=s.position;
            s.behavior=j==1?FishBehavior::Shoaling:FishBehavior::Foraging;sources.push_back(s);
        }
    }
}
float Simulation::spatialDistance(Vec p,float h) const {
    return distance3(player,altitude,p,h);
}
void Simulation::refreshNeighborhood() {
    currentRegion=regionAt(player);water=waterAt(player,altitude);homeDistance=spatialDistance(exit,entryAltitude);
    nearestSource=nearestRelay=nearestFood=nearestRefuge=-1;
    float best=1e9f;
    for(int i=0;i<(int)sources.size();++i) {
        const float d=spatialDistance(sources[i].position,sources[i].height);
        if(d<best){best=d;nearestSource=i;}
    }
    best=1e9f;
    for(int i=0;i<(int)relays.size();++i)if(!relays[i].complete) {
        const float d=spatialDistance(relays[i].position,relays[i].height);
        if(d<best){best=d;nearestRelay=i;}
    }
    best=1e9f;
    for(int i=0;i<(int)food.size();++i)if(food[i].available()) {
        const float d=spatialDistance(food[i].position,food[i].height);
        if(d<best){best=d;nearestFood=i;}
    }
    best=1e9f;
    for(int i=0;i<(int)refuges.size();++i) {
        const float d=spatialDistance(refuges[i].position,refuges[i].height);
        if(d<best){best=d;nearestRefuge=i;}
    }
    inRefuge=nearestRefuge>=0 && best<refuges[nearestRefuge].radius;
}
float Simulation::signalAt(Vec p,float f,float h) const {
    float sum=0;
    const float conductivity=waterAt(p,h).conductivity;
    for(const auto& s:sources) {
        if(!surveyMode && (s.species==FishSpecies::PulseKnifefish || s.species==FishSpecies::Catfish))continue;
        const float d=distance3(p,h,s.position,s.height)/tuning.bodyLength;
        sum+=clamp(contamination(1,s.amplitude,d,2,.25f,.01f,conductivity)*frequencyWindow(f-s.frequency),0,1);
    }
    return clamp(sum,0,1);
}
bool Simulation::canScan() const {
    return state==RunState::Playing && nearestRelay>=0 && nearestRelay<(int)relays.size() && relays[nearestRelay].found && !relays[nearestRelay].complete &&
        spatialDistance(relays[nearestRelay].position,relays[nearestRelay].height)<tuning.scanRange && jam<tuning.scanMaxJam && (!hushed || !surveyMode);
}
void Simulation::attractListener(Vec origin,float h,float f,float loudness) {
    if(!surveyMode)return;
    for(auto& s:sources) if(s.hunter && distance3(s.position,s.height,origin,h)<=tuning.listenerHearing*loudness) {
        if(s.alert<.05f) events.push_back({EventType::ListenerAlert,s.position});
        noiseOrigin=origin;noiseHeight=h;noiseFrequency=f;noiseRemaining=tuning.listenerMemory*loudness;
        s.alert=std::max(s.alert,loudness);
    }
}
float Simulation::shiftedFrequency() const {
    if(nearestSource<0) return targetFrequency;
    // Shift away from the signal actually masking us. A physically closer fish may
    // already be on a harmless frequency; selecting it makes the rescue key misleading.
    int source=nearestSource;float strongest=0;
    for(int i=0;i<(int)sources.size();++i) {
        const auto& s=sources[i];
        if(s.amplitude<=0 || (!surveyMode && s.species!=FishSpecies::BrownGhost && s.species!=FishSpecies::GlassKnifefish))continue;
        const float strength=interference(frequency,s.frequency,spatialDistance(s.position,s.height)/tuning.bodyLength,s.amplitude);
        if(strength>strongest) {strongest=strength;source=i;}
    }
    // A. leptorhynchus has an upward-only JAR. Manual carrier selection and the
    // optional survey challenge remain explicit control abstractions.
    if(!surveyMode) return clamp(std::max(frequency,targetFrequency)+12,tuning.frequencyMin,tuning.frequencyMax);
    const float preferred=jarTarget(frequency,sources[source].frequency,42,tuning.frequencyMin,tuning.frequencyMax);
    const float present=signalAt(player,frequency,altitude);
    float best=preferred,bestJam=signalAt(player,preferred,altitude);
    if(bestJam<std::max(.04f,present*.5f)) return best;
    // At a band edge, try the other side instead of pinning the player against it.
    auto consider=[&](float candidate) {
        candidate=clamp(candidate,tuning.frequencyMin,tuning.frequencyMax);
        const float j=signalAt(player,candidate,altitude);
        if(j<bestJam-.001f || (std::abs(j-bestJam)<.001f && std::abs(candidate-frequency)<std::abs(best-frequency))) {
            best=candidate;bestJam=j;
        }
    };
    for(const auto& s:sources) {consider(s.frequency-42);consider(s.frequency+42);}
    consider(tuning.frequencyMin);consider(tuning.frequencyMax);
    return best;
}
void Simulation::updateEcology(float dt,const Input& input) {
    water=waterAt(player,altitude);homeDistance=spatialDistance(exit,entryAltitude);
    currentRegion=regionAt(player);
    if(currentRegion<(int)regions.size() && !regions[currentRegion].discovered) {
        regions[currentRegion].discovered=true;++discoveries;events.push_back({EventType::RegionDiscovered,player,currentRegion});
    }
    nearestRefuge=-1;float shelterDistance=1e9f;
    for(int i=0;i<(int)refuges.size();++i) {
        const float d=spatialDistance(refuges[i].position,refuges[i].height);
        if(d<shelterDistance){shelterDistance=d;nearestRefuge=i;}
    }
    inRefuge=nearestRefuge>=0 && shelterDistance<refuges[nearestRefuge].radius;
    const float swimmingSpeed=std::sqrt(length(velocity)*length(velocity)+verticalVelocity*verticalVelocity);
    resting=inRefuge && (input.hush || input.rest) && length(input.move)<.1f && std::abs(input.lift)<.1f &&
        swimmingSpeed<tuning.swimSpeed*.12f && dashRemaining<=0;
    if(resting && !wasResting)events.push_back({EventType::Rested,player,nearestRefuge});
    wasResting=resting;
    const bool atHome=homeDistance<170;
    if(atHome && !wasHome && travelled>250) {++homeVisits;events.push_back({EventType::ReturnedHome,player});}
    wasHome=atHome;
    feedingTimer=std::max(0.f,feedingTimer-dt);nearestFood=-1;float foodDistance=1e9f;
    for(int i=0;i<(int)food.size();++i) {
        food[i].respawn=std::max(0.f,food[i].respawn-dt);
        if(food[i].available()) {
            const float d=spatialDistance(food[i].position,food[i].height);
            if(d<foodDistance){foodDistance=d;nearestFood=i;}
        }
    }
    if(surveyMode)return;
    const float effort=clamp(std::sqrt(length(velocity)*length(velocity)+verticalVelocity*verticalVelocity)/tuning.swimSpeed,0,3);
    nourishment=clamp(nourishment-dt*tuning.metabolism*(resting?.35f:.7f+effort*.7f),0,100);
    fatigue=clamp(fatigue+dt*(resting?-3.5f:effort>.6f?effort*.12f:-.3f),0,100);
    if(dashRemaining>0)fatigue=clamp(fatigue+dt*7,0,100);
    if(resting) {
        const float reserves=clamp(nourishment/25,0,1);
        integrity=std::min(100.f,integrity+dt*tuning.integrityRecovery*reserves);
        charge=std::min(100.f,charge+dt*5*reserves);
    }
    if(nourishment<1)integrity-=dt*.9f;
    if(water.oxygen<3)integrity-=dt*(3-water.oxygen)*(.25f+effort*.25f);
    if((input.feed || input.scan) && nearestFood>=0 && foodDistance<48 && feedingTimer<=0 && nourishment<95) {
        auto& prey=food[nearestFood];nourishment=std::min(100.f,nourishment+prey.nutrition);
        charge=std::min(100.f,charge+12);fatigue=std::max(0.f,fatigue-5);prey.respawn=tuning.foodRespawn;feedingTimer=.6f;
        events.push_back({EventType::FoodEaten,prey.position,nearestFood});
    }
    for(int i=0;i<(int)sources.size();++i) {
        auto& s=sources[i];
        if(s.observed)continue;
        const bool observing=input.scan && spatialDistance(s.position,s.height)<120 && jam<.65f;
        s.observation=clamp(s.observation+dt*(observing?1.f/2.5f:-.25f),0,1);
        if(s.observation>=1) {
            s.observed=true;const int species=(int)s.species;
            if(!speciesKnown[species]) {speciesKnown[species]=true;++observations;events.push_back({EventType::SpeciesObserved,s.position,i});}
        }
    }
}
void Simulation::returnHome() {
    player=exit;altitude=entryAltitude;velocity={};verticalVelocity=0;facing={1,0};facingLift=0;
    dashRemaining=dashTimer=pulseTimer=noiseRemaining=feedingTimer=0;charge=integrity=100;nourishment=70;fatigue=0;
    state=RunState::Playing;pulses.clear();events.clear();jam=0;hushed=resting=false;inRefuge=true;wasHome=true;wasResting=false;
    homeDistance=0;++homeVisits;refreshNeighborhood();jam=signalAt(player,frequency,altitude);
    events.push_back({EventType::ReturnedHome,player});
}
bool Simulation::saveWorld(const std::string& path,std::string& error) const {
    const std::string temporary=path+".tmp";
    std::ofstream out(temporary,std::ios::trunc);
    if(!out){error="Cannot write habitat save";return false;}
    out<<std::setprecision(std::numeric_limits<float>::max_digits10);
    out<<"GHOSTWAVE_HABITAT 3\n"<<worldSeed<<' '<<surveyMode<<' '<<(int)state<<'\n';
    out<<player.x<<' '<<player.y<<' '<<altitude<<' '<<frequency<<' '<<targetFrequency<<' '<<charge<<' '<<integrity<<' '<<elapsed<<' '
       <<nourishment<<' '<<fatigue<<' '<<travelled<<' '<<homeVisits<<' '<<pings<<'\n';
    for(bool known:speciesKnown)out<<known<<' ';
    out<<'\n';
    out<<regions.size()<<'\n';for(const auto& r:regions)out<<r.discovered<<' ';out<<'\n';
    out<<relays.size()<<'\n';for(const auto& r:relays)out<<r.progress<<' '<<r.reveal<<' '<<r.found<<' '<<r.complete<<'\n';
    out<<food.size()<<'\n';for(const auto& f:food)out<<f.respawn<<' ';out<<'\n';
    out<<sources.size()<<'\n';for(const auto& s:sources)out<<s.position.x<<' '<<s.position.y<<' '<<s.height<<' '<<s.frequency<<' '
        <<s.velocity.x<<' '<<s.velocity.y<<' '<<s.observation<<' '<<s.observed<<' '<<s.alert<<' '<<(int)s.behavior<<' '<<s.satiety<<'\n';
    out.flush();if(!out){error="Habitat save write failed";return false;}out.close();
#ifdef _WIN32
    if(!MoveFileExW(std::filesystem::path(temporary).c_str(),std::filesystem::path(path).c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)) {
        error="Cannot replace habitat save";return false;
    }
#else
    std::error_code ec;std::filesystem::rename(temporary,path,ec);if(ec){error="Cannot replace habitat save: "+ec.message();return false;}
#endif
    error.clear();return true;
}
bool Simulation::loadWorld(const std::string& path,std::string& error) {
    std::ifstream in(path);if(!in){error="No saved habitat yet";return false;}
    std::string magic;int version,mode,status;std::uint32_t seed;
    if(!(in>>magic>>version>>seed>>mode>>status) || magic!="GHOSTWAVE_HABITAT" || (version!=2 && version!=3) || (mode!=0 && mode!=1) || status<0 || status>2) {
        error="Unsupported or damaged habitat save";return false;
    }
    Simulation next;next.tuning=tuning;next.worldSeed=seed;next.surveyMode=mode!=0;next.reset();next.state=(RunState)status;
    if(!(in>>next.player.x>>next.player.y>>next.altitude>>next.frequency>>next.targetFrequency>>next.charge>>next.integrity>>next.elapsed
         >>next.nourishment>>next.fatigue>>next.travelled>>next.homeVisits>>next.pings)) {error="Incomplete habitat save";return false;}
    auto valid=[](float v,float low,float high){return std::isfinite(v) && v>=low && v<=high;};
    if(!valid(next.player.x,100,width-100)||!valid(next.player.y,170,height-170)||!valid(next.altitude,floorAltitude,ceilingAltitude)||
       !valid(next.frequency,20,2000)||!valid(next.targetFrequency,20,2000)||!valid(next.charge,0,100)||!valid(next.integrity,0,100)||
       !valid(next.nourishment,0,100)||!valid(next.fatigue,0,100)||!valid(next.elapsed,0,1e8f)||!valid(next.travelled,0,1e10f)||next.homeVisits<0||next.pings<0) {
        error="Invalid values in habitat save";return false;
    }
    for(auto& known:next.speciesKnown) {in>>known;if(known)++next.observations;}
    std::size_t count=0;in>>count;if(count!=next.regions.size()){error="Habitat region count mismatch";return false;}
    for(auto& r:next.regions){in>>r.discovered;if(r.discovered)++next.discoveries;}
    in>>count;if(count!=next.relays.size()){error="Habitat survey count mismatch";return false;}
    for(auto& r:next.relays) {
        in>>r.progress>>r.reveal>>r.found>>r.complete;
        if(!valid(r.progress,0,1)||!valid(r.reveal,0,10)|| (r.complete && (!r.found || r.progress<1))){error="Invalid survey record";return false;}
        if(r.complete)++next.scans;
    }
    in>>count;if(count!=next.food.size()){error="Habitat prey count mismatch";return false;}
    for(auto& f:next.food){in>>f.respawn;if(!valid(f.respawn,0,86400)){error="Invalid prey recovery state";return false;}}
    in>>count;if(count!=next.sources.size()){error="Habitat fish count mismatch";return false;}
    for(auto& s:next.sources) {
        int behavior=0;in>>s.position.x>>s.position.y>>s.height>>s.frequency>>s.velocity.x>>s.velocity.y>>s.observation>>s.observed>>s.alert>>behavior;
        if(version>=3)in>>s.satiety;
        if(!valid(s.position.x,0,width)||!valid(s.position.y,0,height)||!valid(s.height,floorAltitude,ceilingAltitude)||!valid(s.frequency,0,2000)||
           !valid(s.velocity.x,-1000,1000)||!valid(s.velocity.y,-1000,1000)||!valid(s.observation,0,1)||!valid(s.alert,0,1)||!valid(s.satiety,0,100)||behavior<0||behavior>4) {
            error="Invalid fish state";return false;
        }
        s.behavior=(FishBehavior)behavior;
    }
    std::string extra;if(!in || (in>>extra)){error="Truncated or extended habitat save";return false;}
    next.frequency=clamp(next.frequency,tuning.frequencyMin,tuning.frequencyMax);next.targetFrequency=clamp(next.targetFrequency,tuning.frequencyMin,tuning.frequencyMax);
    next.refreshNeighborhood();next.jam=next.signalAt(next.player,next.frequency,next.altitude);
    next.dayPhase=std::fmod(.78f+next.elapsed/tuning.dayLength,1.f);next.wasHome=next.homeDistance<170;
    *this=std::move(next);error.clear();return true;
}
void Simulation::step(float dt,const Input& input) {
    events.clear();
    if(!std::isfinite(dt) || dt<=0 || state!=RunState::Playing) return;
    dt=std::min(dt,.1f);
    Input sub=input;
    while(dt>.000001f) {
        const float h=std::min(dt,1.f/120);
        tick(h,sub); dt-=h;
        sub.ping=sub.dash=sub.autoTune=false; sub.wheel=0;
        if(state!=RunState::Playing) break;
    }
}
void Simulation::tick(float dt,const Input& input) {
    elapsed+=dt; pulseTimer=std::max(0.f,pulseTimer-dt); dashTimer=std::max(0.f,dashTimer-dt);
    dayPhase=std::fmod(.78f+elapsed/tuning.dayLength,1.f);water=waterAt(player,altitude);
    hushed=input.hush;
    inRefuge=false;
    for(const auto& shelter:refuges)if(spatialDistance(shelter.position,shelter.height)<shelter.radius){inRefuge=true;break;}
    noiseRemaining=std::max(0.f,noiseRemaining-dt*(hushed?3.f:1.f));
    for(auto& s:sources) {
        const Vec patrol=s.origin+Vec{std::cos(elapsed*.24f+s.phase)*s.orbit,std::sin(elapsed*.32f+s.phase)*s.orbit*.65f};
        if(!surveyMode) {
            const float activity=nightActivity(),proximity=spatialDistance(s.position,s.height);
            s.satiety=std::max(0.f,s.satiety-dt*(.018f+.017f*activity));
            Vec target=patrol;float targetHeight=s.homeHeight+std::sin(elapsed*.2f+s.phase)*24;
            float speed=(s.species==FishSpecies::GlassKnifefish?55.f:s.species==FishSpecies::Catfish?34.f:42.f)*(.35f+.65f*activity);
            s.alert=std::max(0.f,s.alert-dt*.32f);s.behavior=FishBehavior::Foraging;
            if(activity<.22f) {target=s.origin;targetHeight=s.homeHeight;speed=20;s.behavior=FishBehavior::Resting;}
            else if(s.species==FishSpecies::GlassKnifefish) {
                float best=600;const Source* mate=nullptr;
                for(const auto& other:sources)if(&other!=&s && other.species==s.species) {
                    const float d=distance(s.position,other.position);if(d<best){best=d;mate=&other;}
                }
                if(mate) {target=mate->position+Vec{std::cos(s.phase)*80,std::sin(s.phase)*80};targetHeight=mate->height;s.behavior=FishBehavior::Shoaling;}
            }
            if(activity>.22f && s.behavior==FishBehavior::Foraging && s.satiety<60) {
                float best=420;
                for(auto& prey:food)if(prey.available()) {
                    const float d=distance3(s.position,s.height,prey.position,prey.height);
                    if(d<best) {best=d;target=prey.position;targetHeight=prey.height;}
                    if(d<16) {prey.respawn=tuning.foodRespawn;s.satiety=std::min(100.f,s.satiety+prey.nutrition);break;}
                }
            }
            if(s.species==FishSpecies::BrownGhost && proximity<s.bodyLength*8 && !inRefuge && !hushed) {
                s.behavior=FishBehavior::Territorial;target=player;targetHeight=altitude;speed=proximity>32?58:0;
                if(s.alert<.05f)events.push_back({EventType::ListenerAlert,s.position});
                s.alert=1;
            } else if(proximity<100 && (dashRemaining>0 || length(velocity)>tuning.swimSpeed*.7f)) {
                s.behavior=FishBehavior::Avoiding;target=s.position+unit(s.position-player)*180;targetHeight=s.height;speed=95;s.alert=.75f;
            }
            const float d=distance3(s.position,s.height,target,targetHeight);
            const float factor=d>1?speed/d:0;
            s.velocity=s.velocity+((target-s.position)*factor-s.velocity)*(1-std::exp(-dt*4));
            const Vec current=waterAt(s.position,s.height).current;
            s.position=s.position+(s.velocity+current*.4f)*dt;
            const float previousHeight=s.height;
            s.height=clamp(s.height+(targetHeight-s.height)*factor*dt,floorAltitude,ceilingAltitude);
            s.verticalVelocity=(s.height-previousHeight)/dt;
            s.position.x=clamp(s.position.x,100,width-100);s.position.y=clamp(s.position.y,170,height-170);
            for(const auto& r:rocks)if(s.height>r.bottom-12 && s.height<r.top+12) {
                const Vec off=s.position-r.position;const float separation=length(off),limit=r.radius+12;
                if(separation<limit)s.position=r.position+(separation>.001f?off*(1/separation):Vec{1,0})*limit;
            }
            continue;
        }
        if(!s.hunter) {s.velocity=(patrol-s.position)*(1/dt);s.position=patrol;s.verticalVelocity=0;continue;}
        s.alert=s.alert>0?clamp(noiseRemaining/tuning.listenerMemory,0,1):0;
        const bool listening=s.alert>0 && !hushed;
        // It follows the last loud return. At close range it can track an active
        // field, but silence immediately breaks that lock and sheds its attention.
        if(listening && spatialDistance(s.position,s.height)<230) {
            noiseOrigin=player;noiseHeight=altitude;noiseFrequency=frequency;
        }
        const Vec target=listening?noiseOrigin:patrol;
        const float targetHeight=listening?noiseHeight:310;
        const float d=distance3(s.position,s.height,target,targetHeight);
        const float travel=std::min(1.f,dt*(listening?tuning.listenerSpeed:tuning.listenerSpeed*.45f)/std::max(1.f,d));
        s.velocity=(target-s.position)*(travel/dt);s.verticalVelocity=(targetHeight-s.height)*travel/dt;
        s.position=s.position+s.velocity*dt;s.height+=s.verticalVelocity*dt;
        // The optional dramatic survey listener follows a focus cue, but never
        // invents biological frequency matching or frequency-seeking predation.
        for(const auto& r:rocks) {
            const Vec off=s.position-r.position;const float separation=length(off),limit=r.radius+12;
            if(separation<limit && s.height>r.bottom-12 && s.height<r.top+12) s.position=r.position+(separation>.001f?off*(1/separation):Vec{1,0})*limit;
        }
    }
    float nearestDistance=1e9f; nearestSource=-1;
    for(int i=0;i<(int)sources.size();++i) {
        float d=spatialDistance(sources[i].position,sources[i].height); if(d<nearestDistance) {nearestDistance=d;nearestSource=i;}
    }
    targetFrequency=clamp(targetFrequency+input.tune*tuning.frequencyRate*dt+input.wheel*4,tuning.frequencyMin,tuning.frequencyMax);
    if(input.autoTune && nearestSource>=0) targetFrequency=shiftedFrequency();
    frequency=mix(frequency,targetFrequency,1-std::exp(-dt*12));
    const float magnitude=std::sqrt(input.move.x*input.move.x+input.move.y*input.move.y+input.lift*input.lift);
    const float normalizer=magnitude>1?1/magnitude:1;
    Vec move=input.move*normalizer;
    const float lift=input.lift*normalizer;
    if(magnitude>.01f) {facing=input.move*(1/magnitude);facingLift=input.lift/magnitude;}
    if(input.dash && dashTimer<=0 && charge>=tuning.dashCost) {
        charge-=tuning.dashCost; dashTimer=tuning.dashCooldown; dashRemaining=tuning.dashDuration;
        dashDirection=facing;dashLift=facingLift;
        events.push_back({EventType::Dash,player});
        if(!hushed) attractListener(player,altitude,frequency,.55f);
    }
    // A burst uses one normalized 3D heading for its whole duration, including
    // straight up/down. Integrate its exact remainder so frame rate cannot extend it.
    const Vec previousPosition=player;const float previousAltitude=altitude;
    const float burstTime=std::min(dt,dashRemaining);
    if(burstTime>0) {
        velocity=dashDirection*tuning.dashSpeed;verticalVelocity=dashLift*tuning.dashSpeed;
        player=player+velocity*burstTime;altitude+=verticalVelocity*burstTime;
        dashRemaining=std::max(0.f,dashRemaining-burstTime);
    }
    const float swimTime=dt-burstTime;
    if(swimTime>0) {
        const float rate=magnitude>.01f?tuning.acceleration:tuning.drag;
        const float propulsionScale=std::isfinite(input.propulsionScale)?clamp(input.propulsionScale,.01f,1.f):1.f;
        const float speed=tuning.swimSpeed*propulsionScale*(hushed?.5f:1.f)*(1-fatigue*.003f);
        const float damping=1-std::exp(-swimTime*rate);
        Vec desired=move*speed;
        if(!surveyMode && inRefuge && (hushed || input.rest) && magnitude<.1f)desired=water.current*(-1);
        velocity=velocity+(desired-velocity)*damping;
        verticalVelocity=mix(verticalVelocity,lift*speed,damping);
        player=player+velocity*swimTime;altitude+=verticalVelocity*swimTime;
    }
    if(!surveyMode)player=player+water.current*dt;
    auto bound=[](float& value,float& speed,float low,float high) {
        if(value<low) {value=low;speed=std::max(0.f,speed);}
        else if(value>high) {value=high;speed=std::min(0.f,speed);}
    };
    bound(altitude,verticalVelocity,floorAltitude,ceilingAltitude);
    bound(player.x,velocity.x,100,width-100);bound(player.y,velocity.y,170,height-170);
    for(const auto& r:rocks) {
        Vec off=player-r.position; float d=length(off), limit=r.radius+16;
        if(d<limit && altitude>r.bottom-16 && altitude<r.top+16) {
            // Resolve the shortest route out of a finite cylinder: caps as well as sides.
            const float fromTop=r.top+16-altitude,fromBottom=altitude-(r.bottom-16);
            if(fromTop<limit-d && fromTop<fromBottom && r.top+16<=ceilingAltitude) {
                altitude=r.top+16;verticalVelocity=std::max(0.f,verticalVelocity);continue;
            }
            if(fromBottom<limit-d && r.bottom-16>=floorAltitude) {
                altitude=r.bottom-16;verticalVelocity=std::min(0.f,verticalVelocity);continue;
            }
            Vec n=d>.001f?off*(1/d):Vec{1,0}; player=r.position+n*limit;
            float dot=velocity.x*n.x+velocity.y*n.y;
            if(dot<0) velocity=velocity-n*dot;
        }
    }
    // The journal measures distance through the habitat, including current drift
    // and cap/side collision resolution, not attempted propulsion into a wall.
    // This also accounts correctly for a tick split between a burst and swimming.
    travelled+=distance3(previousPosition,previousAltitude,player,altitude);
    // Damage, pulses, recording and the HUD all use the final position this tick.
    jam=hushed && surveyMode?0:signalAt(player,frequency,altitude);
    charge=clamp(charge+dt*(hushed?tuning.hushRecovery:tuning.chargeRecovery*(1-jam*.65f))*(.35f+.65f*nourishment/100),0,100);
    if(surveyMode && !hushed && jam>.38f) integrity-=dt*tuning.jamDamage*(jam-.38f)/.62f;
    else if(surveyMode || nourishment>15)integrity=std::min(100.f,integrity+dt*tuning.integrityRecovery*(surveyMode?1.f:.08f));
    if(input.ping && (!hushed || !surveyMode) && pulseTimer<=0 && charge>=tuning.pulseCost) {
        charge-=tuning.pulseCost; pulseTimer=tuning.pulseCooldown; ++pings;
        pulses.push_back({player,0,tuning.pulseRadius*(.6f+.4f*(frequency-tuning.frequencyMin)/(tuning.frequencyMax-tuning.frequencyMin))*(1-jam*.52f),1-jam*.7f,altitude});
        events.push_back({EventType::PulseSent,player});
        attractListener(player,altitude,frequency);
    }
    for(auto& p:pulses) {
        const float before=p.radius; p.radius+=dt*tuning.pulseSpeed;
        for(int i=0;i<(int)relays.size();++i) {
            auto& r=relays[i]; const float d=distance3(p.origin,p.height,r.position,r.height);
            if(d>=before && d<=p.radius && d<=p.maximum) {
                if(!r.found) events.push_back({EventType::RelayRevealed,r.position,i});
                r.found=true; r.reveal=6;
            }
        }
    }
    pulses.erase(std::remove_if(pulses.begin(),pulses.end(),[](const Pulse& p){return p.radius>p.maximum;}),pulses.end());
    nearestRelay=-1; nearestDistance=1e9f;
    for(int i=0;i<(int)relays.size();++i) {
        auto& r=relays[i]; r.reveal=std::max(0.f,r.reveal-dt);
        const float d=spatialDistance(r.position,r.height);
        if(!surveyMode && !r.found && d<tuning.scanRange*1.4f) {
            r.found=true;r.reveal=6;events.push_back({EventType::RelayRevealed,r.position,i});
        }
        if(!r.complete && d<nearestDistance) { nearestDistance=d; nearestRelay=i; }
    }
    scanProgress=0;
    const bool recording=input.scan && canScan();
    for(int i=0;i<(int)relays.size();++i) {
        auto& r=relays[i];
        if(!r.complete) r.progress=clamp(r.progress+(recording && i==nearestRelay?dt/tuning.scanSeconds:-dt*.6f),0,1);
    }
    if(nearestRelay>=0) {
        auto& r=relays[nearestRelay];
        scanProgress=r.progress;
        if(r.progress>=1) {
            r.complete=true; ++scans; charge=std::min(100.f,charge+30); integrity=std::min(100.f,integrity+25);
            events.push_back({EventType::RelayScanned,r.position,nearestRelay});
            if(scans==(int)relays.size()) events.push_back({EventType::ExtractionReady,exit});
        }
    }
    updateEcology(dt,input);
    if(integrity<=0) { integrity=0;state=RunState::Lost;events.push_back({EventType::Lost,player}); }
    else if(surveyMode && !relays.empty() && scans==(int)relays.size() && spatialDistance(exit,entryAltitude)<85) {
        state=RunState::Won;events.push_back({EventType::Finished,player});
    }
}
} // namespace ghost
