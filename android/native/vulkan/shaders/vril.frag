#version 450
layout(constant_id=0) const bool LIGHTMAPPED=false;
layout(set=0,binding=0,std140) uniform State {
    mat4 mvp; mat4 mv; mat4 tm; vec4 fog; vec4 fogcolor; vec4 params;
} u;
layout(set=1,binding=0) uniform sampler2D base_texture;
layout(set=2,binding=0) uniform sampler2D lightmap;
layout(location=3) in vec2 v_lightmap_uv;
layout(location=0) in vec2 v_uv;
layout(location=1) in vec4 v_color;
layout(location=2) in float v_depth;
layout(location=0) out vec4 out_color;
void main() {
    vec4 c=v_color;
    if(u.params.x!=0) {
        vec4 t=texture(base_texture,v_uv);
        c=u.params.y!=0?t:t*c;
    }
    if(c.a<=u.params.z)discard;
    float f=1;
    if(u.fog.x==1)f=clamp((u.fog.z-v_depth)/max(u.fog.z-u.fog.y,0.0001),0,1);
    else if(u.fog.x==2)f=exp(-u.fog.w*v_depth);
    else if(u.fog.x==3){float d=u.fog.w*v_depth;f=exp(-d*d);}
    if(LIGHTMAPPED) {
        // Fog belongs to the final lit surface, not to the diffuse input.
        // Otherwise the lightmap multiplies the fog color and dark BSP faces
        // appear to have no fog while dynamic/unlightmapped models do.
        // Match the 8-bit diffuse attachment when fog is disabled.
        vec4 base=round(clamp(c,0,1)*255.0)/255.0;
        vec4 lit=2.0*base*texture(lightmap,v_lightmap_uv);
        out_color=vec4(mix(u.fogcolor.rgb,lit.rgb,clamp(f,0,1)),lit.a);
    } else {
        out_color=vec4(mix(u.fogcolor.rgb,c.rgb,clamp(f,0,1)),c.a);
    }
}
