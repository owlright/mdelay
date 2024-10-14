#include <arpa/inet.h>
#include <errno.h>
#include <getopt.h>
#include <net/ethernet.h> // Add this line to define struct ether_header
#include <net/if.h>
#include <netinet/ether.h>
#include <netinet/if_ether.h>
#include <netinet/ip.h>
#include <netinet/udp.h>
#include <netpacket/packet.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

#include "mdelayhdr.h"
#include "util.h"

#define PACKET_LEN 1024
#define UDP_PAYLOAD_SIZE 900

struct configuration {
    // parsed from cmd arguments
    const char* cfg_iface; /* interface name */
    int cfg_ifidx; /* interface index */
    unsigned short cfg_dport; /* destionation port */
    int cfg_protocol; // ! not supported
    unsigned int cfg_max_packets; /* Stop after this many (0=forever) */

    // context, not parsed from cmd arguments
    int fd; /* sending */
};

int open_fd()
{
    int fd = socket(AF_PACKET, SOCK_RAW, htons(ETH_P_ALL));
    if (fd == -1) {
        perror("socket");
        exit(EXIT_FAILURE);
    }
    return fd;
}

void* send_packets(void* arg)
{
    struct configuration* cfg = (struct configuration*)arg;
    int fd = cfg->fd;
    struct sockaddr_ll sa;
    memset(&sa, 0, sizeof(sa));
    sa.sll_family = AF_PACKET;
    sa.sll_protocol = htons(ETH_P_IP);
    sa.sll_ifindex = cfg->cfg_ifidx;
    sa.sll_hatype = 0;
    sa.sll_pkttype = 0;
    sa.sll_halen = ETH_ALEN;

    // set dest mac addr
    unsigned char dest_mac[ETH_ALEN] = { 0xff, 0xff, 0xff, 0xff, 0xff, 0xff }; // 示例目的 MAC 地址
    memcpy(sa.sll_addr, dest_mac, ETH_ALEN);

    unsigned char buffer[PACKET_LEN];
    memset(buffer, 0, PACKET_LEN);

    // 构建以太网头
    struct ether_header eth_header;
    memcpy(eth_header.ether_dhost, dest_mac, ETH_ALEN); // 目的 MAC 地址
    unsigned char src_mac[ETH_ALEN] = { 0x00, 0x1B, 0x2C, 0x3D, 0x4E, 0x5F }; // 示例源 MAC 地址
    memcpy(eth_header.ether_shost, src_mac, ETH_ALEN); // 源 MAC 地址
    eth_header.ether_type = htons(ETH_P_IP); // 以太网类型

    unsigned char* header = buffer;
    memcpy(header, &eth_header, sizeof(struct ether_header));

    // 构建 IP 头
    struct iphdr ip_header;
    ip_header.version = 4; // IPv4
    ip_header.ihl = 5; // IP 头长度
    ip_header.tos = 0; // 服务类型
    ip_header.tot_len = htons(sizeof(struct iphdr) + sizeof(struct udphdr) + UDP_PAYLOAD_SIZE); // 总长度
    ip_header.id = htons(54321); // 标识
    ip_header.frag_off = 0; // 分片偏移
    ip_header.ttl = 64; // 生存时间
    ip_header.protocol = IPPROTO_UDP; // 协议
    ip_header.check = 0; // 校验和，0 表示由内核计算
    ip_header.saddr = inet_addr("0.0.0.0"); // 源 IP 地址
    ip_header.daddr = inet_addr("0.0.0.0"); // 目的 IP 地址
    // 计算 IP 头校验和
    ip_header.check = 0;
    unsigned short* ip_header_ptr = (unsigned short*)&ip_header;
    unsigned int ip_checksum = 0;
    for (int i = 0; i < sizeof(struct iphdr) / 2; i++) {
        ip_checksum += ip_header_ptr[i];
    }
    ip_checksum = (ip_checksum >> 16) + (ip_checksum & 0xFFFF);
    ip_checksum += (ip_checksum >> 16);
    ip_header.check = (unsigned short)(~ip_checksum);

    // 将 IP 头复制到 buffer 中
    header += sizeof(struct ether_header);
    memcpy(header, &ip_header, sizeof(struct iphdr));

    // 构建 UDP 头
    struct udphdr udp_header;
    udp_header.source = htons(1336); // 源端口
    udp_header.dest = htons(1337); // 目的端口
    udp_header.len = htons(UDP_PAYLOAD_SIZE + sizeof(struct udphdr)); // UDP 包长度
    udp_header.check = 0; // 校验和，0 表示由内核计算

    // 将 UDP 头复制到 buffer 中
    header += sizeof(struct iphdr);
    memcpy(header, &udp_header, sizeof(struct udphdr));

    // self defined header
    unsigned char payload[UDP_PAYLOAD_SIZE];
    memset(payload, 'A', UDP_PAYLOAD_SIZE); // for debugging?
    header += sizeof(struct udphdr);
    memcpy(header, payload, UDP_PAYLOAD_SIZE);
    struct mdelayhdr mdelayhdr;
    memset(&mdelayhdr, 0, sizeof(mdelayhdr));
    for (int i = 0; i < cfg->cfg_max_packets; i++) {
        usleep(200);
        mdelayhdr.seq = htonl(i);
        struct timeval tv;
        gettimeofday(&tv, NULL);
        uint64_t timestamp_nanos = tv.tv_sec * 1000000000ULL + tv.tv_usec * 1000ULL;
        mdelayhdr.t1 = hton64(timestamp_nanos);
        // memcpy(buffer + 42, &timestamp_nanos, sizeof(timestamp_nanos));
        memcpy(header, &mdelayhdr, sizeof(mdelayhdr));
        if (sendto(fd, buffer, PACKET_LEN, 0, (struct sockaddr*)&sa, sizeof(sa)) < 0) {
            perror("sendto");
            exit(EXIT_FAILURE);
        }
    }
    return NULL;
}

static void parse_options(int argc, char** argv, struct configuration* cfg)
{
    int option_index = 0;
    int opt;
    static struct option long_options[] = {
        { "interface", required_argument, 0, 'i' },
        { "dport", required_argument, 0, 'p' },
        { "protocol", required_argument, 0, 'P' },
        { "max", required_argument, 0, 'n' },
        { 0, no_argument, 0, 0 },
    };
    const char* optstring = "i:p:P:n:";

    /* Defaults */
    bzero(cfg, sizeof(struct configuration));
    cfg->cfg_iface = "eth0";
    cfg->cfg_dport = 1337;
    cfg->cfg_protocol = IPPROTO_UDP;

    opt = getopt_long(argc, argv, optstring, long_options, &option_index);
    while (opt != -1) {
        switch (opt) {
        case 'i':
            cfg->cfg_iface = optarg;
            unsigned int if_idx = if_nametoindex(cfg->cfg_iface);
            if (if_idx == 0) {
                fprintf(stderr, "Error: Interface %s not found.\n", cfg->cfg_iface);
                exit(EXIT_FAILURE);
            }
            cfg->cfg_ifidx = if_idx;
            break;
        case 'p':
            cfg->cfg_dport = atoi(optarg);
            break;
        case 'n':
            cfg->cfg_max_packets = atoi(optarg);
            break;
        default:
            fprintf(stderr, "Invalid option\n");
            exit(EXIT_FAILURE);
            break;
        }
        opt = getopt_long(argc, argv, optstring, long_options, &option_index);
    }
}

int main(int argc, char* argv[])
{
    struct configuration cfg;
    parse_options(argc, argv, &cfg);
    int fd = open_fd();
    cfg.fd = fd;
    pthread_t thread;
    pthread_create(&thread, NULL, send_packets, &cfg);

    int sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock < 0) {
        perror("socket");
        exit(EXIT_FAILURE);
    }

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(4200);
    addr.sin_addr.s_addr = htonl(INADDR_ANY);

    if (bind(sock, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        perror("bind");
        exit(EXIT_FAILURE);
    }

    unsigned char buf[UDP_PAYLOAD_SIZE];
    // uint64_t latency_numbers[TOTAL_PACKETS];
    uint64_t* latency_numbers = malloc(cfg.cfg_max_packets * sizeof(uint64_t));
    uint64_t previous_received_timestamp = 0;
    struct mdelayhdr mdelayhdr;
    for (int i = 0; i < cfg.cfg_max_packets; i++) {
        recvfrom(sock, buf, UDP_PAYLOAD_SIZE, 0, NULL, NULL);

        struct timeval tv;
        gettimeofday(&tv, NULL);
        uint64_t timestamp_nanos = tv.tv_sec * 1000000000ULL + tv.tv_usec * 1000ULL;

        // uint64_t received_timestamp;
        // memcpy(&received_timestamp, buf, sizeof(received_timestamp));
        memcpy(&mdelayhdr, buf, sizeof(mdelayhdr));
        uint64_t received_timestamp = ntoh64(mdelayhdr.t1);
        if (previous_received_timestamp == received_timestamp) {
            fprintf(stderr, "Unlikely\n");
            exit(EXIT_FAILURE);
        }
        previous_received_timestamp = received_timestamp;

        uint64_t latency = timestamp_nanos - received_timestamp;
        latency_numbers[i] = latency;
    }

    FILE* file = fopen("end_to_end_latency.txt", "w");
    if (!file) {
        perror("fopen");
        exit(EXIT_FAILURE);
    }

    for (int i = 0; i < cfg.cfg_max_packets; i++) {
        fprintf(file, "%lu\n", latency_numbers[i]);
    }
    free(latency_numbers);
    fclose(file);
    close(fd);
    close(sock);
    return 0;
}