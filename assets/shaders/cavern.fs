#version 330
in vec3 worldPosition;
in vec3 normal;
in vec2 fragTexCoord;
in vec4 fragColor;
uniform sampler2D texture0;
uniform vec4 colDiffuse;
uniform vec3 eye;
uniform vec4 sonar[4];
uniform float sonarRange[4];
uniform float clock;
uniform float jam;
uniform float hush;
uniform float emission;
uniform float waterConductivity;
uniform float sensoryGain;
out vec4 finalColor;
float hash(vec2 p){return fract(sin(dot(p,vec2(127.1,311.7)))*43758.5453);}
void main(){
    vec3 n=normalize(normal);
    vec3 weights=pow(abs(n),vec3(4));weights/=max(dot(weights,vec3(1)),.001);
    vec3 tex=texture(texture0,worldPosition.yz*.38).rgb*weights.x+
             texture(texture0,worldPosition.xz*.38).rgb*weights.y+
             texture(texture0,worldPosition.xy*.38).rgb*weights.z;
    float textureValue=dot(tex,vec3(.24,.66,.1));
    float d=length(worldPosition-eye);
    float memory=0;
    for(int i=0;i<4;++i){if(sonar[i].w>0){
        float pd=length(worldPosition-sonar[i].xyz),age=sonar[i].w-pd;
        memory+=smoothstep(-.25,.25,age)*exp(-max(age,0)*.23)*(1-smoothstep(sonarRange[i]-.5,sonarRange[i],pd));
    }}
    memory=clamp(memory,0,1);
    // Artistic terrain reconstruction; explicit field solve applies to small targets in C++.
    float passive=1/(1+pow(d*.15,2.4));
    float clarity=(passive*.95+memory*.65)*sensoryGain;
    clarity*=1-jam*.42;
    float rim=pow(1-abs(dot(n,normalize(eye-worldPosition))),3.1);
    float engraving=pow(.5+.5*sin(worldPosition.y*11+textureValue*4+sin(worldPosition.x*.8+worldPosition.z*.61)*2.2),18);
    float filaments=pow(.5+.5*sin((worldPosition.x+worldPosition.z*.37)*24+textureValue*33),29);
    float topography=pow(.5+.5*sin(d*8+textureValue*9),20);
    float textureDetail=pow(textureValue,1.5);
    vec3 cold=vec3(.12,.61,.70),pale=vec3(.67,.94,.81);
    float chemistry=clamp((waterConductivity-.004)/.04,0,1);
    cold=mix(cold,vec3(.36,.46,.82),chemistry*.36);
    vec3 electrical=cold*(engraving*.27+filaments*.12+textureDetail*.83+rim*.70);
    electrical+=pale*(topography*.3+rim*.38)*memory;
    vec3 volume=vec3(.034,.125,.135)*(.5+textureValue)*passive;
    float form=.65+.35*abs(dot(n,normalize(vec3(.2,1,.4))));
    vec3 color=(volume+electrical*clarity)*(.65+colDiffuse.rgb*.35)*form;
    color=mix(color,color*vec3(1.8,.56,.36),jam*.22);
    color+=colDiffuse.rgb*emission*(.5+rim);
    color*=exp(-d*.018);
    color+=vec3(.006,.017,.024)*(1-exp(-d*.06));
    float grain=(hash(gl_FragCoord.xy)-.5)*.011;
    finalColor=vec4(pow(max(color,vec3(0)),vec3(.83))+grain,1);
}
