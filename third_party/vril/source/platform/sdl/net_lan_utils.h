/* SPDX-License-Identifier: GPL-2.0-or-later
 * Pure portable helpers for LAN IP parsing and IPv4 subnet broadcast.
 */
#ifndef NZP_NET_LAN_UTILS_H
#define NZP_NET_LAN_UTILS_H
#include <stdint.h>
#include <stddef.h>

/* Reject ambiguous octets, suffixes and commands before creating any
 * "connect" command text. Works without libc sscanf or a network interface. */
static int LAN_ParseIPv4(const char *address, unsigned octet[4],
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
static uint32_t LAN_SubnetBroadcast(uint32_t ip,uint32_t mask)
{
    return (ip & mask) | ~mask;
}
#endif
