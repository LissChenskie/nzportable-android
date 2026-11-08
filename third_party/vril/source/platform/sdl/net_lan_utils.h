/* SPDX-License-Identifier: GPL-2.0-or-later
 * Pure portable helpers for LAN IP parsing and IPv4 subnet broadcast.
 */
#ifndef NZP_NET_LAN_UTILS_H
#define NZP_NET_LAN_UTILS_H
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <errno.h>

/* Reject ambiguous octets, suffixes and commands before creating any
 * "connect" command text. Works without libc sscanf or a network interface. */
static inline int LAN_ParseIPv4(const char *address, unsigned octet[4],
                         unsigned *port, unsigned default_port)
{
    const char *p=address;
    unsigned n=0;
    if(!p || !port || !octet || default_port==0 || default_port>65535)return 0;
    for(int i=0;i<4;i++){
        n=0;
        int digits=0;
        while(*p>='0'&&*p<='9'){
            if(++digits>3)return 0;
            n=n*10+(unsigned)(*p++-'0');
        }
        if(!digits||n>255)return 0;
        octet[i]=n;
        if(i<3){if(*p++!='.')return 0;}
    }
    if(*p==':'){
        p++;
        n=0;
        int digits=0;
        while(*p>='0'&&*p<='9'){
            if(++digits>5)return 0;
            n=n*10+(unsigned)(*p++-'0');
        }
        if(!digits||n==0||n>65535)return 0;
        *port=n;
    }else *port=default_port;
    if(*p)return 0;
    if((octet[0]==0&&octet[1]==0&&octet[2]==0&&octet[3]==0)||
       octet[0]==255)return 0;
    return 1;
}
/* ip and mask use the same byte order; this identity is endian-neutral. */
static inline uint32_t LAN_SubnetBroadcast(uint32_t ip,uint32_t mask)
{
    return (ip & mask) | ~mask;
}

/* ---- Mobile (Android hotspot / Wi-Fi) helpers ------------------------------ */

/* Interface descriptor filled from getifaddrs(); host byte order IPv4. */
typedef struct { char name[16]; uint32_t ip, mask; int up, loopback; } lan_iface_t;

static inline int LAN_NameHasPrefix(const char *name,const char *prefix)
{
    return strncmp(name,prefix,strlen(prefix))==0;
}
/* Cellular modems and VPN/tunnel devices never carry the local game network.
 * On a hotspot host rmnet* is typically listed first and was picked as "my
 * address", which advertised an unreachable carrier-NAT address. */
static inline int LAN_IsNonLocalInterface(const char *name)
{
    static const char *const prefixes[]={"rmnet","v4-rmnet","ccmni","v4-ccmni","pdp","ppp","clat",
        "dummy","tun","ipsec","wwan","seth","r_rmnet","rev_rmnet","ifb","sit","ip6tnl",NULL};
    for(int i=0;prefixes[i];i++)if(LAN_NameHasPrefix(name,prefixes[i]))return 1;
    return 0;
}
static inline int LAN_IsPrivateIPv4(uint32_t ip)
{
    return (ip>>24)==10 || (ip>>20)==0xAC1 /* 172.16/12 */ || (ip>>16)==0xC0A8;
}
/* <0: never use. Higher is a better candidate for "this device's LAN address". */
static inline int LAN_InterfaceScore(const lan_iface_t *i)
{
    int score=1;
    if(!i->up||i->loopback||i->ip==0)return -1;
    if(LAN_IsNonLocalInterface(i->name))return -1;
    if(LAN_IsPrivateIPv4(i->ip))score+=10;
    else if((i->ip>>16)==0xA9FE)score+=2; /* 169.254/16 link-local */
    if(LAN_NameHasPrefix(i->name,"wlan")||LAN_NameHasPrefix(i->name,"p2p"))score+=5;
    if(LAN_NameHasPrefix(i->name,"ap")||LAN_NameHasPrefix(i->name,"swlan")||
       LAN_NameHasPrefix(i->name,"softap"))score+=7; /* Wi-Fi hotspot (AP) interface */
    if(LAN_NameHasPrefix(i->name,"eth")||LAN_NameHasPrefix(i->name,"rndis")||
       LAN_NameHasPrefix(i->name,"usb"))score+=3;
    return score;
}
/* Pick the interface to advertise or to use as source. With peer_ip the
 * interface whose subnet contains the peer wins (the address that peer can
 * actually reach, even with cellular + hotspot + Wi-Fi all active). */
static inline int LAN_PickInterface(const lan_iface_t *list,int count,uint32_t peer_ip)
{
    int best=-1,best_score=-1;
    for(int i=0;i<count;i++){
        int score=LAN_InterfaceScore(&list[i]);
        if(score<0)continue;
        if(peer_ip&&list[i].mask&&((list[i].ip&list[i].mask)==(peer_ip&list[i].mask)))score+=100;
        if(score>best_score){best_score=score;best=i;}
    }
    return best;
}
/* Errors that mean "this packet was lost", never "the connection is dead":
 * Wi-Fi power save / roaming / hotspot toggling produce these transiently. */
static inline int LAN_IsTransientSocketErrno(int err)
{
    return err==EAGAIN||err==EWOULDBLOCK||err==EINTR||err==ENETUNREACH||err==EHOSTUNREACH||
           err==ENETDOWN||err==ENOBUFS||err==ECONNREFUSED||err==ECONNRESET||err==EADDRNOTAVAIL||
           err==EPERM||err==ENOMEM||err==EPROTO;
}

/* Adaptive retransmit timer (RFC 6298 shaped) for Quake's stop-and-wait
 * reliable channel. The original fixed 1.0 s turns every lost reliable packet
 * into a one second stall on Wi-Fi. */
#define LAN_RTO_MIN 0.12
#define LAN_RTO_MAX 1.0
#define LAN_RTO_INITIAL 0.40
static inline double LAN_RtoClamp(double v){return v<LAN_RTO_MIN?LAN_RTO_MIN:v>LAN_RTO_MAX?LAN_RTO_MAX:v;}
/* Only call with samples from packets that were never retransmitted (Karn). */
static inline void LAN_RtoSample(double *srtt,double *rttvar,double *rto,double sample)
{
    double err;
    if(sample<0)sample=0;
    if(*srtt<=0){*srtt=sample;*rttvar=sample*0.5;}
    else{
        err=sample-*srtt;if(err<0)err=-err;
        *rttvar=0.75*(*rttvar)+0.25*err;
        *srtt=0.875*(*srtt)+0.125*sample;
    }
    *rto=LAN_RtoClamp(*srtt+4.0*(*rttvar)+0.02);
}
static inline double LAN_RtoBackoff(double rto){return LAN_RtoClamp(rto*2.0);}
#endif
