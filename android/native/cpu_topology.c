// SPDX-License-Identifier: GPL-2.0-or-later
#include "cpu_topology.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static int read_uint(const char *path, unsigned *value){
    FILE *f=fopen(path,"r");unsigned long v;int ok;
    if(!f)return 0;
    ok=fscanf(f,"%lu",&v)==1;fclose(f);
    if(ok)*value=(unsigned)v;
    return ok;
}
/* Rough relative performance of ARM core parts for the /proc/cpuinfo fallback
 * (some Android versions hide cpufreq/cpu_capacity from apps). */
static unsigned part_rank(unsigned part){
    switch(part){
    case 0xd03:case 0xd04:case 0xd05:case 0xd46:case 0xd80:case 0xd02:case 0xd01:case 0xc07:return 100; /* A53 A35 A55 A510 A520 */
    case 0xd07:case 0xd08:return 180;                         /* A57 A72 */
    case 0xd09:case 0xd0a:return 220;                         /* A73 A75 */
    case 0xd0b:case 0xd0d:case 0xd41:case 0xd47:case 0xd4d:case 0xd81:return 260; /* A76 A77 A78 A710 A715 A720 */
    case 0xd44:case 0xd48:case 0xd4e:case 0xd4f:case 0xd82:case 0xd85:return 300; /* X1 X2 X3 X4 */
    default:return 0;
    }
}
void nzp_cpu_topology_classify(const unsigned *keys,int count,nzp_cpu_topology *out){
    unsigned uniq[NZP_MAX_CPUS];int nu=0,i,j,split=-1;double best=1.12; /* need >=12% gap */
    int known=1;
    if(count>NZP_MAX_CPUS)count=NZP_MAX_CPUS;
    out->cpu_count=count;out->heterogeneous=0;out->perf_count=count;out->eff_count=0;
    out->all_mask=out->perf_mask=count>=64?~0ULL:((1ULL<<count)-1);out->eff_mask=0;
    for(i=0;i<count;i++){out->key[i]=keys[i];if(!keys[i])known=0;}
    if(!known||count<2)return;
    for(i=0;i<count;i++){for(j=0;j<nu;j++)if(uniq[j]==keys[i])break;if(j==nu)uniq[nu++]=keys[i];}
    for(i=0;i<nu;i++)for(j=i+1;j<nu;j++)if(uniq[j]>uniq[i]){unsigned t=uniq[i];uniq[i]=uniq[j];uniq[j]=t;}
    /* Split at the largest relative gap: 4+4 has one, 1+3+4 splits big from LITTLE. */
    for(i=0;i+1<nu;i++){double r=(double)uniq[i]/(double)uniq[i+1];if(r>=best){best=r;split=i;}}
    if(split<0)return;
    out->heterogeneous=1;out->perf_mask=out->eff_mask=0;out->perf_count=out->eff_count=0;
    for(i=0;i<count;i++){
        if(keys[i]>=uniq[split]){out->perf_mask|=1ULL<<i;out->perf_count++;}
        else{out->eff_mask|=1ULL<<i;out->eff_count++;}
    }
}
void nzp_cpu_topology_detect(const char *cpu_root,const char *cpuinfo,nzp_cpu_topology *out){
    static const char *const files[]={"cpu_capacity","cpufreq/cpuinfo_max_freq","cpufreq/scaling_max_freq"};
    static const char *const names[]={"cpu_capacity","cpuinfo_max_freq","scaling_max_freq"};
    unsigned keys[NZP_MAX_CPUS];int count=0,s,i;char path[512];
    if(!cpu_root)cpu_root="/sys/devices/system/cpu";
    if(!cpuinfo)cpuinfo="/proc/cpuinfo";
    memset(out,0,sizeof(*out));memset(keys,0,sizeof(keys));out->source="none";
    for(i=0;i<NZP_MAX_CPUS;i++){
        unsigned dummy;
        /* Probe a few entries: cpu0 often has no "online" file and cpufreq may be hidden. */
        snprintf(path,sizeof(path),"%s/cpu%d/cpufreq/cpuinfo_max_freq",cpu_root,i);
        if(read_uint(path,&dummy)){count=i+1;continue;}
        snprintf(path,sizeof(path),"%s/cpu%d/cpu_capacity",cpu_root,i);
        if(read_uint(path,&dummy)){count=i+1;continue;}
        snprintf(path,sizeof(path),"%s/cpu%d/online",cpu_root,i);
        if(read_uint(path,&dummy)){count=i+1;continue;}
        snprintf(path,sizeof(path),"%s/cpu%d/topology/core_id",cpu_root,i);
        if(read_uint(path,&dummy)){count=i+1;continue;}
        break;
    }
    if(!count){long n=sysconf(_SC_NPROCESSORS_CONF);count=n<1?1:n>NZP_MAX_CPUS?NZP_MAX_CPUS:(int)n;}
    for(s=0;s<3;s++){
        int ok=1;
        for(i=0;i<count;i++){
            snprintf(path,sizeof(path),"%s/cpu%d/%s",cpu_root,i,files[s]);
            if(!read_uint(path,&keys[i])||!keys[i]){ok=0;break;}
        }
        if(ok){nzp_cpu_topology_classify(keys,count,out);out->source=names[s];return;}
    }
    /* Fallback: "CPU part" per processor from /proc/cpuinfo. */
    {
        FILE *f=fopen(cpuinfo,"r");char line[256];int cur=-1,seen=0;
        memset(keys,0,sizeof(keys));
        if(f){
            while(fgets(line,sizeof(line),f)){
                unsigned v;
                if(sscanf(line,"processor : %u",&v)==1&&(int)v<NZP_MAX_CPUS)cur=(int)v;
                else if(cur>=0&&strstr(line,"CPU part")){const char *p=strchr(line,':');
                    if(p){v=(unsigned)strtoul(p+1,NULL,16);keys[cur]=part_rank(v);if(keys[cur])seen++;}}
            }
            fclose(f);
        }
        if(seen==count&&count>0){nzp_cpu_topology_classify(keys,count,out);out->source="cpuinfo_part";return;}
    }
    memset(keys,0,sizeof(keys));nzp_cpu_topology_classify(keys,count,out);
}
