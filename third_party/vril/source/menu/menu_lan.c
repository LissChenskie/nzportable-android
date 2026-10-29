/*
 * NZPortable Android LAN cooperation: Quake UDP server discovery and join UI.
 * GPL-2.0-or-later. This deliberately uses Vril's existing game server.
 */
#include "../nzportable_def.h"
#include "menu_defs.h"
#include <stdio.h>
#include <string.h>
#include "../platform/sdl/net_lan_utils.h"

extern int hostCacheCount;
extern hostcache_t hostcache[HOSTCACHESIZE];
extern qboolean slistInProgress;
extern qboolean slistSilent;
extern qboolean slistLocal;
extern char my_tcpip_address[NET_NAMELEN];
extern int net_hostport;
extern cvar_t hostname;
extern void NET_Slist_f(void);

#ifdef PLATFORM_ANDROID
extern const char *UDP_ActiveAddressString(void);
extern void Android_RequestLANAddress(void);
extern void Android_RequestLANName(void);
#endif

static int lan_slots=4, lan_scroll;
static char lan_status[128]="Connect all devices to the same Wi-Fi / hotspot.";
static char lan_details[7][72];
static int last_count=-1;
static float last_query;

int Menu_LAN_GetSlots(void) { return lan_slots; }
const char *Menu_LAN_Address(void) {
#ifdef PLATFORM_ANDROID
    return UDP_ActiveAddressString();
#else
    return my_tcpip_address;
#endif
}
void Menu_LAN_SetStatus(const char *value) {
    snprintf(lan_status, sizeof(lan_status), "%s", value ? value : "");
}
static void Menu_LAN_CycleSlots(void) {
    if (++lan_slots > 4) lan_slots=2;
}
void Menu_LAN_Set(void) {
    Menu_ResetMenuButtons();
    key_dest=key_menu;
    m_previous_state=m_main;
    m_state=m_lan;
}
void Menu_LAN_Create(void) {
    menu_is_solo=false;
    Menu_StockMaps_Set();
}
static void Menu_LAN_Search(void) {
    if(slistInProgress)return;
    slistLocal=false; /* Exclude loopback, query only real UDP LAN servers. */
    slistSilent=true;
    NET_Slist_f();
    last_query=(float)Sys_FloatTime();
}
void Menu_LAN_BrowserSet(void) {
    lan_scroll=0;last_count=-1;
    Menu_ResetMenuButtons();
    key_dest=key_menu;
    m_previous_state=m_lan;
    m_state=m_lanbrowser;
    Menu_LAN_Search();
}
void Menu_LAN_JoinAddress(const char *ip) {
    unsigned octet[4],port=26000;
    char cmd[80];
    if(!LAN_ParseIPv4(ip,octet,&port,26000)){
        Menu_LAN_SetStatus("Invalid address. Enter IPv4 or IPv4:port.");
        return;
    }
    snprintf(cmd,sizeof(cmd),"connect %u.%u.%u.%u:%u\n",
             octet[0],octet[1],octet[2],octet[3],port);
    menu_is_solo=false;
    Cvar_Set("coop","1");
    Cvar_Set("deathmatch","0");
    if(sv.active)Cbuf_AddText("disconnect\n");
    Cbuf_AddText(cmd);
    extern char m_return_reason[32];
    m_return_reason[0]=0;
    Menu_LAN_SetStatus("Connecting to LAN host...");
    m_state=m_none;
    key_dest=key_game;
}

static void Menu_LAN_JoinSelected(void) {
    int selected=lan_scroll+current_menu.cursor;
    if(selected>=0&&selected<hostCacheCount) {
        if(hostcache[selected].name[0]=='*') {
            Menu_LAN_SetStatus("Incompatible Vril network protocol.");
            return;
        }
        Menu_LAN_JoinAddress(hostcache[selected].cname);
    }
}
static void Menu_LAN_Prev(void){if(lan_scroll>0){lan_scroll-=5;Menu_ResetMenuButtons();}}
static void Menu_LAN_Next(void){
    if(lan_scroll+5<hostCacheCount){lan_scroll+=5;Menu_ResetMenuButtons();}
}
static void Menu_LAN_Manual(void) {
#ifdef PLATFORM_ANDROID
    Android_RequestLANAddress();
#else
    Menu_LAN_SetStatus("Use connect <ip:port> from console on this platform.");
#endif
}
static void Menu_LAN_Rename(void) {
#ifdef PLATFORM_ANDROID
    Android_RequestLANName();
#else
    Menu_LAN_SetStatus("Set server name with hostname <name>.");
#endif
}
void Menu_LAN_SetServerName(const char *name) {
    char valid[NET_NAMELEN];
    size_t i,n=0;
    if(!name)return;
    for(i=0;name[i]&&n<sizeof(valid)-1;i++){
        char c=name[i];
        if(c>=' '&&c<='~'&&c!='"'&&c!=';'&&c!='\\')
            valid[n++]=c;
    }
    valid[n]=0;
    if(n) {
        Cvar_Set("hostname",valid);
        Menu_LAN_SetStatus("Server name updated.");
    }
}
void Menu_LAN_Draw(void) {
    char slots[40],address[80];
    Menu_DrawCustomBackground(true);
    Menu_DrawTitle("COOPERATIVE",MENU_COLOR_WHITE);
    snprintf(slots,sizeof(slots),"MAX PLAYERS: %d",lan_slots);
    snprintf(address,sizeof(address),"%s:%d",Menu_LAN_Address(),net_hostport);
    Menu_DrawButton(1,0,"CREATE LAN GAME","Choose a map and host on this phone.",Menu_LAN_Create);
    Menu_DrawButton(2,1,"FIND LAN GAMES","Discover servers on local Wi-Fi.",Menu_LAN_BrowserSet);
    Menu_DrawButton(3,2,"CONNECT BY IP","Manually enter IP address and port.",Menu_LAN_Manual);
    Menu_DrawButton(4,3,slots,"Number of players including host.",Menu_LAN_CycleSlots);
    Menu_DrawButton(5,4,"SERVER NAME","Edit discoverable server name.",Menu_LAN_Rename);
    Menu_DrawButton(-1,5,"BACK","Main menu.",Menu_Main_Set);
    UI_SetAlignment(UI_ANCHOR_LEFT,UI_ANCHOR_TOP);
    Menu_DrawString(170,72,hostname.string,255,255,255,255,vid.scale,0);
    Menu_DrawString(170,85,address,255,255,150,255,vid.scale,0);
    Menu_DrawString(170,105,lan_status,255,255,255,255,vid.scale,0);
}
void Menu_LAN_BrowserDraw(void) {
    int count=hostCacheCount,shown=0;
    double time=Sys_FloatTime();
    Menu_DrawCustomBackground(true);
    Menu_DrawTitle("FIND LAN GAMES",MENU_COLOR_WHITE);
    if(count!=last_count){last_count=count;Menu_ResetMenuButtons();}
    if(!slistInProgress&&time-last_query>6.0f)Menu_LAN_Search();
    for(int i=lan_scroll;i<count&&shown<5;i++,shown++){
        snprintf(lan_details[shown],sizeof(lan_details[shown]),
                 "%.15s  %u/%u",hostcache[i].map,
                 hostcache[i].users,hostcache[i].maxusers);
        Menu_DrawButton(shown+1,shown,hostcache[i].name,
                        lan_details[shown],Menu_LAN_JoinSelected);
    }
    if(!shown) {
        UI_SetAlignment(UI_ANCHOR_LEFT,UI_ANCHOR_TOP);
        Menu_DrawString(155,82,slistInProgress?"SEARCHING...":"NO LAN SERVERS FOUND",
                        255,255,255,255,vid.scale,0);
    }
    Menu_DrawButton(6,shown,"REFRESH","Search this Wi-Fi network again.",Menu_LAN_Search);
    if(lan_scroll)Menu_DrawButton(7,shown+1,"PREVIOUS","Earlier servers.",Menu_LAN_Prev);
    if(lan_scroll+5<count)Menu_DrawButton(8,shown+(lan_scroll?2:1),
                                         "NEXT","More servers.",Menu_LAN_Next);
    Menu_DrawButton(-1,shown+(lan_scroll?1:0)+(lan_scroll+5<count?1:0)+1,
                    "BACK","Cooperative menu.",Menu_LAN_Set);
    UI_SetAlignment(UI_ANCHOR_LEFT,UI_ANCHOR_TOP);
    Menu_DrawString(160,160,lan_status,255,255,255,255,vid.scale,0);
}
