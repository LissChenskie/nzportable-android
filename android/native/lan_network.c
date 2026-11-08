// SPDX-License-Identifier: GPL-2.0-or-later
#include "lan_network.h"
#include <pthread.h>
#ifdef __ANDROID__
#include <jni.h>
#include <android/multinetwork.h>
#include <android/log.h>
#endif
static pthread_mutex_t lock=PTHREAD_MUTEX_INITIALIZER;
static uint64_t handle_value;
static unsigned bind_count;
void NZP_LanNetwork_Set(uint64_t handle){
    pthread_mutex_lock(&lock);handle_value=handle;pthread_mutex_unlock(&lock);
}
uint64_t NZP_LanNetwork_Get(void){
    uint64_t v;pthread_mutex_lock(&lock);v=handle_value;pthread_mutex_unlock(&lock);return v;
}
unsigned NZP_LanNetwork_BindCount(void){
    unsigned v;pthread_mutex_lock(&lock);v=bind_count;pthread_mutex_unlock(&lock);return v;
}
int NZP_LanNetwork_BindSocket(int fd){
    uint64_t h=NZP_LanNetwork_Get();
    if(!h||fd<0)return 0;
#ifdef __ANDROID__
    if(android_setsocknetwork((net_handle_t)h,fd)!=0){
        __android_log_print(ANDROID_LOG_WARN,"NZPortable","android_setsocknetwork failed for fd %d",fd);
        return -1;
    }
#endif
    pthread_mutex_lock(&lock);bind_count++;pthread_mutex_unlock(&lock);
    return 0;
}
#ifdef __ANDROID__
JNIEXPORT void JNICALL Java_org_nzportable_android_LanNetwork_nativeNetworkChanged(
        JNIEnv *env,jclass unused,jlong handle){
    (void)env;(void)unused;
    NZP_LanNetwork_Set((uint64_t)handle);
}
#endif
