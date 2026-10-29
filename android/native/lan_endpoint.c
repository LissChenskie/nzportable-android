// SPDX-License-Identifier: GPL-2.0-or-later
// No Android, SDL, or engine dependencies: audited IPv4:port parser.
#include "lan_endpoint.h"
#include <stddef.h>
int lan_endpoint_valid(const char *value) {
    int octets=0,port=26000;const char *p=value;
    if(!p||!*p)return 0;
    while(octets<4) {
        int n=0,digits=0;
        while(*p>='0'&&*p<='9') {
            if(++digits>3)return 0;
            n=n*10+(*p++-'0');
        }
        if(!digits||n>255)return 0;
        octets++;
        if(octets<4&&*p++!='.')return 0;
    }
    if(*p==':') {
        int digits=0;port=0;p++;
        while(*p>='0'&&*p<='9') {
            if(++digits>5)return 0;
            port=port*10+(*p++-'0');
        }
        if(!digits)return 0;
    }
    return *p=='\0'&&port>0&&port<=65535;
}
