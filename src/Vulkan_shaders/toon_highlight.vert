#version 450
layout(location=0) in uvec4 vPosition;
layout(location=1) in uvec4 vColor;
layout(location=0) out vec4 fColor;
void main(){
 vec2 p=(vec2(vPosition.xy)*2.0/vec2(256.0,192.0))-1.0;
 gl_Position=vec4(p, float(vPosition.z)/16777216.0, 1.0);
 fColor=vec4(vColor)/vec4(255.0,255.0,255.0,31.0);
}
