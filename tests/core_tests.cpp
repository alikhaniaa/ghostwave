#include "simulation.hpp"
#include <cstdio>
#include <fstream>
#include <iostream>
#include <limits>
#include <stdexcept>
using namespace ghost;
int checks=0;
void check(bool v,const char* what){++checks;if(!v)throw std::runtime_error(what);}
bool near(double a,double b,double t=.001){return std::abs(a-b)<t;}
void advance(Simulation& s,float seconds,Input in={}){
    for(int i=0;i<(int)std::ceil(seconds*120);++i){s.step(1.f/120,in);in.ping=in.dash=in.autoTune=false;in.wheel=0;}
}
void clearWater(Simulation& s,bool survey=true){
    s.surveyMode=survey;s.sources.clear();s.rocks.clear();s.relays.clear();s.regions.clear();s.food.clear();s.refuges.clear();s.water={};
}
Source stationary(Vec p,float h,float f=720){
    Source s;s.origin=s.position=p;s.height=s.homeHeight=h;s.frequency=f;s.orbit=0;return s;
}
bool hasEvent(const Simulation& s,EventType t){for(const auto& e:s.events)if(e.type==t)return true;return false;}
void coreMath(){
    check(near(contamination(1,1,2),1),"Reference contamination");
    check(near(contamination(1,1,4),.125),"Inverse-cube source field falloff");
    check(near(contamination(1,1,4,2,.25,1,2),.0625),"Conductivity scaling");
    check(contamination(0,1,0)==0 && contamination(1,-1,2)==0,"Invalid amplitude guarded");
    check(std::isfinite(contamination(1,1,0)),"Zero distance finite");
    check(frequencyWindow(0)==1 && frequencyWindow(5)==1,"Critical overlap window");
    check(frequencyWindow(38)==0 && frequencyWindow(-100)==0,"Safe separation window");
    check(near(frequencyWindow(-20),frequencyWindow(20)),"Overlap symmetry");
    check(interference(720,724,2)==1 && interference(720,780,1)==0,"Wave frequency overlap");
    check(interference(720,724,10)<.02f,"Spatial separation reduces interference");
    check(jarTarget(720,724,42,680,840)==680,"Challenge shift respects bounds");
    check(band(.1f)==Band::Clear && band(.5f)==Band::Ghosting && band(1)==Band::Collapse,"Perceptual bands");
    Phase a,b;a.target=b.target=800;a.advance(.1);for(int i=0;i<100;++i)b.advance(.001);
    check(near(a.angle,b.angle,1e-9) && near(a.frequency,b.frequency,1e-9),"Phase ramp independent of timestep");
    const double angle=a.angle;a.target=690;a.advance(-1);check(a.angle==angle,"Retune and invalid time preserve phase");
}
void movementAndDepth(){
    Simulation s;clearWater(s);
    check(near(s.spatialDistance(s.player+Vec{30,0},s.altitude+40),50),"Range includes depth");
    s.sources={stationary({600,780},240)};
    check(near(s.signalAt({600,780},720,264),s.signalAt({624,780},720,240)),"Signal attenuation is isotropic");
    check(s.signalAt({600,780},720,564)<.001f,"Depth supplies electrical separation");
    s.sources={stationary({350,780},540),stationary({400,780},240,780)};s.step(1.f/120,{});
    check(s.nearestSource==1,"Nearest emitter uses 3D range");
    s.reset();clearWater(s);Input upward;upward.lift=1;upward.dash=true;const Vec origin=s.player;s.step(1.f/120,upward);
    check(distance(origin,s.player)<.001f && near(s.verticalVelocity,s.tuning.dashSpeed),"Vertical burst has no stale horizontal heading");
    check(near(s.altitude-240,s.tuning.dashSpeed/120),"Vertical burst starts immediately");
    s.reset();clearWater(s);Input diagonal;diagonal.move={1,0};diagonal.lift=1;diagonal.dash=true;s.step(.1f,diagonal);
    check(near(std::sqrt(length(s.velocity)*length(s.velocity)+s.verticalVelocity*s.verticalVelocity),s.tuning.dashSpeed,.01),"Diagonal burst respects total speed");
    check(s.charge>80 && s.charge<82,"One burst cost across substeps");
    Input turn;turn.move={0,1};s.step(.1f,turn);check(std::abs(s.velocity.y)<.001f && s.verticalVelocity>300,"Burst holds launch direction");
    advance(s,.1f,turn);check(s.dashRemaining==0,"Burst expires exactly");s.step(.01f,diagonal);check(s.dashRemaining==0,"Burst cooldown enforced");
    s.reset();clearWater(s);s.charge=0;s.step(.01f,diagonal);check(s.dashRemaining==0,"Burst needs energy");
    s.reset();clearWater(s);diagonal.dash=false;advance(s,1,diagonal);
    const float speed=std::sqrt(length(s.velocity)*length(s.velocity)+s.verticalVelocity*s.verticalVelocity);
    check(speed<=s.tuning.swimSpeed+.01f && speed>s.tuning.swimSpeed*.98f,"Combined movement respects swim speed");
    const float vx=s.velocity.x,vz=s.verticalVelocity;advance(s,.1f);check(near(s.velocity.x/vx,s.verticalVelocity/vz,.001),"Drag treats all axes equally");
    s.reset();clearWater(s);s.altitude=Simulation::ceilingAltitude-1;s.verticalVelocity=500;s.step(1.f/120,{});
    check(s.altitude==Simulation::ceilingAltitude && s.verticalVelocity==0,"Ceiling removes outward momentum");
    Input down;down.lift=-1;s.step(1.f/120,down);check(s.altitude<Simulation::ceilingAltitude,"Fish immediately leaves ceiling");
    s.altitude=51;s.verticalVelocity=-500;s.step(1.f/120,{});check(s.altitude==50 && s.verticalVelocity==0,"Floor removes outward momentum");
    s.player={Simulation::width-101,780};s.velocity={500,0};s.step(1.f/120,{});
    check(s.player.x==Simulation::width-100 && s.velocity.x==0,"World boundary stops outward motion");
    s.reset();clearWater(s);s.rocks={{{700,780},100,0,300}};s.player={700,780};s.altitude=600;s.step(.01f,{});
    check(distance(s.player,{700,780})<.01f,"Finite rock can be crossed above top");
    s.altitude=300;s.step(.01f,{});check(s.altitude>=316,"Rock cap resolves overlap");
    s.altitude=150;s.player={700,780};s.step(.01f,{});check(distance(s.player,{700,780})>=115.99f,"Cylinder side resolves exact center");
    const Vec p=s.player;const float elapsed=s.elapsed;s.step(std::numeric_limits<float>::quiet_NaN(),{});s.step(-1,{});
    check(distance(p,s.player)==0 && s.elapsed==elapsed,"Invalid time ignored");
    s.reset();check(s.altitude==240 && s.verticalVelocity==0 && s.dashRemaining==0 && s.facingLift==0,"Reset clears depth drift and burst heading");
}
void controlledSwimming(){
    Simulation s;clearWater(s,false);s.refuges={{{350,780},240,170,0,"Test refuge"}};
    Input quietMove;quietMove.move={1,0};quietMove.hush=true;quietMove.propulsionScale=1.f/30;
    const Vec start=s.player;advance(s,1,quietMove);
    check(distance(start,s.player)>2 && distance(start,s.player)<3 && !s.resting && s.inRefuge,
          "Slow quiet propulsion keeps full movement intent and never becomes false refuge rest");
    quietMove.move={};s.fatigue=30;advance(s,2,quietMove);
    check(s.resting && s.fatigue<24 && length(s.velocity)<.01f,"Zero-intent quiet observation mode still permits stationary recovery");

    s.reset();clearWater(s);Input slow3D;slow3D.move={1,1};slow3D.lift=1;slow3D.propulsionScale=1.f/30;advance(s,1,slow3D);
    const float speed=std::hypot(length(s.velocity),s.verticalVelocity);
    check(speed>5.9f && speed<=6.001f,"Observation propulsion scale applies after combined 3D intent normalization");
    check(near(s.facing.x,s.facingLift,.0001) && near(length(s.facing)*length(s.facing)+s.facingLift*s.facingLift,1,.0001),
          "Slow propulsion preserves the normalized body heading");
    for(float scale:{0.f,-10.f,4.f,std::numeric_limits<float>::quiet_NaN(),std::numeric_limits<float>::infinity()}) {
        s.reset();clearWater(s);Input move;move.move={1,0};move.propulsionScale=scale;advance(s,1,move);
        const float expected=std::isfinite(scale)?std::clamp(scale,.01f,1.f):1.f;
        check(std::isfinite(s.player.x) && near(length(s.velocity),180*expected*(1-std::exp(-5.f)),.003),
              "Out-of-range and non-finite propulsion scales produce bounded finite swimming");
    }

    s.reset();clearWater(s,false);Region river;river.water.current={12,0};s.regions={river};const Vec driftStart=s.player;
    advance(s,2);
    check(near(s.travelled,distance(driftStart,s.player),.005) && s.travelled>23.9f,
          "Exploration distance includes actual water-current drift");
    s.reset();clearWater(s);s.player={Simulation::width-100,780};Input blocked;blocked.move={1,0};advance(s,2,blocked);
    check(s.travelled<.001f,"Swimming into an impassable bank does not accumulate phantom exploration distance");
    s.reset();clearWater(s);s.dashRemaining=.003f;const Vec splitStart=s.player;s.step(1.f/120,{});
    check(near(s.travelled,distance(splitStart,s.player),.0001),"Mixed burst/swim ticks measure their actual integrated path");
}
void optionalSurvey(){
    Simulation s;clearWater(s);s.frequency=s.targetFrequency=s.tuning.frequencyMax;
    s.relays={{{350,780},0,0,false,false,440},{{350,780},0,0,false,false,720}};
    Input ping;ping.ping=true;s.step(1.f/120,ping);advance(s,.2f);check(!s.relays[0].found,"Survey focus cannot precede wavefront");
    advance(s,.3f);check(s.relays[0].found,"Survey focus reaches vertical target");
    advance(s,.6f);check(!s.relays[1].found && s.pulses.empty(),"Survey focus radius limited in 3D");
    s.reset();clearWater(s);s.relays={{{350,780},0,0,true,false,440}};Input scan;scan.scan=true;advance(s,2,scan);
    check(s.scans==0 && !s.canScan(),"Overhead coincidence cannot record distant depth");
    s.altitude=440;advance(s,.4f,scan);check(s.scanProgress>0,"Recording begins at matched depth");
    Input quiet=scan;quiet.hush=true;const float progress=s.scanProgress;advance(s,.1f,quiet);check(s.scanProgress<progress,"Challenge silence prevents recording");
    advance(s,2,scan);check(s.scans==1 && s.state==RunState::Playing,"Record cannot extract from wrong depth");
    s.altitude=240;s.step(.01f,{});check(s.state==RunState::Won,"Optional survey finishes at entry depth");
    const Vec won=s.player;s.step(.1f,{{1,0}});check(distance(won,s.player)==0,"Finished challenge freezes");
    s.reset();clearWater(s);s.sources={stationary(s.player,s.altitude,680)};s.frequency=s.targetFrequency=680;advance(s,9);
    check(s.state==RunState::Lost,"Optional challenge retains explicit interference loss rule");
    s.reset();clearWater(s);s.frequency=s.targetFrequency=650;s.sources={stationary(s.player,s.altitude,660)};Input shift;shift.autoTune=true;s.step(.01f,shift);
    check(s.targetFrequency>698,"Challenge shift escapes blocked band edge");
    s.reset();clearWater(s);s.relays={{{350,780},.5f,0,true,false,240},{{800,780},.5f,0,true,false,240}};advance(s,.2f);
    check(s.relays[0].progress<.5f && s.relays[1].progress<.5f,"Every abandoned record decays");
}
void habitatGeneration(){
    Simulation a,b;check(!a.surveyMode && a.state==RunState::Playing,"Free habitat is default");
    check(a.regions.size()==6 && a.refuges.size()==6 && a.relays.size()==12,"Six habitats have refuges and tasks");
    check(a.sources.size()==24 && a.food.size()==72 && a.rocks.size()>=170 && a.rocks.size()<300,"Bounded persistent ecology and geometry");
    check(Simulation::width/60*Simulation::height/60>=38000,"World spans thirty-eight thousand square meters");
    bool same=a.rocks.size()==b.rocks.size(),valid=true,frequencyValid=true;
    for(std::size_t i=0;i<a.rocks.size();++i){
        const auto& r=a.rocks[i];same=same&&distance(r.position,b.rocks[i].position)==0 && r.top==b.rocks[i].top;
        valid=valid && r.position.x-r.radius>100 && r.position.x+r.radius<Simulation::width-100 && r.top>r.bottom && r.top<=Simulation::ceilingAltitude;
    }
    check(same,"World seed produces identical geometry");check(valid,"Generated obstacles have valid dimensions");
    ++b.worldSeed;b.reset();check(distance(a.rocks[0].position,b.rocks[0].position)>1,"Different seed changes habitat");
    for(const auto& s:a.sources){
        const float lo=s.species==FishSpecies::BrownGhost?650:s.species==FishSpecies::GlassKnifefish?300:s.species==FishSpecies::PulseKnifefish?35:0;
        const float hi=s.species==FishSpecies::BrownGhost?1000:s.species==FishSpecies::GlassKnifefish?400:s.species==FishSpecies::PulseKnifefish?60:0;
        frequencyValid=frequencyValid && s.frequency>=lo && s.frequency<=hi && (s.species!=FishSpecies::Catfish || s.amplitude==0);
    }
    check(frequencyValid,"Community distinguishes carriers, pulse rates and non-electric species");
    const Water mineral=a.waterAt(a.regions[2].center,600),black=a.waterAt(a.regions[3].center,600);
    check(mineral.conductivity>black.conductivity*4,"Regions differ in SI conductivity");
    check(a.waterAt(a.regions[1].center).turbidity>a.waterAt(a.regions[5].center).turbidity,"Silt and basin differ in suspended sediment");
    check(length(a.waterAt(a.regions[4].center,50).current)<length(a.waterAt(a.regions[4].center,600).current),"Bed boundary layer slows current");
    check(std::abs(a.waterAt({4799,2400}).conductivity-a.waterAt({4801,2400}).conductivity)<.00001f,"Water blends across regions");
    check(a.regionAt({-100,-100})==0 && a.regionAt({99999,99999})==5,"Region lookup clamps outside map");
    advance(a,.1f);check(a.discoveries==1 && a.regions[0].discovered,"Starting habitat discovered through play");
    a.player=a.regions[5].center;a.step(.01f,{});check(a.currentRegion==5 && a.discoveries==2 && hasEvent(a,EventType::RegionDiscovered),"New region increments discovery");
    a.step(.01f,{});check(a.discoveries==2 && !hasEvent(a,EventType::RegionDiscovered),"Region event does not repeat");
}
void livingEcology(){
    Simulation s;clearWater(s,false);s.sources={stationary(s.player,s.altitude,720)};Input quiet;quiet.hush=true;s.step(.01f,quiet);
    check(s.jam>.9f,"Quiet movement never switches off continuous EOD");
    advance(s,2,quiet);check(s.integrity==100 && s.state==RunState::Playing,"Interference alone does not injure free-mode fish");
    Input shift;shift.autoTune=true;s.step(.01f,shift);check(s.targetFrequency>720,"Brown ghost automatic JAR raises carrier");
    s.sources[0].species=FishSpecies::PulseKnifefish;s.sources[0].frequency=50;check(s.signalAt(s.player,720,s.altitude)==0,"Pulse rate is not sinusoidal carrier");
    s.sources[0].species=FishSpecies::Catfish;s.sources[0].frequency=720;check(s.signalAt(s.player,720,s.altitude)==0,"Non-electric fish do not create wave interference");
    s.reset();s.sources.clear();s.player=s.food[0].position;s.altitude=s.food[0].height;s.nourishment=40;Input feed;feed.feed=true;s.step(.01f,feed);
    check(s.nourishment>61 && s.food[0].respawn>0 && hasEvent(s,EventType::FoodEaten),"Feeding consumes prey and restores nutrition");
    const float fed=s.nourishment;s.step(.01f,feed);check(s.nourishment<=fed && !hasEvent(s,EventType::FoodEaten),"Prey cannot be eaten twice");
    s.food[0].respawn=.1f;advance(s,.2f);check(s.food[0].available(),"Prey population replenishes");
    s.player=s.refuges[0].position;s.altitude=s.refuges[0].height;s.fatigue=60;s.charge=20;s.integrity=30;s.nourishment=60;advance(s,2,quiet);
    check(s.resting && s.inRefuge && s.fatigue<54 && s.charge>45 && s.integrity>35,"Refuge rest restores energy fatigue and condition");
    check(s.nourishment<60,"Rest does not generate food");
    s.player=s.regions[2].center;s.altitude=600;s.velocity={};const Vec p=s.player;s.step(.1f,{});check(distance(p,s.player)>1,"Current advects idle fish");
    s.reset();clearWater(s,false);s.nourishment=0;s.integrity=50;advance(s,2);check(s.integrity<49,"Starvation has real condition cost");
    s.reset();clearWater(s,false);s.sources={stationary(s.player+Vec{50,0},s.altitude,850)};Input observe;observe.scan=observe.hush=true;advance(s,3,observe);
    check(s.sources[0].observed && s.speciesKnown[0] && s.observations==1,"Close quiet observation records species");
    advance(s,3,observe);check(s.observations==1,"Species discovery does not duplicate");
    s.reset();clearWater(s,false);s.sources={stationary(s.player+Vec{40,0},s.altitude,724)};s.step(.01f,{});
    check(s.sources[0].behavior==FishBehavior::Territorial && s.sources[0].alert>0,"Close active field can trigger territorial approach");
    const float f=s.sources[0].frequency;s.frequency=s.targetFrequency=950;advance(s,2,quiet);check(s.sources[0].frequency==f,"Neighbors never magically match player frequency");
    s.elapsed=s.tuning.dayLength*.57f;s.step(.01f,quiet);check(s.nightActivity()<.1f && s.sources[0].behavior==FishBehavior::Resting,"Day cycle changes nocturnal activity");
    s.relays={{{350,780},1,0,true,true,240}};s.scans=1;s.player=s.exit;s.altitude=240;s.step(.01f,{});
    check(s.state==RunState::Playing,"Completed research never ends free exploration");
}
void habitatConsistency(){
    Simulation s;
    check(s.inRefuge && s.nearestRefuge==0 && s.nearestFood==0 && s.nearestSource==0,"A fresh habitat has immediately usable spatial state");
    check(s.spatialDistance(s.food[0].position,s.food[0].height)<30,"The first prey is within a half metre at spawn");
    check(s.spatialDistance(s.sources[0].position,s.sources[0].height)<120 && std::abs(s.sources[0].height-s.altitude)<30,"A nearby fish shares the player's initial depth");
    advance(s,30);
    check(s.food[0].available() && s.food[1].available() && s.food[2].available() && s.food[3].available(),"NPC appetite preserves nearby prey through the first thirty seconds");
    check(s.spatialDistance(s.sources[0].position,s.sources[0].height)<180 && s.inRefuge && s.nourishment>80,"First thirty seconds keep a readable fish and safe inhabited refuge nearby");
    Input eat;eat.scan=true;s.step(.01f,eat);check(!s.food[0].available() && s.nourishment>95,"The first meal is reachable with context interaction");
    advance(s,2,eat);check(s.food[2].available(),"Holding observation after a full meal does not waste more prey");

    s.reset();s.fatigue=90;s.charge=10;s.nourishment=60;s.integrity=30;const Vec home=s.player;Input quiet;quiet.hush=true;advance(s,120,quiet);
    check(s.resting && s.inRefuge && distance(home,s.player)<2,"Sheltered station-keeping supports sustained rest without current carrying the fish away");
    check(s.fatigue<.01f && s.charge>99 && s.integrity>99,"Sustained fed rest recovers fatigue energy and condition");
    s.nourishment=0;s.integrity=50;advance(s,2,quiet);check(s.integrity<49,"Starving rest cannot generate healing energy");
    s.nourishment=60;s.integrity=30;s.velocity={400,0};s.step(1.f/120,quiet);check(!s.resting,"Fast coasting does not count as resting stillness");

    s.reset();clearWater(s,false);Source fish=stationary({800,780},240,850);fish.satiety=90;s.sources={fish};s.food={{{800,780},240,22,0,0,0}};
    advance(s,.1f);check(s.food[0].available(),"A satiated NPC does not consume every reachable prey");
    s.sources[0].satiety=30;advance(s,.1f);check(!s.food[0].available() && s.sources[0].satiety>51,"A hungry NPC consumes prey and gains reserves");

    s.reset();s.step(.01f,{});int shoaling=0;
    for(const auto& fish:s.sources)if(fish.species==FishSpecies::GlassKnifefish && fish.behavior==FishBehavior::Shoaling)++shoaling;
    check(shoaling>=4,"The default community actually contains nearby glass-fish shoaling partners");
    bool validSpawn=true;
    for(int seed=0;seed<4;++seed){s.worldSeed+=seed;s.reset();for(const auto& fish:s.sources)for(const auto& obstacle:s.rocks)
        if(fish.height>obstacle.bottom-12 && fish.height<obstacle.top+12 && distance(fish.position,obstacle.position)<obstacle.radius+12-.001f)validSpawn=false;}
    check(validSpawn,"Generated fish begin outside occupied obstacle volumes across seeds");

    s.reset();clearWater(s,false);s.relays={{{380,780},0,0,false,false,240}};Input record;record.scan=true;s.step(.01f,record);
    check(s.relays[0].found && hasEvent(s,EventType::RelayRevealed),"Continuous nearby sensing reveals research sites without an artificial pulse");
    advance(s,2,record);check(s.relays[0].complete && s.pings==0 && s.state==RunState::Playing,"Free-mode research can be completed continuously while the habitat remains open");
}
void persistence(){
    Simulation s;s.player=s.regions[4].center;s.altitude=710;s.nourishment=43;s.fatigue=21;s.elapsed=123.5f;s.travelled=1200;
    s.regions[0].discovered=s.regions[4].discovered=true;s.relays[3].found=s.relays[3].complete=true;s.relays[3].progress=1;
    s.food[5].respawn=77;s.sources[2].observed=true;s.sources[2].observation=1;s.sources[2].satiety=41;s.speciesKnown[2]=true;
    std::string error;const char* path="habitat-test-save.tmp";check(s.saveWorld(path,error),"Habitat state saves");check(s.saveWorld(path,error),"Save atomically replaces existing file");
    Simulation r;check(r.loadWorld(path,error),"Habitat state loads");
    check(distance(r.player,s.player)<.001f && r.altitude==710 && near(r.elapsed,123.5),"Save restores position depth and time");
    check(r.currentRegion==4 && !r.inRefuge && r.nearestFood>=0 && r.nearestSource>=0,"Loaded habitat exposes correct nearby state before its first frame");
    check(r.scans==1 && r.discoveries==2 && r.observations==1,"Save restores research and exploration");
    check(r.food[5].respawn==77 && r.sources[2].observed && r.sources[2].satiety==41 && r.nourishment==43 && r.fatigue==21,"Save restores ecosystem and NPC/player metabolism");
    std::vector<std::string> legacyLines;std::string line;
    {std::ifstream current(path);while(std::getline(current,line))legacyLines.push_back(line);}
    legacyLines[0]="GHOSTWAVE_HABITAT 2";
    for(std::size_t i=legacyLines.size()-s.sources.size();i<legacyLines.size();++i)legacyLines[i].resize(legacyLines[i].find_last_of(' '));
    {std::ofstream legacy(path);for(const auto& text:legacyLines)legacy<<text<<'\n';}
    check(r.loadWorld(path,error) && r.scans==1 && r.sources[2].satiety>=0,"Legacy version-two habitats load with initialized NPC reserves");
    const Vec safePosition=r.player;s.sources[2].satiety=120;check(s.saveWorld(path,error),"Malformed-state fixture written");
    check(!r.loadWorld(path,error) && distance(r.player,safePosition)==0,"Invalid NPC reserves cannot corrupt the running habitat");
    s.sources[2].satiety=41;s.relays[3].progress=.5f;check(s.saveWorld(path,error),"Inconsistent research fixture written");
    check(!r.loadWorld(path,error) && r.scans==1,"Completed research must have a complete saved acquisition");
    const Vec p=r.player;std::ofstream(path)<<"GHOSTWAVE_HABITAT 999\n";check(!r.loadWorld(path,error) && distance(p,r.player)==0,"Corruption cannot partially replace live habitat");
    r.state=RunState::Lost;r.returnHome();check(r.state==RunState::Playing && r.integrity==100 && r.homeDistance==0,"Recovery resumes life at home");
    check(r.relays[3].complete && r.speciesKnown[2] && r.food[5].respawn==77,"Home recovery preserves ecosystem and discoveries");
    std::remove(path);std::remove("habitat-test-save.tmp.tmp");
}
void swimTo(Simulation& s,Vec target){
    for(int i=0;i<16000 && (distance(s.player,target)>18 || length(s.velocity+s.water.current)>18) && s.state==RunState::Playing;++i){
        Input in;in.move=((target-s.player)*2-s.water.current)*(1/s.tuning.swimSpeed);s.step(1.f/120,in);
    }
    check(distance(s.player,target)<22 && s.state==RunState::Playing,"Whole-world route reaches waypoint by movement input");
}
void swimDepth(Simulation& s,float target){
    for(int i=0;i<5000 && (std::abs(s.altitude-target)>6 || std::abs(s.verticalVelocity)>12) && s.state==RunState::Playing;++i){
        Input in;in.lift=clamp((target-s.altitude)*3/s.tuning.swimSpeed,-1,1);s.step(1.f/120,in);
    }
    check(std::abs(s.altitude-target)<8,"Whole-world route reaches depth by movement input");
}
void completeHabitatCycle(){
    Simulation s;swimDepth(s,1240);swimTo(s,{2400,780});for(int r:{0,1,2,5,4,3,0})swimTo(s,s.regions[r].center);
    check(s.discoveries==6 && s.travelled>24000,"Continuous route discovers all six large-world habitats");
    swimTo(s,{2400,780});swimTo(s,s.exit);swimDepth(s,240);check(s.homeVisits>=1 && s.state==RunState::Playing,"Full habitat loop returns home without ending");
    swimTo(s,s.food[0].position);swimDepth(s,s.food[0].height);const float nutrition=s.nourishment;Input feed;feed.feed=true;s.step(.01f,feed);
    check(s.nourishment>nutrition,"Complete exploration loop includes a meal");
    swimTo(s,s.exit);swimDepth(s,240);Input quiet;quiet.hush=true;advance(s,2,quiet);check(s.resting && s.inRefuge && s.integrity>0,"Complete loop ends with sustainable refuge rest");
}
void tuningValidation(){
    Tuning t;std::string error;const char* p="test-tuning.tmp";std::ofstream(p)<<"swim_speed = 250\nfrequency_min = 1100\n";
    check(!t.load(p,error) && t.swimSpeed==180,"Invalid tuning rejected atomically");
    std::ofstream(p)<<"swim_speed = 230\nmetabolism = 0.1\nfood_respawn = 180\n";check(t.load(p,error) && t.swimSpeed==230 && t.foodRespawn==180,"Ecosystem tuning loads");
    std::ofstream(p)<<"listener_speed = 125\nlistener_memory = 90\n";check(!t.load(p,error) && t.listenerSpeed==110,"Invalid cue memory leaves previous tuning");
    std::remove(p);
}
int main(){
    try{
        coreMath();movementAndDepth();controlledSwimming();optionalSurvey();habitatGeneration();livingEcology();habitatConsistency();persistence();completeHabitatCycle();tuningValidation();
        std::cout<<"PASS: "<<checks<<" checks, including six-region 3D traversal, feeding, refuge rest and persistence.\n";return 0;
    }catch(const std::exception& e){std::cerr<<"FAIL after "<<checks<<" checks: "<<e.what()<<'\n';return 1;}
}
