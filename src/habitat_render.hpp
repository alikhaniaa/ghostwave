#pragma once
#include "habitat_layout.hpp"
#include "raylib.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <vector>

namespace ghost {
struct HabitatRenderStats {
    int visibleRocks{},modelDraws{},lineSegments{},visibleChunks{},terrainTriangles{},detailTriangles{};
};
namespace habitat_render_detail {
constexpr float units=60.f,chunkSize=16.f,drawRange=64.f,plantRange=36.f;
constexpr int chunksX=15,chunksZ=10;
inline Vector3 add(Vector3 a,Vector3 b){return {a.x+b.x,a.y+b.y,a.z+b.z};}
inline Vector3 sub(Vector3 a,Vector3 b){return {a.x-b.x,a.y-b.y,a.z-b.z};}
inline Vector3 mul(Vector3 a,float n){return {a.x*n,a.y*n,a.z*n};}
inline float dot(Vector3 a,Vector3 b){return a.x*b.x+a.y*b.y+a.z*b.z;}
inline Vector3 cross(Vector3 a,Vector3 b){return {a.y*b.z-a.z*b.y,a.z*b.x-a.x*b.z,a.x*b.y-a.y*b.x};}
inline Vector3 normalized(Vector3 v){const float n=std::sqrt(dot(v,v));return n>1e-8f?mul(v,1/n):Vector3{0,1,0};}
inline Vector3 point(HabitatPoint p){return {p.position.x/units,p.height/units,p.position.y/units};}
inline float floorM(float x,float z){return habitatFloor({x*units,z*units})/units;}
inline float random(int x,int z,int salt=0){return habitat_layout_detail::random(x,z,salt);}
inline Color tint(Color c,float value){return {(unsigned char)std::clamp(c.r*value,0.f,255.f),
    (unsigned char)std::clamp(c.g*value,0.f,255.f),(unsigned char)std::clamp(c.b*value,0.f,255.f),255};}
inline Color blend(Color a,Color b,float t){return {(unsigned char)mix(a.r,b.r,t),(unsigned char)mix(a.g,b.g,t),(unsigned char)mix(a.b,b.b,t),255};}
inline Color bedColor(int biome){constexpr Color colors[]={{102,88,59,255},{129,110,76,255},{91,106,109,255},
    {111,107,66,255},{145,132,91,255},{76,91,97,255}};return colors[std::clamp(biome,0,5)];}
inline Color woodColor(int biome){constexpr Color colors[]={{112,91,62,255},{121,102,74,255},{91,96,81,255},
    {113,103,67,255},{122,113,88,255},{119,117,102,255}};return colors[std::clamp(biome,0,5)];}
inline Color leafColor(int biome){constexpr Color colors[]={{85,124,76,255},{109,125,72,255},{89,112,91,255},
    {105,139,72,255},{131,146,83,255},{83,111,95,255}};return colors[std::clamp(biome,0,5)];}
struct Vertex {Vector3 p,n;Vector2 uv;Color color;};
struct Builder {
    std::vector<Vertex> vertices;
    void tri(Vector3 a,Vector3 b,Vector3 c,Color color) {
        const Vector3 n=normalized(cross(sub(b,a),sub(c,a)));
        vertices.push_back({a,n,{a.x,a.z},color});vertices.push_back({b,n,{b.x,b.z},color});vertices.push_back({c,n,{c.x,c.z},color});
    }
    void tri(Vertex a,Vertex b,Vertex c){vertices.push_back(a);vertices.push_back(b);vertices.push_back(c);}
    void leafQuad(Vector3 a,Vector3 b,Vector3 c,Vector3 d,Color low,Color high,float v0,float v1) {
        const Vector3 n=normalized(cross(sub(b,a),sub(c,a)));
        const Vertex p{a,n,{0,v0},low},q{b,n,{1,v0},low},r{c,n,{1,v1},high},s{d,n,{0,v1},high};
        tri(p,q,r);tri(p,r,s);
        const Vector3 back=mul(n,-1);
        tri({a,back,{0,v0},low},{c,back,{1,v1},high},{b,back,{1,v0},low});
        tri({a,back,{0,v0},low},{d,back,{0,v1},high},{c,back,{1,v1},high});
    }
    Mesh mesh() const {
        Mesh result{};if(vertices.empty())return result;
        result.vertexCount=(int)vertices.size();result.triangleCount=result.vertexCount/3;
        result.vertices=(float*)MemAlloc((unsigned int)(vertices.size()*3*sizeof(float)));
        result.normals=(float*)MemAlloc((unsigned int)(vertices.size()*3*sizeof(float)));
        result.texcoords=(float*)MemAlloc((unsigned int)(vertices.size()*2*sizeof(float)));
        result.colors=(unsigned char*)MemAlloc((unsigned int)(vertices.size()*4));
        for(std::size_t i=0;i<vertices.size();++i) {
            const auto& v=vertices[i];
            result.vertices[3*i]=v.p.x;result.vertices[3*i+1]=v.p.y;result.vertices[3*i+2]=v.p.z;
            result.normals[3*i]=v.n.x;result.normals[3*i+1]=v.n.y;result.normals[3*i+2]=v.n.z;
            result.texcoords[2*i]=v.uv.x;result.texcoords[2*i+1]=v.uv.y;
            result.colors[4*i]=v.color.r;result.colors[4*i+1]=v.color.g;result.colors[4*i+2]=v.color.b;result.colors[4*i+3]=v.color.a;
        }
        UploadMesh(&result,false);return result;
    }
};
struct Chunk {
    std::array<Model,5> models{};std::array<int,5> triangles{};
    Vector3 center{};float extent=14.f;int rocks{};
};
struct Cache {
    std::vector<Chunk> chunks;std::uint32_t seed{};std::size_t rockCount{};unsigned int shaderId{};
    int kindLocation=-1;Shader shader{};bool initialized{};
};
inline Cache cache;
inline HabitatRenderStats stats;
inline int chunkIndex(float x,float z){return std::clamp((int)std::floor(x/chunkSize),0,chunksX-1)+
    chunksX*std::clamp((int)std::floor(z/chunkSize),0,chunksZ-1);}
inline void modelFrom(Chunk& chunk,int kind,const Builder& builder,Shader shader,Texture2D texture) {
    if(builder.vertices.empty())return;
    auto& model=chunk.models[(std::size_t)kind];model=LoadModelFromMesh(builder.mesh());
    model.materials[0].shader=shader;model.materials[0].maps[MATERIAL_MAP_DIFFUSE].texture=texture;
    chunk.triangles[(std::size_t)kind]=(int)builder.vertices.size()/3;
    for(const auto& v:builder.vertices) {
        const float distance=std::hypot(v.p.x-chunk.center.x,v.p.z-chunk.center.z);
        chunk.extent=std::max(chunk.extent,distance+.25f);
    }
}
inline Vertex groundVertex(float x,float z) {
    const float y=floorM(x,z),e=.12f;
    const Vector3 normal=normalized({floorM(x-e,z)-floorM(x+e,z),2*e,floorM(x,z-e)-floorM(x,z+e)});
    Color color=bedColor(habitatBiome({x*units,z*units}));
    color=tint(color,.77f+habitat_layout_detail::noise(x*.72f,z*.72f,19)*.36f);
    if(normal.y<.72f)color=blend(color,Color{114,116,101,255},(.72f-normal.y)*.6f);
    return {{x,y,z},normal,{x*.35f,z*.35f},color};
}
inline void terrain(Builder& builder,float x0,float z0) {
    constexpr int steps=32;constexpr float step=chunkSize/steps;
    std::array<Vertex,(steps+1)*(steps+1)> points{};
    for(int z=0;z<=steps;++z)for(int x=0;x<=steps;++x)
        points[(std::size_t)z*(steps+1)+x]=groundVertex(x0+x*step,z0+z*step);
    for(int z=0;z<steps;++z)for(int x=0;x<steps;++x) {
        const auto a=points[(std::size_t)z*(steps+1)+x],b=points[(std::size_t)(z+1)*(steps+1)+x];
        const auto c=points[(std::size_t)z*(steps+1)+x+1],d=points[(std::size_t)(z+1)*(steps+1)+x+1];
        builder.tri(a,b,c);builder.tri(c,b,d);
    }
}
inline void surface(Builder& builder,float x0,float z0) {
    auto vertex=[](float x,float z) {
        const float wave=.035f*std::sin(x*.73f+z*.22f)+.023f*std::cos(z*.91f-x*.16f);
        return Vertex{{x,habitatSurfaceAltitude/units+wave,z},{0,-1,0},{x*.14f,z*.14f},Color{100,145,127,255}};
    };
    for(int z=0;z<4;++z)for(int x=0;x<4;++x) {
        const float px=x0+x*4,pz=z0+z*4;
        auto a=vertex(px,pz),b=vertex(px+4,pz),c=vertex(px+4,pz+4),d=vertex(px,pz+4);
        builder.tri(a,b,c);builder.tri(a,c,d);
    }
}
inline void tube(Builder& builder,const HabitatLimb& limb,int sides=9) {
    if(limb.count<2)return;
    std::array<std::array<Vertex,12>,7> rings{};
    std::array<Vector3,7> centers{};Vector3 previousSide{1,0,0};float length=0;
    const Color base=woodColor(limb.biome);
    for(int i=0;i<limb.count;++i)centers[(std::size_t)i]=point(limb.points[(std::size_t)i]);
    for(int i=0;i<limb.count;++i) {
        const Vector3 center=centers[(std::size_t)i];
        const Vector3 tangent=normalized(sub(centers[(std::size_t)std::min(i+1,limb.count-1)],centers[(std::size_t)std::max(0,i-1)]));
        Vector3 side=sub(previousSide,mul(tangent,dot(previousSide,tangent)));
        if(dot(side,side)<.001f)side=cross(tangent,std::abs(tangent.y)<.9f?Vector3{0,1,0}:Vector3{1,0,0});
        side=normalized(side);previousSide=side;const Vector3 up=normalized(cross(tangent,side));
        if(i)length+=std::sqrt(dot(sub(center,centers[(std::size_t)i-1]),sub(center,centers[(std::size_t)i-1])));
        for(int j=0;j<sides;++j) {
            const float angle=j*2*pi/sides;const Vector3 radial=add(mul(side,std::cos(angle)),mul(up,std::sin(angle)));
            const float irregularity=.96f+.04f*std::sin(angle*3+i*.42f);
            const Vector3 position=add(center,mul(radial,limb.radii[(std::size_t)i]/units*irregularity));
            const Color color=tint(base,.77f+.22f*(.5f+.5f*std::sin(angle*5+i*.32f)));
            rings[(std::size_t)i][(std::size_t)j]={position,radial,{j/(float)sides,length*.6f},color};
        }
    }
    for(int i=1;i<limb.count;++i)for(int j=0;j<sides;++j) {
        const int next=(j+1)%sides;
        const auto a=rings[(std::size_t)i-1][(std::size_t)j],b=rings[(std::size_t)i-1][(std::size_t)next];
        const auto c=rings[(std::size_t)i][(std::size_t)j],d=rings[(std::size_t)i][(std::size_t)next];
        builder.tri(a,b,c);builder.tri(c,b,d);
    }
    for(int j=0;j<sides;++j) {
        const int next=(j+1)%sides;
        builder.tri(centers[0],rings[0][(std::size_t)next].p,rings[0][(std::size_t)j].p,tint(base,1.13f));
        builder.tri(centers[(std::size_t)limb.count-1],rings[(std::size_t)limb.count-1][(std::size_t)j].p,
                    rings[(std::size_t)limb.count-1][(std::size_t)next].p,tint(base,1.18f));
    }
}
inline void stone(Builder& builder,const Rock& rock,int biome,int index) {
    constexpr int sides=11,rings=5;
    std::array<std::array<Vector3,sides>,rings> points{};
    const float x=rock.position.x/units,z=rock.position.y/units,bottom=rock.bottom/units,height=(rock.top-rock.bottom)/units;
    const float radius=rock.radius/units;constexpr float profiles[]={.77f,.98f,.92f,.78f,.55f};
    const Color base=blend(bedColor(biome),Color{137,145,134,255},.23f);
    for(int ring=0;ring<rings;++ring)for(int side=0;side<sides;++side) {
        const float angle=side*2*pi/sides+random(index,side,41)*.17f;
        const float r=radius*profiles[ring]*(.89f+.1f*random(index,side,42));
        const float y=bottom+height*std::clamp(ring/4.f+(random(index,side+ring,43)-.5f)*.11f,0.f,1.f);
        points[(std::size_t)ring][(std::size_t)side]={x+std::cos(angle)*r,y,z+std::sin(angle)*r};
    }
    for(int ring=1;ring<rings;++ring)for(int side=0;side<sides;++side) {
        const int next=(side+1)%sides;const Color color=tint(base,.77f+ring*.045f+random(index,side,45)*.15f);
        builder.tri(points[(std::size_t)ring-1][(std::size_t)side],points[(std::size_t)ring][(std::size_t)side],points[(std::size_t)ring][(std::size_t)next],color);
        builder.tri(points[(std::size_t)ring-1][(std::size_t)side],points[(std::size_t)ring][(std::size_t)next],points[(std::size_t)ring-1][(std::size_t)next],color);
    }
    for(int side=0;side<sides;++side)
        builder.tri({x,bottom+height*.98f,z},points[rings-1][(std::size_t)((side+1)%sides)],points[rings-1][(std::size_t)side],tint(base,1.02f));
}
inline void blade(Builder& builder,Vector3 base,float angle,float height,float width,float lean,Color color,int segments=5) {
    const Vector3 direction{std::cos(angle),0,std::sin(angle)},side{-std::sin(angle),0,std::cos(angle)};
    auto edge=[&](float t,float sign) {
        const float profile=std::pow(std::max(0.f,std::sin(t*pi)),.72f)*width+.003f*(1-t);
        const Vector3 center=add(base,add(mul(direction,lean*t*t),Vector3{0,height*t,0}));
        return add(center,mul(side,sign*profile));
    };
    for(int i=0;i<segments;++i) {
        const float a=i/(float)segments,b=(i+1)/(float)segments;
        builder.leafQuad(edge(a,-1),edge(a,1),edge(b,1),edge(b,-1),tint(color,.70f+a*.27f),tint(color,.70f+b*.27f),a,b);
    }
}
inline void plant(Builder& builder,float x,float z,int biome,int seed,bool dense=false) {
    const float ground=floorM(x,z);if(ground>19)return;
    const Color color=leafColor(biome);const float phase=random(seed,biome,2)*2*pi;
    const bool tall=(biome==3 || biome==4) && random(seed,biome,5)<.52f;
    const bool broad=(biome==0 || biome==1 || biome==3) && random(seed,biome,8)<.40f;
    const int count=dense?7:4+(seed%3);
    for(int j=0;j<count;++j) {
        const float a=phase+j*2.39f,n=random(seed,j,11);
        const float height=(tall?1.6f+n*2.1f:broad?.75f+n*1.15f:.35f+n*1.05f)*(dense?1.15f:1.f);
        const float width=broad?.07f+n*.10f:tall?.022f+n*.018f:.025f+n*.045f;
        const Vector3 base{x+std::cos(a)*.12f,ground+.025f,z+std::sin(a)*.12f};
        blade(builder,base,a,height,width,.22f+n*(broad?.95f:.5f),tint(color,.83f+n*.28f));
        if(tall && j<3) {
            const Vector3 stem=add(base,{std::cos(a)*.18f,height*.46f,std::sin(a)*.18f});
            blade(builder,stem,a+1.4f,height*.35f,.08f,.62f,tint(color,1.04f),4);
            blade(builder,add(stem,{0,height*.16f,0}),a-1.8f,height*.28f,.065f,.57f,tint(color,.86f),4);
        }
    }
    if(biome==0 || biome==1 || biome==3)for(int j=0;j<2;++j) {
        const float a=phase+j*2.2f,offset=.22f+j*.18f;
        blade(builder,{x+std::cos(a)*offset,ground+.018f,z+std::sin(a)*offset},a,.035f,.075f,.35f,Color{126,106,61,255},4);
    }
}
inline void vegetation(Builder& builder,float x0,float z0) {
    constexpr float cell=2.15f;
    const int minX=(int)std::floor(x0/cell),maxX=(int)std::ceil((x0+chunkSize)/cell);
    const int minZ=(int)std::floor(z0/cell),maxZ=(int)std::ceil((z0+chunkSize)/cell);
    for(int z=minZ;z<=maxZ;++z)for(int x=minX;x<=maxX;++x) {
        const float px=(x+.08f+.84f*random(x,z,72))*cell,pz=(z+.08f+.84f*random(x,z,73))*cell;
        if(px<x0 || px>=x0+chunkSize || pz<z0 || pz>=z0+chunkSize)continue;
        const int biome=habitatBiome({px*units,pz*units});
        constexpr float density[]={.79f,.76f,.28f,.92f,.62f,.21f};
        if(random(x,z,74)>density[biome])continue;
        plant(builder,px,pz,biome,x+z*137);
    }
    // Foreground leaves frame the first hunt without filling its clear pocket.
    for(int z=5;z<=25;++z)for(int x=0;x<=28;++x) {
        const float px=x*.68f+1.2f+random(x,z,88)*.28f,pz=z*.72f+.5f+random(x,z,89)*.28f;
        if(px<x0 || px>=x0+chunkSize || pz<z0 || pz>=z0+chunkSize)continue;
        const float d=std::hypot(px-5.83f,pz-13.f);
        if(d<2.1f || d>9.5f || random(x,z,90)>.58f)continue;
        plant(builder,px,pz,0,x+z*43,true);
    }
}
} // namespace habitat_render_detail

// These resources own meshes; shader and texture handles remain App-owned.
inline void unloadHabitatGeometry() {
    using namespace habitat_render_detail;
    for(auto& chunk:cache.chunks)for(auto& model:chunk.models)if(model.meshCount>0)UnloadModel(model);
    cache={};stats={};
}
inline void initHabitatGeometry(const Simulation& sim,Shader shader,Texture2D texture) {
    using namespace habitat_render_detail;
    unloadHabitatGeometry();cache.chunks.resize(chunksX*chunksZ);
    std::array<std::vector<std::size_t>,chunksX*chunksZ> limbLists,rockLists;
    const auto& limbs=habitatLimbs();
    for(std::size_t i=0;i<limbs.size();++i) {
        const auto& limb=limbs[i];Vector3 midpoint{};
        for(int j=0;j<limb.count;++j)midpoint=add(midpoint,point(limb.points[(std::size_t)j]));
        midpoint=mul(midpoint,1.f/limb.count);limbLists[(std::size_t)chunkIndex(midpoint.x,midpoint.z)].push_back(i);
    }
    for(std::size_t i=0;i<sim.rocks.size();++i)
        rockLists[(std::size_t)chunkIndex(sim.rocks[i].position.x/units,sim.rocks[i].position.y/units)].push_back(i);
    for(int z=0;z<chunksZ;++z)for(int x=0;x<chunksX;++x) {
        const int index=x+z*chunksX;auto& chunk=cache.chunks[(std::size_t)index];
        const float x0=x*chunkSize,z0=z*chunkSize;chunk.center={x0+8,11,z0+8};
        Builder ground;ground.vertices.reserve(6144);terrain(ground,x0,z0);modelFrom(chunk,0,ground,shader,texture);
        Builder wood;for(auto limb:limbLists[(std::size_t)index])tube(wood,limbs[limb]);modelFrom(chunk,1,wood,shader,texture);
        Builder leaves;vegetation(leaves,x0,z0);modelFrom(chunk,2,leaves,shader,texture);
        Builder rocks;chunk.rocks=(int)rockLists[(std::size_t)index].size();
        for(auto rock:rockLists[(std::size_t)index])stone(rocks,sim.rocks[rock],habitatBiome(sim.rocks[rock].position),(int)rock);
        modelFrom(chunk,3,rocks,shader,texture);
        Builder water;surface(water,x0,z0);modelFrom(chunk,4,water,shader,texture);
    }
    cache.seed=sim.worldSeed;cache.rockCount=sim.rocks.size();cache.shaderId=shader.id;cache.shader=shader;
    cache.kindLocation=GetShaderLocation(shader,"habitatKind");cache.initialized=true;
}
// Compatibility for App's optional mineral resource. Not used as repeated scenery.
inline Mesh createHabitatStoneMesh() {
    habitat_render_detail::Builder builder;
    habitat_render_detail::stone(builder,{{0,0},60,-60,60},2,17);return builder.mesh();
}
// Terrain, roots, logs, leaves and surface are cached per 16 m chunk. The
// cache reuses the shader and diffuse texture of the caller's rock material.
inline void drawHabitatGeometry(const Simulation& sim,Camera3D camera,const Model& rock) {
    using namespace habitat_render_detail;
    const auto& material=rock.materials[0];
    if(!cache.initialized || cache.seed!=sim.worldSeed || cache.rockCount!=sim.rocks.size() || cache.shaderId!=material.shader.id)
        initHabitatGeometry(sim,material.shader,material.maps[MATERIAL_MAP_DIFFUSE].texture);
    stats={};const Vector3 forward=normalized(sub(camera.target,camera.position));
    struct Visible {std::size_t index;float distance;};std::vector<Visible> visible;visible.reserve(80);
    for(std::size_t i=0;i<cache.chunks.size();++i) {
        const auto& chunk=cache.chunks[i];
        const float horizontal=std::hypot(camera.position.x-chunk.center.x,camera.position.z-chunk.center.z);
        const float distance=std::max(0.f,horizontal-chunk.extent);
        if(distance>drawRange)continue;
        const Vector3 delta=sub(chunk.center,camera.position);
        if(horizontal>chunk.extent+12 && dot(delta,forward)<-chunk.extent-12)continue;
        visible.push_back({i,distance});
    }
    std::sort(visible.begin(),visible.end(),[](const Visible& a,const Visible& b){return a.distance<b.distance;});
    stats.visibleChunks=(int)visible.size();
    for(int kind:{0,3,1,2,4}) {
        if(cache.kindLocation>=0){const float value=(float)kind;SetShaderValue(cache.shader,cache.kindLocation,&value,SHADER_UNIFORM_FLOAT);}
        for(const auto& selected:visible) {
            const auto& chunk=cache.chunks[selected.index];
            if(kind==2 && selected.distance>plantRange)continue;
            const auto& model=chunk.models[(std::size_t)kind];if(model.meshCount==0)continue;
            DrawModel(model,{0,0,0},1,WHITE);++stats.modelDraws;
            if(kind==0)stats.terrainTriangles+=chunk.triangles[0];else stats.detailTriangles+=chunk.triangles[(std::size_t)kind];
            if(kind==3)stats.visibleRocks+=chunk.rocks;
        }
    }
    if(cache.kindLocation>=0){const float value=0;SetShaderValue(cache.shader,cache.kindLocation,&value,SHADER_UNIFORM_FLOAT);}
}
inline const HabitatRenderStats& habitatRenderStats(){return habitat_render_detail::stats;}
} // namespace ghost
