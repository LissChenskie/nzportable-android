// SPDX-License-Identifier: GPL-2.0-or-later
// Vril's fixed pipeline expressed in GLES 2 shaders. No desktop GL/GL4ES.
// Draws with identical state coalesce; raster/texture/matrix mutations flush.
#define NZP_GLES_IMPLEMENTATION
#include "gles_compat.h"
#include "render_math.h"
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>

typedef struct { float xyz[3], uv[2]; unsigned char color[4]; } nzp_vertex;
typedef struct { const unsigned char *data; int size, stride; GLenum type; int enabled; } nzp_array;
enum { BATCH_CAPACITY=12288, MATRIX_DEPTH=32 };
static nzp_vertex batch[BATCH_CAPACITY], current, first_vertex, previous[3];
static int batch_count, primitive_count;
static GLenum primitive, batch_mode=GL_TRIANGLES, matrix_mode=GL_MODELVIEW;
static nzp_array arrays[3];
static float matrices[3][MATRIX_DEPTH][16];
static int matrix_depth[3];
static int textured=1, alpha_test, fog_enabled, replace_mode, flat_shading;
static float alpha_threshold=.666f, fog_start, fog_end=1.f, fog_density=1.f, fog_color[4];
static int fog_mode=GL_LINEAR;
static GLuint program, streams[3], bound_texture;
static int stream_index;
static size_t stream_offset;
enum { STREAM_BYTES=BATCH_CAPACITY*4*sizeof(nzp_vertex) };
static int raster_enabled[4];
static unsigned frame_draw_calls, frame_upload_bytes, frame_batches;
void NZP_GLES_FrameStats(unsigned *draw_calls,unsigned *upload_bytes,unsigned *batches) {
    if(draw_calls)*draw_calls=frame_draw_calls;
    if(upload_bytes)*upload_bytes=frame_upload_bytes;
    if(batches)*batches=frame_batches;
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
    GLuint v=shader(GL_VERTEX_SHADER,vs),f=shader(GL_FRAGMENT_SHADER,fs);GLint ok;
    program=glCreateProgram();glAttachShader(program,v);glAttachShader(program,f);
    glBindAttribLocation(program,0,"a_position");glBindAttribLocation(program,1,"a_uv");glBindAttribLocation(program,2,"a_color");
    glLinkProgram(program);glGetProgramiv(program,GL_LINK_STATUS,&ok);
    if(!ok){char log[2048];glGetProgramInfoLog(program,sizeof(log),NULL,log);fail(log);}
    glDeleteShader(v);glDeleteShader(f);
    glUseProgram(program);
#define UNIFORM(name) name=glGetUniformLocation(program,#name)
    UNIFORM(u_mvp);UNIFORM(u_mv);UNIFORM(u_tm);UNIFORM(u_texture);UNIFORM(u_textured);
    UNIFORM(u_replace);UNIFORM(u_alpha);UNIFORM(u_fog);UNIFORM(u_fogcolor);
#undef UNIFORM
    memset(&last_uniforms,0,sizeof(last_uniforms));
    // Triple buffered streaming allocations, created just once at startup.
    // Avoid one driver-side 1+ MB GL buffer orphan/allocation every frame.
    glGenBuffers(3,streams);
    for(int i=0;i<3;i++) {
        glBindBuffer(GL_ARRAY_BUFFER,streams[i]);
        glBufferData(GL_ARRAY_BUFFER,STREAM_BYTES,NULL,GL_STREAM_DRAW);
    }
    for(int i=0;i<3;i++)nzp_identity(matrices[i][0]);
    memset(current.color,255,sizeof(current.color));
}
static void uniforms(const float *mvp,const float *mv,const float *tm,int tex,int replace,float alpha,float fog) {
    const float fog_params[4]={fog,fog_start,fog_end,fog_density};
    // Bound once per frame; no per-draw shader program switching on Mali.
    if(!last_uniforms.valid||memcmp(last_uniforms.mvp,mvp,16*sizeof(float))) {
        glUniformMatrix4fv(u_mvp,1,GL_FALSE,mvp);
        memcpy(last_uniforms.mvp,mvp,16*sizeof(float));
    }
    if(!last_uniforms.valid||memcmp(last_uniforms.mv,mv,16*sizeof(float))) {
        glUniformMatrix4fv(u_mv,1,GL_FALSE,mv);
        memcpy(last_uniforms.mv,mv,16*sizeof(float));
    }
    if(!last_uniforms.valid||memcmp(last_uniforms.tm,tm,16*sizeof(float))) {
        glUniformMatrix4fv(u_tm,1,GL_FALSE,tm);
        memcpy(last_uniforms.tm,tm,16*sizeof(float));
    }
    if(!last_uniforms.valid||last_uniforms.textured!=tex) {
        glUniform1i(u_textured,tex);last_uniforms.textured=tex;
    }
    if(!last_uniforms.valid||last_uniforms.replace!=replace) {
        glUniform1i(u_replace,replace);last_uniforms.replace=replace;
    }
    if(!last_uniforms.valid||last_uniforms.alpha!=alpha) {
        glUniform1f(u_alpha,alpha);last_uniforms.alpha=alpha;
    }
    if(!last_uniforms.valid||memcmp(last_uniforms.fog,fog_params,sizeof(fog_params))) {
        glUniform4fv(u_fog,1,fog_params);memcpy(last_uniforms.fog,fog_params,sizeof(fog_params));
    }
    if(!last_uniforms.valid||memcmp(last_uniforms.fogcolor,fog_color,sizeof(fog_color))) {
        glUniform4fv(u_fogcolor,1,fog_color);memcpy(last_uniforms.fogcolor,fog_color,sizeof(fog_color));
    }
    if(!last_uniforms.valid)glUniform1i(u_texture,0);
    last_uniforms.valid=1;
}
static void upload(const nzp_vertex *vertices,int count,GLenum mode) {
    glBindBuffer(GL_ARRAY_BUFFER,streams[stream_index]);glBindBuffer(GL_ELEMENT_ARRAY_BUFFER,0);
    size_t bytes=count*sizeof(nzp_vertex);
    if(stream_offset+bytes>STREAM_BYTES){glBufferData(GL_ARRAY_BUFFER,STREAM_BYTES,NULL,GL_STREAM_DRAW);stream_offset=0;}
    glBufferSubData(GL_ARRAY_BUFFER,stream_offset,bytes,vertices);
    glEnableVertexAttribArray(0);glEnableVertexAttribArray(1);glEnableVertexAttribArray(2);
    glVertexAttribPointer(0,3,GL_FLOAT,GL_FALSE,sizeof(nzp_vertex),(void*)(stream_offset+offsetof(nzp_vertex,xyz)));
    glVertexAttribPointer(1,2,GL_FLOAT,GL_FALSE,sizeof(nzp_vertex),(void*)(stream_offset+offsetof(nzp_vertex,uv)));
    glVertexAttribPointer(2,4,GL_UNSIGNED_BYTE,GL_TRUE,sizeof(nzp_vertex),(void*)(stream_offset+offsetof(nzp_vertex,color)));
    glDrawArrays(mode,0,count);
    frame_draw_calls++;
    frame_upload_bytes+=(unsigned)bytes;
    stream_offset+=bytes;
}
void NZP_GLES_BeginFrame(void){
    NZP_GLES_Flush();stream_index=(stream_index+1)%3;stream_offset=0;
    frame_draw_calls=frame_upload_bytes=frame_batches=0;
    // Fixed-pipeline renderer owns this GLES2 program for the whole frame.
    glUseProgram(program);
    glBindBuffer(GL_ARRAY_BUFFER,streams[stream_index]);
    // STREAM_BYTES storage already exists; cycle through three buffers.
    // A capacity overflow still orphans the current buffer safely.
}
void NZP_GLES_Flush(void) {
    if(!batch_count)return;
    frame_batches++;
    float mvp[16];nzp_multiply(mvp,matrices[1][matrix_depth[1]],matrices[0][matrix_depth[0]]);
    uniforms(mvp,matrices[0][matrix_depth[0]],matrices[2][matrix_depth[2]],textured,replace_mode,
        alpha_test?alpha_threshold:-1.f,fog_enabled?(fog_mode==GL_LINEAR?1.f:fog_mode==GL_EXP?2.f:3.f):0.f);
    glActiveTexture(GL_TEXTURE0);glBindTexture(GL_TEXTURE_2D,bound_texture);upload(batch,batch_count,batch_mode);
    batch_count=0;
}
void NZP_GLES_Shutdown(void) { NZP_GLES_Flush();last_uniforms.valid=0;glDeleteBuffers(3,streams);glDeleteProgram(program);program=0;memset(streams,0,sizeof(streams)); }
static void reserve(int count,GLenum mode) {
    if(batch_mode!=mode||batch_count+count>BATCH_CAPACITY)NZP_GLES_Flush();batch_mode=mode;
}
static void triangle(nzp_vertex a,nzp_vertex b,nzp_vertex c,const unsigned char *color) {
    reserve(3,GL_TRIANGLES);
    if(flat_shading){memcpy(a.color,color,4);memcpy(b.color,color,4);memcpy(c.color,color,4);}
    batch[batch_count++]=a;batch[batch_count++]=b;batch[batch_count++]=c;
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
void nzp_glLoadIdentity(void){NZP_GLES_Flush();nzp_identity(matrix());}
void nzp_glLoadMatrixf(const float *m){NZP_GLES_Flush();memcpy(matrix(),m,16*sizeof(float));}
void nzp_glPushMatrix(void){int i=matrix_index();if(matrix_depth[i]+1>=MATRIX_DEPTH)fail("matrix stack overflow");memcpy(matrices[i][matrix_depth[i]+1],matrix(),16*sizeof(float));matrix_depth[i]++;}
void nzp_glPopMatrix(void){NZP_GLES_Flush();int i=matrix_index();if(!matrix_depth[i])fail("matrix stack underflow");matrix_depth[i]--;}
static void multiply(const float *m){NZP_GLES_Flush();nzp_multiply(matrix(),matrix(),m);}
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
        if(i<0||raster_enabled[i]!=value){NZP_GLES_Flush();if(i>=0)raster_enabled[i]=value;if(value)glEnable(cap);else glDisable(cap);}
    }}
void nzp_glEnable(GLenum cap){enable(cap,1);}void nzp_glDisable(GLenum cap){enable(cap,0);}
void nzp_glAlphaFunc(GLenum f,float threshold){if(f!=GL_GREATER)fail("unsupported alpha test");if(alpha_threshold!=threshold){NZP_GLES_Flush();alpha_threshold=threshold;}}
void nzp_glTexEnvf(GLenum t,GLenum n,float v){(void)t;(void)n;int replace=(int)v==GL_REPLACE;if(replace!=replace_mode){NZP_GLES_Flush();replace_mode=replace;}}
void nzp_glShadeModel(GLenum mode){flat_shading=mode==GL_FLAT;}
void nzp_glFogf(GLenum n,float v){NZP_GLES_Flush();if(n==GL_FOG_START)fog_start=v;else if(n==GL_FOG_END)fog_end=v;else if(n==GL_FOG_DENSITY)fog_density=v;else if(n==GL_FOG_MODE)fog_mode=(int)v;}
void nzp_glFogi(GLenum n,GLint v){nzp_glFogf(n,(float)v);}
void nzp_glFogfv(GLenum n,const float *v){if(n==GL_FOG_COLOR){NZP_GLES_Flush();memcpy(fog_color,v,4*sizeof(float));}else nzp_glFogf(n,*v);}
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

void nzp_glBindTexture(GLenum t,GLuint texture){if(bound_texture!=texture){NZP_GLES_Flush();bound_texture=texture;glBindTexture(t,texture);}}
void nzp_glTexParameterf(GLenum t,GLenum n,float v){NZP_GLES_Flush();glTexParameterf(t,n,v);}
void nzp_glTexImage2D(GLenum t,int l,int in,int w,int h,int b,GLenum f,GLenum type,const void *data){(void)in;NZP_GLES_Flush();glTexImage2D(t,l,f,w,h,b,f,type,data);}
void nzp_glTexSubImage2D(GLenum t,int l,int x,int y,int w,int h,GLenum f,GLenum type,const void *data){NZP_GLES_Flush();glTexSubImage2D(t,l,x,y,w,h,f,type,data);}
void nzp_glDeleteTextures(int n,const GLuint *textures){NZP_GLES_Flush();for(int i=0;i<n;i++)if(textures[i]==bound_texture)bound_texture=0;glDeleteTextures(n,textures);}
#define FLUSH_CALL(name,args,call) void nzp_##name args {NZP_GLES_Flush();name call;}
FLUSH_CALL(glBlendFunc,(GLenum s,GLenum d),(s,d))
FLUSH_CALL(glDepthFunc,(GLenum f),(f))
FLUSH_CALL(glDepthMask,(GLboolean e),(e))
FLUSH_CALL(glCullFace,(GLenum f),(f))
FLUSH_CALL(glPolygonOffset,(float f,float u),(f,u))
FLUSH_CALL(glViewport,(int x,int y,int w,int h),(x,y,w,h))
FLUSH_CALL(glClear,(GLbitfield mask),(mask))
FLUSH_CALL(glReadPixels,(int x,int y,int w,int h,GLenum f,GLenum t,void *p),(x,y,w,h,f,t,p))
FLUSH_CALL(glFinish,(void),())
#undef FLUSH_CALL
void nzp_glDepthRange(double n,double f){NZP_GLES_Flush();glDepthRangef(n,f);}
void nzp_glHint(GLenum target,GLenum mode){(void)target;(void)mode;}
void nzp_glPolygonMode(GLenum face,GLenum mode){(void)face;(void)mode;} // wireframe debug only
void nzp_glReadBuffer(GLenum b){(void)b;}void nzp_glDrawBuffer(GLenum b){(void)b;}

int NZP_GLES_DrawGPU(GLenum mode,const nzp_gpu_array *a,GLuint index_buffer,size_t offset,int count){
    if(!a[0].buffer||mode==GL_QUADS||mode==GL_POLYGON||(flat_shading&&a[2].buffer))return 0;
    NZP_GLES_Flush();float mvp[16];nzp_multiply(mvp,matrices[1][matrix_depth[1]],matrices[0][matrix_depth[0]]);
    uniforms(mvp,matrices[0][matrix_depth[0]],matrices[2][matrix_depth[2]],textured,replace_mode,
        alpha_test?alpha_threshold:-1.f,fog_enabled?(fog_mode==GL_LINEAR?1.f:fog_mode==GL_EXP?2.f:3.f):0.f);
    glActiveTexture(GL_TEXTURE0);glBindTexture(GL_TEXTURE_2D,bound_texture);
    for(int i=0;i<3;i++){
        if(a[i].buffer){glBindBuffer(GL_ARRAY_BUFFER,a[i].buffer);glEnableVertexAttribArray(i);
            glVertexAttribPointer(i,a[i].size,a[i].type,i==2?GL_TRUE:GL_FALSE,a[i].stride,a[i].offset);
        }else{glDisableVertexAttribArray(i);if(i==1)glVertexAttrib2f(i,current.uv[0],current.uv[1]);
            else if(i==2)glVertexAttrib4f(i,current.color[0]/255.f,current.color[1]/255.f,current.color[2]/255.f,current.color[3]/255.f);}
    }
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER,index_buffer);
    if(index_buffer)glDrawElements(mode,count,GL_UNSIGNED_SHORT,(const void*)offset);else glDrawArrays(mode,0,count);
    frame_draw_calls++;
    glBindBuffer(GL_ARRAY_BUFFER,0);glBindBuffer(GL_ELEMENT_ARRAY_BUFFER,0);return 1;
}

void NZP_GLES_Present(GLuint texture,int width,int height){
    NZP_GLES_Flush();GLboolean depth=glIsEnabled(GL_DEPTH_TEST),blend=glIsEnabled(GL_BLEND),cull=glIsEnabled(GL_CULL_FACE);
    GLboolean offset=glIsEnabled(GL_POLYGON_OFFSET_FILL);float identity[16];nzp_identity(identity);
    const nzp_vertex quad[]={{{-1,-1,0},{0,0},{255,255,255,255}},{{1,-1,0},{1,0},{255,255,255,255}},{{1,1,0},{1,1},{255,255,255,255}},
        {{-1,-1,0},{0,0},{255,255,255,255}},{{1,1,0},{1,1},{255,255,255,255}},{{-1,1,0},{0,1},{255,255,255,255}}};
    glViewport(0,0,width,height);glDisable(GL_DEPTH_TEST);glDisable(GL_BLEND);glDisable(GL_CULL_FACE);glDisable(GL_POLYGON_OFFSET_FILL);
    uniforms(identity,identity,identity,1,1,-1,0);glActiveTexture(GL_TEXTURE0);glBindTexture(GL_TEXTURE_2D,texture);upload(quad,6,GL_TRIANGLES);
    glBindTexture(GL_TEXTURE_2D,bound_texture);
    if(depth)glEnable(GL_DEPTH_TEST);if(blend)glEnable(GL_BLEND);if(cull)glEnable(GL_CULL_FACE);if(offset)glEnable(GL_POLYGON_OFFSET_FILL);
}
