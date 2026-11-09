// SPDX-License-Identifier: GPL-2.0-or-later
// Thread placement for big.LITTLE CPUs.
#ifndef NZP_THREAD_ROLES_H
#define NZP_THREAD_ROLES_H
#include "cpu_topology.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef enum {
    NZP_ROLE_GAME,       /* engine/render thread: performance cores, raised priority */
    NZP_ROLE_BACKGROUND, /* efficiency cores, lowered priority */
    NZP_ROLE_BURST,      /* work done while the game thread waits (loading): any core */
} nzp_thread_role;
/* Detect topology once (idempotent). Safe to call from any thread. */
const nzp_cpu_topology *nzp_threads_init(void);
/* 0 disables pinning (roles still adjust priority); default 1. */
void nzp_threads_set_affinity_enabled(int enabled);
/* Applies to the calling thread. Best effort; returns 1 if affinity was applied. */
int nzp_thread_apply_role(nzp_thread_role role);
#ifdef __cplusplus
}
#endif
#endif
