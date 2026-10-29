/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "../third_party/vril/source/platform/sdl/net_lan_utils.h"
#include <stdio.h>
#include <string.h>

static int address_ok(const char *value,unsigned a,unsigned b,unsigned c,
                      unsigned d,unsigned expected_port)
{
    unsigned ip[4],port;
    return LAN_ParseIPv4(value,ip,&port,26000) &&
        ip[0]==a&&ip[1]==b&&ip[2]==c&&ip[3]==d&&port==expected_port;
}
static int address_bad(const char *value)
{
    unsigned ip[4],port;
    return !LAN_ParseIPv4(value,ip,&port,26000);
}
int main(void)
{
    int errors=0;
#define CHECK(cond) do { if(!(cond)){fprintf(stderr,"LAN test failed at %d: %s\n",__LINE__,#cond);errors++;}}while(0)
    CHECK(address_ok("192.168.43.1",192,168,43,1,26000));
    CHECK(address_ok("192.168.43.1:26000",192,168,43,1,26000));
    CHECK(address_ok("172.20.10.1:27015",172,20,10,1,27015));
    CHECK(address_ok("10.0.0.42:65535",10,0,0,42,65535));
    CHECK(address_bad("192.168.1.1:0"));
    CHECK(address_bad("192.168.1.1:65536"));
    CHECK(address_bad("192.168.1.1:-1"));
    CHECK(address_bad("192.168.1.1:26000;disconnect"));
    CHECK(address_bad("192.168.1.1:26000junk"));
    CHECK(address_bad("192.168.1"));
    CHECK(address_bad("192.168.1.999"));
    CHECK(address_bad("300.1.1.1"));
    CHECK(address_bad("1.2.3.4.5"));
    CHECK(address_bad("1.2.3.4:"));
    CHECK(address_bad("1.2.3.4:123456"));
    CHECK(address_bad(" 1.2.3.4"));
    CHECK(address_bad("0.0.0.0"));
    CHECK(address_bad("255.255.255.255"));
    CHECK(address_bad(""));
    CHECK(LAN_SubnetBroadcast(0xc0a82b04u,0xffffff00u)==0xc0a82bffu);
    CHECK(LAN_SubnetBroadcast(0xac140a03u,0xfffffff0u)==0xac140a0fu);
    CHECK(LAN_SubnetBroadcast(0x0a000102u,0xffff0000u)==0x0a00ffffu);
    if(errors)return 1;
    puts("LAN IPv4 parser, port safety and directed broadcast tests passed");
    return 0;
}
