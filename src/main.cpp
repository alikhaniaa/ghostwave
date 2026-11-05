#include "simulation.hpp"
#include "sensory_frame.hpp"
#include "habitat_render.hpp"
#include "audio.hpp"
#include "raylib.h"
#include "rlgl.h"
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <vector>

using namespace ghost;
namespace {
constexpr int W=1440,H=900;
constexpr float scale=60;
const Color ink{4,11,14,255},bone{212,231,214,255},mint{111,201,195,255},dim{107,145,145,255},rust{231,137,107,255},gold{212,193,137,255};
Vector3 world(Vec p,float h=240){return {p.x/scale,h/scale,p.y/scale};}
Vector3 add3(Vector3 a,Vector3 b){return {a.x+b.x,a.y+b.y,a.z+b.z};}
Vector3 sub3(Vector3 a,Vector3 b){return {a.x-b.x,a.y-b.y,a.z-b.z};}
float dot3(Vector3 a,Vector3 b){return a.x*b.x+a.y*b.y+a.z*b.z;}
Vector3 mul3(Vector3 a,float s){return {a.x*s,a.y*s,a.z*s};}
Vector3 cross3(Vector3 a,Vector3 b){return {a.y*b.z-a.z*b.y,a.z*b.x-a.x*b.z,a.x*b.y-a.y*b.x};}
Vector3 normalize3(Vector3 a){float n=std::sqrt(dot3(a,a));return n>1e-6f?mul3(a,1/n):Vector3{0,1,0};}
float distance3(Vector3 a,Vector3 b){Vector3 d=sub3(a,b);return std::sqrt(dot3(d,d));}
std::string num(double f,int p=0){std::ostringstream s;s<<std::fixed<<std::setprecision(p)<<f;return s.str();}
std::string scientific(double f){std::ostringstream s;s<<std::scientific<<std::setprecision(2)<<f;return s.str();}
enum class Screen{Title,Play,Pause,Help,Journal,Result};
struct App {
    struct Echo{Vector3 origin;float front,maximum,age;};
    std::vector<Echo> echoes;
    Simulation sim;
    SensoryClock senseClock;
    SensoryFrame senses;
    Screen screen=Screen::Title,returnScreen=Screen::Title;
    Camera3D camera{};
    Font font{};
    Texture2D stone{},fish{},glow{},titleArt{};
    Model rock{},floor{},sphere{},mineral{};
    Shader material{},post{},fishShader{};
    RenderTexture2D scene{};
    Audio audio;
    float yaw=0,pitch=0,time=0,noticeTimer=0,pulseEcho=0,autosave=0;
    bool muted=false,reduced=false,map=false,quit=false,smoke=false,captured=false,assetsOK=true;
    bool started=false,hasSave=false,fieldNotes=false,hideHud=false,confirmNew=false,travelAssist=false,lastSaveOK=true;
    std::string notice,saveOverride;
    Vector2 mouse{};
    std::array<bool,512> keyEvents{};

    bool keyPressed(int key) const {return key>=0&&key<(int)keyEvents.size()&&(keyEvents[key]||IsKeyPressed(key));}

    void text(const std::string& t,float x,float y,float size,Color c=bone,float spacing=1){DrawTextEx(font,t.c_str(),{x,y},size,spacing,c);}
    void center(const std::string& t,float x,float y,float size,Color c=bone,float spacing=1){text(t,x-MeasureTextEx(font,t.c_str(),size,spacing).x/2,y,size,c,spacing);}
    void line(float x,float y,float w,Color c=Fade(mint,.2f)){DrawLineEx({x,y},{x+w,y},1,c);}
    void panel(Rectangle r,float alpha=.88f){DrawRectangleRec(r,Fade(ink,alpha));DrawRectangleLinesEx(r,1,Fade(mint,.19f));}
    void tell(std::string value,float seconds=4){notice=std::move(value);noticeTimer=seconds;}
    bool button(const char* label,Rectangle r,bool primary=false){
        const bool hover=CheckCollisionPointRec(mouse,r);
        DrawRectangleRec(r,primary?Fade(bone,hover?1.f:.9f):Fade(ink,.7f));DrawRectangleLinesEx(r,1,Fade(bone,hover?.8f:.2f));
        text(label,r.x+22,r.y+(r.height-16)/2,16,primary?ink:bone,1.7f);
        return !smoke&&hover&&IsMouseButtonPressed(MOUSE_BUTTON_LEFT);
    }
    std::string settingsPath(){
        const char* a=std::getenv("APPDATA"),*x=std::getenv("XDG_CONFIG_HOME"),*h=std::getenv("HOME");
        std::filesystem::path p=a?a:x?x:h?std::string(h)+"/.config":".";std::error_code e;
        std::filesystem::create_directories(p/"Ghostwave",e);return (p/"Ghostwave"/"settings.txt").string();
    }
    std::string worldPath(){return saveOverride.empty()?(std::filesystem::path(settingsPath()).parent_path()/"habitat.gws").string():saveOverride;}
    void saveSettings(){if(!smoke){std::ofstream f(settingsPath());f<<muted<<' '<<reduced<<' '<<travelAssist;}}
    bool saveHabitat(bool notify=true){
        if(!started||(smoke&&saveOverride.empty()))return false;
        std::string error;const bool ok=sim.saveWorld(worldPath(),error);lastSaveOK=ok;autosave=0;
        if(ok){hasSave=true;std::ofstream view(worldPath()+".view");view<<yaw<<' '<<pitch;}
        if(notify||!ok)tell(ok?"HABITAT SAVED":error,5);
        return ok;
    }
    bool continueHabitat(){
        std::string error;if(!sim.loadWorld(worldPath(),error)){tell("LOAD FAILED / "+error,8);return false;}
        yaw=0;pitch=0;std::ifstream view(worldPath()+".view");float y=0,p=0;if(view>>y>>p&&std::isfinite(y)&&std::isfinite(p)){yaw=y;pitch=clamp(p,-1.25f,1.25f);}
        if(sim.state!=RunState::Playing)sim.returnHome();
        started=hasSave=true;screen=Screen::Play;echoes.clear();senseClock={};map=false;autosave=0;tell("THE WATER REMEMBERS.");return true;
    }
    void uniform(const char* name,float f){SetShaderValue(material,GetShaderLocation(material,name),&f,SHADER_UNIFORM_FLOAT);}
    void init(){
        std::string base=GetApplicationDirectory();if(!FileExists("assets/art/etched-basalt.png")&&FileExists((base+"assets/art/etched-basalt.png").c_str()))ChangeDirectory(base.c_str());
        font=LoadFontEx("assets/fonts/Oxanium-Regular.ttf",96,nullptr,0);GenTextureMipmaps(&font.texture);SetTextureFilter(font.texture,TEXTURE_FILTER_TRILINEAR);
        stone=LoadTexture("assets/art/etched-basalt.png");fish=LoadTexture("assets/art/knifefish.png");glow=LoadTexture("assets/particles/glow.png");titleArt=LoadTexture("assets/art/signal-title.png");
        assetsOK=stone.id&&fish.id&&glow.id&&titleArt.id&&font.texture.id;
        GenTextureMipmaps(&stone);SetTextureFilter(stone,TEXTURE_FILTER_TRILINEAR);SetTextureWrap(stone,TEXTURE_WRAP_REPEAT);
        SetTextureFilter(fish,TEXTURE_FILTER_BILINEAR);SetTextureFilter(glow,TEXTURE_FILTER_BILINEAR);GenTextureMipmaps(&titleArt);SetTextureFilter(titleArt,TEXTURE_FILTER_TRILINEAR);
        material=LoadShader("assets/shaders/cavern.vs","assets/shaders/cavern.fs");
        material.locs[SHADER_LOC_MATRIX_MODEL]=GetShaderLocation(material,"matModel");material.locs[SHADER_LOC_MATRIX_NORMAL]=GetShaderLocation(material,"matNormal");
        post=LoadShader(nullptr,"assets/shaders/water.fs");fishShader=LoadShader(nullptr,"assets/shaders/signal_fish.fs");
        assetsOK=assetsOK&&GetShaderLocation(material,"sensoryGain")>=0&&GetShaderLocation(post,"disturbance")>=0&&GetShaderLocation(fishShader,"clock")>=0;
        Mesh mesh=GenMeshSphere(1,16,22);
        for(int i=0;i<mesh.vertexCount;++i){float* p=mesh.vertices+i*3;float n=1+.065f*std::sin(p[0]*13+p[1]*6)*std::cos(p[2]*11-p[1]*7);p[0]*=n;p[1]*=n;p[2]*=n;}
        UpdateMeshBuffer(mesh,0,mesh.vertices,mesh.vertexCount*3*sizeof(float),0);
        rock=LoadModelFromMesh(mesh);floor=LoadModelFromMesh(GenMeshPlane(50,32,24,20));sphere=LoadModelFromMesh(GenMeshSphere(1,10,16));mineral=LoadModelFromMesh(createHabitatStoneMesh());
        for(Model* model:{&rock,&floor,&sphere,&mineral}){model->materials[0].shader=material;model->materials[0].maps[MATERIAL_MAP_DIFFUSE].texture=stone;}
        scene=LoadRenderTexture(W,H);SetTextureFilter(scene.texture,TEXTURE_FILTER_BILINEAR);
        std::string error;if(!sim.tuning.load("config/game.cfg",error))TraceLog(LOG_WARNING,"%s",error.c_str());sim.reset();
        audio.init();if(!smoke){std::ifstream f(settingsPath());f>>muted>>reduced;f>>travelAssist;hasSave=FileExists(worldPath().c_str());}
        updateCamera();evaluate();
    }
    void begin(){sim.reset();started=true;echoes.clear();senseClock={};yaw=0;pitch=-.04f;screen=Screen::Play;map=false;confirmNew=false;autosave=0;lastSaveOK=true;travelAssist=false;tell("YOU ARE A FIELD IN THE DARK. MOVE SLOWLY. SENSE WHAT CHANGES.",8);}
    void recover(){sim.returnHome();screen=Screen::Play;echoes.clear();senseClock={};yaw=0;pitch=0;tell("BACK IN SHELTER / YOUR EXPLORATION IS PRESERVED");audio.update(sim,muted,false,yaw);saveHabitat(false);}
    Vector3 look() const{return {std::cos(pitch)*std::cos(yaw),std::sin(pitch),std::cos(pitch)*std::sin(yaw)};}
    void evaluate(){Vector3 f=look();senses=evaluateSenses(sim,{f.x,f.y,f.z},&senseClock);}
    void updateCamera(){
        camera.position=world(sim.player,sim.altitude);
        if(screen==Screen::Title){camera.position=world({350,780},175);camera.target=add3(camera.position,{1,.015f,.36f});}
        else {if(!reduced&&screen==Screen::Play)camera.position.y+=std::sin(time*1.7f)*.012f;camera.target=add3(camera.position,look());}
        camera.up={0,1,0};camera.fovy=74;camera.projection=CAMERA_PERSPECTIVE;
    }
    void update(float dt){
        // A complete press/release may arrive within one GLFW event poll.
        // Preserve its queued press even when the end-of-frame held state is up.
        keyEvents.fill(false);for(int key=GetKeyPressed();key!=0;key=GetKeyPressed())if(key>0&&key<(int)keyEvents.size())keyEvents[key]=true;
        time+=dt;noticeTimer=std::max(0.f,noticeTimer-dt);pulseEcho=std::max(0.f,pulseEcho-dt);
        float s=std::min(GetScreenWidth()/(float)W,GetScreenHeight()/(float)H);Vector2 m=GetMousePosition();
        mouse={(m.x-(GetScreenWidth()-W*s)/2)/s,(m.y-(GetScreenHeight()-H*s)/2)/s};
        if(!smoke){
            if(keyPressed(KEY_F11))ToggleFullscreen();
            if(keyPressed(KEY_M)){muted=!muted;saveSettings();}
            if(keyPressed(KEY_ESCAPE)){
                if(screen==Screen::Play){screen=Screen::Pause;saveHabitat(false);}else if(screen==Screen::Pause)screen=Screen::Play;
                else if(screen==Screen::Help||screen==Screen::Journal)screen=returnScreen;else screen=Screen::Title;
            }
            if(keyPressed(KEY_ENTER)){
                if(screen==Screen::Title){if(hasSave)continueHabitat();else begin();}else if(screen==Screen::Result)recover();else if(screen==Screen::Pause)screen=Screen::Play;
            }
            if(screen==Screen::Play&&!IsWindowFocused()){screen=Screen::Pause;saveHabitat(false);}
            const bool needsCapture=screen==Screen::Play;
            if(needsCapture!=captured){if(needsCapture)DisableCursor();else EnableCursor();captured=needsCapture;}
            sim.events.clear();
            if(screen==Screen::Play){
                Vector2 delta=GetMouseDelta();yaw+=delta.x*.0022f;pitch=clamp(pitch-delta.y*.0022f,-1.25f,1.25f);
                float forward=(float)(IsKeyDown(KEY_W)||IsKeyDown(KEY_UP))-(float)(IsKeyDown(KEY_S)||IsKeyDown(KEY_DOWN));
                float strafe=(float)(IsKeyDown(KEY_D)||IsKeyDown(KEY_RIGHT))-(float)(IsKeyDown(KEY_A)||IsKeyDown(KEY_LEFT));
                Input in;Vector3 direction=look();in.move={direction.x*forward-std::sin(yaw)*strafe,direction.z*forward+std::cos(yaw)*strafe};
                in.lift=direction.y*forward+(float)IsKeyDown(KEY_R)-(float)IsKeyDown(KEY_F);
                in.ping=keyPressed(KEY_SPACE)||IsMouseButtonPressed(MOUSE_BUTTON_LEFT);in.dash=keyPressed(KEY_LEFT_SHIFT)||IsMouseButtonPressed(MOUSE_BUTTON_RIGHT);
                in.hush=IsKeyDown(KEY_LEFT_CONTROL)||IsKeyDown(KEY_C);in.scan=IsKeyDown(KEY_E);in.feed=in.scan;in.rest=in.hush;
                in.tune=(float)IsKeyDown(KEY_X)-(float)IsKeyDown(KEY_Z);in.wheel=GetMouseWheelMove();in.autoTune=keyPressed(KEY_Q);
                if(IsGamepadAvailable(0)){
                    float gx=GetGamepadAxisMovement(0,GAMEPAD_AXIS_LEFT_X),gy=GetGamepadAxisMovement(0,GAMEPAD_AXIS_LEFT_Y);
                    float rx=GetGamepadAxisMovement(0,GAMEPAD_AXIS_RIGHT_X),ry=GetGamepadAxisMovement(0,GAMEPAD_AXIS_RIGHT_Y);
                    if(std::abs(rx)>.16f)yaw+=rx*dt*1.7f;
                    if(std::abs(ry)>.16f)pitch=clamp(pitch-ry*dt*1.7f,-1.25f,1.25f);
                    if(std::abs(gx)>.16f||std::abs(gy)>.16f){direction=look();in.move={-direction.x*gy-std::sin(yaw)*gx,-direction.z*gy+std::cos(yaw)*gx};in.lift=-direction.y*gy;}
                    in.ping|=IsGamepadButtonPressed(0,GAMEPAD_BUTTON_RIGHT_FACE_DOWN);in.dash|=IsGamepadButtonPressed(0,GAMEPAD_BUTTON_RIGHT_FACE_RIGHT);
                    in.scan|=IsGamepadButtonDown(0,GAMEPAD_BUTTON_RIGHT_FACE_LEFT);in.feed=in.scan;in.autoTune|=IsGamepadButtonPressed(0,GAMEPAD_BUTTON_RIGHT_FACE_UP);
                    in.hush|=IsGamepadButtonDown(0,GAMEPAD_BUTTON_LEFT_TRIGGER_1);in.rest=in.hush;
                    in.lift+=(float)IsGamepadButtonDown(0,GAMEPAD_BUTTON_LEFT_FACE_UP)-(float)IsGamepadButtonDown(0,GAMEPAD_BUTTON_LEFT_FACE_DOWN);
                    in.tune+=(float)IsGamepadButtonDown(0,GAMEPAD_BUTTON_LEFT_FACE_RIGHT)-(float)IsGamepadButtonDown(0,GAMEPAD_BUTTON_LEFT_FACE_LEFT);
                }
                if(keyPressed(KEY_T)||(IsGamepadAvailable(0)&&IsGamepadButtonPressed(0,GAMEPAD_BUTTON_RIGHT_THUMB))){
                    travelAssist=!travelAssist;saveSettings();tell(travelAssist?"TRAVEL ASSIST / ACCELERATED LOCOMOTION":"OBSERVATION / SLOW PROPULSION");
                    if(!travelAssist){sim.dashRemaining=0;const float v=std::hypot(length(sim.velocity),sim.verticalVelocity),limit=sim.tuning.swimSpeed/30;if(v>limit){sim.velocity=sim.velocity*(limit/v);sim.verticalVelocity*=limit/v;}}
                }
                in.propulsionScale=travelAssist?1.f:1.f/30;
                if(!travelAssist){if(in.dash)tell("BURST REQUIRES TRAVEL ASSIST / T");in.dash=false;}
                if(in.dash&&length(in.move)<.05f&&std::abs(in.lift)<.05f){direction=look();in.move={direction.x,direction.z};in.lift=direction.y;}
                if(keyPressed(KEY_TAB))map=!map;
                if(keyPressed(KEY_V))fieldNotes=!fieldNotes;
                if(keyPressed(KEY_H))hideHud=!hideHud;
                if(keyPressed(KEY_F5)){std::string error;tell(sim.tuning.load("config/game.cfg",error)?"TUNING RELOADED":error);sim.targetFrequency=clamp(sim.targetFrequency,sim.tuning.frequencyMin,sim.tuning.frequencyMax);}
                if(keyPressed(KEY_F6))saveHabitat();
                if(keyPressed(KEY_F9)){continueHabitat();in={};}
                if(keyPressed(KEY_J)){screen=Screen::Journal;returnScreen=Screen::Play;map=false;}
                if(screen==Screen::Play){sim.step(dt,in);events();autosave+=dt;if(autosave>=60)saveHabitat(false);}
            }else if(screen==Screen::Journal&&keyPressed(KEY_J))screen=returnScreen;
        }
        if(screen==Screen::Play){for(auto& e:echoes){e.front+=sim.tuning.pulseSpeed/scale*dt;e.age+=dt;}
            echoes.erase(std::remove_if(echoes.begin(),echoes.end(),[](const Echo& e){return e.age>3;}),echoes.end());}
        updateCamera();evaluate();audio.update(sim,muted,screen!=Screen::Play,yaw);
    }
    void events(){for(const auto& e:sim.events){switch(e.type){
        case EventType::PulseSent:pulseEcho=2;if(!sim.pulses.empty()){auto& p=sim.pulses.back();echoes.push_back({world(p.origin,p.height),0,p.maximum/scale,0});if(echoes.size()>4)echoes.erase(echoes.begin());}break;
        case EventType::RelayRevealed:tell("A SIGNATURE IN THE WATER / HOLD E NEARBY TO RECORD");break;
        case EventType::RelayScanned:tell("HABITAT RECORD / "+std::to_string(sim.scans)+" OF "+std::to_string(sim.relays.size()));break;
        case EventType::RegionDiscovered:if(e.index>=0&&e.index<(int)sim.regions.size())tell(sim.regions[e.index].name+" / NEW WATER",6);break;
        case EventType::FoodEaten:tell("PREY FOUND / NOURISHMENT RESTORED");break;
        case EventType::SpeciesObserved:if(e.index>=0&&e.index<(int)sim.sources.size())tell(std::string("OBSERVED / ")+speciesName(sim.sources[e.index].species));break;
        case EventType::Rested:tell("STILL WATER / RESTORING IN SHELTER");break;
        case EventType::ReturnedHome:tell("A FAMILIAR CURRENT / HOME");break;
        case EventType::ExtractionReady:tell("EVERY SITE RECORDED / THE HABITAT REMAINS OPEN",6);break;
        case EventType::Finished:case EventType::Lost:screen=Screen::Result;saveHabitat(false);break;
        default:break;
    }}}
    Color speciesColor(FishSpecies s){return s==FishSpecies::Catfish?Color{153,162,172,255}:s==FishSpecies::PulseKnifefish?gold:s==FishSpecies::GlassKnifefish?Color{151,184,231,255}:mint;}
    Vector3 fishAxis(const Source& source){auto axis=sensory_detail::sourceDirection(source);return {(float)axis.x,(float)axis.y,(float)axis.z};}
    void livingField(const Source& source,int index){
        const Vector3 p=world(source.position,source.height);const float d=distance3(p,camera.position);if(d>8)return;
        const Color c=speciesColor(source.species);const float body=source.bodyLength/scale;
        const Vector3 axis=fishAxis(source),side=normalize3(cross3(axis,std::abs(axis.y)>.95f?Vector3{1,0,0}:Vector3{0,1,0})),up=cross3(side,axis);
        // Dipole-shaped visual streamlines are an amplified spatial metaphor, not a reach threshold.
        if(source.species!=FishSpecies::Catfish){
            const float gain=clamp(1-d/8,0,.8f);const int arcs=d<3?10:6;
            for(int j=0;j<arcs;++j){float az=j*2*pi/arcs+index*.31f;Vector3 prev{};
                for(int k=0;k<=32;++k){float t=k*pi/32;float r=(body*.9f+.12f)*std::pow(std::sin(t),2);float along=r*std::cos(t);float radial=r*std::sin(t);
                    Vector3 q=add3(p,add3(mul3(axis,along),add3(mul3(side,radial*std::cos(az)),mul3(up,radial*std::sin(az)))));
                    if(k)DrawLine3D(prev,q,Fade(c,gain*(.12f+.15f*std::pow(.5f+.5f*std::sin(t*8-time*2+index),4))));
                    prev=q;
                }
            }
        }
    }
    void drawFish(const Source& source,int index){
        const Vector3 p=world(source.position,source.height),axis=fishAxis(source);
        Vector3 up=normalize3(cross3(sub3(camera.position,p),axis));
        const float body=source.bodyLength/scale,d=distance3(p,camera.position);Color tint=Fade(speciesColor(source.species),clamp(1-d/11,.15f,1));
        // A painted ribbon follows the same 3D longitudinal axis as the electric body.
        BeginShaderMode(fishShader);rlSetTexture(fish.id);rlBegin(RL_QUADS);rlColor4ub(tint.r,tint.g,tint.b,tint.a);
        auto vertex=[&](float u,float v){const float bend=std::sin(u*9-time*3+index)*body*.017f*(1-u);Vector3 q=add3(p,add3(mul3(axis,(u-.5f)*body),mul3(up,(.5f-v)*body/3+bend)));rlTexCoord2f(u,v);rlVertex3f(q.x,q.y,q.z);};
        for(int strip=0;strip<16;++strip){float a=strip/16.f,b=(strip+1)/16.f;vertex(a,1);vertex(b,1);vertex(b,0);vertex(a,0);}
        rlEnd();rlSetTexture(0);EndShaderMode();
    }
    void drawGeometry(){
        uniform("clock",time);uniform("jam",screen==Screen::Title?0:sim.jam);uniform("hush",sim.hushed?1.f:0);uniform("emission",0);
        uniform("waterConductivity",sim.water.conductivity);uniform("sensoryGain",1.25f);
        SetShaderValue(material,GetShaderLocation(material,"eye"),&camera.position,SHADER_UNIFORM_VEC3);
        Vector4 waves[4]{};float ranges[4]{};
        for(int i=0;i<(int)echoes.size()&&i<4;++i){const auto& e=echoes[i];waves[i]={e.origin.x,e.origin.y,e.origin.z,e.front};ranges[i]=e.maximum;}
        if(screen==Screen::Title){waves[0]={camera.position.x,camera.position.y,camera.position.z,8.5f+std::sin(time*.13f)*2};ranges[0]=14;}
        SetShaderValueV(material,GetShaderLocation(material,"sonar[0]"),waves,SHADER_UNIFORM_VEC4,4);SetShaderValueV(material,GetShaderLocation(material,"sonarRange[0]"),ranges,SHADER_UNIFORM_FLOAT,4);
        BeginMode3D(camera);drawHabitatGeometry(sim,camera,rock);
        // Solid signatures are small, and remain depth-tested against actual terrain.
        uniform("emission",.14f);
        for(const auto& r:sim.relays){Vector3 p=world(r.position,r.height);const float d=distance3(p,camera.position);if(d>13||(!r.found&&d>2.2f))continue;
            DrawModelEx(sphere,p,{0,1,0},time*8,{.055f,.14f,.055f},r.complete?mint:gold);
        }
        uniform("emission",0);
        rlDrawRenderBatchActive();
        for(int i=0;i<(int)sim.sources.size();++i)livingField(sim.sources[i],i);
        rlDrawRenderBatchActive();rlDisableDepthMask();
        SetShaderValue(fishShader,GetShaderLocation(fishShader,"clock"),&time,SHADER_UNIFORM_FLOAT);
        for(int i=0;i<(int)sim.sources.size();++i){const auto& source=sim.sources[i];const Vector3 p=world(source.position,source.height);const float d=distance3(p,camera.position);if(d>10)continue;
            const float body=source.bodyLength/scale;float disturbance=source.species==FishSpecies::PulseKnifefish?.65f:source.hunter?.35f:sim.jam*.5f;
            SetShaderValue(fishShader,GetShaderLocation(fishShader,"disturbance"),&disturbance,SHADER_UNIFORM_FLOAT);
            drawFish(source,i);
            if(source.species!=FishSpecies::Catfish){BeginBlendMode(BLEND_ADDITIVE);DrawBillboard(camera,glow,p,body*2.2f,Fade(speciesColor(source.species),.16f/(1+d)));EndBlendMode();}
        }
        for(int i=0;i<(int)sim.food.size();++i){const auto& food=sim.food[i];if(!food.available())continue;const Vector3 p=world(food.position,food.height);const float d=distance3(p,camera.position);if(d>5)continue;
            float electric=0;for(std::size_t k=0;k<senses.targetCount;++k)if(senses.targets[k].kind==SensedKind::Food&&senses.targets[k].index==i&&!senses.targets[k].excluded)electric=(float)std::log1p(senses.targets[k].peakSkinAmplitudeV*1e10);
            const float gain=clamp(.13f+electric*.10f,0,.9f);const float size=.005f+(food.kind%3)*.002f;
            DrawSphere(p,size,Fade(gold,clamp(1-d/5,0,.9f)));BeginBlendMode(BLEND_ADDITIVE);DrawBillboard(camera,glow,p,.045f+gain*.04f,Fade(gold,gain*.45f));EndBlendMode();
            if(d<2){for(int j=0;j<7;++j){const float a=j*.004f,b=(j+1)*.004f;DrawLine3D({p.x+a-.014f,p.y+std::sin(a*110+time)*.005f,p.z},{p.x+b-.014f,p.y+std::sin(b*110+time)*.005f,p.z},Fade(gold,.7f));}}
        }
        // Dust is locally tiled with stable world positions; it expresses water flow.
        const float tile=8;const int cx=(int)std::floor(camera.position.x/tile),cz=(int)std::floor(camera.position.z/tile);
        for(int tz=cz-1;tz<=cz+1;++tz)for(int tx=cx-1;tx<=cx+1;++tx)for(int i=0;i<18;++i){
            float seed=tx*13.31f+tz*21.7f+i*7.931f;auto fract=[](float v){return v-std::floor(v);};
            Vector3 p{tx*tile+fract(std::sin(seed)*4375)*tile,fract(std::cos(seed*1.32f)*7213)*22,tz*tile+fract(std::sin(seed*2.13f)*9813)*tile};
            float d=distance3(p,camera.position);if(d<8){p.y+=std::sin(time*.22f+seed)*.025f;DrawSphere(p,.006f,Fade(mint,(1-d/8)*.23f));}
        }
        rlDrawRenderBatchActive();rlEnableDepthMask();EndMode3D();
    }
    bool onScreen(Vector3 p,Vector2& out){Vector3 dir=sub3(p,camera.position);if(dot3(dir,look())<=0)return false;out=GetWorldToScreenEx(p,camera,W,H);return out.x>60&&out.x<W-60&&out.y>140&&out.y<H-180;}
    void worldLabels(){
        if(sim.nearestFood>=0){const auto& f=sim.food[sim.nearestFood];Vector2 p;const float d=sim.spatialDistance(f.position,f.height)/scale;
            if(f.available()&&d<1.4f&&onScreen(world(f.position,f.height),p)){center("LIVING RETURN",p.x,p.y-39,10,gold,2);center(d<.8f?"HOLD E / FEED":num(d,2)+" m",p.x,p.y+27,12,gold);}}
        if(sim.nearestSource>=0){const auto& f=sim.sources[sim.nearestSource];Vector2 p;const float d=sim.spatialDistance(f.position,f.height)/scale;
            if(d<2.5f&&onScreen(world(f.position,f.height),p)){center(f.observed?speciesName(f.species):"ANOTHER PRESENCE",p.x,p.y-46,12,speciesColor(f.species),1.5f);center(f.frequency>0?num(f.frequency)+(f.species==FishSpecies::PulseKnifefish?" pulses/s  /  ":" Hz  /  ")+behaviorName(f.behavior):"NO ELECTRIC ORGAN",p.x,p.y+32,10,dim);if(!f.observed&&d<2)center("HOLD E / OBSERVE",p.x,p.y+54,10,bone);}}
        for(int i=0;i<(int)sim.relays.size();++i){const auto& r=sim.relays[i];Vector2 p;const float d=sim.spatialDistance(r.position,r.height)/scale;
            if(r.found&&d<6&&onScreen(world(r.position,r.height),p)){center(r.complete?"RECORDED":"HABITAT SITE / "+num(d,1)+" m",p.x,p.y-40,10,r.complete?dim:gold,1.3f);if(!r.complete&&d<sim.tuning.scanRange/scale)center("HOLD E / RECORD",p.x,p.y+33,11,gold);}}
    }
    void bodySense(float x,float y){
        // Amplified skin samples, anatomically arranged along both body flanks.
        const float half=91;DrawLineEx({x-half,y},{x+half,y},1,Fade(dim,.2f));
        for(int side=0;side<2;++side){Vector2 previous{};for(int k=0;k<=40;++k){float t=k/40.f;float width=std::pow(std::sin(t*pi),.75f)*(13+10*t);Vector2 p{x-half+2*half*t,y+(side?1:-1)*width};if(k)DrawLineEx(previous,p,1,Fade(mint,.40f));previous=p;}}
        for(std::size_t i=0;i<senses.skinCount;++i){const auto& sample=senses.skin[i];const float t=(float)(sample.receptor.longitudinal+1)*.5f;
            const double change=std::abs(sample.signal.objectPerturbationV+sample.signal.externalBasebandV);const float gain=clamp((float)std::log1p(change*1e8)*.15f,0,1);
            const float row=(float)(i%6)-2.5f;Vector2 p{x-half+2*half*t,y+row*5.3f};
            DrawCircleV(p,1.2f+gain*2.2f,Fade(gain>.45f?gold:mint,.25f+gain*.75f));
        }
        text("TAIL",x-half-35,y-4,8,dim,.5f);text("HEAD",x+half+9,y-4,8,dim,.5f);
    }
    void meter(const char* label,float value,float x,float y,Color color){text(label,x,y,10,dim,1.7f);text(num(value)+"%",x+171,y-3,15,color);line(x,y+22,217,Fade(dim,.18f));line(x,y+22,217*clamp(value/100,0,1),color);}
    void chart(){
        panel({300,150,840,552},.96f);text("THE REMEMBERED RIVER",334,178,23,bone,2);text("240 x 160 m  /  6 connected habitats",334,217,12,dim);
        Rectangle r{336,267,765,350};auto point=[&](Vec p){return Vector2{r.x+p.x/Simulation::width*r.width,r.y+p.y/Simulation::height*r.height};};
        for(int i=0;i<6;++i){const auto& region=sim.regions[i];Vector2 a=point(region.minimum),b=point(region.maximum);Rectangle box{a.x,a.y,b.x-a.x,b.y-a.y};DrawRectangleRec(box,Fade(mint,region.discovered?.055f:.012f));DrawRectangleLinesEx(box,1,Fade(mint,.14f));text(region.discovered?region.name:"UNFAMILIAR WATER",a.x+10,a.y+12,11,region.discovered?mint:Fade(dim,.35f),.6f);}
        for(const auto& obstacle:sim.rocks)if(sim.regions[sim.regionAt(obstacle.position)].discovered)DrawCircleV(point(obstacle.position),std::max(1.f,obstacle.radius/Simulation::width*r.width),Fade(mint,.18f));
        for(const auto& site:sim.relays)if(site.found)DrawPoly(point(site.position),4,4,45,site.complete?mint:gold);
        for(const auto& refuge:sim.refuges)if(sim.regions[refuge.region].discovered)DrawCircleLinesV(point(refuge.position),6,mint);
        for(const auto& source:sim.sources)if(source.observed)DrawCircleV(point(source.position),2,speciesColor(source.species));
        DrawCircleLinesV(point(sim.exit),8,bone);Vector2 p=point(sim.player);DrawCircleV(p,3,bone);DrawLineEx(p,{p.x+std::cos(yaw)*18,p.y+std::sin(yaw)*18},1,bone);
        text("CIRCLE / SHELTER     DIAMOND / SITE     POINT / OBSERVED FISH",336,642,10,dim,1);text("TAB / CLOSE",951,664,10,mint,2);
    }
    void readings(){
        panel({36,190,333,300},.9f);text("ELECTRIC FIELD / MODEL",55,211,14,mint,1.5f);
        const std::pair<std::string,std::string> rows[]={
            {"Water conductivity",num(senses.waterSm*1e4,1)+" uS/cm"},{"Own baseline RMS",scientific(senses.baselineRmsV)+" V"},{"Target perturbation RMS",scientific(senses.objectRmsV)+" V"},{"Other wave RMS",scientific(senses.externalRmsV)+" V"},{"Phase RMS",num(senses.phaseRmsRad*180/pi,4)+" deg"},{"Body samples",std::to_string(senses.skinCount)}};
        for(int i=0;i<6;++i){text(rows[i].first,55,249+i*27.f,11,dim,.4f);text(rows[i].second,228,249+i*27.f,11,bone,.3f);}
        bool excluded=false;for(std::size_t i=0;i<senses.targetCount;++i)excluded|=senses.targets[i].excluded;
        text(excluded?"LIMIT / OVERLAPPING TARGET EXCLUDED":senses.targetBudgetLimited||senses.emitterBudgetLimited?"LIMIT / NEAREST-TARGET BUDGET":senses.approximationLimited?"LIMIT / NEAR-BODY APPROXIMATION":"Surface-minus-centre voltage proxy",55,425,9,excluded||senses.approximationLimited?gold:mint,.4f);
        text("Small-target approximation. Terrain unsolved.",55,450,10,dim,.3f);text("V / HIDE READINGS    J / MODEL NOTES",55,472,9,dim,.7f);
    }
    void hud(){
        if(hideHud){if(map)chart();return;}
        DrawRectangleGradientV(0,0,W,150,Fade(ink,.78f),BLANK);DrawRectangleGradientV(0,H-184,W,184,BLANK,Fade(ink,.89f));
        text("G H O S T W A V E",38,29,17,bone,1.4f);text(sim.regions[sim.currentRegion].name,39,60,11,mint,1.8f);
        text("DEPTH  "+num((Simulation::ceilingAltitude+24-sim.altitude)/scale,1)+" m",39,96,11,dim);text(sim.resting?"RESTING / SHELTER":sim.inRefuge?"SHELTER / HOLD CTRL TO REST":sim.nightActivity()>.5f?"NOCTURNAL WATER":"DAYLIGHT WATER",39,121,10,sim.inRefuge?mint:dim,1.3f);
        center(travelAssist?"TRAVEL ASSIST / T":"OBSERVATION / T FOR TRAVEL",720,35,9,travelAssist?gold:dim,1.5f);
        text("YOUR REMEMBERED WORLD",W-271.f,31,10,dim,1.8f);text(std::to_string(sim.discoveries)+" / 6  HABITATS",W-271.f,56,19,bone,1.5f);
        text("J / JOURNAL    TAB / CHART",W-271.f,93,10,dim,1.2f);text("HOME  "+num(sim.homeDistance/scale,1)+" m",W-271.f,117,10,mint,1.5f);
        DrawCircle(W/2,H/2,1.1f,Fade(bone,.5f));if(sim.scanProgress>0)DrawRing({W/2.f,H/2.f},23,25,-90,-90+sim.scanProgress*360,60,gold);worldLabels();
        meter("NOURISHMENT",sim.nourishment,39,758,sim.nourishment<25?rust:mint);meter("CONDITION",sim.integrity,39,805,sim.integrity<40?rust:dim);
        text("BURST  "+num(sim.charge)+"%   /   FATIGUE  "+num(sim.fatigue)+"%",39,856,10,dim,1.2f);
        bodySense(720,785);center(num(sim.frequency)+" Hz",720,824,26,sim.jam>.38f?rust:bone,2);center("Z / X  TUNE     Q  JAR RESPONSE",720,863,9,dim,1.5f);
        Color c=sim.jam>.38f?rust:mint;text(sim.resting?"QUIET WATER":bandName(band(sim.jam)),W-271.f,758,18,c,1.5f);
        for(int i=0;i<43;++i){const auto& receptor=senses.skin[(std::size_t)i%senses.skinCount].signal;float envelope=(float)std::log1p(std::abs(receptor.objectPerturbationV+receptor.externalBasebandV)*1e7);float h=2+clamp(envelope,0,5)*3;DrawLineEx({W-270.f+i*5,804-h},{W-270.f+i*5,804+h},1,Fade(c,.65f));}
        text("CONTINUOUS ELECTRIC ORGAN",W-271.f,841,10,dim,1.2f);text("SPACE / SENSORY FOCUS",W-271.f,864,9,mint,1.5f);
        if(noticeTimer>0)center(notice,720,155,12,Fade(bone,std::min(1.f,noticeTimer)),1.5f);
        if(sim.elapsed<18)center("WASD  SWIM    R / F  RISE / SINK    E  FEED / OBSERVE    T  TRAVEL ASSIST",720,688,11,mint,1.1f);
        if(fieldNotes)readings();
        if(map)chart();
    }
    void title(){
        DrawRectangleGradientH(0,0,1000,H,Fade(ink,.96f),BLANK);text("AN ELECTRIC LIFE / SENSORIUM 01",86,107,11,mint,2.2f);
        text("GHOST",78,171,105,bone,5);text("WAVE",78,270,105,bone,5);line(88,402,70,rust);
        text("The dark is full of living things.",88,436,23,bone);text("A river perceived through electricity.",88,480,14,dim);text("Sense. Forage. Drift. Find a place to be still.",88,508,13,dim);
        if(button(hasSave?"CONTINUE YOUR HABITAT":"ENTER THE WATER",{88,582,362,59},true)){if(hasSave)continueHabitat();else begin();}
        if(hasSave){if(button(confirmNew?"CONFIRM NEW HABITAT":"NEW HABITAT",{88,656,362,45})){if(confirmNew)begin();else confirmNew=true;}}
        if(button("LEARN TO SENSE",{88,hasSave?715.f:658.f,242,46})){returnScreen=Screen::Title;screen=Screen::Help;}
        if(button("QUIT",{344,hasSave?715.f:658.f,106,46})&&(!started||saveHabitat()))quit=true;
        text("240 x 160 m",1060,686,24,bone,1.5f);text("SIX CONNECTED HABITATS",1060,727,10,mint,1.5f);text("ONE CONTINUOUS SIGNAL",1060,751,10,dim,1.5f);
        if(noticeTimer>0)text(notice,88,793,11,rust,.5f);
        line(39,839,W-78);text("C++ / ELECTRIC HABITAT SIMULATION",39,861,10,dim,1.4f);text(muted?"M / SOUND OFF":"M / SOUND ON",1220,861,10,dim);
    }
    void help(){
        DrawRectangle(0,0,W,H,Fade(ink,.90f));panel({220,62,1000,778},.94f);text("LEARN TO SENSE",265,103,32,bone,2);
        text("Your electric organ never goes quiet. The world changes its field.",265,155,16,mint);
        const char* keys[]={"MOUSE / RIGHT STICK","WASD / LEFT STICK","R / F","SPACE / LEFT CLICK","Z / X / WHEEL","Q","CTRL / C","SHIFT / RIGHT CLICK","HOLD E","TAB / J / V / H","F6 / F9"};
        const char* values[]={"Look. Forward swimming follows the direction you face.","Swim and strafe through the connected river habitat.","Rise and sink. Controller: D-pad up / down.","Sensory focus: amplify the visual memory of nearby surfaces.","Experiment with the organ frequency. Close signals interfere.","Invoke the brown ghost's upward jamming-avoidance response.","Move quietly. Remain still in a shelter to recover and rest.","Travel assist: burst in your movement direction. Uses stamina.","Feed on nearby prey, observe a fish, or record a habitat site.","Remembered chart / exploration journal / model readings / hide HUD.","Save the habitat / reload your last save. Autosaves every minute."};
        for(int i=0;i<11;++i){float y=207+i*39.f;text(keys[i],265,y,10,mint,1.2f);text(values[i],517,y,12,bone,.5f);line(265,y+28,910,Fade(mint,.07f));}
        text("Controller: A focus / B burst / X interact / Y JAR / LB quiet / D-pad left-right tune",265,650,11,dim,.6f);
        text("T / R3 travel assist. M mute / F11 fullscreen / Esc pause. Focus is a visual aid, not sonar.",265,680,11,dim,.6f);
        if(button("BACK",{265,742,158,49}))screen=returnScreen;
        if(button(reduced?"REDUCED MOTION / ON":"REDUCED MOTION / OFF",{804,742,370,49})){reduced=!reduced;saveSettings();}
    }
    void journal(){
        DrawRectangle(0,0,W,H,Fade(ink,.94f));text("THE WATER YOU KNOW",74,62,32,bone,2);text("HABITAT JOURNAL / SIMULATION PAUSED",76,112,11,mint,2);line(76,147,1288);
        const int known=(int)std::count(sim.speciesKnown.begin(),sim.speciesKnown.end(),true);
        text(std::to_string(sim.discoveries)+" / 6 habitats",76,183,22,bone);text(std::to_string(known)+" / 4 species",420,183,22,bone);text(std::to_string(sim.scans)+" / 12 site records",745,183,22,bone);text(num(sim.travelled/scale)+" m travelled",1106,183,19,bone);
        text("FAMILIAR WATERS",76,245,12,mint,2);
        for(int i=0;i<6;++i){const auto& r=sim.regions[i];float y=282+i*61.f;text(r.discovered?r.name:"UNFAMILIAR WATER",76,y,17,r.discovered?bone:dim);text(r.discovered?r.description:"Enter this region to remember its water.",76,y+27,10,dim,.3f);}
        text("LIVING SIGNALS",745,245,12,mint,2);
        const char* notes[]={"Wave organ / continuous carrier / upward JAR model","Wave organ / different carrier / behavioural approximation","Pulse-type neighbour / intermittent electric communication","No electric organ / passive conductive body return"};
        for(int i=0;i<4;++i){float y=282+i*72.f;text(sim.speciesKnown[i]?speciesName((FishSpecies)i):"UNOBSERVED PRESENCE",745,y,17,sim.speciesKnown[i]?speciesColor((FishSpecies)i):dim);text(sim.speciesKnown[i]?notes[i]:"Approach and hold E to observe.",745,y+28,10,dim,.3f);}
        text("WHAT THIS MODEL CAN CLAIM",745,603,12,mint,1.5f);text("Dimensioned dipole fields, conductivity, small-target polarization,",745,634,11,bone,.3f);text("48 skin samples, coherent interference and a reduced JAR model.",745,657,11,bone,.3f);
        text("The luminous world is an artistic translation. Terrain fields, fish",745,690,10,dim,.3f);text("neural perception and ecological rates are not experimentally solved.",745,711,10,dim,.3f);text("See docs/SCIENCE.md and the reproducible field laboratory.",745,742,10,dim,.3f);
        line(76,792,1288);if(button("RETURN TO THE WATER",{76,813,318,49},true))screen=returnScreen;text("J / CLOSE     EXPLORATION PERSISTS IN YOUR SAVE",755,832,11,mint,1.2f);
    }
    void pause(){
        DrawRectangle(0,0,W,H,Fade(ink,.9f));center("HOLD YOUR SIGNAL",720,171,34,bone,2);center(lastSaveOK?"HABITAT PAUSED / PROGRESS SAVED":"HABITAT PAUSED / SAVE FAILED",720,225,10,lastSaveOK?dim:rust,2);
        if(button("RESUME",{506,296,428,55},true))screen=Screen::Play;
        if(button("JOURNAL",{506,369,428,47})){returnScreen=Screen::Pause;screen=Screen::Journal;}
        if(button("CONTROLS & MOTION",{506,434,428,47})){returnScreen=Screen::Pause;screen=Screen::Help;}
        if(button("RETURN TO HOME SHELTER",{506,499,428,47}))recover();
        if(button("SAVE & RETURN TO TITLE",{506,564,428,47})&&saveHabitat())screen=Screen::Title;
        if(button("SAVE & QUIT",{506,629,428,47})&&saveHabitat())quit=true;
        if(!lastSaveOK&&noticeTimer>0)center(notice,720,729,11,rust,.3f);
    }
    void result(){
        DrawRectangle(0,0,W,H,Fade(ink,.91f));center("THE CURRENT CARRIES YOU HOME",720,260,31,bone,2);center("Recover in your shelter. Your remembered world remains.",720,324,15,dim);
        center(num(sim.travelled/scale)+" m travelled  /  "+std::to_string(sim.discoveries)+" habitats  /  "+std::to_string(sim.observations)+" observations",720,417,18,mint);
        if(button("RECOVER IN SHELTER",{495,529,450,58},true))recover();
        if(button("SAVE & RETURN TO TITLE",{495,607,450,48})&&saveHabitat())screen=Screen::Title;
    }
    void render(){
        BeginTextureMode(scene);ClearBackground(Color{2,7,9,255});
        if(screen==Screen::Title)DrawTexturePro(titleArt,{0,0,(float)titleArt.width,(float)titleArt.height},{0,0,W,H},{0,0},0,WHITE);
        else drawGeometry();
        EndTextureMode();
        BeginDrawing();ClearBackground(BLACK);float s=std::min(GetScreenWidth()/(float)W,GetScreenHeight()/(float)H);
        Camera2D ui{{(GetScreenWidth()-W*s)/2,(GetScreenHeight()-H*s)/2},{0,0},0,s};BeginMode2D(ui);
        float j=screen==Screen::Title?0:sim.jam,r=reduced?1.f:0;SetShaderValue(post,GetShaderLocation(post,"clock"),&time,SHADER_UNIFORM_FLOAT);SetShaderValue(post,GetShaderLocation(post,"disturbance"),&j,SHADER_UNIFORM_FLOAT);SetShaderValue(post,GetShaderLocation(post,"reduced"),&r,SHADER_UNIFORM_FLOAT);
        BeginShaderMode(post);DrawTexturePro(scene.texture,{0,0,(float)W,-(float)H},{0,0,(float)W,(float)H},{0,0},0,WHITE);EndShaderMode();
        if(screen==Screen::Title)title();else if(screen==Screen::Help)help();else if(screen==Screen::Journal)journal();else {hud();if(screen==Screen::Pause)pause();else if(screen==Screen::Result)result();}
        if(!assetsOK){DrawRectangle(0,0,W,48,rust);text("ART OR SHADER MISSING / keep the assets folder beside the executable",20,15,16,ink);}
        EndMode2D();EndDrawing();
    }
    void close(){
        if(started)saveHabitat(false);
        saveSettings();if(captured)EnableCursor();UnloadRenderTexture(scene);UnloadModel(rock);UnloadModel(floor);UnloadModel(sphere);UnloadModel(mineral);
        UnloadShader(material);UnloadShader(post);UnloadShader(fishShader);UnloadTexture(stone);UnloadTexture(fish);UnloadTexture(glow);UnloadTexture(titleArt);UnloadFont(font);
    }
};
}
int main(int argc,char** argv){
    bool smoke=false;std::string captures="build/captures";
    for(int i=1;i<argc;++i){std::string arg=argv[i];if(arg=="--smoke-test")smoke=true;else if(arg=="--capture-dir"&&i+1<argc)captures=argv[++i];}
    captures=std::filesystem::absolute(captures).string();
    SetTraceLogLevel(LOG_WARNING);SetConfigFlags(FLAG_WINDOW_RESIZABLE|FLAG_MSAA_4X_HINT|FLAG_VSYNC_HINT);
    InitWindow(1280,800,"GHOSTWAVE | Electric habitat");SetWindowMinSize(960,600);SetExitKey(KEY_NULL);SetTargetFPS(60);int result=0;
    {
        App game;game.smoke=smoke;game.init();
        if(smoke){
            std::filesystem::create_directories(captures);game.saveOverride=captures+"/smoke-habitat.gws";SetWindowSize(W,H);
            auto shot=[&](const char* name){game.updateCamera();game.evaluate();game.render();game.render();Image image=LoadImageFromScreen();if(!ExportImage(image,(captures+"/"+name).c_str()))result=3;UnloadImage(image);};
            auto step=[&](int frames,Input in={}){for(int i=0;i<frames;++i){game.sim.step(1.f/60,in);in.ping=false;game.events();game.update(1.f/60);game.render();}};
            for(int i=0;i<8;++i){game.update(1.f/60);game.render();}shot("01-title.png");
            game.begin();step(25);shot("02-habitat.png");
            if(!game.sim.sources.empty()){const auto& fish=game.sim.sources[0];game.sim.player={fish.position.x-30,fish.position.y-18};game.sim.altitude=fish.height;game.yaw=.48f;game.pitch=-.07f;game.sim.frequency=game.sim.targetFrequency=fish.frequency-4;}
            Input focus;focus.ping=true;step(12,focus);game.fieldNotes=true;shot("03-living-signal.png");game.fieldNotes=false;
            Input observe;observe.scan=true;step(170,observe);if(game.sim.observations<1)result=7;
            if(!game.sim.food.empty()){const auto food=game.sim.food[0];game.sim.player={food.position.x-20,food.position.y};game.sim.altitude=food.height+5;game.yaw=0;game.pitch=-.2f;game.sim.nourishment=50;Input feed;feed.feed=feed.scan=true;step(12,feed);if(game.sim.nourishment<=50)result=4;}
            if(!game.sim.relays.empty()){const auto site=game.sim.relays[0];game.sim.player={site.position.x-25,site.position.y};game.sim.altitude=site.height;game.sim.frequency=game.sim.targetFrequency=800;step(120,observe);if(game.sim.scans<1)result=8;}
            const Vec saved=game.sim.player;const float savedHeight=game.sim.altitude;if(!game.saveHabitat(false))result=5;game.sim.player={6000,6000};if(!game.continueHabitat()||distance(saved,game.sim.player)>.1f||std::abs(savedHeight-game.sim.altitude)>.1f)result=6;
            game.map=true;shot("04-chart.png");game.map=false;game.screen=Screen::Journal;game.returnScreen=Screen::Play;shot("05-journal.png");game.screen=Screen::Help;shot("06-controls.png");
            game.screen=Screen::Play;game.sim.player={11000,6600};game.sim.altitude=380;game.yaw=1.3f;game.pitch=-.15f;step(4);shot("07-deep-water.png");
            if(!game.assetsOK)result=2;
            std::printf("Habitat rendering, feeding and persistence smoke test %s (code %d).\n",result==0?"passed":"failed",result);
        }else while(!game.quit){
            if(WindowShouldClose()){if(!game.started||game.saveHabitat())game.quit=true;else game.screen=Screen::Pause;}
            if(!game.quit){game.update(std::min(GetFrameTime(),.05f));game.render();}
        }
        game.close();
    }
    CloseWindow();return result;
}
