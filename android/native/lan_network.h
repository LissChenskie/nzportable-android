// SPDX-License-Identifier: GPL-2.0-or-later
// Android multi-network support for LAN play. Java (LanNetwork) publishes the
// current Wi-Fi Network handle; client UDP sockets are bound to it so game
// traffic to a hotspot address does not leave through mobile data when the
// hotspot has no internet (Android then makes cellular the default network).
#ifndef NZP_LAN_NETWORK_H
#define NZP_LAN_NETWORK_H
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
void NZP_LanNetwork_Set(uint64_t network_handle); /* 0 = no Wi-Fi network */
uint64_t NZP_LanNetwork_Get(void);
/* Incremented every time the published handle changes. Sockets remember the
 * generation they were bound at and re-bind when it differs (Wi-Fi reconnect,
 * hotspot toggled, handle published after the socket was created). */
unsigned NZP_LanNetwork_Generation(void);
/* 0: bound or nothing to bind; -1: Android refused (socket keeps default routing).
 * errno is preserved from android_setsocknetwork() on failure. */
int NZP_LanNetwork_BindSocket(int fd);
/* Remove a previous binding (Wi-Fi network gone): back to default routing.
 * 0 on success or when there is nothing to do, -1 on error (errno set). */
int NZP_LanNetwork_ClearSocket(int fd);
unsigned NZP_LanNetwork_BindCount(void); /* diagnostics/tests */

/* Single log tag "NZP_LAN" (logcat: adb logcat -s NZP_LAN). Not for per-frame use.
 * On non-Android host builds it prints to stderr only if NZP_LAN_LOG is set. */
void NZP_LanLog(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
#ifdef __cplusplus
}
#endif
#endif
