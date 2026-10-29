#include "../android/native/lan_endpoint.h"
#include <assert.h>
#include <stdio.h>
int main(void) {
    const char *ok[]={"192.168.43.1","10.0.0.1:26000","172.20.10.1:1",
                     "192.168.1.2:65535","0.0.0.0:26000"};
    const char *bad[]={"", "localhost", "192.168.1", "192.168.1.256",
        "192.168.1.1:", "192.168.1.1:0", "192.168.1.1:65536",
        "192.168.1.1;quit", "192.168.1.1\\ndisconnect", "256.1.1.1",
        "1.2.3.4:abc","1.2.3.4:1 extra","1.2.3.4:99999999999"};
    for(size_t i=0;i<sizeof(ok)/sizeof(*ok);i++) assert(lan_endpoint_valid(ok[i]));
    for(size_t i=0;i<sizeof(bad)/sizeof(*bad);i++) assert(!lan_endpoint_valid(bad[i]));
    puts("LAN IPv4 endpoint validation: PASS");
    return 0;
}
