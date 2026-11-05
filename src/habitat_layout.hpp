#pragma once

#include "simulation.hpp"
#include <array>
#include <cmath>
#include <cstdint>
#include <vector>

// Authored habitat geometry, shared by rendering and collision. Positions,
// heights and radii in this public interface are simulation units (60 per m).
// This is a navigable height field and woody skeleton, not river bathymetry.
namespace ghost {
constexpr float habitatUnitsPerMetre=60.f;
constexpr float habitatSurfaceAltitude=1344.f;

namespace habitat_layout_detail {
inline float smooth(float a,float b,float x) {
    const float t=std::clamp((x-a)/(b-a),0.f,1.f);return t*t*(3-2*t);
}
inline std::uint32_t hash(std::uint32_t n) {
    n^=n>>16;n*=0x7feb352du;n^=n>>15;n*=0x846ca68bu;return n^(n>>16);
}
inline float random(int x,int z,int salt=0) {
    return (hash((std::uint32_t)x*73856093u^(std::uint32_t)z*19349663u^
                 (std::uint32_t)salt*83492791u^0x52495652u)>>8)*(1.f/16777216.f);
}
inline float noise(float x,float z,int salt=0) {
    const int ix=(int)std::floor(x),iz=(int)std::floor(z);
    const float a=smooth(0,1,x-ix),b=smooth(0,1,z-iz);
    return mix(mix(random(ix,iz,salt),random(ix+1,iz,salt),a),
               mix(random(ix,iz+1,salt),random(ix+1,iz+1,salt),a),b);
}
inline float weight(float x,float z,int biome) {
    const float left=(biome%3)*80.f,bottom=(biome/3)*80.f;
    return smooth(left-10,left+10,x)*(1-smooth(left+70,left+90,x))*
           smooth(bottom-10,bottom+10,z)*(1-smooth(bottom+70,bottom+90,z));
}
inline float channelDistance(float x,float z) {
    float d=std::min(std::abs(z-(39+8*std::sin(x*.034f)+3*std::sin(x*.11f))),
                     std::abs(z-(118+9*std::sin(x*.032f+1.4f))));
    for(int i=0;i<3;++i)
        d=std::min(d,std::abs(x-(41+i*80+6*std::sin(z*.039f+i*1.9f))));
    // A shallow tributary begins at the player's mangrove refuge and joins
    // the northern channel. It is a real depression, not a flat start room.
    const float startZ=13+28*smooth(7,87,x)+2*std::sin(x*.067f)*smooth(8,30,x);
    d=std::min(d,std::hypot(std::max(0.f,x-92),z-startZ));
    return d;
}
} // namespace habitat_layout_detail

inline int habitatBiome(Vec p) {
    return std::clamp((int)(p.x/4800),0,2)+3*std::clamp((int)(p.y/4800),0,1);
}
inline float habitatFloor(Vec position) {
    using namespace habitat_layout_detail;
    const float x=position.x/habitatUnitsPerMetre,z=position.y/habitatUnitsPerMetre;
    const float d=channelDistance(x,z);
    constexpr float bankHeight[]={7.0f,4.7f,14.8f,7.6f,4.2f,8.5f};
    constexpr float channelWidth[]={7.f,11.f,5.3f,12.f,17.f,13.f};
    float height=0,total=0;
    for(int biome=0;biome<6;++biome) {
        const float w=weight(x,z,biome);if(w<=0)continue;
        float h=.45f+bankHeight[biome]*smooth(channelWidth[biome]*.40f,channelWidth[biome]*2.1f,d);
        h+=(noise(x*.087f,z*.087f,4)-.4f)*.85f;
        h+=.20f*std::sin(x*.61f+z*.23f)*std::sin(z*.41f-x*.09f);
        if(biome==1)h+=.32f*std::sin(x*.17f+z*.49f);
        if(biome==2)h+=1.1f*smooth(.40f,.66f,noise(x*.16f,z*.09f,23))*smooth(3,13,d);
        if(biome==3)h+=.75f*noise(x*.23f,z*.17f,9);
        if(biome==4)h+=.25f*std::pow(.5f+.5f*std::sin(x*.20f+z*.85f),3);
        if(biome==5) {
            const float basin=std::exp(-((x-205)*(x-205)+(z-122)*(z-122))/720.f);
            h*=1-.80f*basin;
        }
        height+=h*w;total+=w;
    }
    height=total>0?height/total:10.f;
    const float edge=std::min({x,z,240-x,160-z});
    height+=(1-smooth(0,8,edge))*13.f;
    const float startDistance=std::hypot(x-7.0f,z-13.f);
    const float startBed=1.45f+.20f*noise(x*.31f,z*.25f,31)+.055f*(x-5.8f);
    height=mix(height,startBed,1-smooth(5.0f,13.f,startDistance));
    // Refuge clearings retain a shallow depression under the authored shelter
    // positions. Physics can therefore place animals directly above this bed.
    for(int region=1;region<6;++region) {
        const float sx=32+(region%3)*80.f,sz=46.5f+(region/3)*80.f;
        const float desired=(160+region*45)/60.f-.8f;
        const float influence=1-smooth(2.7f,6.5f,std::hypot(x-sx,z-sz));
        height=mix(height,std::min(height,desired),influence);
    }
    return std::clamp(height,.12f,19.6f)*habitatUnitsPerMetre;
}

struct HabitatPoint {Vec position{};float height{};};
struct HabitatLimb {
    std::array<HabitatPoint,7> points{};
    std::array<float,7> radii{};
    int count{},biome{};bool solid=true,fallen=false;
};
struct HabitatBranch {
    Vec a{},b{};float aHeight{},bHeight{},radiusA{},radiusB{};
    int biome{};bool solid=true;
};

namespace habitat_layout_detail {
inline HabitatPoint point(float x,float y,float z) {return {{x*60,z*60},y*60};}
inline float floorM(float x,float z) {return habitatFloor({x*60,z*60})/60;}
inline float pointSegmentDistance(HabitatPoint p,HabitatPoint a,HabitatPoint b) {
    const float dx=b.position.x-a.position.x,dy=b.height-a.height,dz=b.position.y-a.position.y;
    const float lengthSquared=dx*dx+dy*dy+dz*dz;
    const float t=lengthSquared>1e-8f?std::clamp(((p.position.x-a.position.x)*dx+
        (p.height-a.height)*dy+(p.position.y-a.position.y)*dz)/lengthSquared,0.f,1.f):0;
    return std::sqrt(std::pow(p.position.x-a.position.x-t*dx,2)+
                     std::pow(p.height-a.height-t*dy,2)+std::pow(p.position.y-a.position.y-t*dz,2));
}
inline void addLimb(std::vector<HabitatLimb>& limbs,HabitatLimb limb) {
    // Preserve the initial observation/feeding volume. Other wood is shared
    // with physics as tapered capsule segments, never an invisible cylinder.
    const HabitatPoint spawn{{350,780},240};
    for(int i=1;i<limb.count;++i)
        if(pointSegmentDistance(spawn,limb.points[i-1],limb.points[i])<
           76+std::max(limb.radii[i-1],limb.radii[i]))return;
    limbs.push_back(limb);
}
inline void tree(std::vector<HabitatLimb>& limbs,float x,float z,int biome,int seed,float scale=1) {
    const float ground=floorM(x,z),angle=random(seed,biome,13)*2*pi;
    const float leanX=std::cos(angle)*2.6f*scale,leanZ=std::sin(angle)*2.6f*scale;
    const float crown=std::min(22.8f,ground+(12+random(seed,biome,3)*7)*scale);
    const float collar=std::min(ground+4.1f*scale,crown-1);
    const float trunkRadius=(.30f+random(seed,biome,9)*.24f)*scale;
    HabitatLimb trunk;trunk.biome=biome;trunk.count=5;
    for(int i=0;i<5;++i) {
        const float t=i/4.f;
        trunk.points[i]=point(x+leanX*t*t,collar+(crown-collar)*t,z+leanZ*t*t);
        trunk.radii[i]=60*trunkRadius*(1-.67f*t);
    }
    addLimb(limbs,trunk);
    const int roots=5+(seed%3);
    for(int j=0;j<roots;++j) {
        const float a=angle+j*2*pi/roots+random(seed,j,7)*.45f;
        const float reach=(2.4f+random(seed,j,12)*3.7f)*scale;
        const float fx=x+std::cos(a)*reach,fz=z+std::sin(a)*reach;
        const float foot=floorM(fx,fz);
        HabitatLimb root;root.biome=biome;root.count=6;
        for(int i=0;i<6;++i) {
            const float t=i/5.f,q=1-t;
            const float px=x+std::cos(a)*reach*q*q,pz=z+std::sin(a)*reach*q*q;
            const float y=foot+(collar-foot)*std::pow(t,.72f)+.55f*std::sin(t*pi);
            root.points[i]=point(px,y,pz);
            root.radii[i]=60*(.045f*scale+trunkRadius*.72f*std::pow(t,.8f));
        }
        addLimb(limbs,root);
        if(j%2==0) {
            HabitatLimb fork;fork.biome=biome;fork.count=4;
            const HabitatPoint joint=root.points[2];
            const float ex=fx+std::cos(a+1.2f)*1.8f*scale,ez=fz+std::sin(a+1.2f)*1.8f*scale;
            for(int i=0;i<4;++i) {
                const float t=i/3.f;
                fork.points[i]=point(mix(ex,joint.position.x/60,t),mix(floorM(ex,ez),joint.height/60,t)+.16f*std::sin(t*pi),mix(ez,joint.position.y/60,t));
                fork.radii[i]=60*scale*(.025f+.07f*t);
            }
            addLimb(limbs,fork);
        }
    }
    for(int j=0;j<3;++j) {
        const HabitatPoint start=trunk.points[j+1];
        const float a=angle+j*2.2f,reach=(2.5f+random(seed,j,21)*3.2f)*scale;
        HabitatLimb b;b.biome=biome;b.count=5;
        for(int i=0;i<5;++i) {
            const float t=i/4.f;
            b.points[i]=point(start.position.x/60+std::cos(a)*reach*t,
                std::min(23.f,start.height/60+1.8f*std::sin(t*pi*.72f)-.5f*t),start.position.y/60+std::sin(a)*reach*t);
            b.radii[i]=60*trunkRadius*.6f*(1-.86f*t);
        }
        addLimb(limbs,b);
    }
}
inline void log(std::vector<HabitatLimb>& limbs,float x,float z,int biome,int seed,float scale=1) {
    const float a=random(seed,biome,47)*2*pi,reach=(3+random(seed,biome,12)*5)*scale;
    HabitatLimb log;log.biome=biome;log.count=7;log.fallen=true;
    for(int i=0;i<7;++i) {
        const float t=i/6.f,px=x+std::cos(a)*(t-.5f)*reach*2,pz=z+std::sin(a)*(t-.5f)*reach*2;
        const float radius=(.14f+.25f*(1-t))*scale;
        log.points[i]=point(px,floorM(px,pz)+radius+.30f*std::sin(t*pi),pz);
        log.radii[i]=radius*60;
    }
    addLimb(limbs,log);
    for(int j=0;j<3;++j) {
        const auto base=log.points[j+2];const float angle=a+(j%2?1:-1)*(.7f+j*.25f);
        HabitatLimb branch;branch.biome=biome;branch.count=4;branch.fallen=true;
        for(int i=0;i<4;++i) {
            const float t=i/3.f;
            branch.points[i]=point(base.position.x/60+std::cos(angle)*t*2.3f*scale,
                base.height/60+std::sin(t*pi*.62f)*1.1f*scale,base.position.y/60+std::sin(angle)*t*2.3f*scale);
            branch.radii[i]=60*scale*(.12f-.096f*t);
        }
        addLimb(limbs,branch);
    }
}
} // namespace habitat_layout_detail

inline const std::vector<HabitatLimb>& habitatLimbs() {
    static const std::vector<HabitatLimb> limbs=[] {
        using namespace habitat_layout_detail;
        std::vector<HabitatLimb> result;result.reserve(1500);
        // Deliberate refuge composition: a root arch on each side, fallen wood
        // at the bed and further trunks that make a layered passage downstream.
        tree(result,3.0f,7.6f,0,1001,.72f);
        tree(result,9.3f,19.6f,0,1002,.86f);
        tree(result,17.3f,8.7f,0,1003,.94f);
        tree(result,26.6f,20.4f,0,1004,1.1f);
        log(result,11.2f,16.5f,0,1005,.7f);
        for(int iz=0;iz<12;++iz)for(int ix=0;ix<18;++ix) {
            const float x=7+ix*13.f+(random(ix,iz,1)-.5f)*7;
            const float z=7+iz*13.f+(random(ix,iz,2)-.5f)*7;
            if(x>236 || z>156 || std::hypot(x-10,z-13)<20)continue;
            const int biome=habitatBiome({x*60,z*60}),seed=ix+iz*19;
            constexpr float density[]={.90f,.40f,.08f,.88f,.13f,.15f};
            if(random(ix,iz,5)<density[biome] && floorM(x,z)<17)
                tree(result,x,z,biome,seed,.65f+random(ix,iz,6)*.45f);
            if(random(ix,iz,19)<(biome==1?.72f:biome==5?.44f:.24f))
                log(result,x+2.2f,z-2,biome,seed,.7f+random(ix,iz,22)*.45f);
        }
        // Each major habitat has a recognisable structural landmark.
        log(result,113,38,1,2201,1.65f); // Waterlogged giant across the silt bank.
        tree(result,37,121,3,2202,1.5f); // Flooded gallery's spreading crown.
        log(result,206,121,5,2203,1.65f); // Pale deadfall above the deep basin.
        return result;
    }();
    return limbs;
}
inline const std::vector<HabitatBranch>& habitatBranches() {
    static const std::vector<HabitatBranch> branches=[] {
        std::vector<HabitatBranch> result;
        for(const auto& limb:habitatLimbs())for(int i=1;i<limb.count;++i)
            result.push_back({limb.points[i-1].position,limb.points[i].position,
                limb.points[i-1].height,limb.points[i].height,limb.radii[i-1],limb.radii[i],limb.biome,limb.solid});
        return result;
    }();
    return branches;
}

namespace habitat_layout_detail {
constexpr int branchCellsX=15,branchCellsZ=10;
constexpr float branchCellSize=960.f;
inline int branchCellX(float x){return std::clamp((int)std::floor(x/branchCellSize),0,branchCellsX-1);}
inline int branchCellZ(float z){return std::clamp((int)std::floor(z/branchCellSize),0,branchCellsZ-1);}
struct BranchBounds {Vec minimum{},maximum{};int firstX{},firstZ{};};
struct BranchIndex {
    std::array<std::vector<std::size_t>,branchCellsX*branchCellsZ> cells;
    std::vector<BranchBounds> bounds;
};
inline const BranchIndex& branchIndex() {
    static const BranchIndex index=[] {
        BranchIndex result;const auto& branches=habitatBranches();result.bounds.reserve(branches.size());
        for(std::size_t i=0;i<branches.size();++i) {
            const auto& b=branches[i];const float radius=std::max(b.radiusA,b.radiusB);
            const Vec minimum{std::min(b.a.x,b.b.x)-radius,std::min(b.a.y,b.b.y)-radius};
            const Vec maximum{std::max(b.a.x,b.b.x)+radius,std::max(b.a.y,b.b.y)+radius};
            const int firstX=branchCellX(minimum.x),firstZ=branchCellZ(minimum.y);
            result.bounds.push_back({minimum,maximum,firstX,firstZ});
            for(int z=firstZ;z<=branchCellZ(maximum.y);++z)for(int x=firstX;x<=branchCellX(maximum.x);++x)
                result.cells[(std::size_t)(x+z*branchCellsX)].push_back(i);
        }
        return result;
    }();
    return index;
}
} // namespace habitat_layout_detail

// Allocation-free broadphase. Each branch whose horizontal radius-expanded
// bounds intersects this query is reported once. Callers perform their own
// vertical/tapered capsule narrowphase. A segment query uses endpoint min/max.
template<class Callback>
inline void forEachHabitatBranchInBounds(Vec minimum,Vec maximum,Callback&& callback) {
    using namespace habitat_layout_detail;
    if(!std::isfinite(minimum.x)||!std::isfinite(minimum.y)||!std::isfinite(maximum.x)||!std::isfinite(maximum.y))return;
    if(minimum.x>maximum.x)std::swap(minimum.x,maximum.x);
    if(minimum.y>maximum.y)std::swap(minimum.y,maximum.y);
    const auto& index=branchIndex();const auto& branches=habitatBranches();
    const int firstX=branchCellX(minimum.x),firstZ=branchCellZ(minimum.y);
    for(int z=firstZ;z<=branchCellZ(maximum.y);++z)for(int x=firstX;x<=branchCellX(maximum.x);++x)
        for(const auto i:index.cells[(std::size_t)(x+z*branchCellsX)]) {
            const auto& bounds=index.bounds[i];
            // Visit the first cell in the overlap of branch/query cell ranges.
            // This avoids per-query sets, temporary vectors, or mutable stamps.
            if(x!=std::max(firstX,bounds.firstX)||z!=std::max(firstZ,bounds.firstZ))continue;
            if(bounds.maximum.x<minimum.x||bounds.maximum.y<minimum.y||bounds.minimum.x>maximum.x||bounds.minimum.y>maximum.y)continue;
            callback(branches[i]);
        }
}
} // namespace ghost
