// SPDX-License-Identifier: GPL-2.0-or-later
#include "nzportable_def.h"
#include "menu/menu_defs.h"
#include "platform/sdl/sdl_local.h"
#include "android_port.h"
#include "touch_gameplay.h"
#include "menu/menu_defs.h"
#include <SDL_mixer.h>
#include <jni.h>
#include <unistd.h>
#include <stdint.h>
#include <sys/resource.h>

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
static jmethodID thermal_method, touch_method;
static int external_touch, last_touch_mode=-1;
static TouchSprintState touch_sprint;
static int touch_pressed[BUTTONS];
static cvar_t touch_auto_sprint={"touch_auto_sprint","1",true};
enum { NZP_TOUCH_ACTION=0x4e5a10, NZP_TOUCH_MOVE=0x4e5a11,
       NZP_TOUCH_LOOK=0x4e5a12, NZP_TOUCH_NEUTRAL=0x4e5a13,
       NZP_LAN_CONNECT=0x4e5a14, NZP_LAN_NAME=0x4e5a15 };
static intptr_t float_bits(float f) { uint32_t bits; memcpy(&bits,&f,sizeof(bits)); return (intptr_t)bits; }
static float bits_float(const void *p) { uint32_t bits=(uint32_t)(uintptr_t)p;float f;memcpy(&f,&bits,sizeof(f));return f; }
static void push_touch(int code, intptr_t a, intptr_t b) {
    if (!SDL_WasInit(SDL_INIT_EVENTS)) return;
    SDL_Event e;SDL_zero(e);e.type=SDL_USEREVENT;e.user.code=code;
    e.user.data1=(void*)a;e.user.data2=(void*)b;SDL_PushEvent(&e);
}
/* Touch actions must use game commands, not keyboard bindings.
 * The imported config.cfg may not bind E to +use (door / wall purchase).
 * All actions run on the SDL engine thread after receiving the UI event.
 */
static const char *Android_ActionCommand(int key)
{
    switch(key) {
    case K_MOUSE1: return Touch_ActionCommand("fire");
    case K_MOUSE2: return Touch_ActionCommand("aim");
    case ' ': return Touch_ActionCommand("jump");
    case 'r': return Touch_ActionCommand("reload");
    case 'e': return Touch_ActionCommand("use");
    case 'q': return Touch_ActionCommand("swap");
    case 'v': return Touch_ActionCommand("knife");
    case 'g': return Touch_ActionCommand("nade");
    default: return NULL;
    }
}
static void Android_SendSprint(int impulse)
{
    if (impulse == 23) Cbuf_AddText("impulse 23\n");
    else if (impulse == 24) Cbuf_AddText("impulse 24\n");
}
static void Android_UpdateSprint(void)
{
    int playing=(key_dest==key_game && cls.state==ca_connected &&
                 cls.signon==SIGNONS && !cl.paused && foreground);
    int impulse=TouchSprint_Update(&touch_sprint,move_x,move_y,
                                   touch_auto_sprint.value!=0,playing);
    Android_SendSprint(impulse);
}
static void Android_EmitTouchAction(int key,int down)
{
    if(key==K_ESCAPE) {
        Key_Event(K_ESCAPE,down);
        return;
    }
    if(key==K_SHIFT) {
        touch_sprint.manual_requested=down;
        Android_UpdateSprint();
        return;
    }
    const char *command=Android_ActionCommand(key);
    if(!command)return;
    /* Commands carry a synthetic, per-action key id. Do not let
     * simultaneous firing and +use share a KeyDown slot. */
    int idx=key==K_MOUSE1?0:key==K_MOUSE2?1:key==' '?2:
            key=='r'?3:key=='e'?4:key=='q'?5:key=='v'?6:7;
    if(!!touch_pressed[idx]==!!down)return;
    if(down && (key_dest!=key_game || cls.state!=ca_connected))return;
    touch_pressed[idx]=down;
    char line[56];
    snprintf(line,sizeof(line),"%c%s %d\n",
             down?'+':'-',command+1,400+idx);
    Cbuf_AddText(line);
}
static int touch_action_key(const char *id) {
    if (!strcmp(id,"fire"))return K_MOUSE1;
    if (!strcmp(id,"aim"))return K_MOUSE2;
    if (!strcmp(id,"jump"))return ' ';
    if (!strcmp(id,"reload"))return 'r';
    if (!strcmp(id,"use"))return 'e';
    if (!strcmp(id,"swap"))return 'q';
    if (!strcmp(id,"run"))return K_SHIFT;
    if (!strcmp(id,"knife"))return 'v';
    if (!strcmp(id,"nade"))return 'g';
    if (!strcmp(id,"menu"))return K_ESCAPE;
    return 0;
}
JNIEXPORT void JNICALL Java_org_nzportable_android_GameActivity_nativeTouchAction(
        JNIEnv *env,jclass unused,jstring name,jboolean down) {
    (void)unused;if(!name)return;const char *s=(*env)->GetStringUTFChars(env,name,NULL);if(!s)return;
    int key=touch_action_key(s);(*env)->ReleaseStringUTFChars(env,name,s);
    if(key)push_touch(NZP_TOUCH_ACTION,key,down?1:0);
}
JNIEXPORT void JNICALL Java_org_nzportable_android_GameActivity_nativeTouchMove(
        JNIEnv *env,jclass unused,jfloat x,jfloat y) {
    (void)env;(void)unused;push_touch(NZP_TOUCH_MOVE,float_bits(x),float_bits(y));
}
JNIEXPORT void JNICALL Java_org_nzportable_android_GameActivity_nativeTouchLook(
        JNIEnv *env,jclass unused,jfloat x,jfloat y) {
    (void)env;(void)unused;push_touch(NZP_TOUCH_LOOK,float_bits(x),float_bits(y));
}
static void Android_LanDialog(const char *method) {
    JNIEnv *env=SDL_AndroidGetJNIEnv();
    jobject activity=SDL_AndroidGetActivity();
    if(!env || !activity)return;
    jclass cls=(*env)->GetObjectClass(env,activity);
    jmethodID mid=(*env)->GetMethodID(env,cls,method,"()V");
    if(mid)(*env)->CallVoidMethod(env,activity,mid);
    if((*env)->ExceptionCheck(env))(*env)->ExceptionClear(env);
    (*env)->DeleteLocalRef(env,cls);
    (*env)->DeleteLocalRef(env,activity);
}
void Android_RequestLANAddress(void) {Android_LanDialog("openLanAddressDialog");}
void Android_RequestLANName(void) {Android_LanDialog("openLanServerNameDialog");}
static void Android_QueueLanString(JNIEnv *env,jstring value,int code) {
    if(!value)return;
    const char *bytes=(*env)->GetStringUTFChars(env,value,NULL);
    if(!bytes)return;
    size_t len=strlen(bytes);
    char *copy=malloc(len+1);
    if(copy)memcpy(copy,bytes,len+1);
    (*env)->ReleaseStringUTFChars(env,value,bytes);
    if(!copy)return;
    SDL_Event event;SDL_zero(event);event.type=SDL_USEREVENT;
    event.user.code=code;event.user.data1=copy;
    if(SDL_PushEvent(&event)<0)free(copy);
}
JNIEXPORT void JNICALL Java_org_nzportable_android_GameActivity_nativeLanConnect(
    JNIEnv *env,jclass unused,jstring value) {
    (void)unused;Android_QueueLanString(env,value,NZP_LAN_CONNECT);
}
JNIEXPORT void JNICALL Java_org_nzportable_android_GameActivity_nativeLanSetName(
    JNIEnv *env,jclass unused,jstring value) {
    (void)unused;Android_QueueLanString(env,value,NZP_LAN_NAME);
}
JNIEXPORT void JNICALL Java_org_nzportable_android_GameActivity_nativeTouchNeutral(
        JNIEnv *env,jclass unused) {
    (void)env;(void)unused;push_touch(NZP_TOUCH_NEUTRAL,0,0);
}

static cvar_t touch_sensitivity={"touch_sensitivity","1.0",true};
cvar_t nzp_profile={"nzp_profile","0",true};
extern cvar_t cl_maxfps;
extern void Host_WriteConfiguration(void);
extern void SNDDMA_SetPaused(int paused);

int Android_ThermalStatus(void){return thermal;}
/* No thermal FPS or quality downgrade. 0 = explicit unlimited. */
int Android_FrameRate(void) {
    if (cl_maxfps.value<1.f)return 0;
    int fps=(int)cl_maxfps.value;
    return fps>500?500:fps;
}
int Android_Foreground(void){return foreground;}
void Android_Init(void){
    Cvar_RegisterVariable(&touch_sensitivity);
    Cvar_RegisterVariable(&touch_auto_sprint);
    Cvar_RegisterVariable(&nzp_profile);
    SDL_SetHint(SDL_HINT_TOUCH_MOUSE_EVENTS,"0");SDL_SetHint(SDL_HINT_MOUSE_TOUCH_EVENTS,"0");
    SDL_Log("Android device profile: native drawable + user-selected 3D scale");
}
void Android_ResetTouch(void){
    for(int i=0;i<BUTTONS;i++){
        if(button_refs[i])Android_EmitTouchAction(buttons[i].key,false);
        button_refs[i]=0;
    }
    const int keys[]={K_MOUSE1,K_MOUSE2,' ','r','e','q','v','g'};
    for(unsigned i=0;i<sizeof(keys)/sizeof(keys[0]);i++)
        if(touch_pressed[i])Android_EmitTouchAction(keys[i],false);
    Android_SendSprint(TouchSprint_Stop(&touch_sprint));
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
    Android_UpdateSprint();
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
        if(button>=0){f->role=button;if(button_refs[button]++==0)Android_EmitTouchAction(buttons[button].key,true);}
        else if(event->x<.45f&&!role_in_use(-1)){
            f->role=-1;move_origin_x=event->x;move_origin_y=event->y;update_move(event->x,event->y);
        }else if(event->x>=.45f&&!role_in_use(-2))f->role=-2;
        return;
    }
    if(!f)return;
    if(event->type==SDL_FINGERUP){
        if(f->role>=0&&button_refs[f->role]>0&&--button_refs[f->role]==0)Android_EmitTouchAction(buttons[f->role].key,false);
        if(f->role==-1){move_x=move_y=0;Android_UpdateSprint();}
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
        case SDL_USEREVENT:
            switch (event->user.code) {
                case NZP_TOUCH_ACTION:
                    Android_EmitTouchAction((int)(intptr_t)event->user.data1,
                                            (int)(intptr_t)event->user.data2);
                    return 1;
                case NZP_TOUCH_MOVE:
                    move_x=bits_float(event->user.data1);
                    move_y=-bits_float(event->user.data2);
                    Android_UpdateSprint();
                    return 1;
                case NZP_TOUCH_LOOK: {
                    float dx=bits_float(event->user.data1),dy=bits_float(event->user.data2);
                    int width=0,height=0;SDL_GL_GetDrawableSize(sdl_window,&width,&height);
                    // RetroTouch builds report relative look, either normalized or pixels.
                    if(fabsf(dx)>1.f)dx/=fmaxf(1.f,(float)width);
                    if(fabsf(dy)>1.f)dy/=fmaxf(1.f,(float)height);
                    look_x+=dx;look_y+=dy;return 1;
                }
                case NZP_TOUCH_NEUTRAL:Android_ResetTouch();return 1;
                case NZP_LAN_CONNECT: {
                    char *ip=(char *)event->user.data1;
                    Menu_LAN_JoinAddress(ip);
                    free(ip);
                    return 1;
                }
                case NZP_LAN_NAME: {
                    char *name=(char *)event->user.data1;
                    Menu_LAN_SetServerName(name);
                    free(name);
                    return 1;
                }
                default:break;
            }
            break;
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
    static Uint64 thermal_poll,ui_poll,last_hot;Uint64 now=SDL_GetTicks64();
    /* Thermal monitoring is diagnostic only: never changes resolution/FPS.
     * Poll less often so JNI and main-thread wakeups cost less power. */
    int need_thermal=now-thermal_poll>=5000;
    int need_ui=now-ui_poll>=1000;
    if(!need_thermal&&!need_ui)return;
    JNIEnv *env=SDL_AndroidGetJNIEnv();jobject activity=SDL_AndroidGetActivity();
    if(!env||!activity)return;
    jclass activity_class=(*env)->GetObjectClass(env,activity);
    if(need_thermal) {
        thermal_poll=now;
        if(!thermal_method)thermal_method=(*env)->GetMethodID(env,activity_class,"getThermalStatus","()I");
        if(thermal_method){int value=(*env)->CallIntMethod(env,activity,thermal_method);
            if(!(*env)->ExceptionCheck(env)){
                if(value>=thermal){thermal=value;last_hot=now;}
                else if(now-last_hot>=30000){thermal=value;last_hot=now;}
            }}
    }
    if(need_ui) {
        ui_poll=now;
        int playing=host_initialized && key_dest==key_game && cls.state==ca_connected &&
                    cls.signon==SIGNONS && !cl.paused && !LoadingScreen_IsActive();
        // Re-evaluate controllers on the Java side, without reallocating the overlay.
        if(!touch_method)touch_method=(*env)->GetMethodID(env,activity_class,
            "setTouchControlsVisible","(Z)V");
        if(touch_method) {
            (*env)->CallVoidMethod(env,activity,touch_method,playing?JNI_TRUE:JNI_FALSE);
            external_touch=1;last_touch_mode=playing;
        }
    }
    if((*env)->ExceptionCheck(env)){
        (*env)->ExceptionDescribe(env);(*env)->ExceptionClear(env);
    }
    (*env)->DeleteLocalRef(env,activity_class);(*env)->DeleteLocalRef(env,activity);
}
/* On-demand metrics. With profiling disabled, there are no allocations,
 * sorting or periodic logcat writes in the frame loop. FPS is counted from
 * actual rendered frames rather than CPU iterations rejected by Host_FilterTime. */
static int perf_compare_desc(const void *a,const void *b) {
    double v=*(const double*)a, w=*(const double*)b;
    return (v<w)?1:(v>w)?-1:0;
}
void Android_RecordFrame(double cpu_frame_ms) {
    static Uint64 last_report,last_render_timestamp;
    static double frame_time[2048],cpu_sum,cpu_max;
    static unsigned recorded,frames,draw_calls;
    static int last_r_frame;
    static struct rusage previous_usage;
    const int profiling=nzp_profile.value>0.f;
    if(!profiling){
        last_report=last_render_timestamp=0;
        last_r_frame=r_framecount;
        recorded=frames=draw_calls=0;
        cpu_sum=cpu_max=0.0;
        return;
    }
    const Uint64 now=SDL_GetPerformanceCounter();
    const double frequency=(double)SDL_GetPerformanceFrequency();
    if(!last_report){
        last_report=now;
        getrusage(RUSAGE_SELF,&previous_usage);
    }
    if(r_framecount!=last_r_frame) {
        last_r_frame=r_framecount;
        frames++;
        cpu_sum+=cpu_frame_ms;
        if(cpu_frame_ms>cpu_max)cpu_max=cpu_frame_ms;
        draw_calls+=NZP_GLES_DrawCalls();
        if(last_render_timestamp && recorded<2048)
            frame_time[recorded++]=1000.0*(now-last_render_timestamp)/frequency;
        last_render_timestamp=now;
    }
    const double elapsed=(double)(now-last_report)/frequency;
    if(elapsed<5.0)return;
    qsort(frame_time,recorded,sizeof(frame_time[0]),perf_compare_desc);
    const unsigned tail=(recorded>0)?(unsigned)fmax(0.0,ceil(recorded*0.01)-1):0;
    const double low=recorded&&frame_time[tail]>0 ? 1000.0/frame_time[tail] : 0.0;
    struct rusage usage;
    getrusage(RUSAGE_SELF,&usage);
    double cpu_seconds=(usage.ru_utime.tv_sec-previous_usage.ru_utime.tv_sec)+
        (usage.ru_utime.tv_usec-previous_usage.ru_utime.tv_usec)*0.000001+
        (usage.ru_stime.tv_sec-previous_usage.ru_stime.tv_sec)+
        (usage.ru_stime.tv_usec-previous_usage.ru_stime.tv_usec)*0.000001;
    previous_usage=usage;
    int world_width=0,world_height=0;
    extern void VID_GetWorldResolution(int *w,int *h);
    VID_GetWorldResolution(&world_width,&world_height);
    SDL_Log("NZP_PERF fps=%.1f low_1pct_fps=%.1f cpu_ms_avg=%.2f "
            "cpu_ms_max=%.2f draw_calls_per_frame=%.1f cpu_percent_one_core=%.1f "
            "rss_peak_kb=%ld native=%dx%d world=%dx%d thermal=%d fps_cap=%d",
            frames/elapsed,low,frames?cpu_sum/frames:0.0,cpu_max,
            frames?(double)draw_calls/frames:0.0,100.0*cpu_seconds/elapsed,
            (long)usage.ru_maxrss,vid.width,vid.height,
            world_width,world_height,thermal,Android_FrameRate());
    frames=draw_calls=recorded=0;cpu_sum=cpu_max=0.0;last_report=now;
}

void Android_DrawTouch(void){
    if(external_touch)return; // RetroTouch draws on a separate Android overlay view.
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
