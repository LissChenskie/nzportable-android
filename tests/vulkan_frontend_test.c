// SPDX-License-Identifier: GPL-2.0-or-later
// Exercise the actual engine-facing frontend without a GL context.
#include <SDL.h>
#include "gles_compat.h"
#include "vulkan/renderer.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
static void quad(float x0,float y0,float x1,float y1){glBegin(GL_QUADS);glTexCoord2f(0,0);glVertex3f(x0,y0,0);glTexCoord2f(1,0);glVertex3f(x1,y0,0);glTexCoord2f(1,1);glVertex3f(x1,y1,0);glTexCoord2f(0,1);glVertex3f(x0,y1,0);glEnd();}
static void begin(void){assert(NZP_VK_BeginFrame(0,100));NZP_GLES_BeginFrame();glViewport(0,0,64,64);glClear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT);}
static void end(void){NZP_GLES_Flush();NZP_VK_RequestCapture(NULL);NZP_VK_EndFrame();assert(NZP_VK_ValidationErrors()==0);}
static void pixel(int x,int y,int r,int g,int b){unsigned char p[4];assert(NZP_VK_ReadPixels(x,y,1,1,p));if(abs(p[0]-r)>2||abs(p[1]-g)>2||abs(p[2]-b)>2){fprintf(stderr,"frontend %d,%d got %u %u %u expected %d %d %d\n",x,y,p[0],p[1],p[2],r,g,b);abort();}}
int main(void){
    assert(SDL_Init(SDL_INIT_VIDEO)==0);SDL_Window *w=SDL_CreateWindow("Vril frontend",0,0,64,64,SDL_WINDOW_VULKAN|SDL_WINDOW_HIDDEN);assert(w);assert(NZP_VK_Init(w,"build/host-tests",1));
    NZP_GLES_Init();glClearColor(0,0,0,1);glClearDepthf(1);glDisable(GL_TEXTURE_2D);
    begin();glColor4f(1,0,0,1);quad(-1,-1,1,1);end();pixel(32,32,255,0,0);
    begin();glColor4f(0,0,1,1);quad(-1,-1,1,1);glEnable(GL_BLEND);glBlendFunc(GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA);glColor4f(1,0,0,.5f);quad(-1,-1,1,1);glDisable(GL_BLEND);end();pixel(32,32,128,0,127);
    // Native culling convention, depth conversion and GL-space viewport origin.
    begin();glEnable(GL_CULL_FACE);glCullFace(GL_BACK);glEnable(GL_DEPTH_TEST);glDepthFunc(GL_LEQUAL);glColor4f(0,1,0,1);quad(-1,-1,1,1);glPushMatrix();glTranslatef(0,0,.5f);glColor4f(1,0,0,1);quad(-1,-1,1,1);glPopMatrix();end();pixel(32,32,0,255,0);glDisable(GL_CULL_FACE);glDisable(GL_DEPTH_TEST);
    // Packed texture decoding, pixel alignment, alpha discard and filtering.
    GLuint tex;glGenTextures(1,&tex);glBindTexture(GL_TEXTURE_2D,tex);unsigned short green=0x07c1;
    glPixelStorei(GL_UNPACK_ALIGNMENT,1);glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA,1,1,0,GL_RGBA,GL_UNSIGNED_SHORT_5_5_5_1,&green);
    glTexParameterf(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_NEAREST);glTexParameterf(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_NEAREST);
    begin();glEnable(GL_TEXTURE_2D);glEnable(GL_ALPHA_TEST);glAlphaFunc(GL_GREATER,.5f);glTexEnvf(GL_TEXTURE_ENV,GL_TEXTURE_ENV_MODE,GL_REPLACE);quad(-1,-1,1,1);end();pixel(32,32,0,255,0);glDeleteTextures(1,&tex);glDisable(GL_TEXTURE_2D);glDisable(GL_ALPHA_TEST);
    // Indexed BSP arrays retain their GPU buffers until the frame fence.
    float positions[]={-1,-1,0,1,-1,0,1,1,0,-1,1,0};unsigned short indices[]={0,1,2,0,2,3};GLuint buffers[2];glGenBuffers(2,buffers);
    glBindBuffer(GL_ARRAY_BUFFER,buffers[0]);glBufferData(GL_ARRAY_BUFFER,sizeof(positions),positions,GL_STATIC_DRAW);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER,buffers[1]);glBufferData(GL_ELEMENT_ARRAY_BUFFER,sizeof(indices),indices,GL_STREAM_DRAW);
    nzp_gpu_array arrays[3]={{buffers[0],0,3,GL_FLOAT,0},{0},{0}};
    begin();glColor4f(0,0,1,1);assert(NZP_GLES_DrawGPU(GL_TRIANGLES,arrays,buffers[1],0,6));glDeleteBuffers(2,buffers);end();pixel(32,32,0,0,255);
    NZP_GLES_Shutdown();NZP_VK_Shutdown();SDL_DestroyWindow(w);SDL_Quit();puts("Vril Vulkan frontend pixels and GPU resource dispatch passed");
}
