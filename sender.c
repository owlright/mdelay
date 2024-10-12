#include <arpa/inet.h>
#include <errno.h>
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

#define PACKET_LEN 1024
#define UDP_PAYLOAD_SIZE 900
#define TOTAL_PACKETS 10

int open_fd()
{
    int fd = socket(AF_PACKET, SOCK_RAW, htons(ETH_P_ALL));
    if (fd == -1) {
        perror("socket");
        exit(EXIT_FAILURE);
    }
    return fd;
}

struct thread_args {
    int fd;
    unsigned int if_idx;
};

void* send_packets(void* arg)
{
    struct thread_args* args = (struct thread_args*)arg;
    int fd = args->fd;
    struct sockaddr_ll sa;
    memset(&sa, 0, sizeof(sa));
    sa.sll_family = AF_PACKET;
    sa.sll_protocol = htons(ETH_P_IP);
    sa.sll_ifindex = args->if_idx;
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

    memcpy(buffer, &eth_header, sizeof(struct ether_header));

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
    ip_header.saddr = inet_addr("192.168.1.1"); // 源 IP 地址
    ip_header.daddr = inet_addr("192.168.1.2"); // 目的 IP 地址
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
    memcpy(buffer + sizeof(struct ether_header), &ip_header, sizeof(struct iphdr));

    // 构建 UDP 头
    struct udphdr udp_header;
    udp_header.source = htons(1336); // 源端口
    udp_header.dest = htons(1337); // 目的端口
    udp_header.len = htons(UDP_PAYLOAD_SIZE + sizeof(struct udphdr)); // UDP 包长度
    udp_header.check = 0; // 校验和，0 表示由内核计算

    // 将 UDP 头复制到 buffer 中
    memcpy(buffer + sizeof(struct ether_header) + sizeof(struct iphdr), &udp_header,
        sizeof(struct udphdr)); // 假设以太网头和 IP 头共占 34 字节
    unsigned char payload[UDP_PAYLOAD_SIZE];
    memset(payload, 'A', UDP_PAYLOAD_SIZE);
    memcpy(buffer + 42, payload, UDP_PAYLOAD_SIZE);

    for (int i = 0; i < TOTAL_PACKETS; i++) {
        usleep(200);

        struct timeval tv;
        gettimeofday(&tv, NULL);
        uint64_t timestamp_nanos = tv.tv_sec * 1000000000ULL + tv.tv_usec * 1000ULL;
        memcpy(buffer + 42, &timestamp_nanos, sizeof(timestamp_nanos));

        if (sendto(fd, buffer, PACKET_LEN, 0, (struct sockaddr*)&sa, sizeof(sa)) < 0) {
            perror("sendto");
            exit(EXIT_FAILURE);
        }
    }
    return NULL;
}

int main(int argc, char* argv[])
{
    const char* interface = (argc >= 2) ? argv[1] : "eth0";
    unsigned int if_idx = if_nametoindex(interface);
    if (if_idx == 0) {
        fprintf(stderr, "Error: Interface %s not found.\n", interface);
        exit(EXIT_FAILURE);
    }

    int fd = open_fd();

    struct thread_args args = { fd, if_idx };
    pthread_t thread;
    pthread_create(&thread, NULL, send_packets, &args);

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
    uint64_t latency_numbers[TOTAL_PACKETS];
    uint64_t previous_received_timestamp = 0;

    for (int i = 0; i < TOTAL_PACKETS; i++) {
        recvfrom(sock, buf, UDP_PAYLOAD_SIZE, 0, NULL, NULL);

        struct timeval tv;
        gettimeofday(&tv, NULL);
        uint64_t timestamp_nanos = tv.tv_sec * 1000000000ULL + tv.tv_usec * 1000ULL;

        uint64_t received_timestamp;
        memcpy(&received_timestamp, buf, sizeof(received_timestamp));

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

    for (int i = 0; i < TOTAL_PACKETS; i++) {
        fprintf(file, "%lu\n", latency_numbers[i]);
    }

    fclose(file);
    close(fd);
    close(sock);
    return 0;
}