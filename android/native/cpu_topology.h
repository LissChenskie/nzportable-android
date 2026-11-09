// SPDX-License-Identifier: GPL-2.0-or-later
// CPU cluster detection for big.LITTLE phones (e.g. 4x Cortex-A73 + 4x A53).
// Pure C + stdio so it can be unit tested against a fake sysfs tree.
#ifndef NZP_CPU_TOPOLOGY_H
#define NZP_CPU_TOPOLOGY_H
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
#define NZP_MAX_CPUS 64
typedef struct {
    int cpu_count;          /* logical CPUs found (<= NZP_MAX_CPUS) */
    int perf_count;         /* "big"/prime cores (all cores if homogeneous) */
    int eff_count;          /* efficiency ("LITTLE") cores, 0 if homogeneous */
    int heterogeneous;
    uint64_t all_mask, perf_mask, eff_mask;
    unsigned key[NZP_MAX_CPUS]; /* capacity, max kHz or part rank used to classify */
    const char *source;     /* "cpu_capacity", "cpuinfo_max_freq", "scaling_max_freq", "cpuinfo_part", "none" */
} nzp_cpu_topology;
/* cpu_root defaults to /sys/devices/system/cpu, cpuinfo to /proc/cpuinfo (NULL).
 * Never fails: unreadable data yields a homogeneous topology. */
void nzp_cpu_topology_detect(const char *cpu_root, const char *cpuinfo, nzp_cpu_topology *out);
/* Pure classifier; keys[i]==0 means unknown. Exposed for tests. */
void nzp_cpu_topology_classify(const unsigned *keys, int count, nzp_cpu_topology *out);
#ifdef __cplusplus
}
#endif
#endif
