// SPDX-License-Identifier: GPL-2.0-or-later
// Vril's fixed pipeline expressed in GLES 2 shaders. No desktop GL/GL4ES.
// Draws with identical state coalesce; raster/texture/matrix mutations flush.
#define NZP_GLES_IMPLEMENTATION
#include "gles_compat.h"
#include "render_math.h"
#include "vulkan/renderer.h"
#ifndef NZP_VULKAN
#define NZP_VK_Active() 0
#define NZP_VK_Draw(...) ((void)0)
#define NZP_VK_DrawBuffers(...) 0
#define NZP_VK_Texture(...) ((void)0)
#define NZP_VK_TextureFilter(...) ((void)0)
#define NZP_VK_DeleteTexture(...) ((void)0)
#define NZP_VK_Clear(...) ((void)0)
#define NZP_VK_ReadPixels(...) 0
#endif
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>

typedef nzp_vk_vertex nzp_vertex;
typedef struct { const unsigned char *data; int size, stride; GLenum type; int enabled; } nzp_array;
enum { BATCH_CAPACITY=12288, MATRIX_DEPTH=32 };
static int nzp_unpack_alignment=4;
static float nzp_clear_color[4],nzp_clear_depth=1;
#ifdef NZP_VULKAN
static unsigned resource_id=1,vertex_buffer,index_buffer;
#endif
static nzp_vertex batch[BATCH_CAPACITY], current, first_vertex, previous[3];
static int batch_count, primitive_count;
static GLenum primitive, batch_mode=GL_TRIANGLES, matrix_mode=GL_MODELVIEW;
static nzp_array arrays[3];
static float matrices[3][MATRIX_DEPTH][16];
static int matrix_depth[3];
/* Recalculate projection*modelview only when either matrix actually changes.
 * HUD glyph batches frequently reuse both matrices for dozens of flushes. */
static float cached_mvp[16];
static int mvp_valid;
static const float *mvp_matrix(void) {
    if(!mvp_valid) {
        nzp_multiply(cached_mvp,matrices[1][matrix_depth[1]],
                    matrices[0][matrix_depth[0]]);
        mvp_valid=1;
    }
    return cached_mvp;
}
static void mark_matrix_changed(int i) {
    if(i!=2)mvp_valid=0; /* Texture matrix does not affect MVP. */
}
static int textured=1, alpha_test, fog_enabled, replace_mode, flat_shading;
static float alpha_threshold=.666f, fog_start, fog_end=1.f, fog_density=1.f, fog_color[4];
static int fog_mode=GL_LINEAR;
static GLuint program, streams[3], bound_texture;
static int stream_index;
static size_t stream_offset;
enum { STREAM_BYTES=BATCH_CAPACITY*4*sizeof(nzp_vertex) };
static int raster_enabled[4];
static float viewport_xscale=1.f,viewport_yscale=1.f;
static unsigned draw_calls_this_frame;
static GLuint current_program;
static GLuint driver_bound_texture;
static int driver_texture_valid;
static nzp_render_stats_t render_stats;
static int vertex_attribute_enabled[3]={-1,-1,-1};
static void enable_vertex_attribute(int index,int enable) {
    if(vertex_attribute_enabled[index]==enable) {
        ++render_stats.skipped_state_changes;return;
    }
    if(enable)glEnableVertexAttribArray(index);
    else glDisableVertexAttribArray(index);
    vertex_attribute_enabled[index]=enable;
}
static void bind_tex0(GLuint texture) {
    /* The Quake GLES2 backend uses texture unit 0. GPU API calls from
     * presentation/surface creation invalidate this cache explicitly. */
    if(NZP_VK_Active()){driver_bound_texture=texture;driver_texture_valid=1;return;}
    glActiveTexture(GL_TEXTURE0);
    if(!driver_texture_valid || driver_bound_texture!=texture) {
        glBindTexture(GL_TEXTURE_2D,texture);
        driver_bound_texture=texture;
        driver_texture_valid=1;
        ++render_stats.texture_binds;
    } else ++render_stats.skipped_state_changes;
}
/* Direct mapped per-texture cache. Exact values only; deleted textures
 * invalidate entries to protect against GL object-id reuse. */
enum { FILTER_SLOTS=1024 };
static struct {
    GLuint texture;
    GLint min_filter,mag_filter;
    unsigned valid; /* bit 1=min, bit 2=mag */
} filter_cache[FILTER_SLOTS];
/* GLES2 driver state that only our wrapper modifies. */
static struct {
    unsigned valid;
    GLenum blend_src,blend_dst,depth_func,cull_face;
    GLboolean depth_mask;
    float polygon_factor,polygon_units;
    float depth_near,depth_far;
    GLint viewport[4];
} driver_state;
enum {
    DS_BLEND=1,DS_DEPTHFUNC=2,DS_DEPTHMASK=4,DS_CULL=8,
    DS_POLYGON=16,DS_DEPTHRANGE=32,DS_VIEWPORT=64
};
void NZP_GLES_GetFrameStats(nzp_render_stats_t *out) {
    if(out)*out=render_stats;
}
void NZP_GLES_RecordExternalUpload(GLenum target,size_t bytes) {
    /* Called for Vril's static BSP buffers and dynamic world IBO uploads.
     * Streamed immediate vertices are counted by upload() separately. */
    ++render_stats.buffer_uploads;
    if(target==GL_ELEMENT_ARRAY_BUFFER)
        render_stats.index_upload_bytes+=(unsigned)bytes;
    else
        render_stats.vertex_upload_bytes+=(unsigned)bytes;
}
void NZP_GLES_InvalidateTextureState(void) {
    /* Native EGL/FBO code may bind a texture directly; the next wrapped
     * texture bind must restore our desired binding. */
    /* Direct GLES binding must not discard the engine's logical texture. */
    driver_texture_valid=0;
}

// Avoid redundant GLES uniform updates on Mali, especially for frequent HUD/world
// draw calls that reuse the same projection, modelview and texture state.
static struct {
    int valid,textured,replace;
    float mvp[16],mv[16],tm[16],alpha,fog[4],fogcolor[4];
} last_uniforms;
static GLint u_mvp,u_mv,u_tm,u_texture,u_textured,u_replace,u_alpha,u_fog,u_fogcolor;

static void fail(const char *message) { fprintf(stderr,"NZP GLES: %s\n",message); abort(); }
static int matrix_index(void) { return matrix_mode==GL_PROJECTION?1:matrix_mode==GL_TEXTURE?2:0; }
static float *matrix(void) { int i=matrix_index();return matrices[i][matrix_depth[i]]; }
static GLuint shader(GLenum kind,const char *source) {
    GLuint s=glCreateShader(kind); GLint ok; glShaderSource(s,1,&source,NULL);glCompileShader(s);
    glGetShaderiv(s,GL_COMPILE_STATUS,&ok);if(!ok){char log[2048];glGetShaderInfoLog(s,sizeof(log),NULL,log);fail(log);}return s;
}
static nzp_vk_raster vk_raster(void) {
    nzp_vk_raster s={0};
    s.blend=raster_enabled[0];s.depth=raster_enabled[1];s.cull=raster_enabled[2];s.offset=raster_enabled[3];
    s.blend_src=(driver_state.valid&DS_BLEND)?driver_state.blend_src:GL_ONE;
    s.blend_dst=(driver_state.valid&DS_BLEND)?driver_state.blend_dst:GL_ZERO;
    s.depth_func=(driver_state.valid&DS_DEPTHFUNC)?driver_state.depth_func:GL_LESS;
    s.depth_write=(driver_state.valid&DS_DEPTHMASK)?driver_state.depth_mask:1;
    s.cull_face=(driver_state.valid&DS_CULL)?driver_state.cull_face:GL_BACK;
    s.depth_near=(driver_state.valid&DS_DEPTHRANGE)?driver_state.depth_near:0;
    s.depth_far=(driver_state.valid&DS_DEPTHRANGE)?driver_state.depth_far:1;
    s.offset_factor=driver_state.polygon_factor;s.offset_units=driver_state.polygon_units;
    memcpy(s.viewport,driver_state.viewport,sizeof(s.viewport));return s;
}
static nzp_vk_uniform vk_uniform(void) {
    nzp_vk_uniform u={0};memcpy(u.mvp,mvp_matrix(),64);
    memcpy(u.mv,matrices[0][matrix_depth[0]],64);memcpy(u.tm,matrices[2][matrix_depth[2]],64);
    u.fog[0]=fog_enabled?(fog_mode==GL_LINEAR?1.f:fog_mode==GL_EXP?2.f:3.f):0.f;
    u.fog[1]=fog_start;u.fog[2]=fog_end;u.fog[3]=fog_density;memcpy(u.fogcolor,fog_color,16);
    u.params[0]=textured;u.params[1]=replace_mode;u.params[2]=alpha_test?alpha_threshold:-1.f;return u;
}
void NZP_GLES_Init(void) {
    static const char *vs=
        "attribute vec3 a_position;attribute vec2 a_uv;attribute vec4 a_color;"
        "uniform mat4 u_mvp,u_mv,u_tm;varying mediump vec2 v_uv;varying lowp vec4 v_color;varying mediump float v_depth;"
        "void main(){gl_Position=u_mvp*vec4(a_position,1.0);"
        "v_uv=(u_tm*vec4(a_uv,0.0,1.0)).xy;v_color=a_color;v_depth=abs((u_mv*vec4(a_position,1.0)).z);}";
    static const char *fs=
        "precision mediump float;uniform sampler2D u_texture;uniform int u_textured,u_replace;"
        "uniform float u_alpha;uniform vec4 u_fog,u_fogcolor;"
        "varying mediump vec2 v_uv;varying lowp vec4 v_color;varying mediump float v_depth;"
        "void main(){vec4 c=v_color;if(u_textured!=0){vec4 t=texture2D(u_texture,v_uv);c=u_replace!=0?t:t*c;}"
        "if(c.a<=u_alpha)discard;float f=1.0;"
        "if(u_fog.x==1.0)f=clamp((u_fog.z-v_depth)/max(u_fog.z-u_fog.y,0.0001),0.0,1.0);"
        "else if(u_fog.x==2.0)f=exp(-u_fog.w*v_depth);"
        "else if(u_fog.x==3.0){float d=u_fog.w*v_depth;f=exp(-d*d);}"
        "gl_FragColor=vec4(mix(u_fogcolor.rgb,c.rgb,clamp(f,0.0,1.0)),c.a);}";
    if(NZP_VK_Active()) {
        memset(&driver_state,0,sizeof(driver_state));
        memset(&render_stats,0,sizeof(render_stats));memset(raster_enabled,0,sizeof(raster_enabled));
        memset(matrix_depth,0,sizeof(matrix_depth));mvp_valid=0;batch_count=0;
        for(int i=0;i<3;i++)nzp_identity(matrices[i][0]);
        memset(current.color,255,4);return;
    }
    GLuint v=shader(GL_VERTEX_SHADER,vs),f=shader(GL_FRAGMENT_SHADER,fs);GLint ok;
    program=glCreateProgram();glAttachShader(program,v);glAttachShader(program,f);
    glBindAttribLocation(program,0,"a_position");glBindAttribLocation(program,1,"a_uv");glBindAttribLocation(program,2,"a_color");
    glLinkProgram(program);glGetProgramiv(program,GL_LINK_STATUS,&ok);
    if(!ok){char log[2048];glGetProgramInfoLog(program,sizeof(log),NULL,log);fail(log);}
    glDeleteShader(v);glDeleteShader(f);
#define UNIFORM(name) name=glGetUniformLocation(program,#name)
    UNIFORM(u_mvp);UNIFORM(u_mv);UNIFORM(u_tm);UNIFORM(u_texture);UNIFORM(u_textured);
    UNIFORM(u_replace);UNIFORM(u_alpha);UNIFORM(u_fog);UNIFORM(u_fogcolor);
#undef UNIFORM
    memset(&last_uniforms,0,sizeof(last_uniforms));
    memset(&render_stats,0,sizeof(render_stats));
    memset(&driver_state,0,sizeof(driver_state));
    memset(filter_cache,0,sizeof(filter_cache));
    current_program=0;
    driver_texture_valid=0;
    mvp_valid=0;
    for(int i=0;i<3;i++)vertex_attribute_enabled[i]=-1;
    glGenBuffers(3,streams);
    glBindBuffer(GL_ARRAY_BUFFER,streams[0]);glBufferData(GL_ARRAY_BUFFER,STREAM_BYTES,NULL,GL_STREAM_DRAW);
    for(int i=0;i<3;i++)nzp_identity(matrices[i][0]);
    memset(current.color,255,sizeof(current.color));
}
static void uniforms(const float *mvp,const float *mv,const float *tm,int tex,int replace,float alpha,float fog) {
    const float fog_params[4]={fog,fog_start,fog_end,fog_density};
    if(current_program!=program) {
        glUseProgram(program);
        ++render_stats.shader_switches;
        current_program=program;
    }
    if(!last_uniforms.valid||memcmp(last_uniforms.mvp,mvp,16*sizeof(float))) {
        glUniformMatrix4fv(u_mvp,1,GL_FALSE,mvp); ++render_stats.uniform_updates;
        memcpy(last_uniforms.mvp,mvp,16*sizeof(float));
    }
    if(!last_uniforms.valid||memcmp(last_uniforms.mv,mv,16*sizeof(float))) {
        glUniformMatrix4fv(u_mv,1,GL_FALSE,mv); ++render_stats.uniform_updates;
        memcpy(last_uniforms.mv,mv,16*sizeof(float));
    }
    if(!last_uniforms.valid||memcmp(last_uniforms.tm,tm,16*sizeof(float))) {
        glUniformMatrix4fv(u_tm,1,GL_FALSE,tm); ++render_stats.uniform_updates;
        memcpy(last_uniforms.tm,tm,16*sizeof(float));
    }
    if(!last_uniforms.valid||last_uniforms.textured!=tex) {
        glUniform1i(u_textured,tex); ++render_stats.uniform_updates;last_uniforms.textured=tex;
    }
    if(!last_uniforms.valid||last_uniforms.replace!=replace) {
        glUniform1i(u_replace,replace); ++render_stats.uniform_updates;last_uniforms.replace=replace;
    }
    if(!last_uniforms.valid||last_uniforms.alpha!=alpha) {
        glUniform1f(u_alpha,alpha); ++render_stats.uniform_updates;last_uniforms.alpha=alpha;
    }
    if(!last_uniforms.valid||memcmp(last_uniforms.fog,fog_params,sizeof(fog_params))) {
        glUniform4fv(u_fog,1,fog_params); ++render_stats.uniform_updates;memcpy(last_uniforms.fog,fog_params,sizeof(fog_params));
    }
    if(!last_uniforms.valid||memcmp(last_uniforms.fogcolor,fog_color,sizeof(fog_color))) {
        glUniform4fv(u_fogcolor,1,fog_color); ++render_stats.uniform_updates;memcpy(last_uniforms.fogcolor,fog_color,sizeof(fog_color));
    }
    if(!last_uniforms.valid)glUniform1i(u_texture,0); ++render_stats.uniform_updates;
    last_uniforms.valid=1;
}
static void upload(const nzp_vertex *vertices,int count,GLenum mode) {
    glBindBuffer(GL_ARRAY_BUFFER,streams[stream_index]);glBindBuffer(GL_ELEMENT_ARRAY_BUFFER,0);
    size_t bytes=count*sizeof(nzp_vertex);
    if(stream_offset+bytes>STREAM_BYTES){glBufferData(GL_ARRAY_BUFFER,STREAM_BYTES,NULL,GL_STREAM_DRAW);stream_offset=0;}
    glBufferSubData(GL_ARRAY_BUFFER,stream_offset,bytes,vertices);
    ++render_stats.buffer_uploads;
    render_stats.vertex_upload_bytes+=(unsigned)bytes;
    enable_vertex_attribute(0,1);
    enable_vertex_attribute(1,1);
    enable_vertex_attribute(2,1);
    glVertexAttribPointer(0,3,GL_FLOAT,GL_FALSE,sizeof(nzp_vertex),(void*)(stream_offset+offsetof(nzp_vertex,xyz)));
    glVertexAttribPointer(1,2,GL_FLOAT,GL_FALSE,sizeof(nzp_vertex),(void*)(stream_offset+offsetof(nzp_vertex,uv)));
    glVertexAttribPointer(2,4,GL_UNSIGNED_BYTE,GL_TRUE,sizeof(nzp_vertex),(void*)(stream_offset+offsetof(nzp_vertex,color)));
    glDrawArrays(mode,0,count);
    ++draw_calls_this_frame;
    ++render_stats.draw_calls;
    stream_offset+=bytes;
}
void NZP_GLES_BeginFrame(void){
    NZP_GLES_Flush();
    draw_calls_this_frame=0;
    memset(&render_stats,0,sizeof(render_stats));stream_index=(stream_index+1)%3;stream_offset=0;
    if(NZP_VK_Active())return;
    glBindBuffer(GL_ARRAY_BUFFER,streams[stream_index]);
    // One orphan per frame, instead of allocating a new store per HUD glyph batch.
    glBufferData(GL_ARRAY_BUFFER,STREAM_BYTES,NULL,GL_STREAM_DRAW);
}
void NZP_GLES_Flush(void) {
    if(!batch_count)return;
    ++render_stats.nonempty_flushes;
    if(NZP_VK_Active()) {
        nzp_vk_uniform u=vk_uniform();nzp_vk_raster s=vk_raster();
        NZP_VK_Draw(batch,batch_count,batch_mode,&u,&s,bound_texture);
        ++draw_calls_this_frame;++render_stats.draw_calls;
        render_stats.vertex_upload_bytes+=batch_count*sizeof(nzp_vertex);
        batch_count=0;return;
    }
    uniforms(mvp_matrix(),matrices[0][matrix_depth[0]],matrices[2][matrix_depth[2]],textured,replace_mode,
        alpha_test?alpha_threshold:-1.f,fog_enabled?(fog_mode==GL_LINEAR?1.f:fog_mode==GL_EXP?2.f:3.f):0.f);
    bind_tex0(bound_texture);upload(batch,batch_count,batch_mode);
    batch_count=0;
}
void NZP_GLES_Shutdown(void) { NZP_GLES_Flush();if(NZP_VK_Active())return;last_uniforms.valid=0;current_program=0;
    memset(filter_cache,0,sizeof(filter_cache));glDeleteBuffers(3,streams);glDeleteProgram(program);program=0;memset(streams,0,sizeof(streams)); }
static void reserve(int count,GLenum mode) {
    if(batch_mode!=mode||batch_count+count>BATCH_CAPACITY)NZP_GLES_Flush();batch_mode=mode;
}
static void triangle(nzp_vertex a,nzp_vertex b,nzp_vertex c,const unsigned char *color) {
    reserve(3,GL_TRIANGLES);
    if(flat_shading){memcpy(a.color,color,4);memcpy(b.color,color,4);memcpy(c.color,color,4);}
    batch[batch_count++]=a;batch[batch_count++]=b;batch[batch_count++]=c;
}
void NZP_GLES_AppendUIQuad(float x0,float y0,float x1,float y1,
                            float u0,float v0,float u1,float v1,
                            unsigned char r,unsigned char g,
                            unsigned char b,unsigned char a) {
    /* Same corner order and GL_TRIANGLES semantics as Hyena_Draw2DQuads,
     * but no RGB->float->RGB conversion and no 3 separate staging arrays. */
    nzp_vertex v[4]={
        {{x0,y0,0},{u0,v0},{r,g,b,a}},
        {{x1,y0,0},{u1,v0},{r,g,b,a}},
        {{x1,y1,0},{u1,v1},{r,g,b,a}},
        {{x0,y1,0},{u0,v1},{r,g,b,a}}
    };
    triangle(v[0],v[1],v[2],v[2].color);
    triangle(v[0],v[2],v[3],v[3].color);
    current=v[3];
}
void nzp_glBegin(GLenum mode) { primitive=mode;primitive_count=0; }
void nzp_glEnd(void) {}
void nzp_glVertex3f(float x,float y,float z) {
    current.xyz[0]=x;current.xyz[1]=y;current.xyz[2]=z;int n=primitive_count++;
    if(n==0)first_vertex=current;
    if(primitive==GL_TRIANGLES) {
        previous[n%3]=current;if(n%3==2)triangle(previous[0],previous[1],current,current.color);
    } else if(primitive==GL_QUADS) {
        if(n%4<3)previous[n%4]=current;
        else {triangle(previous[0],previous[1],previous[2],current.color);triangle(previous[0],previous[2],current,current.color);}
    } else if(primitive==GL_TRIANGLE_FAN||primitive==GL_POLYGON) {
        if(n>=2)triangle(first_vertex,previous[0],current,current.color);previous[0]=current;
    } else if(primitive==GL_TRIANGLE_STRIP) {
        if(n>=2) {if(n&1)triangle(previous[1],previous[0],current,current.color);else triangle(previous[0],previous[1],current,current.color);}
        previous[0]=previous[1];previous[1]=current;
    } else if(primitive==GL_LINES||primitive==GL_LINE_STRIP) {
        if(n>0&&(primitive==GL_LINE_STRIP||(n&1))){reserve(2,GL_LINES);batch[batch_count++]=previous[0];batch[batch_count++]=current;}previous[0]=current;
    } else if(primitive==GL_POINTS) {reserve(1,GL_POINTS);batch[batch_count++]=current;}
    else fail("unsupported primitive");
}
void nzp_glVertex3fv(const float *v){nzp_glVertex3f(v[0],v[1],v[2]);}
void nzp_glTexCoord2f(float s,float t){current.uv[0]=s;current.uv[1]=t;}
void nzp_glTexCoord2fv(const float *v){nzp_glTexCoord2f(v[0],v[1]);}
static unsigned char color_byte(float v){return (unsigned char)(fminf(1.f,fmaxf(0.f,v))*255.f+.5f);}
void nzp_glColor4f(float r,float g,float b,float a){current.color[0]=color_byte(r);current.color[1]=color_byte(g);current.color[2]=color_byte(b);current.color[3]=color_byte(a);}
void nzp_glColor3f(float r,float g,float b){nzp_glColor4f(r,g,b,1);}
void nzp_glColor4fv(const float *v){nzp_glColor4f(v[0],v[1],v[2],v[3]);}
void nzp_glMatrixMode(GLenum mode){matrix_mode=mode;}
void nzp_glLoadIdentity(void){
    float identity[16];nzp_identity(identity);
    if(!memcmp(matrix(),identity,sizeof(identity))) {
        ++render_stats.skipped_state_changes; return;
    }
    NZP_GLES_Flush();memcpy(matrix(),identity,sizeof(identity));
    mark_matrix_changed(matrix_index());
}
void nzp_glLoadMatrixf(const float *m){
    if(!memcmp(matrix(),m,16*sizeof(float))) {
        ++render_stats.skipped_state_changes;return;
    }
    NZP_GLES_Flush();memcpy(matrix(),m,16*sizeof(float));
    mark_matrix_changed(matrix_index());
}
void nzp_glPushMatrix(void){int i=matrix_index();if(matrix_depth[i]+1>=MATRIX_DEPTH)fail("matrix stack overflow");memcpy(matrices[i][matrix_depth[i]+1],matrix(),16*sizeof(float));matrix_depth[i]++;}
void nzp_glPopMatrix(void){
    int i=matrix_index();
    if(!matrix_depth[i])fail("matrix stack underflow");
    if(memcmp(matrices[i][matrix_depth[i]],matrices[i][matrix_depth[i]-1],
              16*sizeof(float))) NZP_GLES_Flush();
    else ++render_stats.skipped_state_changes;
    matrix_depth[i]--;
    mark_matrix_changed(i);
}
static void multiply(const float *m){
    float identity[16];nzp_identity(identity);
    if(!memcmp(m,identity,sizeof(identity))) {
        ++render_stats.skipped_state_changes;return;
    }
    NZP_GLES_Flush();nzp_multiply(matrix(),matrix(),m);
    mark_matrix_changed(matrix_index());
}
void nzp_glTranslatef(float x,float y,float z){float m[16];nzp_identity(m);m[12]=x;m[13]=y;m[14]=z;multiply(m);}
void nzp_glScalef(float x,float y,float z){float m[16];nzp_identity(m);m[0]=x;m[5]=y;m[10]=z;multiply(m);}
void nzp_glRotatef(float angle,float x,float y,float z){
    float length=sqrtf(x*x+y*y+z*z);if(length==0)return;x/=length;y/=length;z/=length;
    float a=angle*.0174532925199433f,c=cosf(a),s=sinf(a),t=1-c,m[16];nzp_identity(m);
    m[0]=t*x*x+c;m[4]=t*x*y-s*z;m[8]=t*x*z+s*y;
    m[1]=t*x*y+s*z;m[5]=t*y*y+c;m[9]=t*y*z-s*x;
    m[2]=t*x*z-s*y;m[6]=t*y*z+s*x;m[10]=t*z*z+c;multiply(m);
}
void nzp_glOrtho(double l,double r,double b,double t,double n,double f){float m[16];nzp_identity(m);m[0]=2/(r-l);m[5]=2/(t-b);m[10]=-2/(f-n);m[12]=-(r+l)/(r-l);m[13]=-(t+b)/(t-b);m[14]=-(f+n)/(f-n);multiply(m);}
void nzp_glFrustum(double l,double r,double b,double t,double n,double f){float m[16]={0};m[0]=2*n/(r-l);m[5]=2*n/(t-b);m[8]=(r+l)/(r-l);m[9]=(t+b)/(t-b);m[10]=-(f+n)/(f-n);m[11]=-1;m[14]=-2*f*n/(f-n);multiply(m);}
void nzp_glGetFloatv(GLenum name,float *v){int i=name==GL_PROJECTION_MATRIX?1:name==GL_TEXTURE_MATRIX?2:0;memcpy(v,matrices[i][matrix_depth[i]],16*sizeof(float));}
static void enable(GLenum cap,int value){int *p=cap==GL_TEXTURE_2D?&textured:cap==GL_ALPHA_TEST?&alpha_test:cap==GL_FOG?&fog_enabled:NULL;
    if(p){if(*p!=value){NZP_GLES_Flush();*p=value;}}else {
        int i=cap==GL_BLEND?0:cap==GL_DEPTH_TEST?1:cap==GL_CULL_FACE?2:cap==GL_POLYGON_OFFSET_FILL?3:-1;
        if(i<0||raster_enabled[i]!=value){NZP_GLES_Flush();if(i>=0)raster_enabled[i]=value;if(!NZP_VK_Active()){if(value)glEnable(cap);else glDisable(cap);}}
    }}
void nzp_glEnable(GLenum cap){enable(cap,1);}void nzp_glDisable(GLenum cap){enable(cap,0);}
void nzp_glAlphaFunc(GLenum f,float threshold){if(f!=GL_GREATER)fail("unsupported alpha test");if(alpha_threshold!=threshold){NZP_GLES_Flush();alpha_threshold=threshold;}}
void nzp_glTexEnvf(GLenum t,GLenum n,float v){(void)t;(void)n;int replace=(int)v==GL_REPLACE;if(replace!=replace_mode){NZP_GLES_Flush();replace_mode=replace;}}
void nzp_glShadeModel(GLenum mode){flat_shading=mode==GL_FLAT;}
void nzp_glFogf(GLenum n,float v){
    float *target=n==GL_FOG_START?&fog_start:
        n==GL_FOG_END?&fog_end:n==GL_FOG_DENSITY?&fog_density:NULL;
    if(target) {
        if(*target==v) {++render_stats.skipped_state_changes;return;}
        NZP_GLES_Flush();*target=v;return;
    }
    if(n==GL_FOG_MODE && fog_mode!=(int)v) {
        NZP_GLES_Flush();fog_mode=(int)v;
    }
}
void nzp_glFogi(GLenum n,GLint v){nzp_glFogf(n,(float)v);}
void nzp_glFogfv(GLenum n,const float *v){
    if(n==GL_FOG_COLOR){
        if(!memcmp(fog_color,v,4*sizeof(float))) {
            ++render_stats.skipped_state_changes;return;
        }
        NZP_GLES_Flush();memcpy(fog_color,v,4*sizeof(float));
    } else nzp_glFogf(n,*v);
}
static int array_index(GLenum a){return a==GL_VERTEX_ARRAY?0:a==GL_TEXTURE_COORD_ARRAY?1:2;}
void nzp_glEnableClientState(GLenum a){arrays[array_index(a)].enabled=1;}
void nzp_glDisableClientState(GLenum a){arrays[array_index(a)].enabled=0;}
static int scalar_size(GLenum type){return type==GL_FLOAT?4:type==GL_SHORT?2:1;}
static void pointer(int i,int size,GLenum type,int stride,const void *p){arrays[i].size=size;arrays[i].type=type;arrays[i].stride=stride?stride:size*scalar_size(type);arrays[i].data=p;}
void nzp_glVertexPointer(int n,GLenum t,int s,const void *p){pointer(0,n,t,s,p);}
void nzp_glTexCoordPointer(int n,GLenum t,int s,const void *p){pointer(1,n,t,s,p);}
void nzp_glColorPointer(int n,GLenum t,int s,const void *p){pointer(2,n,t,s,p);}
static float component(int array,int vertex,int c){nzp_array *a=&arrays[array];const unsigned char *p=a->data+(size_t)vertex*a->stride+c*scalar_size(a->type);float f;short s;
    if(a->type==GL_FLOAT){memcpy(&f,p,4);return f;}if(a->type==GL_SHORT){memcpy(&s,p,2);return s;}return array==2?*p/255.f:*p;}
static void array_vertex(int i){if(arrays[1].enabled)nzp_glTexCoord2f(component(1,i,0),component(1,i,1));
    if(arrays[2].enabled)nzp_glColor4f(component(2,i,0),component(2,i,1),component(2,i,2),arrays[2].size==4?component(2,i,3):1.f);
    nzp_glVertex3f(component(0,i,0),component(0,i,1),arrays[0].size>2?component(0,i,2):0.f);}
void nzp_glDrawArrays(GLenum mode,int first,int count){nzp_glBegin(mode);for(int i=0;i<count;i++)array_vertex(first+i);nzp_glEnd();}
void nzp_glDrawElements(GLenum mode,int count,GLenum type,const void *indices){nzp_glBegin(mode);for(int i=0;i<count;i++){unsigned index;
    if(type==GL_UNSIGNED_SHORT){unsigned short s;memcpy(&s,(const unsigned char*)indices+2*i,2);index=s;}else if(type==GL_UNSIGNED_INT){memcpy(&index,(const unsigned char*)indices+4*i,4);}else index=((const unsigned char*)indices)[i];array_vertex(index);}nzp_glEnd();}

void nzp_glBindTexture(GLenum t,GLuint texture){
    if(bound_texture!=texture) {
        NZP_GLES_Flush();bound_texture=texture;
        bind_tex0(texture);
    } else if(!driver_texture_valid || driver_bound_texture!=texture)
        bind_tex0(texture);
    else ++render_stats.skipped_state_changes;
    (void)t;
}
void nzp_glTexParameterf(GLenum t,GLenum n,float v){
    if(t==GL_TEXTURE_2D && bound_texture &&
       (n==GL_TEXTURE_MIN_FILTER || n==GL_TEXTURE_MAG_FILTER)) {
        unsigned slot=bound_texture&(FILTER_SLOTS-1);
        unsigned mask=n==GL_TEXTURE_MIN_FILTER?1u:2u;
        GLint value=(GLint)v;
        if(filter_cache[slot].texture==bound_texture &&
           (filter_cache[slot].valid&mask) &&
           (mask==1u?filter_cache[slot].min_filter:
                      filter_cache[slot].mag_filter)==value) {
            ++render_stats.skipped_state_changes;
            return;
        }
        NZP_GLES_Flush();
        if(NZP_VK_Active())NZP_VK_TextureFilter(bound_texture,n,(int)v);
        else glTexParameterf(t,n,v);
        ++render_stats.texture_parameter_changes;
        if(filter_cache[slot].texture!=bound_texture) {
            filter_cache[slot].texture=bound_texture;
            filter_cache[slot].valid=0;
        }
        if(mask==1u)filter_cache[slot].min_filter=value;
        else filter_cache[slot].mag_filter=value;
        filter_cache[slot].valid|=mask;
        return;
    }
    NZP_GLES_Flush();if(NZP_VK_Active())NZP_VK_TextureFilter(bound_texture,n,(int)v);else glTexParameterf(t,n,v);
    ++render_stats.texture_parameter_changes;
}
void nzp_glTexImage2D(GLenum t,int l,int in,int w,int h,int b,GLenum f,GLenum type,const void *data){(void)in;NZP_GLES_Flush();if(NZP_VK_Active())NZP_VK_Texture(bound_texture,l,0,0,w,h,f,type,data,1,nzp_unpack_alignment);else glTexImage2D(t,l,f,w,h,b,f,type,data);}
void nzp_glTexSubImage2D(GLenum t,int l,int x,int y,int w,int h,GLenum f,GLenum type,const void *data){NZP_GLES_Flush();if(NZP_VK_Active())NZP_VK_Texture(bound_texture,l,x,y,w,h,f,type,data,0,nzp_unpack_alignment);else glTexSubImage2D(t,l,x,y,w,h,f,type,data);}
void nzp_glDeleteTextures(int n,const GLuint *textures){
    NZP_GLES_Flush();
    for(int i=0;i<n;i++) {
        GLuint id=textures[i];
        if(id==bound_texture)bound_texture=0;
        unsigned slot=id&(FILTER_SLOTS-1);
        if(filter_cache[slot].texture==id)
            filter_cache[slot].valid=0;
    }
    if(NZP_VK_Active()){for(int i=0;i<n;i++)NZP_VK_DeleteTexture(textures[i]);}else glDeleteTextures(n,textures);
    driver_texture_valid=0;
}
/* Only flush if a *real* GLES state mutation would affect queued draws. */
void nzp_glBlendFunc(GLenum s,GLenum d){
    if((driver_state.valid&DS_BLEND)&&driver_state.blend_src==s &&
       driver_state.blend_dst==d){++render_stats.skipped_state_changes;return;}
    NZP_GLES_Flush();if(!NZP_VK_Active())glBlendFunc(s,d);
    driver_state.blend_src=s;driver_state.blend_dst=d;driver_state.valid|=DS_BLEND;
}
void nzp_glDepthFunc(GLenum f){
    if((driver_state.valid&DS_DEPTHFUNC)&&driver_state.depth_func==f)
       {++render_stats.skipped_state_changes;return;}
    NZP_GLES_Flush();if(!NZP_VK_Active())glDepthFunc(f);
    driver_state.depth_func=f;driver_state.valid|=DS_DEPTHFUNC;
}
void nzp_glDepthMask(GLboolean e){
    if((driver_state.valid&DS_DEPTHMASK)&&driver_state.depth_mask==e)
       {++render_stats.skipped_state_changes;return;}
    NZP_GLES_Flush();if(!NZP_VK_Active())glDepthMask(e);
    driver_state.depth_mask=e;driver_state.valid|=DS_DEPTHMASK;
}
void nzp_glCullFace(GLenum f){
    if((driver_state.valid&DS_CULL)&&driver_state.cull_face==f)
       {++render_stats.skipped_state_changes;return;}
    NZP_GLES_Flush();if(!NZP_VK_Active())glCullFace(f);
    driver_state.cull_face=f;driver_state.valid|=DS_CULL;
}
void nzp_glPolygonOffset(float f,float u){
    if((driver_state.valid&DS_POLYGON)&&driver_state.polygon_factor==f &&
       driver_state.polygon_units==u){++render_stats.skipped_state_changes;return;}
    NZP_GLES_Flush();if(!NZP_VK_Active())glPolygonOffset(f,u);
    driver_state.polygon_factor=f;driver_state.polygon_units=u;
    driver_state.valid|=DS_POLYGON;
}
/* Render only the 3D world at the user's optional resolution percentage.
 * Logical viewport/refdef remains surface-native for a sharp HUD. */
void NZP_GLES_SetWorldViewport(int ww,int wh,int sw,int sh) {
    driver_state.valid&=~DS_VIEWPORT;
    viewport_xscale=(float)ww/(float)(sw>0?sw:1);
    viewport_yscale=(float)wh/(float)(sh>0?sh:1);
}
void NZP_GLES_SetNativeViewport(void) {
    driver_state.valid&=~DS_VIEWPORT;
    viewport_xscale=viewport_yscale=1.f;
}
void nzp_glViewport(int x,int y,int w,int h) {
    GLint rect[4]={
        (GLint)lroundf(x*viewport_xscale),
        (GLint)lroundf(y*viewport_yscale),
        (GLint)lroundf(w*viewport_xscale),
        (GLint)lroundf(h*viewport_yscale)
    };
    if((driver_state.valid&DS_VIEWPORT) &&
       !memcmp(rect,driver_state.viewport,sizeof(rect))) {
        ++render_stats.skipped_state_changes;return;
    }
    NZP_GLES_Flush();
    if(!NZP_VK_Active())glViewport(rect[0],rect[1],rect[2],rect[3]);
    memcpy(driver_state.viewport,rect,sizeof(rect));
    driver_state.valid|=DS_VIEWPORT;
}
unsigned NZP_GLES_DrawCalls(void) { return draw_calls_this_frame; }
void nzp_glClear(GLbitfield mask){NZP_GLES_Flush();if(NZP_VK_Active())NZP_VK_Clear(mask,nzp_clear_color,nzp_clear_depth);else glClear(mask);}
void nzp_glReadPixels(int x,int y,int w,int h,GLenum f,GLenum t,void *p){NZP_GLES_Flush();
    if(NZP_VK_Active()) {if(f!=GL_RGBA||t!=GL_UNSIGNED_BYTE||!NZP_VK_ReadPixels(x,y,w,h,p))fail("Vulkan readback unavailable");}
    else if(f==GL_RGB && t==GL_UNSIGNED_BYTE){
        unsigned char *rgba=malloc((size_t)w*h*4);if(!rgba)fail("screenshot allocation failed");
        GLint alignment=4;glGetIntegerv(GL_PACK_ALIGNMENT,&alignment);
        glReadPixels(x,y,w,h,GL_RGBA,t,rgba);
        size_t stride=((size_t)w*3+alignment-1)/alignment*alignment;
        for(int j=0;j<h;j++)for(int i=0;i<w;i++)memcpy((unsigned char*)p+j*stride+i*3,rgba+((size_t)j*w+i)*4,3);
        free(rgba);
    } else glReadPixels(x,y,w,h,f,t,p);
}
void nzp_glFinish(void){NZP_GLES_Flush();if(!NZP_VK_Active())glFinish();}

void nzp_glDepthRange(double n,double f){
    if((driver_state.valid&DS_DEPTHRANGE)&&driver_state.depth_near==(float)n &&
       driver_state.depth_far==(float)f) {
        ++render_stats.skipped_state_changes;return;
    }
    NZP_GLES_Flush();if(!NZP_VK_Active())glDepthRangef((float)n,(float)f);
    driver_state.depth_near=(float)n;driver_state.depth_far=(float)f;
    driver_state.valid|=DS_DEPTHRANGE;
}
void nzp_glHint(GLenum target,GLenum mode){(void)target;(void)mode;}
void nzp_glPolygonMode(GLenum face,GLenum mode){(void)face;(void)mode;} // wireframe debug only
void nzp_glReadBuffer(GLenum b){(void)b;}void nzp_glDrawBuffer(GLenum b){(void)b;}

int NZP_GLES_DrawGPU(GLenum mode,const nzp_gpu_array *a,GLuint index_buffer,size_t offset,int count){
    if(!a[0].buffer||mode==GL_QUADS||mode==GL_POLYGON||(flat_shading&&a[2].buffer))return 0;
    NZP_GLES_Flush();
    if(NZP_VK_Active()) {
        nzp_vk_array va[3];for(int i=0;i<3;i++){
            va[i]=(nzp_vk_array){a[i].buffer,(size_t)a[i].offset,a[i].size,a[i].type,a[i].stride};
        }
        nzp_vk_uniform u=vk_uniform();nzp_vk_raster s=vk_raster();
        int result=NZP_VK_DrawBuffers(va,index_buffer,offset,count,mode,&u,&s,bound_texture,current.uv,current.color);
        if(result){++draw_calls_this_frame;++render_stats.draw_calls;}return result;
    }
    uniforms(mvp_matrix(),matrices[0][matrix_depth[0]],matrices[2][matrix_depth[2]],textured,replace_mode,
        alpha_test?alpha_threshold:-1.f,fog_enabled?(fog_mode==GL_LINEAR?1.f:fog_mode==GL_EXP?2.f:3.f):0.f);
    bind_tex0(bound_texture);
    for(int i=0;i<3;i++){
        if(a[i].buffer){glBindBuffer(GL_ARRAY_BUFFER,a[i].buffer);enable_vertex_attribute(i,1);
            glVertexAttribPointer(i,a[i].size,a[i].type,i==2?GL_TRUE:GL_FALSE,a[i].stride,a[i].offset);
        }else{enable_vertex_attribute(i,0);if(i==1)glVertexAttrib2f(i,current.uv[0],current.uv[1]);
            else if(i==2)glVertexAttrib4f(i,current.color[0]/255.f,current.color[1]/255.f,current.color[2]/255.f,current.color[3]/255.f);}
    }
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER,index_buffer);
    if(index_buffer)glDrawElements(mode,count,GL_UNSIGNED_SHORT,(const void*)offset);else glDrawArrays(mode,0,count);
    ++draw_calls_this_frame;
    ++render_stats.draw_calls;
    glBindBuffer(GL_ARRAY_BUFFER,0);glBindBuffer(GL_ELEMENT_ARRAY_BUFFER,0);return 1;
}

void NZP_GLES_Present(GLuint texture,int width,int height){
    NZP_GLES_Flush();
    /* These values are written only through nzp_glEnable/Disable.
     * Avoid four glIsEnabled driver queries (potential CPU/GPU sync). */
    GLboolean depth=raster_enabled[1],blend=raster_enabled[0],
              cull=raster_enabled[2],offset=raster_enabled[3];
    float identity[16];nzp_identity(identity);
    const nzp_vertex quad[]={{{-1,-1,0},{0,0},{255,255,255,255}},{{1,-1,0},{1,0},{255,255,255,255}},{{1,1,0},{1,1},{255,255,255,255}},
        {{-1,-1,0},{0,0},{255,255,255,255}},{{1,1,0},{1,1},{255,255,255,255}},{{-1,1,0},{0,1},{255,255,255,255}}};
    glViewport(0,0,width,height);
    driver_state.valid&=~DS_VIEWPORT;
    if(depth)glDisable(GL_DEPTH_TEST);
    if(blend)glDisable(GL_BLEND);
    if(cull)glDisable(GL_CULL_FACE);
    if(offset)glDisable(GL_POLYGON_OFFSET_FILL);
    uniforms(identity,identity,identity,1,1,-1,0);
    bind_tex0(texture);upload(quad,6,GL_TRIANGLES);
    bind_tex0(bound_texture);
    if(depth)glEnable(GL_DEPTH_TEST);if(blend)glEnable(GL_BLEND);if(cull)glEnable(GL_CULL_FACE);if(offset)glEnable(GL_POLYGON_OFFSET_FILL);
}

#ifdef NZP_VULKAN
void nzp_glGenTextures(int n,GLuint *ids){if(NZP_VK_Active()){while(n--)*ids++=resource_id++;}else glGenTextures(n,ids);}
void nzp_glGenBuffers(int n,GLuint *ids){if(NZP_VK_Active()){while(n--)*ids++=resource_id++;}else glGenBuffers(n,ids);}
void nzp_glDeleteBuffers(int n,const GLuint *ids){NZP_GLES_Flush();if(NZP_VK_Active()){for(int i=0;i<n;i++)NZP_VK_DeleteBuffer(ids[i]);}else glDeleteBuffers(n,ids);}
void nzp_glBindBuffer(GLenum target,GLuint id){if(NZP_VK_Active()){if(target==GL_ARRAY_BUFFER)vertex_buffer=id;else index_buffer=id;}else glBindBuffer(target,id);}
void nzp_glBufferData(GLenum target,GLsizeiptr bytes,const void *data,GLenum usage){
    if(NZP_VK_Active()){NZP_GLES_Flush();NZP_VK_Buffer(target==GL_ARRAY_BUFFER?vertex_buffer:index_buffer,bytes,data,usage==GL_STATIC_DRAW);}
    else glBufferData(target,bytes,data,usage);
}
void nzp_glPixelStorei(GLenum name,GLint value){if(name==GL_UNPACK_ALIGNMENT)nzp_unpack_alignment=value;if(!NZP_VK_Active())glPixelStorei(name,value);}
void nzp_glGetIntegerv(GLenum name,GLint *value){
    if(NZP_VK_Active()){if(name==GL_UNPACK_ALIGNMENT)*value=nzp_unpack_alignment;else if(name==GL_MAX_TEXTURE_SIZE)*value=NZP_VK_MaxTextureSize();else fail("unsupported Vulkan integer query");}
    else glGetIntegerv(name,value);
}
void nzp_glClearColor(float r,float g,float b,float a){nzp_clear_color[0]=r;nzp_clear_color[1]=g;nzp_clear_color[2]=b;nzp_clear_color[3]=a;if(!NZP_VK_Active())glClearColor(r,g,b,a);}
void nzp_glClearDepthf(float d){nzp_clear_depth=d;if(!NZP_VK_Active())glClearDepthf(d);}
#endif

int NZP_Render_DrawLightmappedGPU(const nzp_gpu_array *a,const nzp_gpu_array *lm,GLuint ib,size_t offset,int count,GLuint lmtex){
#ifdef NZP_VULKAN
    if(!NZP_VK_Active())return 0;
    NZP_GLES_Flush();nzp_vk_array va[3],vlm={lm->buffer,(size_t)lm->offset,lm->size,lm->type,lm->stride};
    for(int i=0;i<3;i++)va[i]=(nzp_vk_array){a[i].buffer,(size_t)a[i].offset,a[i].size,a[i].type,a[i].stride};
    nzp_vk_uniform u=vk_uniform();nzp_vk_raster s=vk_raster();
    int ok=NZP_VK_DrawLightmappedBuffers(va,&vlm,ib,offset,count,&u,&s,bound_texture,lmtex,current.uv,current.color);
    if(ok){++draw_calls_this_frame;++render_stats.draw_calls;}return ok;
#else
    (void)a;(void)lm;(void)ib;(void)offset;(void)count;(void)lmtex;return 0;
#endif
}
