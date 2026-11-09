// SPDX-License-Identifier: GPL-2.0-or-later
#define _GNU_SOURCE
#include "../android/native/cpu_topology.c"
#include "../android/native/thread_roles.c"
#include "../android/native/jobs.c"
#include <assert.h>
#include <stdio.h>
#include <sys/stat.h>
#include <stdatomic.h>
static char root[128];
static void put(int cpu,const char *file,unsigned v){
    char dir[512],path[600];
    snprintf(dir,sizeof(dir),"%s/cpu%d",root,cpu);mkdir(dir,0755);
    if(strchr(file,'/')){char sub[600];snprintf(sub,sizeof(sub),"%s/cpufreq",dir);mkdir(sub,0755);}
    snprintf(path,sizeof(path),"%s/%s",dir,file);FILE *f=fopen(path,"w");fprintf(f,"%u\n",v);fclose(f);
}
static void fresh(const char *name){snprintf(root,sizeof(root),"/tmp/nzp_topo_%s_%d",name,(int)getpid());
    char cmd[600];snprintf(cmd,sizeof(cmd),"rm -rf %s && mkdir -p %s",root,root);assert(system(cmd)==0);}
static atomic_int sum,calls;
static void add(int i,void *ctx){(void)ctx;atomic_fetch_add(&sum,i);atomic_fetch_add(&calls,1);}
static void *concurrent(void *arg){
    for(int r=0;r<50;r++){atomic_int local=0;(void)local;nzp_parallel_for(37,add,arg);}
    return NULL;
}
static pthread_t seen[64];static int nseen;static pthread_mutex_t seen_lock=PTHREAD_MUTEX_INITIALIZER;
static void note(int i,void *ctx){(void)i;(void)ctx;pthread_mutex_lock(&seen_lock);
    for(int k=0;k<nseen;k++)if(pthread_equal(seen[k],pthread_self())){pthread_mutex_unlock(&seen_lock);return;}
    seen[nseen++]=pthread_self();pthread_mutex_unlock(&seen_lock);
    struct timespec ts={0,2000000};nanosleep(&ts,NULL);}
int main(void){
    nzp_cpu_topology t;
    /* Galaxy A51 (Exynos 9611): cpu0-3 Cortex-A53 1.7 GHz, cpu4-7 Cortex-A73 2.3 GHz. */
    fresh("a51");
    for(int i=0;i<8;i++)put(i,"cpufreq/cpuinfo_max_freq",i<4?1704000:2314000);
    nzp_cpu_topology_detect(root,"/nonexistent",&t);
    assert(t.cpu_count==8&&t.heterogeneous&&t.eff_mask==0x0F&&t.perf_mask==0xF0&&t.perf_count==4&&t.eff_count==4);
    assert(strcmp(t.source,"cpuinfo_max_freq")==0);
    /* cpu_capacity wins over frequency when present. */
    for(int i=0;i<8;i++)put(i,"cpu_capacity",i<4?485:1024);
    nzp_cpu_topology_detect(root,"/nonexistent",&t);assert(strcmp(t.source,"cpu_capacity")==0&&t.perf_mask==0xF0);
    /* 1+3+4 flagship: prime and big are both "performance". */
    fresh("flagship");
    unsigned f3[8]={1800000,1800000,1800000,1800000,2420000,2420000,2420000,2840000};
    for(int i=0;i<8;i++)put(i,"cpufreq/cpuinfo_max_freq",f3[i]);
    nzp_cpu_topology_detect(root,"/nonexistent",&t);assert(t.eff_mask==0x0F&&t.perf_mask==0xF0);
    /* Homogeneous and a tiny frequency spread are not split. */
    fresh("homog");for(int i=0;i<4;i++)put(i,"cpufreq/cpuinfo_max_freq",i==3?2050000:2000000);
    nzp_cpu_topology_detect(root,"/nonexistent",&t);assert(!t.heterogeneous&&t.perf_count==4&&t.eff_count==0&&t.perf_mask==0xF);
    /* Hidden sysfs data: fall back to /proc/cpuinfo core parts (A53 0xd03 + A73 0xd09). */
    fresh("hidden");for(int i=0;i<8;i++){char d[600];snprintf(d,sizeof(d),"%s/cpu%d",root,i);mkdir(d,0755);put(i,"online",1);}
    char ci[300];snprintf(ci,sizeof(ci),"%s/cpuinfo",root);FILE *f=fopen(ci,"w");
    for(int i=0;i<8;i++){fprintf(f,"processor\t: %d\nBogoMIPS\t: 26.00\nCPU part\t: 0x%x\n\n",i,i<4?0xd03:0xd09);}
    fclose(f);
    nzp_cpu_topology_detect(root,ci,&t);assert(t.heterogeneous&&t.perf_mask==0xF0&&strcmp(t.source,"cpuinfo_part")==0);
    /* Nothing readable at all: safe homogeneous default. */
    nzp_cpu_topology_detect("/nonexistent","/nonexistent",&t);assert(!t.heterogeneous&&t.cpu_count>=1);

    /* Thread roles on this machine must not crash, whatever the topology. */
    nzp_threads_init();nzp_thread_apply_role(NZP_ROLE_GAME);nzp_thread_apply_role(NZP_ROLE_BURST);
    nzp_threads_set_affinity_enabled(0);nzp_thread_apply_role(NZP_ROLE_BACKGROUND);

    /* Jobs: run the same checks with real workers regardless of the host CPU count. */
    for(int pass=0;pass<2;pass++){
    int eff=pass?3:2,perf=pass?2:1;
    int workers=nzp_jobs_init_custom(eff,perf);assert(workers==eff+perf&&workers==nzp_jobs_worker_count());
    assert(nzp_jobs_init_custom(9,9)==workers);
    atomic_store(&sum,0);atomic_store(&calls,0);
    nzp_parallel_for(1000,add,NULL);assert(atomic_load(&calls)==1000&&atomic_load(&sum)==999*1000/2);
    nzp_parallel_for(0,add,NULL);nzp_parallel_for(1,add,NULL);assert(atomic_load(&calls)==1001);
    pthread_t th[4];atomic_store(&sum,0);atomic_store(&calls,0);
    nzp_jobs_set_burst(1);
    for(int i=0;i<4;i++)pthread_create(&th[i],NULL,concurrent,NULL);
    for(int i=0;i<4;i++)pthread_join(th[i],NULL);
    assert(atomic_load(&calls)==4*50*37&&atomic_load(&sum)==4*50*(36*37/2));
    /* Without burst only efficiency workers plus the caller may participate. */
    nzp_jobs_set_burst(0);nseen=0;nzp_parallel_for(64,note,NULL);
    assert(nseen<=eff+1&&nseen>=2);
    /* With burst the performance workers join in as well. */
    nzp_jobs_set_burst(1);nseen=0;nzp_parallel_for(200,note,NULL);assert(nseen>=2&&nseen<=eff+perf+1);
    nzp_jobs_shutdown();assert(nzp_jobs_worker_count()==0);
    nzp_parallel_for(10,add,NULL); /* still works inline after shutdown */
    }
    assert(nzp_jobs_init()>=0);nzp_jobs_shutdown();
    puts("CPU topology, thread roles and big.LITTLE job pool tests passed");
    return 0;
}
