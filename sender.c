#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <netinet/if_ether.h>
#include <netinet/ip.h>
#include <netinet/udp.h>
#include <netpacket/packet.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <net/if.h>
#include <pthread.h>
#include <errno.h>

#define PACKET_LEN 1024
#define UDP_PAYLOAD_SIZE 900
#define TOTAL_PACKETS 10

int open_fd() {
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

void *send_packets(void *arg) {
    struct thread_args *args = (struct thread_args *)arg;
    int fd = args->fd;
    struct sockaddr_ll sa;
    memset(&sa, 0, sizeof(sa));
    sa.sll_family = AF_PACKET;
    sa.sll_protocol = htons(ETH_P_IP);
    sa.sll_ifindex = args->if_idx;

    unsigned char buffer[PACKET_LEN];
    memset(buffer, 0, PACKET_LEN);

    for (int i = 0; i < TOTAL_PACKETS; i++) {
        usleep(200);

        struct timeval tv;
        gettimeofday(&tv, NULL);
        uint64_t timestamp_nanos = tv.tv_sec * 1000000000ULL + tv.tv_usec * 1000ULL;
        memcpy(buffer + 42, &timestamp_nanos, sizeof(timestamp_nanos));

        if (sendto(fd, buffer, PACKET_LEN, 0, (struct sockaddr *)&sa, sizeof(sa)) < 0) {
            perror("sendto");
            exit(EXIT_FAILURE);
        }
    }
    return NULL;
}

int main(int argc, char *argv[]) {
    const char *interface = (argc >= 2) ? argv[1] : "eth0";
    unsigned int if_idx = if_nametoindex(interface);
    if (if_idx == 0) {
        fprintf(stderr, "Error: Interface %s not found.\n", interface);
        exit(EXIT_FAILURE);
    }

    int fd = open_fd();

    struct thread_args args = {fd, if_idx};
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

    if (bind(sock, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
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

    FILE *file = fopen("end_to_end_latency.txt", "w");
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