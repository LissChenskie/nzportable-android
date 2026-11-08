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
/* 0: bound or nothing to bind; -1: Android refused (socket keeps default routing). */
int NZP_LanNetwork_BindSocket(int fd);
unsigned NZP_LanNetwork_BindCount(void); /* diagnostics/tests */
#ifdef __cplusplus
}
#endif
#endif
