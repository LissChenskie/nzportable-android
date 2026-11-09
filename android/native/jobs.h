// SPDX-License-Identifier: GPL-2.0-or-later
// Small job pool that respects big.LITTLE. While the game runs, only the
// efficiency-core workers take jobs, so the game thread keeps the fast cores.
// Burst mode (loading screens, startup) lets the performance cores help too.
#ifndef NZP_JOBS_H
#define NZP_JOBS_H
#ifdef __cplusplus
extern "C" {
#endif
/* Creates workers (idempotent). Returns the number of worker threads. */
int nzp_jobs_init(void);
/* Explicit worker counts (efficiency-tier, extra performance-tier); used by tests. */
int nzp_jobs_init_custom(int efficiency_workers, int performance_workers);
void nzp_jobs_shutdown(void);
/* While nonzero, performance-core workers also run jobs. */
void nzp_jobs_set_burst(int enabled);
int nzp_jobs_worker_count(void);
/* Runs fn(0..count-1, ctx), blocking until all finished. The caller helps.
 * Safe to call concurrently from several threads; runs inline without workers. */
void nzp_parallel_for(int count, void (*fn)(int index, void *ctx), void *ctx);
#ifdef __cplusplus
}
#endif
#endif
