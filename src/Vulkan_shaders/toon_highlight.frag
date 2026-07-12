#version 450
layout(std140,set=0,binding=0) uniform ToonHighlightConfig { vec4 toon[32]; uint dispCnt; uint mode; uint textured; uint pad; } cfg;
vec4 ApplyToonHighlight(vec4 vertexColor, vec4 textureColor) {
 int idx=int(clamp(vertexColor.r,0.0,1.0)*31.0);
 vec4 col=vertexColor;
 if(cfg.mode==1u) col.rgb=cfg.toon[idx].rgb;
 else if(cfg.mode==2u){ col.rgb=vertexColor.rrr; col.rgb=min(col.rgb+cfg.toon[idx].rgb,vec3(1.0)); }
 if(cfg.textured!=0u) col*=textureColor;
 return col;
}
layout(location=0) in vec4 fColor; layout(location=0) out vec4 oColor; void main(){oColor=ApplyToonHighlight(fColor,vec4(1.0));}
