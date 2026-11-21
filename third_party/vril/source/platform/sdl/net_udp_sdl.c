/*
Copyright (C) 1996-1997 Id Software, Inc.

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

See the GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.

*/
// net_udp.c
#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <ws2tcpip.h>
#endif

#include "../../nzportable_def.h"
#include "net_udp.h"

#if !defined(_WIN32)
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <arpa/inet.h>
#endif

#include <sys/fcntl.h>
#include <unistd.h>
#ifdef PLATFORM_ANDROID
#include <ifaddrs.h>
#include <net/if.h>
#include <errno.h>
#include <SDL.h>
#include "lan_network.h"
#endif
#include "net_lan_utils.h"

extern cvar_t hostname;

static int net_acceptsocket = -1;		// socket for fielding new connections
static int net_controlsocket;
static int net_broadcastsocket = 0;
static struct qsockaddr broadcastaddr;

static unsigned long myAddr;
// Android Hotspot and Wi-Fi can have different directed broadcast addresses.
// Refresh each scan: switching networks after app startup must keep discovery alive.
#ifdef PLATFORM_ANDROID
enum { UDP_MAX_IFACES = 24 };
/* Snapshot of active IPv4 interfaces. Re-read on every use: hotspot, Wi-Fi and
 * mobile data come and go while the game is running. */
static int UDP_ListInterfaces(lan_iface_t *out, struct sockaddr_in *broadcast_out)
{
    struct ifaddrs *list = NULL, *it;
    int n = 0;
    if (getifaddrs(&list) != 0) return 0;
    for (it = list; it && n < UDP_MAX_IFACES; it = it->ifa_next) {
        lan_iface_t *i = &out[n];
        if (!it->ifa_addr || it->ifa_addr->sa_family != AF_INET) continue;
        memset(i, 0, sizeof(*i));
        strncpy(i->name, it->ifa_name ? it->ifa_name : "", sizeof(i->name) - 1);
        i->ip = ntohl(((struct sockaddr_in *)it->ifa_addr)->sin_addr.s_addr);
        i->mask = it->ifa_netmask && it->ifa_netmask->sa_family == AF_INET ?
                  ntohl(((struct sockaddr_in *)it->ifa_netmask)->sin_addr.s_addr) : 0;
        i->up = (it->ifa_flags & IFF_UP) != 0;
        i->loopback = (it->ifa_flags & IFF_LOOPBACK) != 0;
        if (broadcast_out) {
            memset(&broadcast_out[n], 0, sizeof(broadcast_out[n]));
            broadcast_out[n].sin_family = AF_INET;
            if ((it->ifa_flags & IFF_BROADCAST) && it->ifa_broadaddr && it->ifa_broadaddr->sa_family == AF_INET)
                broadcast_out[n].sin_addr = ((struct sockaddr_in *)it->ifa_broadaddr)->sin_addr;
            else if (i->mask)
                broadcast_out[n].sin_addr.s_addr = htonl(LAN_SubnetBroadcast(i->ip, i->mask));
        }
        n++;
    }
    freeifaddrs(list);
    return n;
}
/* Address this device should use/advertise; peer (host order, 0 = unknown) selects
 * the interface whose subnet contains it. Returns network-order s_addr or 0. */
static unsigned long UDP_LocalAddress(uint32_t peer_ip)
{
    lan_iface_t ifaces[UDP_MAX_IFACES];
    int n = UDP_ListInterfaces(ifaces, NULL), best = LAN_PickInterface(ifaces, n, peer_ip);
    if (LAN_LEGACY_MODE) { /* original behaviour: first active non-loopback interface */
        best = -1;
        for (int i = 0; i < n; i++)
            if (ifaces[i].up && !ifaces[i].loopback && ifaces[i].ip) { best = i; break; }
    }
    return best >= 0 ? (unsigned long)htonl(ifaces[best].ip) : 0;
}
/* ---- NZP_LAN diagnostics ------------------------------------------------- */
static void UDP_FmtAddr(const struct qsockaddr *a, char out[24])
{
    const struct sockaddr_in *in = (const struct sockaddr_in *)a;
    char ip[16];
    snprintf(out, 24, "%s:%u", LAN_FormatIPv4(ntohl(in->sin_addr.s_addr), ip), (unsigned)ntohs(in->sin_port));
}
/* Dump every IPv4 interface with the verdict of the shared selection logic. */
static void UDP_LogInterfaces(const char *why)
{
    lan_iface_t ifaces[UDP_MAX_IFACES];
    struct sockaddr_in bc[UDP_MAX_IFACES];
    int n = UDP_ListInterfaces(ifaces, bc), best = LAN_PickInterface(ifaces, n, 0);
    NZP_LAN_LOG("interfaces (%s): %d IPv4 entries, advertised=%s", why, n, best >= 0 ? ifaces[best].name : "NONE");
    for (int i = 0; i < n; i++) {
        char ip[16], mk[16], bs[16];
        NZP_LAN_LOG("  %-14s ip=%s mask=%s bcast=%s up=%d lo=%d score=%d%s", ifaces[i].name,
                    LAN_FormatIPv4(ifaces[i].ip, ip), LAN_FormatIPv4(ifaces[i].mask, mk),
                    LAN_FormatIPv4(ntohl(bc[i].sin_addr.s_addr), bs), ifaces[i].up, ifaces[i].loopback,
                    LAN_InterfaceScore(&ifaces[i]), i == best ? "  <== advertised" : "");
    }
}
/* Re-dump only when the interface set actually changed (hotspot/Wi-Fi toggled). */
static void UDP_LogInterfacesIfChanged(const char *why)
{
    static uint32_t last_sig;
    lan_iface_t ifaces[UDP_MAX_IFACES];
    int n = UDP_ListInterfaces(ifaces, NULL);
    uint32_t sig = 2166136261u;
    for (int i = 0; i < n; i++) {
        for (const char *c = ifaces[i].name; *c; c++) sig = (sig ^ (unsigned char)*c) * 16777619u;
        sig = (sig ^ ifaces[i].ip) * 16777619u;
        sig = (sig ^ ifaces[i].mask) * 16777619u;
        sig = (sig ^ (uint32_t)ifaces[i].up) * 16777619u;
    }
    if (sig != last_sig) { last_sig = sig; UDP_LogInterfaces(why); }
}
enum { UDP_OP_SEND, UDP_OP_RECV, UDP_OP_BCAST, UDP_OP_COUNT };
/* One line per (operation, errno) change or every 3 s; suppressed repeats are counted. */
static void UDP_LogSockError(int op, int fd, int err, const struct qsockaddr *peer)
{
    static const char *const names[UDP_OP_COUNT] = { "sendto", "recvfrom", "broadcast" };
    static int last_err[UDP_OP_COUNT];
    static Uint64 last_tick[UDP_OP_COUNT];
    static unsigned suppressed[UDP_OP_COUNT];
    Uint64 now = SDL_GetTicks64();
    char where[24] = "-";
    if (err == last_err[op] && last_tick[op] && now - last_tick[op] < 3000) { suppressed[op]++; return; }
    if (peer) UDP_FmtAddr(peer, where);
    NZP_LAN_LOG("UDP %s error fd=%d peer=%s errno=%d (%s)%s [+%u similar suppressed]", names[op], fd, where, err,
                strerror(err), LAN_IsTransientSocketErrno(err) ? " treated-as-lost-packet" : " FATAL-for-connection",
                suppressed[op]);
    last_err[op] = err; last_tick[op] = now; suppressed[op] = 0;
}

/* Discovery broadcast on every real local network. Cellular and tunnel
 * interfaces are skipped: they only waste airtime and can leak packets. */
static int UDP_SendInterfaceBroadcasts(int socket, byte *buf, int len)
{
    lan_iface_t ifaces[UDP_MAX_IFACES];
    struct sockaddr_in broadcast[UDP_MAX_IFACES];
    int n = UDP_ListInterfaces(ifaces, broadcast), sent = 0;
    for (int i = 0; i < n; i++) {
        struct sockaddr_in dest = broadcast[i];
        char bs[16];
        if (LAN_LEGACY_MODE ? (!ifaces[i].up || ifaces[i].loopback || !ifaces[i].ip) : (LAN_InterfaceScore(&ifaces[i]) < 0)) continue;
        if (dest.sin_addr.s_addr == 0 || dest.sin_addr.s_addr == INADDR_BROADCAST) continue;
        dest.sin_port = htons(net_hostport);
        if (sendto(socket, (const char *)buf, len, 0, (struct sockaddr *)&dest, sizeof(dest)) == len) {
            sent++;
            NZP_LAN_LOG("broadcast sent: %s -> %s:%d (%d bytes)", ifaces[i].name,
                        LAN_FormatIPv4(ntohl(dest.sin_addr.s_addr), bs), net_hostport, len);
        } else {
            int err = errno;
            NZP_LAN_LOG("broadcast FAILED: %s -> %s:%d errno=%d (%s)", ifaces[i].name,
                        LAN_FormatIPv4(ntohl(dest.sin_addr.s_addr), bs), net_hostport, err, strerror(err));
        }
    }
    return sent;
}

/* ---- Wi-Fi Network binding for CLIENT sockets ---------------------------- */
/* A hotspot host has no Wi-Fi "Network" object, and its per-client game sockets must keep
 * default routing (binding them to some unrelated STA network would send replies the wrong
 * way). That is why binding is skipped while this process is actually serving.
 * NOTE: the previous test was only "accept socket open"; a client that hosted earlier and
 * left through a path that did not run "listen 0" kept the accept socket open and then never
 * bound any outgoing socket, so joining a hotspot without internet went out over mobile data. */
static int UDP_IsServing(void)
{
    return net_acceptsocket != -1 && sv.active;
}
enum { UDP_MAX_TRACKED = 8 };
static struct { int used, fd; unsigned generation; int bound; } udp_tracked[UDP_MAX_TRACKED];

static void UDP_ApplyBinding(int idx)
{
    int fd = udp_tracked[idx].fd, rc = 0;
    unsigned gen = NZP_LanNetwork_Generation();
    uint64_t handle = NZP_LanNetwork_Get();
    if (handle) {
        rc = NZP_LanNetwork_BindSocket(fd);
        udp_tracked[idx].bound = (rc == 0);
    } else if (udp_tracked[idx].bound) {
        rc = NZP_LanNetwork_ClearSocket(fd); /* network is gone: drop the stale binding */
        udp_tracked[idx].bound = 0;
    }
    udp_tracked[idx].generation = gen;
    NZP_LAN_LOG("socket fd=%d binding: wifi_handle=%llu gen=%u -> %s", fd, (unsigned long long)handle, gen,
                handle ? (rc == 0 ? "bound to Wi-Fi network" : "BIND FAILED, default routing") : "no Wi-Fi network, default routing");
}
/* Register an outgoing/discovery socket and bind it to the current Wi-Fi network. */
static void UDP_BindClientSocket(int fd)
{
    int i, free_slot = -1;
    if (LAN_LEGACY_MODE || UDP_IsServing()) return;
    for (i = 0; i < UDP_MAX_TRACKED; i++) {
        if (udp_tracked[i].used && udp_tracked[i].fd == fd) { UDP_ApplyBinding(i); return; }
        if (free_slot < 0 && !udp_tracked[i].used) free_slot = i;
    }
    if (free_slot < 0) { NZP_LAN_LOG("socket fd=%d NOT tracked: table full", fd); NZP_LanNetwork_BindSocket(fd); return; }
    udp_tracked[free_slot].used = 1;
    udp_tracked[free_slot].fd = fd;
    UDP_ApplyBinding(free_slot);
}
static void UDP_UntrackSocket(int fd)
{
    for (int i = 0; i < UDP_MAX_TRACKED; i++)
        if (udp_tracked[i].used && udp_tracked[i].fd == fd) memset(&udp_tracked[i], 0, sizeof(udp_tracked[i]));
}
/* Cheap per-packet check: re-bind when the Wi-Fi Network changed after the socket was
 * bound (Wi-Fi reconnect, hotspot toggled, handle published late). */
static void UDP_RefreshBinding(int fd)
{
    unsigned gen = NZP_LanNetwork_Generation();
    for (int i = 0; i < UDP_MAX_TRACKED; i++)
        if (udp_tracked[i].used && udp_tracked[i].fd == fd) {
            if (udp_tracked[i].generation != gen) UDP_ApplyBinding(i);
            return;
        }
}
/* Used by the LAN menu: same selection logic as the address advertised to peers. */
const char *UDP_BestLocalAddress(void)
{
    static char text[16];
    unsigned long a = UDP_LocalAddress(0);
    text[0] = 0;
    if (a) LAN_FormatIPv4(ntohl((uint32_t)a), text);
    return text;
}
int UDP_IsListening(void) { return net_acceptsocket != -1; }
#endif


#include "net_udp.h"

//=============================================================================

int UDP_Init (void)
{
	#if defined(_WIN32)
	WSADATA wsaData;

	if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0)
		return -1;
	#endif

	struct qsockaddr addr;
	char *colon;

	if (COM_CheckParm ("-noudp"))
		return -1;
	#ifdef PLATFORM_ANDROID
	NZP_LAN_LOG("LAN subsystem start: UDP port %d legacy=%d wifi_handle=%llu", net_hostport,
			LAN_LEGACY_MODE ? 1 : 0, (unsigned long long)NZP_LanNetwork_Get());
	#endif

	#if defined(_WIN32)
		{
			char hostname[256];
			struct hostent *hostentry;

			gethostname(hostname, sizeof(hostname));

			hostentry = gethostbyname(hostname);
			if (hostentry && hostentry->h_addr_list[0])
				myAddr = *(unsigned long *)hostentry->h_addr_list[0];
			else
				myAddr = inet_addr("127.0.0.1");
		}
	#elif defined(PLATFORM_ANDROID)
		// Bionic has no gethostid(). Prefer the Wi-Fi/hotspot interface over cellular.
		myAddr = UDP_LocalAddress(0);
		if (!myAddr) myAddr = htonl(INADDR_LOOPBACK);
	#else
		myAddr = gethostid();
	#endif

	// if the quake hostname isn't set, set it to the machine name
	if (strcmp(hostname.string, "UNNAMED") == 0)
	{
		char local_hostname[NET_NAMELEN] = "localhost";
		gethostname(local_hostname, sizeof(local_hostname));
		Cvar_Set ("hostname", local_hostname);
	}

	if ((net_controlsocket = UDP_OpenSocket (0)) == -1)
	{
		return -1;
	}

	((struct sockaddr_in *)&broadcastaddr)->sin_family = AF_INET;
	((struct sockaddr_in *)&broadcastaddr)->sin_addr.s_addr = INADDR_BROADCAST;
	((struct sockaddr_in *)&broadcastaddr)->sin_port = htons(net_hostport);

	UDP_GetSocketAddr (net_controlsocket, &addr);
	Q_strcpy(my_tcpip_address,  UDP_AddrToString (&addr));
	colon = Q_strrchr (my_tcpip_address, ':');
	if (colon)
		*colon = 0;

	Con_Printf("UDP Initialized\n");
	#ifdef PLATFORM_ANDROID
	UDP_LogInterfacesIfChanged("init");
	#endif
	tcpipAvailable = true;

	return net_controlsocket;
}

//=============================================================================

void UDP_Shutdown (void)
{
	UDP_Listen (false);
	UDP_CloseSocket (net_controlsocket);
}

//=============================================================================

void UDP_Listen (qboolean state)
{
	// enable listening
	if (state)
	{
		if (net_acceptsocket != -1)
			return;
		#ifdef PLATFORM_ANDROID
		UDP_LogInterfaces("listen");
		#endif
		if ((net_acceptsocket = UDP_OpenSocket (net_hostport)) == -1)
		{
		#ifdef PLATFORM_ANDROID
			/* Port busy / socket error: report to the LAN menu (UDP_IsListening) instead of
			 * killing the app. The reason with errno was logged by UDP_OpenSocket. */
			NZP_LAN_LOG("LISTEN FAILED on UDP port %d: this phone cannot host", net_hostport);
			Con_Printf ("Unable to open accept socket (UDP %d)\n", net_hostport);
			return;
		#else
			Sys_Error ("Unable to open accept socket\n");
		#endif
		}
		#ifdef PLATFORM_ANDROID
		{
			struct qsockaddr bound; int blen = sizeof(bound);
			char txt[24];
			memset(&bound, 0, sizeof(bound));
			getsockname(net_acceptsocket, (struct sockaddr *)&bound, (socklen_t *)&blen);
			UDP_FmtAddr(&bound, txt);
			NZP_LAN_LOG("LISTEN OK fd=%d bound=%s (all interfaces, not pinned to a Wi-Fi Network)", net_acceptsocket, txt);
		}
		#endif
		return;
	}

	// disable listening
	if (net_acceptsocket == -1)
		return;
	#ifdef PLATFORM_ANDROID
	NZP_LAN_LOG("LISTEN CLOSED fd=%d", net_acceptsocket);
	#endif
	UDP_CloseSocket (net_acceptsocket);
	net_acceptsocket = -1;
}

//=============================================================================

int UDP_OpenSocket (int port)
{
	int newsocket;
	struct sockaddr_in address;
	#ifdef PLATFORM_ANDROID
	const char *failed_step = "socket";
	#endif

	if ((newsocket = socket(AF_INET, SOCK_DGRAM, 0)) == -1)
	{
		#ifdef PLATFORM_ANDROID
		NZP_LAN_LOG("UDP_OpenSocket port=%d: socket() failed errno=%d (%s)", port, errno, strerror(errno));
		#endif
		return -1;
	}

	#if defined(_WIN32)
	u_long mode = 1;
	if (ioctlsocket(newsocket, FIONBIO, &mode) != 0)
		goto ErrorReturn;
	#else
	int flags = fcntl(newsocket, F_GETFL, 0);
	if ( fcntl(newsocket, F_SETFL, flags | O_NONBLOCK) == -1)
	{
		#ifdef PLATFORM_ANDROID
		failed_step = "fcntl(O_NONBLOCK)";
		#endif
		goto ErrorReturn;
	}
	#endif

#ifdef PLATFORM_ANDROID
	failed_step = "bind";
	if (port == 0)
	{
		if (UDP_IsServing())
			NZP_LAN_LOG("socket fd=%d: server-side peer socket, default routing (not bound to a Wi-Fi Network)", newsocket);
		else
			UDP_BindClientSocket(newsocket);
	}
#endif
	address.sin_family = AF_INET;
	address.sin_addr.s_addr = htonl(INADDR_ANY);
	address.sin_port = htons(port);


	if( bind (newsocket, (void *)&address, sizeof(address)) == -1)
		goto ErrorReturn;

	return newsocket;

ErrorReturn:
	#ifdef PLATFORM_ANDROID
	{
		int err = errno;
		NZP_LAN_LOG("UDP_OpenSocket port=%d: %s failed errno=%d (%s)", port, failed_step, err, strerror(err));
		UDP_UntrackSocket(newsocket);
		errno = err;
	}
	#endif
	#if defined(_WIN32)
	closesocket(newsocket);
	#else
	close (newsocket);
	#endif
	return -1;
}

//=============================================================================

int UDP_CloseSocket (int socket)
{
	if (socket == net_broadcastsocket)
		net_broadcastsocket = 0;
	#ifdef PLATFORM_ANDROID
	UDP_UntrackSocket(socket);
	#endif

	return close (socket);
}


//=============================================================================
/*
============
PartialIPAddress

this lets you type only as much of the net address as required, using
the local network components to fill in the rest
============
*/
static int PartialIPAddress (char *in, struct qsockaddr *hostaddr)
{
	char buff[256];
	char *b;
	int addr;
	int num;
	int mask;
	int run;
	int port;

	buff[0] = '.';
	b = buff;
	strcpy(buff+1, in);
	if (buff[1] == '.')
		b++;

	addr = 0;
	mask=-1;
	while (*b == '.')
	{
		b++;
		num = 0;
		run = 0;
		while (!( *b < '0' || *b > '9'))
		{
		  num = num*10 + *b++ - '0';
		  if (++run > 3)
		  	return -1;
		}
		if ((*b < '0' || *b > '9') && *b != '.' && *b != ':' && *b != 0)
			return -1;
		if (num < 0 || num > 255)
			return -1;
		mask<<=8;
		addr = (addr<<8) + num;
	}

	if (*b++ == ':')
		port = Q_atoi(b);
	else
		port = net_hostport;

	hostaddr->sa_family = AF_INET;
	((struct sockaddr_in *)hostaddr)->sin_port = htons((short)port);
	((struct sockaddr_in *)hostaddr)->sin_addr.s_addr = (myAddr & htonl(mask)) | htonl(addr);

	return 0;
}
//=============================================================================

int UDP_Connect (int socket, struct qsockaddr *addr)
{
	return 0;
}

//=============================================================================

int UDP_CheckNewConnections (void)
{
	char buf[4096];
	
	if (net_acceptsocket == -1)
		return -1;

	if (recvfrom(net_acceptsocket, buf, 4096, MSG_PEEK, NULL, NULL) > 0)
		return net_acceptsocket;
		
	return -1;
}

//=============================================================================

int UDP_Read (int socket, byte *buf, int len, struct qsockaddr *addr)
{
	int addrlen = sizeof (struct qsockaddr);
	int ret;

	#ifdef PLATFORM_ANDROID
	UDP_RefreshBinding(socket);
	#endif
	ret = recvfrom(socket, (char *)buf, len, 0, (struct sockaddr *)addr, (socklen_t*)&addrlen);
	if (ret == -1) {
#ifdef PLATFORM_ANDROID
		// Wi-Fi power save, roaming and hotspot toggling produce transient errors
		// (ENETUNREACH, ECONNREFUSED...). They mean one lost packet, not a dead
		// connection; returning -1 made Datagram_GetMessage drop the player.
		if (errno != EAGAIN && errno != EWOULDBLOCK) UDP_LogSockError(UDP_OP_RECV, socket, errno, NULL);
		if (LAN_LEGACY_MODE ? (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR) : LAN_IsTransientSocketErrno(errno)) return 0;
		return -1;
#else
		return 0;
#endif
	}
	return ret;
}

//=============================================================================

int UDP_MakeSocketBroadcastCapable (int socket)
{
	int enabled = 1;
	#if defined(_WIN32)
	if (setsockopt(socket, SOL_SOCKET, SO_BROADCAST, (const char *)&enabled, sizeof(enabled)) == SOCKET_ERROR)
		return -1;
	#else
	if (setsockopt(socket, SOL_SOCKET, SO_BROADCAST, &enabled, sizeof(enabled)) == -1)
		return -1;
	#endif
	net_broadcastsocket = socket;
	return 0;
}


//=============================================================================

int UDP_Broadcast (int socket, byte *buf, int len)
{
	int ret;

	if (socket != net_broadcastsocket)
	{
		if (net_broadcastsocket != 0)
			Sys_Error("Attempted to use multiple broadcasts sockets\n");
		ret = UDP_MakeSocketBroadcastCapable (socket);
		if (ret == -1)
		{
			Con_Printf("Unable to make socket broadcast capable\n");
			return ret;
		}
	}

#ifdef PLATFORM_ANDROID
	UDP_RefreshBinding(socket); // Wi-Fi may have (re)connected since the socket was opened
	UDP_LogInterfacesIfChanged("search");
	NZP_LAN_LOG("discovery broadcast: limited 255.255.255.255:%d (%d bytes) wifi_handle=%llu", net_hostport, len,
			(unsigned long long)NZP_LanNetwork_Get());
#endif
	ret = UDP_Write (socket, buf, len, &broadcastaddr);
#ifdef PLATFORM_ANDROID
	// Some Android tethering routers filter limited broadcast but permit subnet broadcast.
	if (UDP_SendInterfaceBroadcasts(socket, buf, len) > 0) ret = len;
#endif
	return ret;
}

//=============================================================================

int UDP_Write (int socket, byte *buf, int len, struct qsockaddr *addr)
{
	int ret;

	#ifdef PLATFORM_ANDROID
	UDP_RefreshBinding(socket);
	#endif
	ret = sendto (socket, (const char *)buf, len, 0, (struct sockaddr *)addr, sizeof(struct qsockaddr));
	if (ret == -1) {
#ifdef PLATFORM_ANDROID
		if (errno != EAGAIN && errno != EWOULDBLOCK) UDP_LogSockError(UDP_OP_SEND, socket, errno, addr);
		if (LAN_LEGACY_MODE ? (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR) : LAN_IsTransientSocketErrno(errno)) return 0;
		return -1;
#else
		return 0;
#endif
	}
	return ret;
}

//=============================================================================

char *UDP_AddrToString (struct qsockaddr *addr)
{
	static char buffer[22];
	int haddr;

	haddr = ntohl(((struct sockaddr_in *)addr)->sin_addr.s_addr);
	sprintf(buffer, "%d.%d.%d.%d:%d", (haddr >> 24) & 0xff, (haddr >> 16) & 0xff, (haddr >> 8) & 0xff, haddr & 0xff, ntohs(((struct sockaddr_in *)addr)->sin_port));
	return buffer;
}

//=============================================================================

int UDP_StringToAddr (char *string, struct qsockaddr *addr)
{
	int ha1, ha2, ha3, ha4, hp;
	int ipaddr;

	sscanf(string, "%d.%d.%d.%d:%d", &ha1, &ha2, &ha3, &ha4, &hp);
	ipaddr = (ha1 << 24) | (ha2 << 16) | (ha3 << 8) | ha4;

	addr->sa_family = AF_INET;
	((struct sockaddr_in *)addr)->sin_addr.s_addr = htonl(ipaddr);
	((struct sockaddr_in *)addr)->sin_port = htons(hp);
	return 0;
}

//=============================================================================

int UDP_GetSocketAddr (int socket, struct qsockaddr *addr)
{
	int addrlen = sizeof(struct qsockaddr);
	unsigned int a;

	Q_memset(addr, 0, sizeof(struct qsockaddr));
	getsockname(socket, (struct sockaddr *)addr, (socklen_t*)&addrlen);
	a = ((struct sockaddr_in *)addr)->sin_addr.s_addr;
#ifdef PLATFORM_ANDROID
	if (a == 0 || a == inet_addr("127.0.0.1")) {
		// Re-evaluated on every call: the hotspot may be switched on after launch.
		unsigned long fresh = UDP_LocalAddress(0);
		if (fresh) myAddr = fresh;
	}
#endif
	if (a == 0 || a == inet_addr("127.0.0.1"))
		((struct sockaddr_in *)addr)->sin_addr.s_addr = myAddr;

	return 0;
}

//=============================================================================

int UDP_GetNameFromAddr (struct qsockaddr *addr, char *name)
{
#ifdef PLATFORM_ANDROID
	// Reverse DNS may block indefinitely on isolated Hotspot networks.
	Q_strncpyz(name, UDP_AddrToString(addr), NET_NAMELEN);
	return 0;
#endif
	struct hostent *hostentry;

	hostentry = gethostbyaddr ((char *)&((struct sockaddr_in *)addr)->sin_addr, sizeof(struct in_addr), AF_INET);
	if (hostentry)
	{
		Q_strncpy (name, (char *)hostentry->h_name, NET_NAMELEN - 1);
		return 0;
	}

	Q_strcpy (name, UDP_AddrToString (addr));
	return 0;
}

//=============================================================================

int UDP_GetAddrFromName(char *name, struct qsockaddr *addr)
{
	struct hostent *hostentry;

#ifdef PLATFORM_ANDROID
	if (name[0] >= '0' && name[0] <= '9') {
		struct in_addr ip;
		char host[INET_ADDRSTRLEN];
		const char *colon = strchr(name, ':');
		size_t len = colon ? (size_t)(colon-name) : strlen(name);
		unsigned long port = (unsigned long)net_hostport;
		if (!len || len >= sizeof(host)) return -1;
		memcpy(host,name,len);host[len]=0;
		if (inet_pton(AF_INET,host,&ip)!=1)return -1;
		if(colon){
			char *end=NULL;
			port=strtoul(colon+1,&end,10);
			if(end==colon+1 || *end || port==0 || port>65535)return -1;
		}
		memset(addr,0,sizeof(*addr));
		((struct sockaddr_in *)addr)->sin_family=AF_INET;
		((struct sockaddr_in *)addr)->sin_addr=ip;
		((struct sockaddr_in *)addr)->sin_port=htons((unsigned short)port);
		return 0;
	}
#else
	if (name[0] >= '0' && name[0] <= '9')
		return PartialIPAddress (name, addr);
#endif

	hostentry = gethostbyname (name);
	if (!hostentry)
		return -1;

	addr->sa_family = AF_INET;
	((struct sockaddr_in *)addr)->sin_port = htons(net_hostport);
	((struct sockaddr_in *)addr)->sin_addr.s_addr = *(int *)hostentry->h_addr_list[0];

	return 0;
}

//=============================================================================

int UDP_AddrCompare (struct qsockaddr *addr1, struct qsockaddr *addr2)
{
	if (addr1->sa_family != addr2->sa_family)
		return -1;

	if (((struct sockaddr_in *)addr1)->sin_addr.s_addr != ((struct sockaddr_in *)addr2)->sin_addr.s_addr)
		return -1;

	if (((struct sockaddr_in *)addr1)->sin_port != ((struct sockaddr_in *)addr2)->sin_port)
		return 1;

	return 0;
}

//=============================================================================

int UDP_GetSocketPort (struct qsockaddr *addr)
{
	return ntohs(((struct sockaddr_in *)addr)->sin_port);
}


int UDP_SetSocketPort (struct qsockaddr *addr, int port)
{
	((struct sockaddr_in *)addr)->sin_port = htons(port);
	return 0;
}

//=============================================================================

