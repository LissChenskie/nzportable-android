// SPDX-License-Identifier: GPL-2.0-or-later
#include "nzportable_def.h"
#include "menu/menu_defs.h"
#include "platform/sdl/sdl_local.h"
#include "android_port.h"
#include <SDL_mixer.h>
#include <jni.h>
#include <unistd.h>

typedef struct {float x,y,r;int key;const char *label;} touch_button;
// Coordinates normalized to the rendered area, with radii relative to height.
static const touch_button buttons[]={
    {.87f,.56f,.090f,K_MOUSE1,"FIRE"},{.73f,.63f,.065f,K_MOUSE2,"AIM"},
    {.91f,.83f,.065f,' ',"JUMP"},{.76f,.84f,.060f,'r',"RELOAD"},
    {.64f,.85f,.060f,'e',"USE"},{.60f,.66f,.055f,'q',"SWAP"},
    {.88f,.32f,.055f,'v',"KNIFE"},{.72f,.33f,.055f,'g',"NADE"},
    {.29f,.82f,.055f,K_SHIFT,"RUN"},{.94f,.10f,.050f,K_ESCAPE,"MENU"}
};
enum { BUTTONS=sizeof(buttons)/sizeof(buttons[0]), FINGERS=16 };
typedef struct {SDL_FingerID id;int active,role;float x,y;} touch_finger;
static touch_finger fingers[FINGERS];
static int button_refs[BUTTONS], foreground=1, thermal, touch_visible=1;
static float move_x,move_y,move_origin_x,move_origin_y,look_x,look_y;
static jmethodID thermal_method;
static cvar_t touch_sensitivity={"touch_sensitivity","1.0",true};
extern cvar_t cl_maxfps;
extern void Host_WriteConfiguration(void);
extern void SNDDMA_SetPaused(int paused);

int Android_ThermalStatus(void){return thermal;}
int Android_FrameRate(void){int cap=thermal>=4?15:thermal>=2?30:60;return cl_maxfps.value>=1&&cl_maxfps.value<cap?(int)cl_maxfps.value:cap;}
int Android_Foreground(void){return foreground;}
void Android_Init(void){
    Cvar_RegisterVariable(&touch_sensitivity);
    SDL_SetHint(SDL_HINT_TOUCH_MOUSE_EVENTS,"0");SDL_SetHint(SDL_HINT_MOUSE_TOUCH_EVENTS,"0");
    SDL_Log("Android device profile: GLES2 / 1280x720 bounds / 128 MiB engine heap");
}
void Android_ResetTouch(void){
    for(int i=0;i<BUTTONS;i++){if(button_refs[i])Key_Event(buttons[i].key,false);button_refs[i]=0;}
    memset(fingers,0,sizeof(fingers));move_x=move_y=look_x=look_y=0;
}
static void reset_input(void){Android_ResetTouch();IN_ClearPendingInput();}
static void suspend_game(void){
    foreground=0;reset_input();
    if(key_dest==key_game){Key_Event(K_ESCAPE,true);Key_Event(K_ESCAPE,false);}
    Host_WriteConfiguration();Mix_PauseMusic();SNDDMA_SetPaused(1);
}
static void resume_game(void){foreground=1;reset_input();SNDDMA_SetPaused(0);if(!music_paused)Mix_ResumeMusic();}
static int hit_button(float x,float y){
    float aspect=(float)vid.width/fmaxf(1.f,vid.height);
    for(int i=0;i<BUTTONS;i++){float dx=(x-buttons[i].x)*aspect,dy=y-buttons[i].y;
        if(dx*dx+dy*dy<buttons[i].r*buttons[i].r)return i;}
    return -1;
}
static int role_in_use(int role){for(int i=0;i<FINGERS;i++)if(fingers[i].active&&fingers[i].role==role)return 1;return 0;}
static void update_move(float x,float y){
    float aspect=(float)vid.width/fmaxf(1.f,vid.height);
    move_x=(x-move_origin_x)*aspect/.12f;move_y=(move_origin_y-y)/.12f;
    float length=sqrtf(move_x*move_x+move_y*move_y);if(length>1){move_x/=length;move_y/=length;}
}
static void touch(const SDL_TouchFingerEvent *event){
    // A loading screen is not gameplay. Do not generate movement/attack actions;
    // tap to continue only after its precache and intro have finished.
    if (LoadingScreen_IsWaiting()) {
        if (event->type == SDL_FINGERDOWN) {
            Key_Event(K_ENTER, true);
            Key_Event(K_ENTER, false);
        }
        return;
    }
    touch_finger *f=NULL;
    for(int i=0;i<FINGERS;i++)if(fingers[i].active&&fingers[i].id==event->fingerId){f=&fingers[i];break;}
    if(event->type==SDL_FINGERDOWN){
        if(f)return;
        for(int i=0;i<FINGERS;i++)if(!fingers[i].active){f=&fingers[i];break;}if(!f)return;
        f->active=1;f->id=event->fingerId;f->x=event->x;f->y=event->y;f->role=-4;
        touch_visible=1;IN_SetActiveDevice(IN_DEVICE_KEYBOARD_MOUSE);Menu_SetInputDevice(IN_DEVICE_KEYBOARD_MOUSE);
        if(key_dest!=key_game){
            f->role=-3;Menu_MouseMove(event->x*vid.width,event->y*vid.height);
            if(!Menu_MouseButton(event->x*vid.width,event->y*vid.height,true))Menu_ButtonPress();return;
        }
        int button=hit_button(event->x,event->y);
        if(button>=0){f->role=button;if(button_refs[button]++==0)Key_Event(buttons[button].key,true);}
        else if(event->x<.45f&&!role_in_use(-1)){
            f->role=-1;move_origin_x=event->x;move_origin_y=event->y;update_move(event->x,event->y);
        }else if(event->x>=.45f&&!role_in_use(-2))f->role=-2;
        return;
    }
    if(!f)return;
    if(event->type==SDL_FINGERUP){
        if(f->role>=0&&button_refs[f->role]>0&&--button_refs[f->role]==0)Key_Event(buttons[f->role].key,false);
        if(f->role==-1)move_x=move_y=0;
        if(f->role==-3)Menu_MouseButton(event->x*vid.width,event->y*vid.height,false);
        f->active=0;return;
    }
    if(f->role==-3){Menu_MouseMove(event->x*vid.width,event->y*vid.height);}
    else if(key_dest==key_game){
        if(f->role==-1)update_move(event->x,event->y);
        if(f->role==-2){look_x+=(event->x-f->x);look_y+=(event->y-f->y);}
    }
    f->x=event->x;f->y=event->y;
}
int Android_Event(const SDL_Event *event){
    switch(event->type){
        case SDL_FINGERDOWN:case SDL_FINGERUP:case SDL_FINGERMOTION:touch(&event->tfinger);return 1;
        case SDL_APP_WILLENTERBACKGROUND:if(foreground)suspend_game();return 1;
        case SDL_APP_DIDENTERFOREGROUND:if(!foreground)resume_game();return 1;
        case SDL_APP_LOWMEMORY:SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,"Android low memory");return 1;
        case SDL_APP_TERMINATING:Host_WriteConfiguration();sdl_running=false;return 1;
        case SDL_CONTROLLERBUTTONDOWN:touch_visible=0;Android_ResetTouch();break;
        case SDL_CONTROLLERAXISMOTION:
            if(event->caxis.value>8192||event->caxis.value<-8192){touch_visible=0;Android_ResetTouch();}break;
        case SDL_WINDOWEVENT:
            if(event->window.event==SDL_WINDOWEVENT_FOCUS_LOST)reset_input();break;
        case SDL_KEYDOWN:case SDL_KEYUP:
            if(event->key.keysym.sym==SDLK_AC_BACK){Key_Event(K_ESCAPE,event->type==SDL_KEYDOWN);return 1;}break;
    }return 0;
}
void Android_TouchMove(float *x,float *y){if(fabsf(move_x)>.01f||fabsf(move_y)>.01f){*x=move_x;*y=move_y;}}
void Android_TouchLook(void){
    if(key_dest==key_game&&!cl.paused){
        float zoom=cl.stats[STAT_ZOOM]>0?.5f:1.f;
        cl.viewangles[YAW]-=look_x*180.f*touch_sensitivity.value*zoom;
        cl.viewangles[PITCH]+=look_y*100.f*touch_sensitivity.value*zoom;
        cl.viewangles[PITCH]=fmaxf(-70,fminf(80,cl.viewangles[PITCH]));
    }look_x=look_y=0;
}
void Android_Frame(void){
    static Uint64 poll_time,last_hot;Uint64 now=SDL_GetTicks64();
    if(now-poll_time>=1000){
        poll_time=now;JNIEnv *env=SDL_AndroidGetJNIEnv();jobject activity=SDL_AndroidGetActivity();
        if(env&&activity){jclass cls=(*env)->GetObjectClass(env,activity);
            if(!thermal_method)thermal_method=(*env)->GetMethodID(env,cls,"getThermalStatus","()I");
            if(thermal_method){int value=(*env)->CallIntMethod(env,activity,thermal_method);if(!(*env)->ExceptionCheck(env)){
                if(value>=thermal){thermal=value;last_hot=now;}else if(now-last_hot>=30000){thermal=value;last_hot=now;}
            }}
            if((*env)->ExceptionCheck(env))(*env)->ExceptionClear(env);
            (*env)->DeleteLocalRef(env,cls);(*env)->DeleteLocalRef(env,activity);
        }
    }
}
void Android_DrawTouch(void){
    if(!touch_visible||!host_initialized||key_dest!=key_game||cls.state!=ca_connected||LoadingScreen_IsWaiting())return;
    for(int i=0;i<BUTTONS;i++){
        int r=buttons[i].r*vid.height,x=buttons[i].x*vid.width,y=buttons[i].y*vid.height;
        Draw_FillByColor(x-r,y-r,r*2,r*2,button_refs[i]?180:32,button_refs[i]?110:40,50,button_refs[i]?170:95);
        // Vril's text API takes byte-range colors, unlike the GL color API.
        int scale=(r*2-8)/(8*(int)strlen(buttons[i].label));
        if(scale<1)scale=1;if(scale>vid.scale)scale=vid.scale;
        Draw_ColoredString(x-getTextWidth((char*)buttons[i].label,scale)/2,y-4*scale,(char*)buttons[i].label,255,255,255,210,scale);
    }
    int size=vid.height*.06f,x=role_in_use(-1)?move_origin_x*vid.width:vid.width*.16f,y=role_in_use(-1)?move_origin_y*vid.height:vid.height*.76f;
    Draw_FillByColor(x-size,y-size,size*2,size*2,48,58,68,90);
    Draw_FillByColor(x+move_x*size-size/3,y-move_y*size-size/3,size*2/3,size*2/3,210,220,230,130);
}
