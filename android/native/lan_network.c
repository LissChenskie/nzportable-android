// SPDX-License-Identifier: GPL-2.0-or-later
#include "lan_network.h"
#include <errno.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef __ANDROID__
#include <jni.h>
#include <android/multinetwork.h>
#include <android/log.h>
#endif
static pthread_mutex_t lock=PTHREAD_MUTEX_INITIALIZER;
static uint64_t handle_value;
static unsigned bind_count,generation;

#ifdef __ANDROID__
void NZP_FileLog(const char *line); /* crash_report.c */
#endif
void NZP_LanLog(const char *fmt,...){
    va_list ap;
    va_start(ap,fmt);
#ifdef __ANDROID__
    {
        char line[480];
        va_list copy;
        va_copy(copy,ap);
        vsnprintf(line,sizeof(line),fmt,copy);
        va_end(copy);
        NZP_FileLog(line);
    }
    __android_log_vprint(ANDROID_LOG_INFO,"NZP_LAN",fmt,ap);
#else
    if(getenv("NZP_LAN_LOG")){fputs("NZP_LAN: ",stderr);vfprintf(stderr,fmt,ap);fputc('\n',stderr);}
#endif
    va_end(ap);
}
void NZP_LanNetwork_Set(uint64_t handle){
    int changed;
    pthread_mutex_lock(&lock);
    changed=handle!=handle_value;
    handle_value=handle;
    if(changed)generation++;
    pthread_mutex_unlock(&lock);
    if(changed)NZP_LanLog("Wi-Fi network handle %s: %llu",handle?"set":"cleared (no Wi-Fi client network; default routing)",(unsigned long long)handle);
}
uint64_t NZP_LanNetwork_Get(void){
    uint64_t v;pthread_mutex_lock(&lock);v=handle_value;pthread_mutex_unlock(&lock);return v;
}
unsigned NZP_LanNetwork_Generation(void){
    unsigned v;pthread_mutex_lock(&lock);v=generation;pthread_mutex_unlock(&lock);return v;
}
unsigned NZP_LanNetwork_BindCount(void){
    unsigned v;pthread_mutex_lock(&lock);v=bind_count;pthread_mutex_unlock(&lock);return v;
}
int NZP_LanNetwork_BindSocket(int fd){
    uint64_t h=NZP_LanNetwork_Get();
    if(!h||fd<0)return 0;
#ifdef __ANDROID__
    if(android_setsocknetwork((net_handle_t)h,fd)!=0){
        int err=errno;
        NZP_LanLog("android_setsocknetwork FAILED fd=%d handle=%llu errno=%d (%s)",fd,(unsigned long long)h,err,strerror(err));
        errno=err;
        return -1;
    }
#endif
    pthread_mutex_lock(&lock);bind_count++;pthread_mutex_unlock(&lock);
    return 0;
}
int NZP_LanNetwork_ClearSocket(int fd){
    if(fd<0)return 0;
#ifdef __ANDROID__
    if(android_setsocknetwork(NETWORK_UNSPECIFIED,fd)!=0){
        int err=errno;
        NZP_LanLog("android_setsocknetwork(UNSPECIFIED) FAILED fd=%d errno=%d (%s)",fd,err,strerror(err));
        errno=err;
        return -1;
    }
#endif
    return 0;
}
#ifdef __ANDROID__
JNIEXPORT void JNICALL Java_org_nzportable_android_LanNetwork_nativeNetworkChanged(
        JNIEnv *env,jclass unused,jlong handle){
    (void)env;(void)unused;
    NZP_LanNetwork_Set((uint64_t)handle);
}
#endif
