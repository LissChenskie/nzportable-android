// SPDX-License-Identifier: GPL-2.0-or-later
#include "../third_party/vril/source/platform/sdl/net_lan_utils.h"
#include "../android/native/lan_network.c" /* host build: no android_setsocknetwork */
#include <assert.h>
#include <stdio.h>
#define IP(a,b,c,d) (((uint32_t)(a)<<24)|((b)<<16)|((c)<<8)|(d))
static lan_iface_t mk(const char *n,uint32_t ip,uint32_t mask,int up,int lo){
    lan_iface_t i;memset(&i,0,sizeof(i));strncpy(i.name,n,sizeof(i.name)-1);i.ip=ip;i.mask=mask;i.up=up;i.loopback=lo;return i;}
int main(void){
    const uint32_t m24=IP(255,255,255,0);
    /* Hotspot host: cellular is listed first (this used to be chosen), plus a
     * VPN and loopback. The AP interface must win. */
    lan_iface_t host[]={mk("lo",IP(127,0,0,1),IP(255,0,0,0),1,1),mk("rmnet_data0",IP(10,64,12,9),IP(255,255,255,240),1,0),
        mk("tun0",IP(10,8,0,2),m24,1,0),mk("swlan0",IP(192,168,43,1),m24,1,0)};
    assert(LAN_PickInterface(host,4,0)==3);
    assert(LAN_PickInterface(host,4,IP(192,168,43,77))==3);
    /* Pixel-style AP name and a client on plain Wi-Fi with mobile data on. */
    lan_iface_t pixel[]={mk("rmnet0",IP(100,70,1,2),IP(255,255,255,0),1,0),mk("ap0",IP(10,63,155,1),m24,1,0)};
    assert(LAN_PickInterface(pixel,2,0)==1);
    lan_iface_t client[]={mk("rmnet_data1",IP(10,1,1,1),m24,1,0),mk("wlan0",IP(192,168,43,23),m24,1,0)};
    assert(LAN_PickInterface(client,2,IP(192,168,43,1))==1);
    /* Both Wi-Fi client and hotspot active: peer's subnet decides. */
    lan_iface_t both[]={mk("wlan0",IP(192,168,1,20),m24,1,0),mk("ap0",IP(192,168,43,1),m24,1,0)};
    assert(LAN_PickInterface(both,2,IP(192,168,1,7))==0);
    assert(LAN_PickInterface(both,2,IP(192,168,43,50))==1);
    assert(LAN_PickInterface(both,2,0)==1); /* no peer: advertise the hotspot */
    lan_iface_t down[]={mk("wlan0",IP(192,168,1,20),m24,0,0),mk("lo",IP(127,0,0,1),IP(255,0,0,0),1,1)};
    assert(LAN_PickInterface(down,2,0)==-1);
    assert(LAN_IsPrivateIPv4(IP(172,16,0,1))&&LAN_IsPrivateIPv4(IP(172,31,255,1))&&!LAN_IsPrivateIPv4(IP(172,32,0,1)));
    assert(LAN_IsPrivateIPv4(IP(192,168,0,5))&&!LAN_IsPrivateIPv4(IP(8,8,8,8)));
    /* Transient socket errors must not drop a connection; real ones must. */
    assert(LAN_IsTransientSocketErrno(ENETUNREACH)&&LAN_IsTransientSocketErrno(ECONNREFUSED)&&LAN_IsTransientSocketErrno(EAGAIN));
    assert(!LAN_IsTransientSocketErrno(EBADF)&&!LAN_IsTransientSocketErrno(EINVAL));
    /* RTO: converges to a small value on a 30 ms link, backs off, stays bounded. */
    double srtt=0,var=0,rto=LAN_RTO_INITIAL;
    for(int i=0;i<40;i++)LAN_RtoSample(&srtt,&var,&rto,0.030);
    assert(rto>=LAN_RTO_MIN&&rto<0.2);
    double r=rto;for(int i=0;i<10;i++)r=LAN_RtoBackoff(r);
    assert(r==LAN_RTO_MAX);
    LAN_RtoSample(&srtt,&var,&rto,0.4); /* one latency spike widens the timer */
    assert(rto>0.1&&rto<=LAN_RTO_MAX);
    double s2=0,v2=0,r2=LAN_RTO_INITIAL;LAN_RtoSample(&s2,&v2,&r2,5.0);assert(r2==LAN_RTO_MAX);
    /* Wi-Fi network handle plumbing: nothing is bound until Java publishes one. */
    assert(NZP_LanNetwork_BindSocket(5)==0&&NZP_LanNetwork_BindCount()==0);
    NZP_LanNetwork_Set(0x1234567890ULL);assert(NZP_LanNetwork_Get()==0x1234567890ULL);
    assert(NZP_LanNetwork_BindSocket(5)==0&&NZP_LanNetwork_BindCount()==1);
    assert(NZP_LanNetwork_BindSocket(-1)==0&&NZP_LanNetwork_BindCount()==1);
    NZP_LanNetwork_Set(0);assert(NZP_LanNetwork_BindSocket(5)==0&&NZP_LanNetwork_BindCount()==1);
    puts("mobile LAN interface selection, transient errors and adaptive RTO tests passed");
    return 0;
}
