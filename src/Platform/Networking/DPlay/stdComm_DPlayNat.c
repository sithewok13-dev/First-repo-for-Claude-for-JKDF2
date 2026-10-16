// Added: entire file. See stdComm_DPlayNat.h. Small and self-contained (plain sockets, no library):
//
//   PCP (RFC 6887) and NAT-PMP (RFC 6886): a datagram to the router (the default gateway) on UDP
//   5351, MAP for each port (and NAT-PMP's external address); given back with a lifetime of 0.
//   UPnP-IGD: an SSDP M-SEARCH (UDP 239.255.255.250:1900) finds the router's description (HTTP),
//   whose WANIPConnection (or WANPPPConnection) service is asked with SOAP: AddPortMapping,
//   GetExternalIPAddress, DeletePortMapping.
//
// For testing on a computer without such a router: OPENJKDF2_DPLAY_GATEWAY=<IPv4> (the router to
// ask with PCP / NAT-PMP) and OPENJKDF2_DPLAY_SSDP=<IPv4> (where the M-SEARCH goes, unicast).

#include "stdComm_DPlayNat.h"

#ifdef PLATFORM_DPLAY

#include "stdPlatform.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <poll.h>
#include <netdb.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#if defined(__APPLE__)
#include <sys/sysctl.h>
#include <net/if.h>
#if defined(__has_include)
#if __has_include(<net/route.h>)
#include <net/route.h>
#define DPLAYNAT_HAVE_ROUTE_H
#endif
#endif
#endif

#define DPLAYNAT_LIFETIME 7200  // (s; renewed by stdComm_DPlay while a game goes on)
#define DPLAYNAT_PCP_PORT 5351

static char stdComm_DPlayNat_problem[256] = "";

// What is forwarded now, to be given back
static int stdComm_DPlayNat_bMapped = 0;
static int stdComm_DPlayNat_method = 0; // 1 PCP, 2 NAT-PMP, 3 UPnP
static uint16_t stdComm_DPlayNat_inTcp, stdComm_DPlayNat_inUdp, stdComm_DPlayNat_exTcp, stdComm_DPlayNat_exUdp;
static uint32_t stdComm_DPlayNat_gateway = 0;
static uint8_t stdComm_DPlayNat_aNonce[2][12];
static char stdComm_DPlayNat_controlUrl[512];
static char stdComm_DPlayNat_serviceType[96];

int stdComm_DPlayNat_IsPrivate(uint32_t ipNet)
{
    const uint32_t h = ntohl(ipNet);
    return (h >> 24) == 10 || (h >> 20) == (172u << 4 | 1) || (h >> 16) == (192u << 8 | 168)
        || (h & 0xFFC00000u) == 0x64400000u || (h >> 16) == (169u << 8 | 254) || (h >> 24) == 127 || h == 0;
}

uint32_t stdComm_DPlayNat_LocalAddressToward(uint32_t ipNet)
{
    int fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0) return 0;
    struct sockaddr_in a;
    memset(&a, 0, sizeof(a));
    a.sin_family = AF_INET;
    a.sin_port = htons(9);
    a.sin_addr.s_addr = ipNet;
    uint32_t r = 0;
    if (connect(fd, (struct sockaddr*)&a, sizeof(a)) == 0) {
        socklen_t l = sizeof(a);
        if (getsockname(fd, (struct sockaddr*)&a, &l) == 0) r = a.sin_addr.s_addr;
    }
    close(fd);
    return r;
}

const char* stdComm_DPlayNat_Problem(void)
{
    return stdComm_DPlayNat_problem;
}

// ------------------------------------------------------------------ the router's address

static uint32_t stdComm_DPlayNat_Gateway(void)
{
    const char* pEnv = getenv("OPENJKDF2_DPLAY_GATEWAY");
    struct in_addr ia;
    if (pEnv && inet_pton(AF_INET, pEnv, &ia) == 1) return ia.s_addr;
#if defined(__linux__)
    FILE* f = fopen("/proc/net/route", "r");
    if (!f) return 0;
    char line[256];
    uint32_t gw = 0;
    while (fgets(line, sizeof(line), f)) {
        char ifname[64];
        unsigned int dest, gate, flags;
        if (sscanf(line, "%63s %x %x %x", ifname, &dest, &gate, &flags) == 4 && dest == 0 && (flags & 2)) {
            gw = gate; // (already in network order, as the kernel prints it)
            break;
        }
    }
    fclose(f);
    return gw;
#elif defined(__APPLE__)
    // (the routing table's default route, from sysctl: iOS's SDK has no <net/route.h>, so its few
    // definitions here)
#ifndef DPLAYNAT_HAVE_ROUTE_H
    struct rt_metrics { uint32_t a[14]; };
    struct rt_msghdr {
        unsigned short rtm_msglen; unsigned char rtm_version; unsigned char rtm_type; unsigned short rtm_index;
        int rtm_flags; int rtm_addrs; pid_t rtm_pid; int rtm_seq; int rtm_errno; int rtm_use; uint32_t rtm_inits;
        struct rt_metrics rtm_rmx;
    };
#define RTF_GATEWAY 0x2
#define RTAX_DST 0
#define RTAX_GATEWAY 1
#define RTAX_MAX 8
#define NET_RT_FLAGS 2
#endif
    int mib[6] = { CTL_NET, PF_ROUTE, 0, AF_INET, NET_RT_FLAGS, RTF_GATEWAY };
    size_t len = 0;
    if (sysctl(mib, 6, NULL, &len, NULL, 0) != 0 || !len) return 0;
    char* buf = (char*)malloc(len);
    if (!buf) return 0;
    uint32_t gw = 0;
    if (sysctl(mib, 6, buf, &len, NULL, 0) == 0) {
        for (char* p = buf; p < buf + len && !gw; ) {
            struct rt_msghdr* rt = (struct rt_msghdr*)p;
            if (!rt->rtm_msglen) break;
            struct sockaddr* sa = (struct sockaddr*)(rt + 1);
            struct sockaddr* aSa[RTAX_MAX] = { 0 };
            for (int i = 0; i < RTAX_MAX; i++) {
                if (rt->rtm_addrs & (1 << i)) {
                    aSa[i] = sa;
                    const int salen = sa->sa_len ? ((sa->sa_len + 3) & ~3) : 4;
                    sa = (struct sockaddr*)((char*)sa + salen);
                }
            }
            if (aSa[RTAX_DST] && aSa[RTAX_GATEWAY] && aSa[RTAX_DST]->sa_family == AF_INET && aSa[RTAX_GATEWAY]->sa_family == AF_INET
                && ((struct sockaddr_in*)aSa[RTAX_DST])->sin_addr.s_addr == 0)
                gw = ((struct sockaddr_in*)aSa[RTAX_GATEWAY])->sin_addr.s_addr;
            p += rt->rtm_msglen;
        }
    }
    free(buf);
    return gw;
#else
    return 0;
#endif
}

// ------------------------------------------------------------------ PCP and NAT-PMP

// A datagram to the router and its answer (a few tries); the answer's length, or 0
static int stdComm_DPlayNat_Ask(uint32_t gw, const uint8_t* pReq, int reqLen, uint8_t* pResp, int respMax)
{
    int fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0) return 0;
    struct sockaddr_in a;
    memset(&a, 0, sizeof(a));
    a.sin_family = AF_INET;
    a.sin_port = htons(DPLAYNAT_PCP_PORT);
    a.sin_addr.s_addr = gw;
    int got = 0;
    if (connect(fd, (struct sockaddr*)&a, sizeof(a)) == 0) {
        for (int t = 0, wait = 250; t < 3 && !got; t++, wait *= 2) {
            if (send(fd, pReq, reqLen, 0) != reqLen) break;
            struct pollfd pf = { fd, POLLIN, 0 };
            if (poll(&pf, 1, wait) > 0) {
                const int n = (int)recv(fd, pResp, respMax, 0);
                if (n > 0) got = n;
                else if (n < 0 && errno == ECONNREFUSED) break; // (no one listening there)
            }
        }
    }
    close(fd);
    return got;
}

static void stdComm_DPlayNat_Random(uint8_t* p, int n)
{
    static uint32_t s = 0;
    if (!s) s = (uint32_t)stdPlatform_GetTimeMsec() * 2654435761u ^ (uint32_t)getpid();
    for (int i = 0; i < n; i++) { s = s * 1103515245u + 12345u; p[i] = (uint8_t)(s >> 16); }
}

// PCP MAP: the port (proto 6 TCP, 17 UDP) forwarded; 1 if it is (*pExt, *pExtIp filled)
static int stdComm_DPlayNat_PcpMap(uint32_t gw, uint32_t localIp, int proto, uint16_t port, uint16_t want, uint32_t lifetime,
                                   uint8_t* pNonce, uint16_t* pExt, uint32_t* pExtIp, int* pbNotPcp)
{
    uint8_t q[60], r[1100];
    memset(q, 0, sizeof(q));
    q[0] = 2; q[1] = 1; // (version 2, MAP)
    q[4] = (uint8_t)(lifetime >> 24); q[5] = (uint8_t)(lifetime >> 16); q[6] = (uint8_t)(lifetime >> 8); q[7] = (uint8_t)lifetime;
    q[18] = 0xFF; q[19] = 0xFF; memcpy(q + 20, &localIp, 4); // (our address, IPv4-mapped)
    memcpy(q + 24, pNonce, 12);
    q[36] = (uint8_t)proto;
    q[40] = (uint8_t)(port >> 8); q[41] = (uint8_t)port;
    q[42] = (uint8_t)(want >> 8); q[43] = (uint8_t)want;
    q[54] = 0xFF; q[55] = 0xFF; // (any external address)
    const int n = stdComm_DPlayNat_Ask(gw, q, sizeof(q), r, sizeof(r));
    if (n < 4) return 0;
    if (r[0] != 2) { *pbNotPcp = 1; return 0; } // (a NAT-PMP router answers "unsupported version")
    if (n < 60 || r[1] != 0x81 || memcmp(r + 24, pNonce, 12)) return 0;
    if (r[3] != 0) {
        snprintf(stdComm_DPlayNat_problem, sizeof(stdComm_DPlayNat_problem), "the router refused (PCP result %d)", r[3]);
        return 0;
    }
    *pExt = (uint16_t)((r[42] << 8) | r[43]);
    memcpy(pExtIp, r + 56, 4);
    return 1;
}

// NAT-PMP: the external address (op 0), or a port forwarded (op 1 UDP, 2 TCP)
static int stdComm_DPlayNat_PmpMap(uint32_t gw, int op, uint16_t port, uint16_t want, uint32_t lifetime, uint16_t* pExt, uint32_t* pExtIp)
{
    uint8_t q[12], r[16];
    memset(q, 0, sizeof(q));
    q[1] = (uint8_t)op;
    q[4] = (uint8_t)(port >> 8); q[5] = (uint8_t)port;
    q[6] = (uint8_t)(want >> 8); q[7] = (uint8_t)want;
    q[8] = (uint8_t)(lifetime >> 24); q[9] = (uint8_t)(lifetime >> 16); q[10] = (uint8_t)(lifetime >> 8); q[11] = (uint8_t)lifetime;
    const int n = stdComm_DPlayNat_Ask(gw, q, op ? 12 : 2, r, sizeof(r));
    if (n < 8 || r[0] != 0 || r[1] != 128 + op) return 0;
    const int result = (r[2] << 8) | r[3];
    if (result) {
        snprintf(stdComm_DPlayNat_problem, sizeof(stdComm_DPlayNat_problem), "the router refused (NAT-PMP result %d)", result);
        return 0;
    }
    if (op == 0) { if (n >= 12) memcpy(pExtIp, r + 8, 4); return 1; }
    if (n < 16) return 0;
    *pExt = (uint16_t)((r[10] << 8) | r[11]);
    return 1;
}

// ------------------------------------------------------------------ UPnP-IGD

// "http://host:port/path" split
static int stdComm_DPlayNat_SplitUrl(const char* pUrl, char* pHost, int hostLen, int* pPort, char* pPath, int pathLen)
{
    if (strncmp(pUrl, "http://", 7)) return 0;
    const char* h = pUrl + 7;
    const char* slash = strchr(h, '/');
    const char* colon = strchr(h, ':');
    int hl = (int)(slash ? slash - h : (int)strlen(h));
    *pPort = 80;
    if (colon && (!slash || colon < slash)) { *pPort = atoi(colon + 1); hl = (int)(colon - h); }
    if (hl <= 0 || hl >= hostLen) return 0;
    memcpy(pHost, h, hl);
    pHost[hl] = 0;
    snprintf(pPath, pathLen, "%s", slash ? slash : "/");
    return 1;
}

// One HTTP request and its whole answer (the router closes it); the answer's length, or -1
static int stdComm_DPlayNat_Http(const char* pUrl, const char* pMethod, const char* pHeaders, const char* pBody, char* pOut, int outMax)
{
    char host[128], path[384];
    int port;
    if (!stdComm_DPlayNat_SplitUrl(pUrl, host, sizeof(host), &port, path, sizeof(path))) return -1;
    struct sockaddr_in a;
    memset(&a, 0, sizeof(a));
    a.sin_family = AF_INET;
    a.sin_port = htons((uint16_t)port);
    if (inet_pton(AF_INET, host, &a.sin_addr) != 1) return -1; // (a router gives its numeric address)
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) return -1;
    struct timeval tv = { 2, 0 };
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
#ifdef SO_NOSIGPIPE
    int one = 1;
    setsockopt(fd, SOL_SOCKET, SO_NOSIGPIPE, &one, sizeof(one));
#endif
    if (connect(fd, (struct sockaddr*)&a, sizeof(a)) != 0) { close(fd); return -1; }
    char req[2048];
    const int n = snprintf(req, sizeof(req), "%s %s HTTP/1.1\r\nHost: %s:%d\r\nConnection: close\r\n%s\r\n%s",
                           pMethod, path, host, port, pHeaders ? pHeaders : "", pBody ? pBody : "");
    if (n <= 0 || n >= (int)sizeof(req) || send(fd, req, n, 0) != n) { close(fd); return -1; }
    int got = 0;
    for (;;) {
        const int k = (int)recv(fd, pOut + got, outMax - 1 - got, 0);
        if (k <= 0) break;
        got += k;
        if (got >= outMax - 1) break;
    }
    close(fd);
    pOut[got] = 0;
    return got;
}

// The text between <tag> and </tag>, at or after p (in the same part, if pEnd)
static int stdComm_DPlayNat_Tag(const char* p, const char* pEnd, const char* tag, char* pOut, int outMax)
{
    char open[64], close_[64];
    snprintf(open, sizeof(open), "<%s>", tag);
    snprintf(close_, sizeof(close_), "</%s>", tag);
    const char* s = strstr(p, open);
    if (!s || (pEnd && s > pEnd)) return 0;
    s += strlen(open);
    const char* e = strstr(s, close_);
    if (!e) return 0;
    int l = (int)(e - s);
    while (l > 0 && (*s == ' ' || *s == '\n' || *s == '\r' || *s == '\t')) { s++; l--; }
    while (l > 0 && (s[l - 1] == ' ' || s[l - 1] == '\n' || s[l - 1] == '\r' || s[l - 1] == '\t')) l--;
    if (l >= outMax) l = outMax - 1;
    memcpy(pOut, s, l);
    pOut[l] = 0;
    return 1;
}

// The router's WAN connection service, found with SSDP
static int stdComm_DPlayNat_UpnpFind(void)
{
    if (stdComm_DPlayNat_controlUrl[0]) return 1;
    int fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0) return 0;
    struct sockaddr_in a;
    memset(&a, 0, sizeof(a));
    a.sin_family = AF_INET;
    a.sin_port = htons(1900);
    const char* pEnv = getenv("OPENJKDF2_DPLAY_SSDP");
    if (!pEnv || inet_pton(AF_INET, pEnv, &a.sin_addr) != 1) inet_pton(AF_INET, "239.255.255.250", &a.sin_addr);
    static const char* const aSt[] = { "urn:schemas-upnp-org:device:InternetGatewayDevice:1", "urn:schemas-upnp-org:service:WANIPConnection:1" };
    char locations[4][256];
    int numLoc = 0;
    for (int t = 0; t < 2; t++) {
        char q[256];
        const int n = snprintf(q, sizeof(q), "M-SEARCH * HTTP/1.1\r\nHOST: 239.255.255.250:1900\r\nMAN: \"ssdp:discover\"\r\nMX: 1\r\nST: %s\r\n\r\n", aSt[t]);
        sendto(fd, q, n, 0, (struct sockaddr*)&a, sizeof(a));
    }
    const uint32_t start = stdPlatform_GetTimeMsec();
    while (stdPlatform_GetTimeMsec() - start < 1500 && numLoc < 4) {
        struct pollfd pf = { fd, POLLIN, 0 };
        if (poll(&pf, 1, 100) <= 0) continue;
        char r[1500];
        const int n = (int)recv(fd, r, sizeof(r) - 1, 0);
        if (n <= 0) continue;
        r[n] = 0;
        for (char* p = r; *p; p++) {
            if (!strncasecmp(p, "\nLOCATION:", 10)) {
                p += 10;
                while (*p == ' ') p++;
                int l = 0;
                while (p[l] && p[l] != '\r' && p[l] != '\n') l++;
                if (l < 256) {
                    int dup = 0;
                    for (int i = 0; i < numLoc; i++) dup |= !strncmp(locations[i], p, l) && !locations[i][l];
                    if (!dup) { memcpy(locations[numLoc], p, l); locations[numLoc][l] = 0; numLoc++; }
                }
                break;
            }
        }
    }
    close(fd);
    static char desc[32768];
    for (int i = 0; i < numLoc; i++) {
        if (stdComm_DPlayNat_Http(locations[i], "GET", NULL, NULL, desc, sizeof(desc)) <= 0) continue;
        static const char* const aSvc[] = { "urn:schemas-upnp-org:service:WANIPConnection:2", "urn:schemas-upnp-org:service:WANIPConnection:1",
                                             "urn:schemas-upnp-org:service:WANPPPConnection:1" };
        for (int s = 0; s < 3; s++) {
            const char* p = strstr(desc, aSvc[s]);
            if (!p) continue;
            const char* pEnd = strstr(p, "</service>");
            char ctl[384];
            if (!stdComm_DPlayNat_Tag(p, pEnd, "controlURL", ctl, sizeof(ctl))) continue;
            // (relative to URLBase, or to where the description was)
            char base[256];
            if (!stdComm_DPlayNat_Tag(desc, NULL, "URLBase", base, sizeof(base))) {
                snprintf(base, sizeof(base), "%s", locations[i]);
                char* pPath = strchr(base + 7, '/');
                if (pPath) *pPath = 0;
            }
            const size_t bl = strlen(base);
            if (bl && base[bl - 1] == '/') base[bl - 1] = 0;
            if (!strncmp(ctl, "http://", 7)) snprintf(stdComm_DPlayNat_controlUrl, sizeof(stdComm_DPlayNat_controlUrl), "%s", ctl);
            else snprintf(stdComm_DPlayNat_controlUrl, sizeof(stdComm_DPlayNat_controlUrl), "%s%s%s", base, ctl[0] == '/' ? "" : "/", ctl);
            snprintf(stdComm_DPlayNat_serviceType, sizeof(stdComm_DPlayNat_serviceType), "%s", aSvc[s]);
            return 1;
        }
    }
    return 0;
}

// A SOAP action on the router's service; the answer (in pOut), and its UPnP error code (0: none, -1: no answer)
static int stdComm_DPlayNat_Soap(const char* pAction, const char* pArgs, char* pOut, int outMax)
{
    static char body[2048];
    snprintf(body, sizeof(body),
             "<?xml version=\"1.0\"?>\r\n<s:Envelope xmlns:s=\"http://schemas.xmlsoap.org/soap/envelope/\" "
             "s:encodingStyle=\"http://schemas.xmlsoap.org/soap/encoding/\"><s:Body><u:%s xmlns:u=\"%s\">%s</u:%s></s:Body></s:Envelope>\r\n",
             pAction, stdComm_DPlayNat_serviceType, pArgs, pAction);
    char headers[512];
    snprintf(headers, sizeof(headers), "Content-Type: text/xml; charset=\"utf-8\"\r\nSOAPAction: \"%s#%s\"\r\nContent-Length: %d\r\n",
             stdComm_DPlayNat_serviceType, pAction, (int)strlen(body));
    const int n = stdComm_DPlayNat_Http(stdComm_DPlayNat_controlUrl, "POST", headers, body, pOut, outMax);
    if (n <= 0) return -1;
    if (!strncmp(pOut, "HTTP/1.1 200", 12) || !strncmp(pOut, "HTTP/1.0 200", 12)) return 0;
    char code[16];
    return stdComm_DPlayNat_Tag(pOut, NULL, "errorCode", code, sizeof(code)) ? atoi(code) : 501;
}

static int stdComm_DPlayNat_UpnpAdd(uint32_t localIp, const char* pProto, uint16_t port, uint16_t* pExt)
{
    char ip[32], args[768], r[4096];
    inet_ntop(AF_INET, &localIp, ip, sizeof(ip));
    // (the same port outside if free, otherwise one of the next few; a lease the router may not take)
    for (int k = 0; k < 8; k++) {
        const uint16_t ext = (uint16_t)(port + k);
        for (int lease = 0; lease < 2; lease++) {
            snprintf(args, sizeof(args),
                     "<NewRemoteHost></NewRemoteHost><NewExternalPort>%u</NewExternalPort><NewProtocol>%s</NewProtocol>"
                     "<NewInternalPort>%u</NewInternalPort><NewInternalClient>%s</NewInternalClient><NewEnabled>1</NewEnabled>"
                     "<NewPortMappingDescription>OpenJKDF2 DirectPlay</NewPortMappingDescription><NewLeaseDuration>%d</NewLeaseDuration>",
                     ext, pProto, port, ip, lease ? 0 : DPLAYNAT_LIFETIME);
            const int e = stdComm_DPlayNat_Soap("AddPortMapping", args, r, sizeof(r));
            if (e == 0) { *pExt = ext; return 1; }
            if (e == 725) continue; // (OnlyPermanentLeasesSupported: again, lease 0)
            if (e == 718 || e == 724) break; // (that port is taken, or must be the same: the next)
            snprintf(stdComm_DPlayNat_problem, sizeof(stdComm_DPlayNat_problem), "the router refused (UPnP error %d)", e);
            return 0;
        }
    }
    snprintf(stdComm_DPlayNat_problem, sizeof(stdComm_DPlayNat_problem), "the router's ports near %u are taken", port);
    return 0;
}

static void stdComm_DPlayNat_UpnpDelete(const char* pProto, uint16_t ext)
{
    char args[256], r[2048];
    snprintf(args, sizeof(args), "<NewRemoteHost></NewRemoteHost><NewExternalPort>%u</NewExternalPort><NewProtocol>%s</NewProtocol>", ext, pProto);
    stdComm_DPlayNat_Soap("DeletePortMapping", args, r, sizeof(r));
}

// ------------------------------------------------------------------ mapping

int stdComm_DPlayNat_Map(uint16_t tcpPort, uint16_t udpPort, stdCommDPlayNatMapping* pOut)
{
    memset(pOut, 0, sizeof(*pOut));
    stdComm_DPlayNat_problem[0] = 0;
    if (stdComm_DPlayNat_bMapped && stdComm_DPlayNat_inTcp == tcpPort && stdComm_DPlayNat_inUdp == udpPort && stdComm_DPlayNat_method == 3) {
        // (UPnP's lease: long, or forever; as it is)
        pOut->extTcpPort = stdComm_DPlayNat_exTcp;
        pOut->extUdpPort = stdComm_DPlayNat_exUdp;
        pOut->pMethod = "UPnP";
        return 1;
    }
    const uint32_t gw = stdComm_DPlayNat_Gateway();
    const uint32_t localIp = gw ? stdComm_DPlayNat_LocalAddressToward(gw) : 0;
    uint16_t exTcp = 0, exUdp = 0;
    uint32_t exIp = 0;

    // PCP, then NAT-PMP (the same port: a renewal keeps what was given)
    if (gw && localIp) {
        if (!stdComm_DPlayNat_bMapped) {
            stdComm_DPlayNat_Random(stdComm_DPlayNat_aNonce[0], 12);
            stdComm_DPlayNat_Random(stdComm_DPlayNat_aNonce[1], 12);
        }
        int bNotPcp = 0;
        uint32_t ip2 = 0;
        if (stdComm_DPlayNat_PcpMap(gw, localIp, 6, tcpPort, stdComm_DPlayNat_bMapped ? stdComm_DPlayNat_exTcp : tcpPort, DPLAYNAT_LIFETIME, stdComm_DPlayNat_aNonce[0], &exTcp, &exIp, &bNotPcp)
            && stdComm_DPlayNat_PcpMap(gw, localIp, 17, udpPort, stdComm_DPlayNat_bMapped ? stdComm_DPlayNat_exUdp : udpPort, DPLAYNAT_LIFETIME, stdComm_DPlayNat_aNonce[1], &exUdp, &ip2, &bNotPcp)) {
            stdComm_DPlayNat_method = 1;
            pOut->pMethod = "PCP";
        }
        else if (stdComm_DPlayNat_PmpMap(gw, 2, tcpPort, stdComm_DPlayNat_bMapped ? stdComm_DPlayNat_exTcp : tcpPort, DPLAYNAT_LIFETIME, &exTcp, NULL)
                 && stdComm_DPlayNat_PmpMap(gw, 1, udpPort, stdComm_DPlayNat_bMapped ? stdComm_DPlayNat_exUdp : udpPort, DPLAYNAT_LIFETIME, &exUdp, NULL)) {
            stdComm_DPlayNat_PmpMap(gw, 0, 0, 0, 0, NULL, &exIp);
            stdComm_DPlayNat_method = 2;
            pOut->pMethod = "NAT-PMP";
        }
    }
    // UPnP-IGD
    if (!pOut->pMethod && stdComm_DPlayNat_UpnpFind()) {
        const uint32_t lip = localIp ? localIp : stdComm_DPlayNat_LocalAddressToward(inet_addr("8.8.8.8"));
        if (stdComm_DPlayNat_UpnpAdd(lip, "TCP", tcpPort, &exTcp)) {
            if (stdComm_DPlayNat_UpnpAdd(lip, "UDP", udpPort, &exUdp)) {
                char r[4096], ipText[32];
                if (!stdComm_DPlayNat_Soap("GetExternalIPAddress", "", r, sizeof(r)) && stdComm_DPlayNat_Tag(r, NULL, "NewExternalIPAddress", ipText, sizeof(ipText)))
                    inet_pton(AF_INET, ipText, &exIp);
                stdComm_DPlayNat_method = 3;
                pOut->pMethod = "UPnP";
            }
            else stdComm_DPlayNat_UpnpDelete("TCP", exTcp);
        }
    }
    if (!pOut->pMethod) {
        if (!stdComm_DPlayNat_problem[0])
            snprintf(stdComm_DPlayNat_problem, sizeof(stdComm_DPlayNat_problem), "no router answered (UPnP, NAT-PMP, PCP)");
        stdComm_DPlayNat_bMapped = 0;
        return 0;
    }
    stdComm_DPlayNat_bMapped = 1;
    stdComm_DPlayNat_gateway = gw;
    stdComm_DPlayNat_inTcp = tcpPort;
    stdComm_DPlayNat_inUdp = udpPort;
    stdComm_DPlayNat_exTcp = exTcp;
    stdComm_DPlayNat_exUdp = exUdp;
    pOut->extTcpPort = exTcp;
    pOut->extUdpPort = exUdp;
    pOut->extIp = exIp;
    // (a router that is itself behind another -- a carrier's NAT: its address no one can reach)
    if (exIp && stdComm_DPlayNat_IsPrivate(exIp))
        snprintf(stdComm_DPlayNat_problem, sizeof(stdComm_DPlayNat_problem), "the router is behind another network (its address is a private one)");
    return 1;
}

void stdComm_DPlayNat_Unmap(void)
{
    if (!stdComm_DPlayNat_bMapped) return;
    const uint32_t gw = stdComm_DPlayNat_gateway;
    uint16_t e;
    uint32_t ip;
    int b;
    if (stdComm_DPlayNat_method == 1) {
        const uint32_t localIp = stdComm_DPlayNat_LocalAddressToward(gw);
        stdComm_DPlayNat_PcpMap(gw, localIp, 6, stdComm_DPlayNat_inTcp, 0, 0, stdComm_DPlayNat_aNonce[0], &e, &ip, &b);
        stdComm_DPlayNat_PcpMap(gw, localIp, 17, stdComm_DPlayNat_inUdp, 0, 0, stdComm_DPlayNat_aNonce[1], &e, &ip, &b);
    }
    else if (stdComm_DPlayNat_method == 2) {
        stdComm_DPlayNat_PmpMap(gw, 2, stdComm_DPlayNat_inTcp, 0, 0, &e, NULL);
        stdComm_DPlayNat_PmpMap(gw, 1, stdComm_DPlayNat_inUdp, 0, 0, &e, NULL);
    }
    else if (stdComm_DPlayNat_method == 3) {
        stdComm_DPlayNat_UpnpDelete("TCP", stdComm_DPlayNat_exTcp);
        stdComm_DPlayNat_UpnpDelete("UDP", stdComm_DPlayNat_exUdp);
    }
    stdPlatform_Printf("DPlay: the router's ports given back\n");
    stdComm_DPlayNat_bMapped = 0;
}

#endif // PLATFORM_DPLAY
