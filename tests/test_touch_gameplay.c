/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "../android/native/touch_gameplay.h"
#include <stdio.h>
#include <string.h>
#define CHECK(condition) do { if(!(condition)) {fprintf(stderr,"touch test failure at line %d: %s\n",__LINE__,#condition);return 1;} }while(0)
int main(void)
{
    CHECK(strcmp(Touch_ActionCommand("use"),"+use")==0);
    CHECK(strcmp(Touch_ActionCommand("fire"),"+attack")==0);
    CHECK(strcmp(Touch_ActionCommand("reload"),"+reload")==0);
    CHECK(strcmp(Touch_ActionCommand("nade"),"+grenade")==0);
    CHECK(Touch_ActionCommand("run")==NULL);
    CHECK(Touch_ActionCommand("unknown")==NULL);
    TouchSprintState s={0};
    CHECK(TouchSprint_Update(&s,0.0f,0.55f,1,1)==0);
    CHECK(TouchSprint_Update(&s,0.0f,0.95f,1,1)==23);
    CHECK(TouchSprint_Update(&s,0.0f,0.83f,1,1)==0);
    CHECK(TouchSprint_Update(&s,0.0f,0.70f,1,1)==24);
    CHECK(TouchSprint_Update(&s,0.0f,0.98f,1,1)==23);
    CHECK(TouchSprint_Update(&s,0.0f,0.98f,1,1)==0);
    CHECK(TouchSprint_Stop(&s)==24);
    CHECK(TouchSprint_Stop(&s)==0);
    CHECK(TouchSprint_Update(&s,0.0f,0.98f,0,1)==0);
    CHECK(TouchSprint_Update(&s,0.0f,0.98f,1,0)==0);
    s.manual_requested=1;
    CHECK(TouchSprint_Update(&s,0.0f,0.0f,1,1)==23);
    CHECK(TouchSprint_Update(&s,0.0f,0.98f,1,1)==0);
    s.manual_requested=0;
    CHECK(TouchSprint_Update(&s,0.0f,0.98f,1,1)==0);
    CHECK(TouchSprint_Update(&s,0.0f,0.0f,1,1)==24);
    puts("Android direct USE/attack/reload bindings and auto-sprint tests passed");
    return 0;
}
