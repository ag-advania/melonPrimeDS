#version 450
layout(std140,set=0,binding=0) uniform Config { vec4 toon[32]; uint dispCnt; uint mode; uint textured; uint pad; } cfg;
layout(location=0) in vec4 fColor;
layout(location=0) out vec4 oColor;
void main(){
 int idx=int(clamp(fColor.r,0.0,1.0)*31.0);
 vec4 col=fColor;
 if(cfg.mode==1u) col.rgb=cfg.toon[idx].rgb;
 else if(cfg.mode==2u){ col.rgb=fColor.rrr; col.rgb=min(col.rgb+cfg.toon[idx].rgb,vec3(1.0)); }
 oColor=col;
}
