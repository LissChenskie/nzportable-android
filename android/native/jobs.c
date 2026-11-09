// SPDX-License-Identifier: GPL-2.0-or-later
#include "jobs.h"
#include "thread_roles.h"
#include <pthread.h>
#include <stdlib.h>

typedef struct batch {
    void (*fn)(int,void*);void *ctx;int count,next,done;struct batch *link;
} batch_t;
typedef struct {pthread_t thread;int perf_tier;} worker_t;
static pthread_mutex_t lock=PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t work_cv=PTHREAD_COND_INITIALIZER,done_cv=PTHREAD_COND_INITIALIZER;
static batch_t *batches;
static worker_t *workers;
static int worker_total,quit,burst;

/* Caller holds lock. perf_tier workers only run during burst. */
static batch_t *claim(int perf_tier,int *index){
    batch_t *b;
    if(perf_tier&&!burst)return NULL;
    for(b=batches;b;b=b->link)if(b->next<b->count){*index=b->next++;return b;}
    return NULL;
}
static void run_one(batch_t *b,int index){
    pthread_mutex_unlock(&lock);
    b->fn(index,b->ctx);
    pthread_mutex_lock(&lock);
    if(++b->done==b->count)pthread_cond_broadcast(&done_cv);
}
static void *worker_main(void *arg){
    worker_t *w=(worker_t*)arg;int index;batch_t *b;
    nzp_thread_apply_role(w->perf_tier?NZP_ROLE_BURST:NZP_ROLE_BACKGROUND);
    pthread_mutex_lock(&lock);
    while(!quit){
        b=claim(w->perf_tier,&index);
        if(b)run_one(b,index);
        else pthread_cond_wait(&work_cv,&lock);
    }
    pthread_mutex_unlock(&lock);
    return NULL;
}
int nzp_jobs_init_custom(int eff,int perf_extra){
    int i;
    if(eff+perf_extra<=0)return 0;
    pthread_mutex_lock(&lock);
    if(workers){int n=worker_total;pthread_mutex_unlock(&lock);return n;}
    workers=(worker_t*)calloc((size_t)(eff+perf_extra),sizeof(worker_t));
    quit=0;worker_total=0;
    if(workers){
        for(i=0;i<eff+perf_extra;i++){
            workers[worker_total].perf_tier=i>=eff;
            if(pthread_create(&workers[worker_total].thread,NULL,worker_main,&workers[worker_total])==0)worker_total++;
            else break;
        }
    }
    i=worker_total;
    pthread_mutex_unlock(&lock);
    return i;
}
int nzp_jobs_init(void){
    const nzp_cpu_topology *t=nzp_threads_init();int eff,perf_extra;
    /* Debug/test override: NZP_JOBS_WORKERS=n forces n workers (0 = run everything inline). */
    const char *forced=getenv("NZP_JOBS_WORKERS");
    if(forced&&forced[0]>='0'&&forced[0]<='9')return nzp_jobs_init_custom(atoi(forced),0);
    if(t->heterogeneous){eff=t->eff_count;perf_extra=t->perf_count>1?t->perf_count-1:0;}
    else{eff=t->cpu_count>1?t->cpu_count-1:0;perf_extra=0;} /* all cores are equal: always eligible */
    return nzp_jobs_init_custom(eff,perf_extra);
}
void nzp_jobs_shutdown(void){
    int i,n;worker_t *w;
    pthread_mutex_lock(&lock);
    w=workers;n=worker_total;quit=1;pthread_cond_broadcast(&work_cv);
    pthread_mutex_unlock(&lock);
    for(i=0;i<n;i++)pthread_join(w[i].thread,NULL);
    pthread_mutex_lock(&lock);
    free(workers);workers=NULL;worker_total=0;quit=0;burst=0;
    pthread_mutex_unlock(&lock);
}
void nzp_jobs_set_burst(int enabled){
    pthread_mutex_lock(&lock);burst=enabled!=0;if(burst)pthread_cond_broadcast(&work_cv);pthread_mutex_unlock(&lock);
}
int nzp_jobs_worker_count(void){int n;pthread_mutex_lock(&lock);n=worker_total;pthread_mutex_unlock(&lock);return n;}
void nzp_parallel_for(int count,void (*fn)(int,void*),void *ctx){
    batch_t b;int index,i;
    if(count<=0)return;
    pthread_mutex_lock(&lock);
    if(count==1||!workers||worker_total==0){
        pthread_mutex_unlock(&lock);
        for(i=0;i<count;i++)fn(i,ctx);
        return;
    }
    b.fn=fn;b.ctx=ctx;b.count=count;b.next=0;b.done=0;b.link=batches;batches=&b;
    pthread_cond_broadcast(&work_cv);
    /* The caller works too (it is already on a fast core). */
    while(b.next<b.count){index=b.next++;run_one(&b,index);}
    while(b.done<b.count)pthread_cond_wait(&done_cv,&lock);
    {batch_t **p=&batches;while(*p!=&b)p=&(*p)->link;*p=b.link;}
    pthread_mutex_unlock(&lock);
}
