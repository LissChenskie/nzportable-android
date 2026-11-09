// SPDX-License-Identifier: GPL-2.0-or-later
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include "thread_roles.h"
#include <pthread.h>
#include <sched.h>
#include <string.h>
#include <sys/resource.h>
#ifdef __ANDROID__
#include <android/log.h>
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO,"NZPortable",__VA_ARGS__)
#else
#include <stdio.h>
#define LOGI(...) (fprintf(stderr,__VA_ARGS__),fputc('\n',stderr))
#endif
static nzp_cpu_topology topo;
static pthread_once_t once=PTHREAD_ONCE_INIT;
static volatile int affinity_enabled=1;
static void detect(void){
    nzp_cpu_topology_detect(NULL,NULL,&topo);
    LOGI("NZP_THREADS cpus=%d perf=%d(mask=0x%llx) eff=%d(mask=0x%llx) hetero=%d source=%s",
         topo.cpu_count,topo.perf_count,(unsigned long long)topo.perf_mask,
         topo.eff_count,(unsigned long long)topo.eff_mask,topo.heterogeneous,topo.source);
}
const nzp_cpu_topology *nzp_threads_init(void){pthread_once(&once,detect);return &topo;}
void nzp_threads_set_affinity_enabled(int enabled){affinity_enabled=enabled!=0;}
static int set_affinity(uint64_t mask){
    cpu_set_t set;int i,n=0;
    CPU_ZERO(&set);
    for(i=0;i<topo.cpu_count&&i<NZP_MAX_CPUS;i++)if(mask&(1ULL<<i)){CPU_SET(i,&set);n++;}
    if(!n)return 0;
    /* pid 0 = calling thread on Linux/Bionic. Failure (OEM policy) is not fatal. */
    return sched_setaffinity(0,sizeof(set),&set)==0;
}
int nzp_thread_apply_role(nzp_thread_role role){
    int applied=0,nice_value=0;
    nzp_threads_init();
    switch(role){
    case NZP_ROLE_GAME:nice_value=-4;break;
    case NZP_ROLE_BACKGROUND:nice_value=6;break;
    case NZP_ROLE_BURST:nice_value=0;break;
    }
    /* Priority first (works even when pinning is unsupported). Negative values may be refused. */
    if(setpriority(PRIO_PROCESS,0,nice_value)!=0&&nice_value<0)setpriority(PRIO_PROCESS,0,-1);
    if(affinity_enabled&&topo.heterogeneous){
        uint64_t mask=role==NZP_ROLE_GAME?topo.perf_mask:role==NZP_ROLE_BACKGROUND?topo.eff_mask:topo.all_mask;
        applied=set_affinity(mask);
    }else if(topo.cpu_count>0){
        /* Undo an earlier pin if pinning was just disabled. */
        applied=0;if(!affinity_enabled)set_affinity(topo.all_mask);
    }
    return applied;
}
