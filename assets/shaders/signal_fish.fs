#version 330
in vec2 fragTexCoord;
in vec4 fragColor;
uniform sampler2D texture0;
uniform vec4 colDiffuse;
uniform float clock;
uniform float disturbance;
out vec4 finalColor;
void main(){
    vec4 tex=texture(texture0,fragTexCoord);
    if(tex.a<.08)discard;
    float luminance=dot(tex.rgb,vec3(.22,.68,.10));
    float fibres=pow(.5+.5*sin(fragTexCoord.x*180+fragTexCoord.y*27-clock*1.5),10);
    float nervous=pow(.5+.5*sin(fragTexCoord.y*110+sin(fragTexCoord.x*8)*3),12);
    vec3 cyan=vec3(.23,.87,.84),warm=vec3(1,.51,.3);
    vec3 color=mix(cyan,warm,disturbance);
    color*=.25+luminance*2.4+fibres*.34+nervous*.12;
    float alpha=tex.a*fragColor.a;
    finalColor=vec4(color*fragColor.rgb,alpha);
}
