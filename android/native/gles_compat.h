// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <stddef.h>
#include <GLES2/gl2.h>
typedef double GLdouble;
typedef double GLclampd;
#define GL_QUADS 0x0007
#define GL_POLYGON 0x0009
#define GL_ALPHA_TEST 0x0BC0
#define GL_FOG 0x0B60
#define GL_FOG_MODE 0x0B65
#define GL_FOG_COLOR 0x0B66
#define GL_FOG_START 0x0B63
#define GL_FOG_END 0x0B64
#define GL_FOG_DENSITY 0x0B62
#define GL_EXP 0x0800
#define GL_EXP2 0x0801
#define GL_MODELVIEW 0x1700
#define GL_PROJECTION 0x1701
#define GL_TEXTURE 0x1702
#define GL_MODELVIEW_MATRIX 0x0BA6
#define GL_PROJECTION_MATRIX 0x0BA7
#define GL_TEXTURE_MATRIX 0x0BA8
#define GL_TEXTURE_ENV 0x2300
#define GL_TEXTURE_ENV_MODE 0x2200
#define GL_MODULATE 0x2100
#define GL_REPLACE 0x1E01
#define GL_FLAT 0x1D00
#define GL_SMOOTH 0x1D01
#define GL_FILL 0x1B02
#define GL_LINE 0x1B01
#define GL_VERTEX_ARRAY 0x8074
#define GL_COLOR_ARRAY 0x8076
#define GL_TEXTURE_COORD_ARRAY 0x8078
#define GL_PERSPECTIVE_CORRECTION_HINT 0x0C50

void NZP_GLES_Init(void);
void NZP_GLES_BeginFrame(void);
void NZP_GLES_Shutdown(void);
void NZP_GLES_Flush(void);
void NZP_GLES_Present(GLuint texture, int width, int height);
void NZP_GLES_SetWorldViewport(int world_width,int world_height,int surface_width,int surface_height);
void NZP_GLES_SetNativeViewport(void);
unsigned NZP_GLES_DrawCalls(void);
/* Graphics API-neutral frame telemetry (no GPU readbacks or allocations).
 * Can also be populated by a future Vulkan backend without exposing GL.
 */
typedef struct nzp_render_stats_s {
    unsigned draw_calls;
    unsigned nonempty_flushes;
    unsigned texture_binds;
    unsigned texture_parameter_changes;
    unsigned shader_switches;
    unsigned uniform_updates;
    unsigned buffer_uploads;
    unsigned vertex_upload_bytes;
    unsigned index_upload_bytes;
    unsigned skipped_state_changes;
} nzp_render_stats_t;
void NZP_GLES_GetFrameStats(nzp_render_stats_t *out);
void NZP_GLES_RecordExternalUpload(GLenum target,size_t bytes);
void NZP_GLES_InvalidateTextureState(void);
/* Android Hyena 2D batches: emit interleaved GPU-ready vertices without
 * three temporary position/UV/color arrays or per-vertex float conversion. */
void NZP_GLES_AppendUIQuad(float x0,float y0,float x1,float y1,
                            float u0,float v0,float u1,float v1,
                            unsigned char r,unsigned char g,
                            unsigned char b,unsigned char a);


typedef struct {GLuint buffer;const void *offset;GLint size;GLenum type;GLsizei stride;} nzp_gpu_array;
int NZP_GLES_DrawGPU(GLenum mode,const nzp_gpu_array *arrays,GLuint index_buffer,size_t offset,int count);
int NZP_Render_DrawLightmappedGPU(const nzp_gpu_array *arrays,const nzp_gpu_array *lightmap_uv,GLuint index_buffer,size_t offset,int count,GLuint lightmap_texture);
void nzp_glBegin(GLenum mode);
void nzp_glEnd(void);
void nzp_glVertex3f(GLfloat x, GLfloat y, GLfloat z);
void nzp_glVertex3fv(const GLfloat *v);
void nzp_glTexCoord2f(GLfloat s, GLfloat t);
void nzp_glTexCoord2fv(const GLfloat *v);
void nzp_glColor4f(GLfloat r, GLfloat g, GLfloat b, GLfloat a);
void nzp_glColor3f(GLfloat r, GLfloat g, GLfloat b);
void nzp_glColor4fv(const GLfloat *v);
void nzp_glMatrixMode(GLenum mode);
void nzp_glLoadIdentity(void);
void nzp_glLoadMatrixf(const GLfloat *m);
void nzp_glPushMatrix(void);
void nzp_glPopMatrix(void);
void nzp_glTranslatef(GLfloat x, GLfloat y, GLfloat z);
void nzp_glScalef(GLfloat x, GLfloat y, GLfloat z);
void nzp_glRotatef(GLfloat angle, GLfloat x, GLfloat y, GLfloat z);
void nzp_glOrtho(GLdouble l, GLdouble r, GLdouble b, GLdouble t, GLdouble n, GLdouble f);
void nzp_glFrustum(GLdouble l, GLdouble r, GLdouble b, GLdouble t, GLdouble n, GLdouble f);
void nzp_glGetFloatv(GLenum name, GLfloat *v);
void nzp_glEnable(GLenum cap);
void nzp_glDisable(GLenum cap);
void nzp_glAlphaFunc(GLenum function, GLfloat threshold);
void nzp_glTexEnvf(GLenum target, GLenum name, GLfloat value);
void nzp_glShadeModel(GLenum mode);
void nzp_glFogf(GLenum name, GLfloat value);
void nzp_glFogi(GLenum name, GLint value);
void nzp_glFogfv(GLenum name, const GLfloat *v);
void nzp_glEnableClientState(GLenum array);
void nzp_glDisableClientState(GLenum array);
void nzp_glVertexPointer(GLint size, GLenum type, GLsizei stride, const void *data);
void nzp_glTexCoordPointer(GLint size, GLenum type, GLsizei stride, const void *data);
void nzp_glColorPointer(GLint size, GLenum type, GLsizei stride, const void *data);
void nzp_glDrawArrays(GLenum mode, GLint first, GLsizei count);
void nzp_glDrawElements(GLenum mode, GLsizei count, GLenum type, const void *indices);
void nzp_glBindTexture(GLenum target, GLuint texture);
void nzp_glTexParameterf(GLenum target, GLenum name, GLfloat value);
void nzp_glTexImage2D(GLenum target, GLint level, GLint internal, GLsizei w, GLsizei h, GLint border, GLenum format, GLenum type, const void *data);
void nzp_glTexSubImage2D(GLenum target, GLint level, GLint x, GLint y, GLsizei w, GLsizei h, GLenum format, GLenum type, const void *data);
void nzp_glDeleteTextures(GLsizei count, const GLuint *textures);
void nzp_glBlendFunc(GLenum source, GLenum dest);
void nzp_glDepthFunc(GLenum function);
void nzp_glDepthMask(GLboolean enabled);
void nzp_glDepthRange(GLdouble near_value, GLdouble far_value);
void nzp_glCullFace(GLenum face);
void nzp_glPolygonOffset(GLfloat factor, GLfloat units);
void nzp_glViewport(GLint x, GLint y, GLsizei w, GLsizei h);
void nzp_glClear(GLbitfield bits);
void nzp_glReadPixels(GLint x, GLint y, GLsizei w, GLsizei h, GLenum format, GLenum type, void *data);
void nzp_glFinish(void);
void nzp_glHint(GLenum target, GLenum mode);
void nzp_glPolygonMode(GLenum face, GLenum mode);
void nzp_glReadBuffer(GLenum buffer);
void nzp_glDrawBuffer(GLenum buffer);

#ifdef NZP_VULKAN
void nzp_glGenTextures(int n,GLuint *ids);
void nzp_glGenBuffers(int n,GLuint *ids);
void nzp_glDeleteBuffers(int n,const GLuint *ids);
void nzp_glBindBuffer(GLenum target,GLuint id);
void nzp_glBufferData(GLenum target,GLsizeiptr bytes,const void *data,GLenum usage);
void nzp_glPixelStorei(GLenum name,GLint value);
void nzp_glGetIntegerv(GLenum name,GLint *value);
void nzp_glClearColor(float r,float g,float b,float a);
void nzp_glClearDepthf(float d);
#ifndef NZP_GLES_IMPLEMENTATION
#define glGenTextures nzp_glGenTextures
#define glGenBuffers nzp_glGenBuffers
#define glDeleteBuffers nzp_glDeleteBuffers
#define glBindBuffer nzp_glBindBuffer
#define glBufferData nzp_glBufferData
#define glPixelStorei nzp_glPixelStorei
#define glGetIntegerv nzp_glGetIntegerv
#define glClearColor nzp_glClearColor
#define glClearDepthf nzp_glClearDepthf
#endif
#endif
#ifndef NZP_GLES_IMPLEMENTATION
#define glBegin nzp_glBegin
#define glEnd nzp_glEnd
#define glVertex3f nzp_glVertex3f
#define glVertex3fv nzp_glVertex3fv
#define glTexCoord2f nzp_glTexCoord2f
#define glTexCoord2fv nzp_glTexCoord2fv
#define glColor4f nzp_glColor4f
#define glColor3f nzp_glColor3f
#define glColor4fv nzp_glColor4fv
#define glMatrixMode nzp_glMatrixMode
#define glLoadIdentity nzp_glLoadIdentity
#define glLoadMatrixf nzp_glLoadMatrixf
#define glPushMatrix nzp_glPushMatrix
#define glPopMatrix nzp_glPopMatrix
#define glTranslatef nzp_glTranslatef
#define glScalef nzp_glScalef
#define glRotatef nzp_glRotatef
#define glOrtho nzp_glOrtho
#define glFrustum nzp_glFrustum
#define glGetFloatv nzp_glGetFloatv
#define glEnable nzp_glEnable
#define glDisable nzp_glDisable
#define glAlphaFunc nzp_glAlphaFunc
#define glTexEnvf nzp_glTexEnvf
#define glShadeModel nzp_glShadeModel
#define glFogf nzp_glFogf
#define glFogi nzp_glFogi
#define glFogfv nzp_glFogfv
#define glEnableClientState nzp_glEnableClientState
#define glDisableClientState nzp_glDisableClientState
#define glVertexPointer nzp_glVertexPointer
#define glTexCoordPointer nzp_glTexCoordPointer
#define glColorPointer nzp_glColorPointer
#define glDrawArrays nzp_glDrawArrays
#define glDrawElements nzp_glDrawElements
#define glBindTexture nzp_glBindTexture
#define glTexParameterf nzp_glTexParameterf
#define glTexImage2D nzp_glTexImage2D
#define glTexSubImage2D nzp_glTexSubImage2D
#define glDeleteTextures nzp_glDeleteTextures
#define glBlendFunc nzp_glBlendFunc
#define glDepthFunc nzp_glDepthFunc
#define glDepthMask nzp_glDepthMask
#define glDepthRange nzp_glDepthRange
#define glClearDepth glClearDepthf
#define glCullFace nzp_glCullFace
#define glPolygonOffset nzp_glPolygonOffset
#define glViewport nzp_glViewport
#define glClear nzp_glClear
#define glReadPixels nzp_glReadPixels
#define glFinish nzp_glFinish
#define glHint nzp_glHint
#define glPolygonMode nzp_glPolygonMode
#define glReadBuffer nzp_glReadBuffer
#define glDrawBuffer nzp_glDrawBuffer
#endif
