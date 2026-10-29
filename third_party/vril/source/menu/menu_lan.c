// SPDX-License-Identifier: GPL-2.0-or-later
// LAN UI uses Vril's original Quake loopback/datagram server; no fake sessions.
#include "../nzportable_def.h"
#include "menu_defs.h"
#include <ctype.h>
#include "lan_endpoint.h"
#include <stdio.h>
#ifdef PLATFORM_ANDROID
#include <jni.h>
#include <SDL.h>
#include <ifaddrs.h>
#include <net/if.h>
#include <arpa/inet.h>
#endif


static int lan_slots = 4;
static int lan_join_pending;
static char lan_status[96] = "Create a game or search your local Wi-Fi.";
static char lan_endpoint[64];
#ifdef PLATFORM_ANDROID
static const char *LAN_CurrentAddress(void) {
    static char address[INET_ADDRSTRLEN],fallback[INET_ADDRSTRLEN];
    static Uint64 last_refresh;
    Uint64 now=SDL_GetTicks64();
    if(now-last_refresh<2000 && last_refresh) return address[0]?address:my_tcpip_address;
    last_refresh=now;address[0]=fallback[0]=0;
    struct ifaddrs *list=NULL,*it;
    if(getifaddrs(&list)==0) {
        for(it=list;it;it=it->ifa_next) {
            if(!it->ifa_addr||it->ifa_addr->sa_family!=AF_INET||
                !(it->ifa_flags & IFF_UP)||(it->ifa_flags & IFF_LOOPBACK))continue;
            const struct sockaddr_in *ip=(const struct sockaddr_in*)it->ifa_addr;
            char candidate[INET_ADDRSTRLEN];
            if(!inet_ntop(AF_INET,&ip->sin_addr,candidate,sizeof(candidate)))continue;
            if(!fallback[0])strncpy(fallback,candidate,sizeof(fallback)-1);
            // Prefer Wi-Fi tethering interfaces over mobile rmnet/rmnet_data.
            if(!strncmp(it->ifa_name,"wlan",4)||!strncmp(it->ifa_name,"ap",2)||
               !strncmp(it->ifa_name,"swlan",5)||!strncmp(it->ifa_name,"eth",3)) {
                strncpy(address,candidate,sizeof(address)-1);break;
            }
        }
        freeifaddrs(list);
    }
    if(!address[0]&&fallback[0])strncpy(address,fallback,sizeof(address)-1);
    return address[0]?address:my_tcpip_address;
}
#else
static const char *LAN_CurrentAddress(void) { return my_tcpip_address; }
#endif

static char lan_line[160];

void Menu_LAN_Set(void) {
    Menu_ResetMenuButtons();
    key_dest = key_menu;
    m_previous_state = m_main;
    m_state = m_lan;
}

void Menu_LANCreate_Set(void) {
    Menu_ResetMenuButtons();
    menu_is_solo = false;
    key_dest = key_menu;
    m_previous_state = m_lan;
    m_state = m_lancreate;
}

void Menu_LANHost_Set(void) {
    Menu_ResetMenuButtons();
    if (!sv.active) {
        snprintf(lan_status, sizeof(lan_status), "Server could not load the selected map.");
        Menu_LAN_Set();
        return;
    }
    // Keep server networking live while the host waits in the lobby.
    sv.paused = true;
    key_dest = key_menu;
    m_state = m_lanhost;
    m_previous_state = m_lan;
}

// Registered console command, queued immediately AFTER map to avoid moving into
// the lobby before SV_SpawnServer/loopback client have been initialized.
static void Menu_LANHost_Command(void) { Menu_LANHost_Set(); }

void Menu_LAN_Init(void) {
    Cmd_AddCommand("lan_host_lobby", Menu_LANHost_Command);
}

void Menu_LAN_CycleSlots(void) {
    lan_slots = lan_slots >= 4 ? 2 : lan_slots + 1;
    Menu_ResetMenuButtons();
}

int Menu_LAN_Slots(void) { return lan_slots; }

void Menu_LAN_CycleDifficulty(void) {
    Cvar_SetValue("sv_difficulty", ((int)sv_difficulty.value + 1) % 4);
    Menu_ResetMenuButtons();
}
void Menu_LAN_CycleRound(void) {
    int r = (int)sv_startround.value;
    r = r >= 10 ? 1 : r + 1;
    Cvar_SetValue("sv_startround", r);
    Menu_ResetMenuButtons();
}
void Menu_LAN_MapSelect(void) {
    menu_is_solo = false;
    Menu_StockMaps_Set();
}

void Menu_LAN_HostStart(void) {
    if (!sv.active) {
        snprintf(lan_status, sizeof(lan_status), "Server is not running.");
        Menu_LAN_Set();
        return;
    }
    // A player in signon cannot safely start the shared zombie simulation.
    for(int i=0;i<svs.maxclients;i++) {
        if(svs.clients[i].active && !svs.clients[i].spawned) {
            Con_Printf("LAN: player %d is still loading\n",i+1);
            return;
        }
    }
    sv.paused = false;
    Menu_ResetMenuButtons();
    key_dest = key_game;
    m_state = m_none;
}

void Menu_LAN_HostClose(void) {
    if (sv.active) {
        sv.paused = false;
        Cbuf_AddText("disconnect\n");
        Host_ShutdownServer(false);
    }
    Cbuf_AddText("listen 0\nmaxplayers 1\ncoop 0\ndeathmatch 0\n");
    Menu_LAN_Set();
}

void Menu_LAN_PrepareHost(void) {
    char cmd[120];
    // Engine command order is significant: coop BEFORE maxplayers because
    // Quake's maxplayers otherwise sets deathmatch=1.
    Cbuf_AddText("coop 1\ndeathmatch 0\n");
    snprintf(cmd, sizeof(cmd), "maxplayers %d\nlisten 1\n", lan_slots);
    Cbuf_AddText(cmd);
}

void Menu_LAN_Find_Set(void) {
    Menu_ResetMenuButtons();
    key_dest = key_menu;
    m_state = m_lanfind;
    m_previous_state = m_lan;
    slistSilent = true;
    slistLocal = false;  // no local loopback results in remote LAN search.
    NET_Slist_f();
}

static void Menu_LAN_FindConnect(void) {
    int idx = current_menu.cursor;
    if (idx < 0 || idx >= hostCacheCount || idx >= 8) return;
    if(hostcache[idx].name[0]=='*') {
        snprintf(lan_status,sizeof(lan_status),"Incompatible LAN protocol version.");
        Menu_LAN_Set();
        return;
    }
    // Hostcache holds the address from the actual Quake server response.
    Menu_LAN_ConnectAddress(hostcache[idx].cname);
}

void Menu_LAN_RequestIp(void) {
#ifdef PLATFORM_ANDROID
    JNIEnv *env = SDL_AndroidGetJNIEnv();
    jobject activity = SDL_AndroidGetActivity();
    if (env && activity) {
        jclass cls = (*env)->GetObjectClass(env, activity);
        jmethodID method = (*env)->GetMethodID(env, cls, "promptLanAddress", "()V");
        if (method) (*env)->CallVoidMethod(env, activity, method);
        if ((*env)->ExceptionCheck(env)) (*env)->ExceptionClear(env);
        (*env)->DeleteLocalRef(env, cls);
        (*env)->DeleteLocalRef(env, activity);
    }
#else
    snprintf(lan_status, sizeof(lan_status), "Use connect <IPv4:port> in the console.");
#endif
}

void Menu_LAN_RequestName(void) {
#ifdef PLATFORM_ANDROID
    JNIEnv *env = SDL_AndroidGetJNIEnv();
    jobject activity = SDL_AndroidGetActivity();
    if (env && activity) {
        jclass cls = (*env)->GetObjectClass(env, activity);
        jmethodID method = (*env)->GetMethodID(env, cls, "promptLanName", "()V");
        if (method) (*env)->CallVoidMethod(env, activity, method);
        if ((*env)->ExceptionCheck(env)) (*env)->ExceptionClear(env);
        (*env)->DeleteLocalRef(env, cls);
        (*env)->DeleteLocalRef(env, activity);
    }
#endif
}

// Explicitly reject console separators, malformed address, and invalid ports.
// Called exclusively on the SDL/engine thread by Android's SDL event handler.
int Menu_LAN_ValidEndpoint(const char *value) { return lan_endpoint_valid(value); }
void Menu_LAN_ConnectAddress(const char *value) {
    char cmd[96];
    if(!Menu_LAN_ValidEndpoint(value)) {
        snprintf(lan_status, sizeof(lan_status), "Invalid IPv4 address or port.");
        return;
    }
    snprintf(cmd, sizeof(cmd), "connect %s\n", value);
    lan_join_pending=1;
    // Client-side local cvars should reflect the server's cooperative rules.
    Cbuf_AddText("coop 1\ndeathmatch 0\n");
    Cbuf_AddText(cmd);
    key_dest=key_game;
    m_state=m_none;
}
void Menu_LAN_Connected(void) { lan_join_pending=0; }
void Menu_LAN_ShowError(const char *message) {
    snprintf(lan_status,sizeof(lan_status),"LAN error: %.72s",message ? message : "unknown");
    Menu_LAN_Set();
}
void Menu_LAN_ConnectionFailed(void) {
    if(!lan_join_pending)return;
    lan_join_pending=0;
    extern char m_return_reason[32];
    const char *reason=m_return_reason[0]?m_return_reason:"No response from server";
    snprintf(lan_status,sizeof(lan_status),"LAN join failed: %.63s",reason);
    Menu_LAN_Set();
}
void Menu_LAN_SetName(const char *value) {
    char name[32]; int n=0;
    if(!value)return;
    while(value[n] && n<sizeof(name)-1) {
        unsigned char c=(unsigned char)value[n];
        if(c<32 || c>126 || c=='"' || c==';' || c=='\\')break;
        name[n]=(char)c;n++;
    }
    if(!n)return;
    name[n]='\0';
    Cvar_Set("hostname",name);
}

static void Menu_LAN_Create_Draw(void) {
    Menu_DrawCustomBackground(true);
    Menu_DrawTitle("CREATE LAN GAME",MENU_COLOR_WHITE);
    Menu_DrawMapPanel();
    Menu_DrawButton(1,0,"SERVER NAME","Edit the name advertised on LAN.",Menu_LAN_RequestName);
    Menu_DrawButton(2,1,"MAX PLAYERS","Supports 2 to 4 total players.",Menu_LAN_CycleSlots);
    Menu_DrawButton(3,2,"DIFFICULTY","Change zombie difficulty.",Menu_LAN_CycleDifficulty);
    Menu_DrawButton(4,3,"START ROUND","Set initial round.",Menu_LAN_CycleRound);
    Menu_DrawButton(5,4,"SELECT MAP","Choose a map then create the server.",Menu_LAN_MapSelect);
    Menu_DrawButton(-1,5,"BACK","Return to LAN menu.",Menu_LAN_Set);
    UI_SetAlignment(UI_ANCHOR_LEFT,UI_ANCHOR_TOP);
    snprintf(lan_line,sizeof(lan_line),"Name: %.28s",Cvar_VariableString("hostname"));
    Menu_DrawString(153,45,lan_line,255,255,255,255,vid.scale,0);
    snprintf(lan_line,sizeof(lan_line),"Players: %d / 4",lan_slots);
    Menu_DrawString(153,65,lan_line,255,255,255,255,vid.scale,0);
    snprintf(lan_line,sizeof(lan_line),"Difficulty: %d | Round: %d",(int)sv_difficulty.value,(int)sv_startround.value);
    Menu_DrawString(153,85,lan_line,255,255,255,255,vid.scale,0);
}

static void Menu_LAN_Find_Draw(void) {
    int count = hostCacheCount < 8 ? hostCacheCount : 8;
    Menu_DrawCustomBackground(true);
    Menu_DrawTitle("FIND LAN GAMES",MENU_COLOR_WHITE);
    Menu_DrawMapPanel();
    for(int i=0;i<count;i++) {
        snprintf(lan_line,sizeof(lan_line),"%.15s %d/%d",
                 hostcache[i].name,hostcache[i].users,hostcache[i].maxusers);
        // Each row calls the same callback, which uses the highlighted row index.
        Menu_DrawButton(i+1,i,lan_line,"Connect using existing Quake UDP.",Menu_LAN_FindConnect);
    }
    Menu_DrawButton(count+1,count,"REFRESH","Search all local interfaces again.",Menu_LAN_Find_Set);
    Menu_DrawButton(-1,count+1,"BACK","Return to cooperative menu.",Menu_LAN_Set);
    UI_SetAlignment(UI_ANCHOR_LEFT,UI_ANCHOR_TOP);
    if(count==0)Menu_DrawString(153,65,
        slistInProgress?"Searching LAN...":"No compatible servers found.",255,255,255,255,vid.scale,0);
    else if(current_menu.cursor<count) {
        hostcache_t *h=&hostcache[current_menu.cursor];
        snprintf(lan_line,sizeof(lan_line),"Map: %.23s",h->map);
        Menu_DrawString(153,55,lan_line,255,255,255,255,vid.scale,0);
        snprintf(lan_endpoint,sizeof(lan_endpoint),"%.55s",h->cname);
        Menu_DrawString(153,75,lan_endpoint,255,255,255,255,vid.scale,0);
    }
}

static void Menu_LAN_Host_Draw(void) {
    Menu_DrawCustomBackground(true);
    Menu_DrawTitle("LAN LOBBY",MENU_COLOR_WHITE);
    Menu_DrawMapPanel();
    Menu_DrawButton(1,0,"START MATCH","Unpause the shared server simulation.",Menu_LAN_HostStart);
    Menu_DrawButton(2,1,"CLOSE SERVER","Disconnect every player and close LAN.",Menu_LAN_HostClose);
    UI_SetAlignment(UI_ANCHOR_LEFT,UI_ANCHOR_TOP);
    snprintf(lan_line,sizeof(lan_line),"%.22s",Cvar_VariableString("hostname"));
    Menu_DrawString(153,45,lan_line,255,255,0,255,vid.scale,0);
    snprintf(lan_line,sizeof(lan_line),"IP: %.32s:%d",LAN_CurrentAddress(),net_hostport);
    Menu_DrawString(153,58,lan_line,255,255,255,255,vid.scale,0);
    snprintf(lan_line,sizeof(lan_line),"Map: %.28s",sv.name);
    Menu_DrawString(153,71,lan_line,255,255,255,255,vid.scale,0);
    int n=0;
    for(int i=0;i<svs.maxclients;i++) if(svs.clients[i].active) {
        snprintf(lan_line,sizeof(lan_line),"%d. %.21s %s",i+1,
                 svs.clients[i].name,svs.clients[i].spawned ? "READY" : "LOADING");
        Menu_DrawString(153,(88+n*11),lan_line,255,255,255,255,vid.scale,0);
        n++;
    }
    snprintf(lan_line,sizeof(lan_line),"Connected %d/%d | Free %d",n,svs.maxclients,svs.maxclients-n);
    Menu_DrawString(153,151,lan_line,255,255,0,255,vid.scale,0);
}

void Menu_LAN_Draw(void) {
    if(m_state==m_lancreate){Menu_LAN_Create_Draw();return;}
    if(m_state==m_lanfind){Menu_LAN_Find_Draw();return;}
    if(m_state==m_lanhost){Menu_LAN_Host_Draw();return;}
    Menu_DrawCustomBackground(true);
    Menu_DrawTitle("COOPERATIVE",MENU_COLOR_WHITE);
    Menu_DrawButton(1,0,"CREATE LAN GAME","Host on this phone over Wi-Fi.",Menu_LANCreate_Set);
    Menu_DrawButton(2,1,"FIND LAN GAMES","Search local Quake servers.",Menu_LAN_Find_Set);
    Menu_DrawButton(3,2,"CONNECT BY IP","Manual address for filtered Hotspot networks.",Menu_LAN_RequestIp);
    Menu_DrawButton(-1,3,"BACK","Return to main menu.",Menu_Main_Set);
    UI_SetAlignment(UI_ANCHOR_LEFT,UI_ANCHOR_TOP);
    Menu_DrawString(153,70,lan_status,255,255,255,255,vid.scale,0);
}
