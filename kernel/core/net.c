#include "kernel.h"
#include "x86_64.h"

static u16 bswap16(u16 v) { return (u16)((v >> 8) | (v << 8)); }
static u32 bswap32(u32 v) { return ((v >> 24) | ((v >> 8) & 0xFF00u) | ((v << 8) & 0xFF0000u) | (v << 24)); }
#define htons(x) bswap16(x)
#define ntohs(x) bswap16(x)
#define htonl(x) bswap32(x)
#define ntohl(x) bswap32(x)

typedef struct __attribute__((packed)) {
    u8 dst_mac[6]; u8 src_mac[6]; u16 ethertype;
} EthHeader;

#define ETHERTYPE_ARP 0x0806
#define ETHERTYPE_IP  0x0800

typedef struct __attribute__((packed)) {
    u16 htype; u16 ptype; u8 hlen; u8 plen; u16 oper;
    u8 sha[6]; u32 spa; u8 tha[6]; u32 tpa;
} ArpPacket;

#define ARP_REQUEST 1
#define ARP_REPLY   2

typedef struct __attribute__((packed)) {
    u8 ver_ihl; u8 tos; u16 total_len; u16 id; u16 frag;
    u8 ttl; u8 proto; u16 checksum; u32 src; u32 dst;
} Ipv4Header;

#define IP_PROTO_TCP  6
#define IP_PROTO_UDP  17

typedef struct __attribute__((packed)) {
    u16 src_port; u16 dst_port; u16 length; u16 checksum;
} UdpHeader;

typedef struct __attribute__((packed)) {
    u16 src_port; u16 dst_port; u32 seq; u32 ack;
    u8 data_offset; u8 flags; u16 window; u16 checksum; u16 urgent;
} TcpHeader;

#define TCP_FIN 0x01
#define TCP_SYN 0x02
#define TCP_RST 0x04
#define TCP_PSH 0x08
#define TCP_ACK 0x10

typedef struct __attribute__((packed)) {
    u8 op; u8 htype; u8 hlen; u8 hops; u32 xid; u16 secs; u16 flags;
    u32 ciaddr; u32 yiaddr; u32 siaddr; u32 giaddr;
    u8 chaddr[16]; u8 sname[64]; u8 file[128]; u32 magic; u8 options[60];
} DhcpPacket;

static u8 g_our_mac[6];
static u32 g_our_ip = 0;
static u32 g_gateway_ip = 0;
static u32 g_dns_ip = 0;
static u32 g_subnet_mask = 0;
static int g_net_ready = 0;

#define ARP_CACHE_SIZE 8
typedef struct { u32 ip; u8 mac[6]; int valid; } ArpEntry;
static ArpEntry g_arp_cache[ARP_CACHE_SIZE];

static u16 ip_checksum(const void *data, usize len) {
    const u16 *words = (const u16 *)data;
    u32 sum = 0;
    for (usize i = 0; i < len / 2; ++i) sum += words[i];
    if (len & 1) sum += ((const u8 *)data)[len - 1];
    while (sum >> 16) sum = (sum & 0xFFFFu) + (sum >> 16);
    return (u16)~sum;
}

static u16 transport_checksum(u32 src_ip, u32 dst_ip, u8 proto, const void *data, u16 len) {
    u32 sum = 0;
    const u16 *words = (const u16 *)data;
    sum += (u16)(src_ip >> 16) + (u16)(src_ip & 0xFFFFu);
    sum += (u16)(dst_ip >> 16) + (u16)(dst_ip & 0xFFFFu);
    sum += htons((u16)proto);
    sum += htons(len);
    for (u16 i = 0; i < len / 2; ++i) sum += words[i];
    if (len & 1) sum += ((const u8 *)data)[len - 1];
    while (sum >> 16) sum = (sum & 0xFFFFu) + (sum >> 16);
    return (u16)~sum;
}

static int net_send_eth(const u8 *dst_mac, u16 ethertype, const void *payload, u16 payload_len) {
    static u8 frame[1518];
    EthHeader *eth = (EthHeader *)frame;
    memcpy(eth->dst_mac, dst_mac, 6);
    memcpy(eth->src_mac, g_our_mac, 6);
    eth->ethertype = htons(ethertype);
    memcpy(frame + sizeof(EthHeader), payload, payload_len);
    return rtl8139_send_packet(frame, (u16)(sizeof(EthHeader) + payload_len));
}

static void arp_send_request(u32 target_ip) {
    ArpPacket arp;
    u8 bcast[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
    arp.htype = htons(1); arp.ptype = htons(ETHERTYPE_IP);
    arp.hlen = 6; arp.plen = 4; arp.oper = htons(ARP_REQUEST);
    memcpy(arp.sha, g_our_mac, 6); arp.spa = g_our_ip;
    memset(arp.tha, 0, 6); arp.tpa = target_ip;
    net_send_eth(bcast, ETHERTYPE_ARP, &arp, sizeof(arp));
}

static void arp_handle(const u8 *data, u16 len) {
    if (len < sizeof(ArpPacket)) return;
    const ArpPacket *arp = (const ArpPacket *)data;
    if (ntohs(arp->oper) == ARP_REQUEST && arp->tpa == g_our_ip) {
        ArpPacket reply;
        reply.htype = htons(1); reply.ptype = htons(ETHERTYPE_IP);
        reply.hlen = 6; reply.plen = 4; reply.oper = htons(ARP_REPLY);
        memcpy(reply.sha, g_our_mac, 6); reply.spa = g_our_ip;
        memcpy(reply.tha, arp->sha, 6); reply.tpa = arp->spa;
        net_send_eth(arp->sha, ETHERTYPE_ARP, &reply, sizeof(reply));
    }
    if (ntohs(arp->oper) == ARP_REPLY) {
        for (int i = 0; i < ARP_CACHE_SIZE; ++i) {
            if (!g_arp_cache[i].valid || g_arp_cache[i].ip == arp->spa) {
                g_arp_cache[i].ip = arp->spa;
                memcpy(g_arp_cache[i].mac, arp->sha, 6);
                g_arp_cache[i].valid = 1;
                break;
            }
        }
    }
}

static int arp_resolve(u32 ip, u8 *mac_out);
static int net_send_ip(u32 dst_ip, u8 proto, const void *payload, u16 payload_len);
static int net_send_udp(u32 dst_ip, u16 src_port, u16 dst_port, const void *data, u16 data_len);
static void dhcp_handle(const u8 *data, u16 len);
static void dns_handle(const u8 *data, u16 len);
static void tcp_handle(const u8 *data, u16 len);

static int arp_resolve(u32 ip, u8 *mac_out) {
    for (int i = 0; i < ARP_CACHE_SIZE; ++i) {
        if (g_arp_cache[i].valid && g_arp_cache[i].ip == ip) {
            memcpy(mac_out, g_arp_cache[i].mac, 6);
            return 1;
        }
    }
    for (int att = 0; att < 3; ++att) {
        arp_send_request(ip);
        for (u32 t = 0; t < 2000000; ++t) {
            net_poll();
            for (int i = 0; i < ARP_CACHE_SIZE; ++i) {
                if (g_arp_cache[i].valid && g_arp_cache[i].ip == ip) {
                    memcpy(mac_out, g_arp_cache[i].mac, 6);
                    return 1;
                }
            }
        }
    }
    return 0;
}

static u16 g_ip_id = 1;

static int net_send_ip(u32 dst_ip, u8 proto, const void *payload, u16 payload_len) {
    static u8 ip_buf[1500];
    Ipv4Header *ip = (Ipv4Header *)ip_buf;
    u16 total_len = (u16)(sizeof(Ipv4Header) + payload_len);

    u8 dst_mac[6];

    if (dst_ip == 0xFFFFFFFFu) {
        u8 bcast[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
        memcpy(dst_mac, bcast, 6);
    } else {
        u32 next_hop = dst_ip;
        if (g_subnet_mask && ((dst_ip & g_subnet_mask) != (g_our_ip & g_subnet_mask))) {
            next_hop = g_gateway_ip;
        }
        if (!arp_resolve(next_hop, dst_mac)) {
            serial_write("net: arp failed\n");
            return 0;
        }
    }

    ip->ver_ihl = 0x45; ip->tos = 0;
    ip->total_len = htons(total_len);
    ip->id = htons(g_ip_id++);
    ip->frag = htons(0x4000); ip->ttl = 64;
    ip->proto = proto; ip->checksum = 0;
    ip->src = g_our_ip; ip->dst = dst_ip;
    ip->checksum = ip_checksum(ip, sizeof(Ipv4Header));
    memcpy(ip_buf + sizeof(Ipv4Header), payload, payload_len);
    return net_send_eth(dst_mac, ETHERTYPE_IP, ip_buf, total_len);
}

static int net_send_udp(u32 dst_ip, u16 src_port, u16 dst_port, const void *data, u16 data_len) {
    static u8 udp_buf[1480];
    UdpHeader *udp = (UdpHeader *)udp_buf;
    u16 udp_len = (u16)(sizeof(UdpHeader) + data_len);
    udp->src_port = htons(src_port); udp->dst_port = htons(dst_port);
    udp->length = htons(udp_len); udp->checksum = 0;
    memcpy(udp_buf + sizeof(UdpHeader), data, data_len);
    udp->checksum = transport_checksum(g_our_ip, dst_ip, IP_PROTO_UDP, udp_buf, udp_len);
    if (udp->checksum == 0) udp->checksum = 0xFFFF;
    return net_send_ip(dst_ip, IP_PROTO_UDP, udp_buf, udp_len);
}

/* ========== DHCP ========== */

static int dhcp_discover(void) {
    static u8 dhcp_buf[sizeof(DhcpPacket)];
    DhcpPacket *dhcp = (DhcpPacket *)dhcp_buf;
    memset(dhcp, 0, sizeof(DhcpPacket));
    dhcp->op = 1; dhcp->htype = 1; dhcp->hlen = 6;
    dhcp->xid = htonl(0x12345678u);
    dhcp->flags = htons(0x8000u);
    memcpy(dhcp->chaddr, g_our_mac, 6);
    dhcp->magic = htonl(0x63825363u);
    dhcp->options[0] = 53; dhcp->options[1] = 1; dhcp->options[2] = 1;
    dhcp->options[3] = 55; dhcp->options[4] = 3;
    dhcp->options[5] = 1; dhcp->options[6] = 3; dhcp->options[7] = 6;
    dhcp->options[8] = 255;
    return net_send_udp(0xFFFFFFFFu, 68, 67, dhcp_buf, sizeof(DhcpPacket) - 52);
}

static void dhcp_handle(const u8 *data, u16 len) {
    if (len < 240) return;
    const DhcpPacket *dhcp = (const DhcpPacket *)data;
    if (dhcp->op != 2) return;

    u8 msg_type = 0;
    const u8 *opts = dhcp->options;
    for (int i = 0; i < 60 && opts[i] != 255;) {
        u8 tag = opts[i];
        if (tag == 0) { ++i; continue; }
        u8 olen = opts[i + 1];
        if (tag == 53 && olen >= 1) msg_type = opts[i + 2];
        if (tag == 3 && olen >= 4) memcpy(&g_gateway_ip, &opts[i + 2], 4);
        if (tag == 6 && olen >= 4) memcpy(&g_dns_ip, &opts[i + 2], 4);
        if (tag == 1 && olen >= 4) memcpy(&g_subnet_mask, &opts[i + 2], 4);
        i += 2 + olen;
    }

    if (msg_type == 2) { /* OFFER */
        g_our_ip = dhcp->yiaddr;
        static u8 req_buf[sizeof(DhcpPacket)];
        DhcpPacket *req = (DhcpPacket *)req_buf;
        memset(req, 0, sizeof(DhcpPacket));
        req->op = 1; req->htype = 1; req->hlen = 6;
        req->xid = dhcp->xid;
        req->flags = htons(0x8000u);
        memcpy(req->chaddr, g_our_mac, 6);
        req->magic = htonl(0x63825363u);
        req->options[0] = 53; req->options[1] = 1; req->options[2] = 3;
        req->options[3] = 50; req->options[4] = 4;
        memcpy(&req->options[5], &dhcp->yiaddr, 4);
        req->options[9] = 54; req->options[10] = 4;
        memcpy(&req->options[11], &dhcp->siaddr, 4);
        req->options[15] = 255;
        net_send_udp(0xFFFFFFFFu, 68, 67, req_buf, sizeof(DhcpPacket) - 52);
    } else if (msg_type == 5) { /* ACK */
        g_net_ready = 1;
        serial_write("dhcp: IP acquired\n");
    }
}

/* ========== DNS ========== */

static u32 g_dns_result = 0;
static volatile int g_dns_done = 0;

static u32 dns_resolve(const char *hostname) {
    if (!g_dns_ip) return 0;
    g_dns_result = 0;
    g_dns_done = 0;

    static u8 dns_buf[512];
    u16 *hdr = (u16 *)dns_buf;
    hdr[0] = htons(0x1234); hdr[1] = htons(0x0100);
    hdr[2] = htons(1); hdr[3] = 0; hdr[4] = 0; hdr[5] = 0;

    u8 *qname = dns_buf + 12;
    usize pos = 0;
    const char *p = hostname;
    while (*p) {
        const char *dot = p;
        while (*dot && *dot != '.') ++dot;
        u8 len = (u8)(dot - p);
        qname[pos++] = len;
        memcpy(qname + pos, p, len);
        pos += len;
        p = *dot ? dot + 1 : dot;
    }
    qname[pos++] = 0;
    qname[pos++] = 0; qname[pos++] = 1;
    qname[pos++] = 0; qname[pos++] = 1;
    u16 total = (u16)(12 + pos);

    for (int att = 0; att < 3; ++att) {
        net_send_udp(g_dns_ip, 12345, 53, dns_buf, total);
        for (u32 t = 0; t < 3000000; ++t) {
            net_poll();
            if (g_dns_done) return g_dns_result;
        }
    }
    return 0;
}

static void dns_handle(const u8 *data, u16 len) {
    if (len < 12) return;
    const u16 *hdr = (const u16 *)data;
    u16 answers = ntohs(hdr[3]);
    if (answers == 0) { g_dns_done = 1; return; }

    /* Skip header + question */
    usize pos = 12;
    while (pos < len && data[pos] != 0) {
        if ((data[pos] & 0xC0) == 0xC0) { pos += 2; break; }
        pos += 1 + data[pos];
    }
    if (pos < len && data[pos] == 0) ++pos;
    pos += 4; /* QTYPE + QCLASS */

    /* Parse answers */
    for (u16 a = 0; a < answers && pos + 12 <= len; ++a) {
        if ((data[pos] & 0xC0) == 0xC0) pos += 2;
        else {
            while (pos < len && data[pos] != 0) { pos += 1 + data[pos]; }
            ++pos;
        }
        u16 type = (u16)((data[pos] << 8) | data[pos + 1]);
        u16 rdlen = (u16)((data[pos + 8] << 8) | data[pos + 9]);
        if (type == 1 && rdlen == 4 && pos + 12 <= len) {
            memcpy(&g_dns_result, &data[pos + 10], 4);
            g_dns_done = 1;
            return;
        }
        pos += 10 + rdlen;
    }
    g_dns_done = 1;
}

/* ========== TCP ========== */

#define TCP_STATE_CLOSED 0
#define TCP_STATE_SYN_SENT 1
#define TCP_STATE_ESTABLISHED 2
#define TCP_STATE_FIN_WAIT 3
#define TCP_STATE_DONE 4

typedef struct {
    int state;
    u32 dst_ip;
    u16 dst_port;
    u16 src_port;
    u32 seq;
    u32 ack;
    u8 *rx_buf;
    u32 rx_len;
    u32 rx_capacity;
    int rx_done;
} TcpConn;

static TcpConn g_tcp;
static u8 g_tcp_rx_buf[8192];

static int tcp_send_flags(TcpConn *conn, u8 flags, const void *data, u16 data_len) {
    static u8 tcp_buf[1500];
    TcpHeader *tcp = (TcpHeader *)tcp_buf;
    u16 total = (u16)(sizeof(TcpHeader) + data_len);

    tcp->src_port = htons(conn->src_port);
    tcp->dst_port = htons(conn->dst_port);
    tcp->seq = htonl(conn->seq);
    tcp->ack = htonl(conn->ack);
    tcp->data_offset = (u8)((sizeof(TcpHeader) / 4) << 4);
    tcp->flags = flags;
    tcp->window = htons(8192);
    tcp->checksum = 0;
    tcp->urgent = 0;

    if (data_len > 0 && data) {
        memcpy(tcp_buf + sizeof(TcpHeader), data, data_len);
    }
    tcp->checksum = transport_checksum(g_our_ip, conn->dst_ip, IP_PROTO_TCP, tcp_buf, total);
    return net_send_ip(conn->dst_ip, IP_PROTO_TCP, tcp_buf, total);
}

static void tcp_handle(const u8 *data, u16 len) {
    if (len < sizeof(TcpHeader)) return;
    const TcpHeader *tcp = (const TcpHeader *)data;

    if (ntohs(tcp->dst_port) != g_tcp.src_port) return;

    u8 flags = tcp->flags;
    u32 seq = ntohl(tcp->seq);
    u32 ack = ntohl(tcp->ack);
    u16 data_off = (u16)((tcp->data_offset >> 4) * 4);
    u16 payload_len = (u16)(len - data_off);
    const u8 *payload = data + data_off;

    if (g_tcp.state == TCP_STATE_SYN_SENT) {
        if ((flags & (TCP_SYN | TCP_ACK)) == (TCP_SYN | TCP_ACK)) {
            g_tcp.ack = seq + 1;
            g_tcp.seq = ack;
            g_tcp.state = TCP_STATE_ESTABLISHED;
            tcp_send_flags(&g_tcp, TCP_ACK, NULL, 0);
        }
        return;
    }

    if (g_tcp.state == TCP_STATE_ESTABLISHED || g_tcp.state == TCP_STATE_FIN_WAIT) {
        if (payload_len > 0) {
            if (g_tcp.rx_len + payload_len <= g_tcp.rx_capacity) {
                memcpy(g_tcp.rx_buf + g_tcp.rx_len, payload, payload_len);
                g_tcp.rx_len += payload_len;
            }
            g_tcp.ack = seq + payload_len;
            tcp_send_flags(&g_tcp, TCP_ACK, NULL, 0);
        }
        if (flags & TCP_FIN) {
            g_tcp.ack = seq + payload_len + 1;
            tcp_send_flags(&g_tcp, TCP_ACK, NULL, 0);
            g_tcp.rx_done = 1;
            g_tcp.state = TCP_STATE_DONE;
        }
        if (flags & TCP_RST) {
            g_tcp.rx_done = 1;
            g_tcp.state = TCP_STATE_DONE;
        }
    }
}

static int tcp_connect(u32 dst_ip, u16 dst_port) {
    g_tcp.state = TCP_STATE_SYN_SENT;
    g_tcp.dst_ip = dst_ip;
    g_tcp.dst_port = dst_port;
    g_tcp.src_port = 49152;
    g_tcp.seq = 0x1000;
    g_tcp.ack = 0;
    g_tcp.rx_buf = g_tcp_rx_buf;
    g_tcp.rx_len = 0;
    g_tcp.rx_capacity = sizeof(g_tcp_rx_buf);
    g_tcp.rx_done = 0;

    tcp_send_flags(&g_tcp, TCP_SYN, NULL, 0);

    for (u32 t = 0; t < 5000000; ++t) {
        net_poll();
        if (g_tcp.state == TCP_STATE_ESTABLISHED) return 1;
        if (g_tcp.state == TCP_STATE_DONE) return 0;
    }
    return 0;
}

static int tcp_send(const void *data, u16 len) {
    if (g_tcp.state != TCP_STATE_ESTABLISHED) return 0;
    int ok = tcp_send_flags(&g_tcp, TCP_PSH | TCP_ACK, data, len);
    if (ok) g_tcp.seq += len;
    return ok;
}

static int tcp_recv_all(void) {
    for (u32 t = 0; t < 10000000; ++t) {
        net_poll();
        if (g_tcp.rx_done) return 1;
    }
    return g_tcp.rx_len > 0;
}

static void tcp_close(void) {
    if (g_tcp.state == TCP_STATE_ESTABLISHED) {
        tcp_send_flags(&g_tcp, TCP_FIN | TCP_ACK, NULL, 0);
    }
    g_tcp.state = TCP_STATE_CLOSED;
}

/* ========== HTTP ========== */

static char g_http_hostname[64];
static char g_http_path[128];

static int parse_url(const char *url) {
    /* Only support http://host/path */
    if (strncmp(url, "http://", 7) != 0) return 0;
    const char *host_start = url + 7;
    const char *path_start = host_start;
    while (*path_start && *path_start != '/') ++path_start;

    usize host_len = (usize)(path_start - host_start);
    if (host_len == 0 || host_len >= sizeof(g_http_hostname)) return 0;
    memcpy(g_http_hostname, host_start, host_len);
    g_http_hostname[host_len] = '\0';

    if (*path_start == '/') {
        usize path_len = strlen(path_start);
        if (path_len >= sizeof(g_http_path)) return 0;
        strcpy(g_http_path, path_start);
    } else {
        strcpy(g_http_path, "/");
    }
    return 1;
}

int net_http_get(const char *url, u8 *out_buf, u32 out_capacity, u32 *out_len) {
    if (!g_net_ready) {
        serial_write("net: no network\n");
        return 0;
    }
    if (!parse_url(url)) {
        serial_write("net: bad URL\n");
        return 0;
    }

    u32 ip = dns_resolve(g_http_hostname);
    if (ip == 0) {
        serial_write("net: DNS failed\n");
        return 0;
    }

    if (!tcp_connect(ip, 80)) {
        serial_write("net: TCP connect failed\n");
        return 0;
    }

    /* Build HTTP request */
    static char request[512];
    usize rlen = 0;
    const char *get = "GET ";
    const char *http = " HTTP/1.0\r\nHost: ";
    const char *end = "\r\nConnection: close\r\n\r\n";

    const char *parts[] = {get, g_http_path, http, g_http_hostname, end};
    for (int i = 0; i < 5; ++i) {
        usize plen = strlen(parts[i]);
        if (rlen + plen >= sizeof(request)) { tcp_close(); return 0; }
        memcpy(request + rlen, parts[i], plen);
        rlen += plen;
    }

    if (!tcp_send(request, (u16)rlen)) {
        tcp_close();
        return 0;
    }

    if (!tcp_recv_all()) {
        tcp_close();
        return 0;
    }

    /* Find body (skip headers) */
    u32 body_start = 0;
    for (u32 i = 0; i + 3 < g_tcp.rx_len; ++i) {
        if (g_tcp_rx_buf[i] == '\r' && g_tcp_rx_buf[i + 1] == '\n' &&
            g_tcp_rx_buf[i + 2] == '\r' && g_tcp_rx_buf[i + 3] == '\n') {
            body_start = i + 4;
            break;
        }
    }

    u32 body_len = g_tcp.rx_len - body_start;
    if (body_len > out_capacity) body_len = out_capacity;
    memcpy(out_buf, g_tcp_rx_buf + body_start, body_len);
    *out_len = body_len;

    tcp_close();
    return 1;
}

/* ========== Public API ========== */

void net_init(void) {
    if (!rtl8139_init()) {
        serial_write("net: no NIC found\n");
        return;
    }
    rtl8139_get_mac(g_our_mac);

    serial_write("net: starting DHCP...\n");
    for (int att = 0; att < 3; ++att) {
        dhcp_discover();
        for (u32 t = 0; t < 5000000; ++t) {
            net_poll();
            if (g_net_ready) break;
        }
        if (g_net_ready) break;
    }

    if (g_net_ready) {
        serial_write("net: online\n");
    } else {
        serial_write("net: DHCP failed\n");
    }
}

int net_is_ready(void) {
    return g_net_ready;
}

void net_get_ip(u32 *ip, u32 *gw, u32 *dns, u32 *mask) {
    if (ip) *ip = g_our_ip;
    if (gw) *gw = g_gateway_ip;
    if (dns) *dns = g_dns_ip;
    if (mask) *mask = g_subnet_mask;
}

void net_poll(void) {
    const u8 *data;
    u16 len;

    while ((len = rtl8139_poll_packet(&data)) > 0) {
        if (len < sizeof(EthHeader)) continue;
        const EthHeader *eth = (const EthHeader *)data;
        u16 etype = ntohs(eth->ethertype);
        const u8 *payload = data + sizeof(EthHeader);
        u16 payload_len = (u16)(len - sizeof(EthHeader));

        if (etype == ETHERTYPE_ARP) {
            arp_handle(payload, payload_len);
        } else if (etype == ETHERTYPE_IP) {
            if (payload_len < sizeof(Ipv4Header)) continue;
            const Ipv4Header *ip = (const Ipv4Header *)payload;
            u8 proto = ip->proto;
            const u8 *ip_payload = payload + sizeof(Ipv4Header);
            u16 ip_payload_len = (u16)(payload_len - sizeof(Ipv4Header));

            if (proto == IP_PROTO_UDP && ip_payload_len >= sizeof(UdpHeader)) {
                const UdpHeader *udp = (const UdpHeader *)ip_payload;
                u16 dst_port = ntohs(udp->dst_port);
                const u8 *udp_data = ip_payload + sizeof(UdpHeader);
                u16 udp_data_len = (u16)(ip_payload_len - sizeof(UdpHeader));
                if (dst_port == 68) dhcp_handle(udp_data, udp_data_len);
                else if (dst_port == 12345) dns_handle(udp_data, udp_data_len);
            } else if (proto == IP_PROTO_TCP) {
                tcp_handle(ip_payload, ip_payload_len);
            }
        }
    }
}
