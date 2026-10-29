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
/* STOCK MAPS MENU */
#ifdef PLATFORM_ANDROID
#define STOCK_MAPS_VISIBLE 3
static int stock_maps_page;
#else
#define STOCK_MAPS_VISIBLE 8
#endif
static void Menu_StockMaps_NextPage(void){
#ifdef PLATFORM_ANDROID
    if ((stock_maps_page+1)*STOCK_MAPS_VISIBLE < num_stock_maps) {
        ++stock_maps_page;Menu_ResetMenuButtons();Menu_SetSound(MENU_SND_NAVIGATE);
    }
#endif
}
static void Menu_StockMaps_PrevPage(void){
#ifdef PLATFORM_ANDROID
    if(stock_maps_page>0){--stock_maps_page;Menu_ResetMenuButtons();Menu_SetSound(MENU_SND_NAVIGATE);}
#endif
}
void Menu_Maps_Scroll(int direction){
#ifdef PLATFORM_ANDROID
    if(m_state==m_stockmaps){
        if(direction>0)Menu_StockMaps_NextPage();
        else if(direction<0)Menu_StockMaps_PrevPage();
    } else if(m_state==m_custommaps){
        if(direction>0)Menu_CustomMaps_NextPage();
        else if(direction<0)Menu_CustomMaps_PrevPage();
    }
#else
    (void)direction;
#endif
}

void Menu_StockMaps_SetMap (void)
{
    Map_SetDefaultValues();
    current_selected_bsp = stock_maps[
#ifdef PLATFORM_ANDROID
        stock_maps_page * STOCK_MAPS_VISIBLE +
#endif
        current_menu.cursor].bsp_name;
    Menu_Lobby_Set();
}

void Menu_StockMaps_SetRandomMap (void)
{
    if(num_user_maps<=0)return;
    int i = (rand() % num_user_maps);

    Map_SetDefaultValues();
    current_selected_bsp = custom_maps[i].map_name;
    Menu_Lobby_Set();
}

void Menu_StockMaps_SetMainMenu (void) { if (menu_is_solo) Menu_Main_Set(); else Menu_LANCreate_Set(); };

/*
=======================
Menu_SinglePlayer_Set
=======================
*/
void Menu_StockMaps_Set (void)
{
    Menu_ResetMenuButtons();
    
    key_dest = key_menu;
    m_previous_state = m_main;
	m_state = m_stockmaps;
}

/*
=======================
Menu_SinglePlayer_Draw
=======================
*/
void Menu_StockMaps_Draw (void)
{
    int     i;
    char*   header_text;
    char*   back_text;

    // Background
	Menu_DrawCustomBackground(true);

    if (menu_is_solo) {
        header_text = "SELECT MAP: SOLO";
        back_text = "Return to Main Menu.";
    } else {
        header_text = "SELECT MAP: COOP";
        back_text = "Return to Create Game Menu.";
    }

    // Header
	Menu_DrawTitle(header_text, MENU_COLOR_WHITE);
    // Map panel makes the background darker
    Menu_DrawMapPanel();

#ifdef PLATFORM_ANDROID
    int pages=(num_stock_maps+STOCK_MAPS_VISIBLE-1)/STOCK_MAPS_VISIBLE;
    if(stock_maps_page>=pages)stock_maps_page=pages>0?pages-1:0;
    int start=stock_maps_page*STOCK_MAPS_VISIBLE;
    int visible=num_stock_maps-start;
    if(visible>STOCK_MAPS_VISIBLE)visible=STOCK_MAPS_VISIBLE;
    if(visible<0)visible=0;
    for(i=0;i<visible;++i){
        int index=start+i;
        Menu_DrawMapButton(i+1,i,-1,stock_maps[index].category,stock_maps[index].bsp_name,Menu_StockMaps_SetMap);
    }
    int row=visible;
    if(start+visible<num_stock_maps)
        Menu_DrawButton(4,row++,"NEXT PAGE","Swipe up for more maps.",Menu_StockMaps_NextPage);
    else Menu_DrawGreyButton(4,"NEXT PAGE");
    if(stock_maps_page>0)
        Menu_DrawButton(5,row++,"PREVIOUS PAGE","Swipe down for earlier maps.",Menu_StockMaps_PrevPage);
    else Menu_DrawGreyButton(5,"PREVIOUS PAGE");
    Menu_DrawButton(6,row++,"USER MAPS","Browse custom maps.",Menu_CustomMaps_Set);
    Menu_DrawButton(-2,row++,"RANDOM","Pick any installed map.",Menu_StockMaps_SetRandomMap);
    Menu_DrawButton(-1,row++,"BACK",back_text,Menu_StockMaps_SetMainMenu);
    char counter[48];
    snprintf(counter,sizeof(counter),"MAP PAGE %d/%d",stock_maps_page+1,pages?pages:1);
    UI_SetAlignment(UI_ANCHOR_LEFT,UI_ANCHOR_TOP);
    Menu_DrawString(153,158,counter,255,255,0,255,vid.scale,0);
#else
    for (i = 0; i < num_stock_maps; i++) {
        Menu_DrawMapButton(i+1, i, -1, stock_maps[i].category, stock_maps[i].bsp_name, Menu_StockMaps_SetMap);
    }
    Menu_DrawDivider(i+1);
    Menu_DrawButton(i+1.5, i, "USER MAPS", "View User-Created Maps.", Menu_CustomMaps_Set);
    Menu_DrawButton(i+2.5, i+1, "RANDOM", "Feeling indecisive? Try rolling the dice.", Menu_StockMaps_SetRandomMap);
    Menu_DrawButton(-1, i+2, "BACK", back_text, Menu_StockMaps_SetMainMenu);
#endif
}
