/*
Copyright (C) 2025-2026 NZ:P Team

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  

See the GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.
*/
#include "../nzportable_def.h"
#include "menu_defs.h"

//=============================================================================
/* VIDEO MENU */

char* fps_string;
char* particles_string;
char* retro_string;
char* dithering_string;
char* ultrawide_string;
#ifdef PLATFORM_SUPPORTS_VIDEO_OPTIONS
char* fullscreen_string;
char* vsync_string;
extern cvar_t vid_fullscreen;
extern cvar_t r_vsync;
void VID_SetFullscreen(qboolean fullscreen);
void VID_SetVSync(qboolean vsync);
#endif

qboolean platform_supports_dithering;

#ifdef PLATFORM_ANDROID
extern cvar_t cl_maxfps;
extern cvar_t r_vsync;
extern cvar_t vid_render_scale;
extern cvar_t vid_ui_scale;
extern cvar_t android_profile;
extern void Host_WriteConfiguration(void);
static int video_android_page;
static char video_resolution_text[80];
static char video_ui_text[32];
static char video_fps_text[32];
static char video_native_text[80];
static void Menu_Video_SaveAndroid(void) {Host_WriteConfiguration();}
static void Menu_Video_ToggleProfile(void) {
    Cvar_SetValue("android_profile",!android_profile.value);
    Menu_Video_SaveAndroid();
}

static void Menu_Video_ResolutionNext(void) {
    static const int modes[]={100,90,80,75,60};
    int n=0;
    while(n<4 && (int)vid_render_scale.value!=modes[n])n++;
    Cvar_SetValue("vid_render_scale",modes[(n+1)%5]);
    Menu_Video_SaveAndroid();
}
static void Menu_Video_UIScaleNext(void) {
    static const int sizes[]={75,100,125,150};
    int n=0;while(n<3&&(int)vid_ui_scale.value!=sizes[n])n++;
    Cvar_SetValue("vid_ui_scale",sizes[(n+1)%4]);
    Menu_Video_SaveAndroid();
}
static void Menu_Video_FPSNext(void) {
    static const int limits[]={30,45,60,0};
    int n=0;while(n<3&&(int)cl_maxfps.value!=limits[n])n++;
    Cvar_SetValue("cl_maxfps",limits[(n+1)%4]);
    Menu_Video_SaveAndroid();
}
static void Menu_Video_VSyncNext(void) {
    Cvar_SetValue("r_vsync",!r_vsync.value);
    Menu_Video_SaveAndroid();
}
static void Menu_Video_NextPage(void) {
    video_android_page=1;
    Menu_ResetMenuButtons();
}
static void Menu_Video_BackPage(void) {
    video_android_page=0;
    Menu_ResetMenuButtons();
}
#endif
extern cvar_t scr_showfps;
extern cvar_t cl_maxfps;
extern cvar_t scr_fov;
extern cvar_t v_gamma;
extern cvar_t r_runqmbparticles;
extern cvar_t r_retro;
extern cvar_t r_dithering;
extern cvar_t vid_ultrawide_limiter;

void Menu_Video_ApplyShowFPS (void)
{
    float current_showfps = scr_showfps.value;

    current_showfps += 1;
    if (current_showfps > 1) {
        current_showfps = 0;
    }

    Cvar_SetValue (scr_showfps.name, current_showfps);
}

void Menu_Video_ApplyParticles (void)
{
    float current_particles = r_runqmbparticles.value;

    current_particles += 1;
    if (current_particles > 1) {
        current_particles = 0;
    }

    Cvar_SetValue ("r_runqmbparticles", current_particles);
}

void Menu_Video_ApplyTextureFiltering (void)
{
    float current_filter = r_retro.value;

    current_filter += 1;
    if (current_filter > 1) {
        current_filter = 0;
    }

    Cvar_SetValue ("r_retro", current_filter);
}

void Menu_Video_ApplyDithering (void)
{
    float current_dithering = r_dithering.value;

    current_dithering += 1;
    if (current_dithering > 1) {
        current_dithering = 0;
    }

    Cvar_SetValue ("r_dithering", current_dithering);
}

void Menu_Video_ApplyUltrawide (void)
{
    Cvar_SetValue ("vid_ultrawide_limiter", !vid_ultrawide_limiter.value);
}

#ifdef PLATFORM_SUPPORTS_VIDEO_OPTIONS
void Menu_Video_ApplyFullscreen (void)
{
    VID_SetFullscreen(!vid_fullscreen.value);
}

void Menu_Video_ApplyVSync (void)
{
    VID_SetVSync(!r_vsync.value);
}
#endif

void Menu_Configuration_ApplySettings (void)
{
#ifdef PLATFORM_ANDROID
    Host_WriteConfiguration();
#endif
    Menu_SetSound(MENU_SND_ENTER);
}

/*
===============
Menu_Video_Set
===============
*/
void Menu_Video_Set (void)
{
#ifdef RENDERER_SUPPORTS_DITHERING
    platform_supports_dithering = true;
#else
    platform_supports_dithering = false;
#endif

    Menu_ResetMenuButtons();
#ifdef PLATFORM_ANDROID
    video_android_page=0;
#endif

    m_previous_state = m_state;
	m_state = m_video;
}

void Menu_Video_SetStrings (void)
{
    if ((int)scr_showfps.value == 1) {
        fps_string = "ENABLED";
    } else {
        fps_string = "DISABLED";
    }

    if ((int)r_runqmbparticles.value == 1) {
        particles_string = "ENABLED";
    } else {
        particles_string = "DISABLED";
    }

    if ((int)r_retro.value == 1) {
        retro_string = "NEAREST";
    } else {
        retro_string = "LINEAR";
    }

    if ((int)r_dithering.value == 1) {
        dithering_string = "ENABLED";
    } else {
        dithering_string = "DISABLED";
    }

    if ((int)vid_ultrawide_limiter.value == 1) {
        ultrawide_string = "ENABLED";
    } else {
        ultrawide_string = "DISABLED";
    }

#ifdef PLATFORM_SUPPORTS_VIDEO_OPTIONS
    fullscreen_string = vid_fullscreen.value ? "ENABLED" : "DISABLED";
    vsync_string = r_vsync.value ? "ENABLED" : "DISABLED";
#endif
}

/*
===============
Menu_Video_Draw
===============
*/
void Menu_Video_Draw (void)
{
    int video_items = 0;

	// Background
	Menu_DrawCustomBackground (true);

	// Header
	Menu_DrawTitle ("VIDEO OPTIONS", MENU_COLOR_WHITE);
    // Map panel makes the background darker
    Menu_DrawMapPanel();
    // Set value strings
    Menu_Video_SetStrings();

#ifdef PLATFORM_ANDROID
    /* Two pages keep large-touch Android UI readable at all DPI values. */
    if(video_android_page==0) {
        const int native_w=(int)vid.width,native_h=(int)vid.height;
        const int render=(int)vid_render_scale.value;
        const int user_ui=(int)vid_ui_scale.value;
        int selected_w=native_w,selected_h=native_h;
        if(render<100&&render>0) {
            selected_w=(int)(native_w*(render/100.f)+0.5f);
            selected_h=(int)(native_h*(render/100.f)+0.5f);
        }
        snprintf(video_native_text,sizeof(video_native_text),
                 "NATIVE DISPLAY: %d x %d",native_w,native_h);
        if(render==100)
            snprintf(video_resolution_text,sizeof(video_resolution_text),
                     "AUTO %dx%d (NATIVE)",native_w,native_h);
        else
            snprintf(video_resolution_text,sizeof(video_resolution_text),
                     "%d%% (%dx%d)",render,selected_w,selected_h);
        snprintf(video_ui_text,sizeof(video_ui_text),"%d%%",user_ui);
        if(cl_maxfps.value<1)
            snprintf(video_fps_text,sizeof(video_fps_text),"UNLIMITED");
        else
            snprintf(video_fps_text,sizeof(video_fps_text),"%d FPS",(int)cl_maxfps.value);
        Menu_DrawButton(1,video_items++,"RESOLUTION",
            "Choose native (100%) or a manual world rendering percentage.",
            Menu_Video_ResolutionNext);
        Menu_DrawOptionButton(1,video_resolution_text);
        Menu_DrawButton(2,video_items++,"UI SCALE",
            "HUD and menus are rendered in native screen pixels.",
            Menu_Video_UIScaleNext);
        Menu_DrawOptionButton(2,video_ui_text);
        Menu_DrawButton(3,video_items++,"FPS LIMIT",
            "30 / 45 / 60 / Unlimited; never changes game speed.",
            Menu_Video_FPSNext);
        Menu_DrawOptionButton(3,video_fps_text);
        Menu_DrawButton(4,video_items++,"SHOW FPS",
            "Display frame rate over the gameplay HUD.",
            Menu_Video_ApplyShowFPS);
        Menu_DrawOptionButton(4,fps_string);
        Menu_DrawButton(5,video_items++,"VSYNC",
            "Synchronize presentation to display refresh.",
            Menu_Video_VSyncNext);
        Menu_DrawOptionButton(5,r_vsync.value?"ENABLED":"DISABLED");
        Menu_DrawButton(6,video_items++,"PERFORMANCE LOG",
            "Opt-in 5-second FPS/CPU/draw-call profiling in Android logcat.",
            Menu_Video_ToggleProfile);
        Menu_DrawOptionButton(6,android_profile.value?"ENABLED":"DISABLED");
        Menu_DrawButton(7,video_items++,"MORE GRAPHICS",
            "Field of view, particles, filtering, and brightness.",
            Menu_Video_NextPage);
        Menu_DrawDivider(-2.5);
        Menu_DrawButton(-2,video_items++,"APPLY",
            "Save selected Android display settings.",Menu_Configuration_ApplySettings);
        Menu_DrawButton(-1,video_items++,"BACK",
            "Return to settings.",Menu_Configuration_Set);
    } else {
        Menu_DrawButton(1,video_items++,"FIELD OF VIEW",
            "Change camera field of view.",NULL);
        Menu_DrawOptionSlider(1,video_items-1,60,160,scr_fov,"fov",false,true,10.0f);
        Menu_DrawButton(2,video_items++,"GAMMA",
            "Adjust game black level.",NULL);
        Menu_DrawOptionSlider(2,video_items-1,0,1,v_gamma,"gamma",false,false,0.1f);
        Menu_DrawButton(3,video_items++,"ULTRAWIDE MODE",
            "Condense HUD closer to center.",Menu_Video_ApplyUltrawide);
        Menu_DrawOptionButton(3,ultrawide_string);
        Menu_DrawButton(4,video_items++,"PARTICLES",
            "Toggle particles manually.",Menu_Video_ApplyParticles);
        Menu_DrawOptionButton(4,particles_string);
        Menu_DrawButton(5,video_items++,"TEXTURE FILTERING",
            "Keep original texture filtering; user controlled.",
            Menu_Video_ApplyTextureFiltering);
        Menu_DrawOptionButton(5,retro_string);
        if(platform_supports_dithering) {
            Menu_DrawButton(6,video_items++,"DITHERING",
                "Adjust color banding.",Menu_Video_ApplyDithering);
            Menu_DrawOptionButton(6,dithering_string);
        }
        Menu_DrawDivider(-2.5);
        Menu_DrawButton(-2,video_items++,"DISPLAY SETTINGS",
            "Native resolution, HUD scale and FPS limit.",Menu_Video_BackPage);
        Menu_DrawButton(-1,video_items++,"BACK",
            "Return to settings.",Menu_Configuration_Set);
    }
    return;
#endif

    // Show FPS
    Menu_DrawButton (1, video_items++, "SHOW FPS", "Toggles display of FPS Values.", Menu_Video_ApplyShowFPS); ;
    Menu_DrawOptionButton (1, fps_string);

    // Max FPS
    Menu_DrawButton (2, video_items++, "MAX FPS", "Sets the max FPS Value.", NULL);
#ifdef PLATFORM_SUPPORTS_HIGH_FRAMERATES
    Menu_DrawOptionSlider (2, video_items-1, 5, 500, cl_maxfps, "cl_maxfps", true, true, 5.0f);
#else
    Menu_DrawOptionSlider (2, video_items-1, 5, 60, cl_maxfps, "cl_maxfps", true, true, 5.0f);
#endif // PLATFORM_SUPPORTS_HIGH_FRAMERATES

    // Field of View
    Menu_DrawButton (3, video_items++, "FIELD OF VIEW", "Change Camera Field of View", NULL);
    Menu_DrawOptionSlider (3, video_items-1, 60, 160, scr_fov, "fov", false, true, 10.0f);

    // Gamma
    Menu_DrawButton (4, video_items++, "GAMMA", "Adjust game Black Level.", NULL);
    Menu_DrawOptionSlider (4, video_items-1, 0, 1, v_gamma, "gamma", false, false, 0.1f);

    // Ultrawide Mode
    Menu_DrawButton (5, video_items++, "ULTRAWIDE MODE", "Condenses HUD closer to center of Display.", Menu_Video_ApplyUltrawide);
    Menu_DrawOptionButton (5, ultrawide_string);

    // Particles 
    Menu_DrawButton (6, video_items++, "PARTICLES", "Trade Particle Effects for Performance.", Menu_Video_ApplyParticles);
    Menu_DrawOptionButton (6, particles_string);

    // Retro (texture filtering)
    Menu_DrawButton (7, video_items++, "TEXTURE FILTERING", "Choose 3D Environment Filtering Mode.", Menu_Video_ApplyTextureFiltering);
    Menu_DrawOptionButton (7, retro_string);

    // Dithering 
    if (platform_supports_dithering) {
        Menu_DrawButton (8, video_items++, "DITHERING", "Toggle Decrease in Color Banding", Menu_Video_ApplyDithering);
        Menu_DrawOptionButton (8, dithering_string);
    }

#ifdef PLATFORM_SUPPORTS_VIDEO_OPTIONS
    Menu_DrawButton (8, video_items++, "FULLSCREEN", "Toggle desktop fullscreen mode.", Menu_Video_ApplyFullscreen);
    Menu_DrawOptionButton (8, fullscreen_string);

    Menu_DrawButton (9, video_items++, "VSYNC", "Synchronize frames to the display refresh rate.", Menu_Video_ApplyVSync);
    Menu_DrawOptionButton (9, vsync_string);
#endif

    Menu_DrawDivider(-2.5);
	Menu_DrawButton(-2, video_items++, "APPLY", "Save & Apply Settings.", Menu_Configuration_ApplySettings);
	Menu_DrawButton(-1, video_items, "BACK", "Return to Main Menu.", Menu_Configuration_Set);
}

