#version 330
in vec2 fragTexCoord;
in vec4 fragColor;
uniform sampler2D texture0;
uniform vec4 colDiffuse;
uniform float clock;
uniform float disturbance;
uniform float reduced;
out vec4 finalColor;
void main(){
    vec2 uv=fragTexCoord;
    float motion=1-reduced;
    uv.x+=sin(uv.y*17+clock*.4)*.0007*motion;
    float split=disturbance*.003*motion;
    vec3 color=texture(texture0,uv).rgb;
    color.r=mix(color.r,texture(texture0,uv+vec2(split,0)).r,disturbance*.7);
    color.b=mix(color.b,texture(texture0,uv-vec2(split,0)).b,disturbance*.7);
    vec3 bloom=vec3(0);
    for(int i=0;i<8;++i){float a=float(i)*.785398;vec2 delta=vec2(cos(a),sin(a));
        bloom+=max(texture(texture0,uv+delta*vec2(.003,.0048)).rgb-.11,0)*.075;
        bloom+=max(texture(texture0,uv+delta*vec2(.008,.0128)).rgb-.15,0)*.05;
    }
    color+=bloom;
    float vignette=1-smoothstep(.21,.75,length((fragTexCoord-.5)*vec2(.88,1.05)))*.5;
    finalColor=vec4(color*vignette,1)*fragColor*colDiffuse;
}
