#include "mdelayhdr.h"
#include "util.h"
#include <stdlib.h>
#include <string.h>
#include <arpa/inet.h>
#include <getopt.h>
#include <pthread.h>
#include <sys/time.h>
#include <unistd.h>
#define PAYLOAD_SIZE 900
struct configuration {
    int protocol; /* IPPROTO_TCP or IPPROTO_UDP */
    const char* slave_ip;
    unsigned short sport; // source port
    unsigned short dport; // destination port
    unsigned int max_packets; /* Stop after this many (0=forever) */
};

void parse_options(int argc, char** argv, struct configuration* cfg)
{
    int option_index = 0;
    int opt;
    static struct option long_options[] = {
        { "slave", required_argument, 0, 's' },
        { "sport", required_argument, 0, 'l' },
        { "dport", required_argument, 0, 'd' },
        { "udp", no_argument, 0, 'u' },
        { "number", required_argument, 0, 'n' },
        { 0, no_argument, 0, 0 },
    };
    const char* optstring = "s:l:d:un:";
    memset(cfg, 0, sizeof(struct configuration));
    cfg->protocol = IPPROTO_TCP;
    cfg->sport = 9336;
    cfg->dport = 9337;
    cfg->max_packets = 10;
    opt = getopt(argc, argv, optstring);
    while (opt != -1) {
        switch (opt) {
        case 's':
            cfg->slave_ip = optarg;
            break;
        case 'l':
            cfg->sport = atoi(optarg);
            break;
        case 'd':
            cfg->dport = atoi(optarg);
            break;
        case 'u':
            cfg->protocol = IPPROTO_UDP;
            break;
        case 'n':
            cfg->max_packets = atoi(optarg);
            break;
        default:
            exit(EXIT_FAILURE);
        }
        opt = getopt(argc, argv, optstring);
    }
}
struct thread_args {
    int fd;
    struct configuration* cfg;
};

static int create_send_socket(struct configuration* cfg)
{
    int s;
    struct sockaddr_in remote_address, local_address;
    int domain = SOCK_DGRAM;
    if (cfg->protocol == IPPROTO_TCP)
        domain = SOCK_STREAM;

    s = socket(AF_INET, domain, cfg->protocol);

    memset(&local_address, 0, sizeof(struct sockaddr_in));
    local_address.sin_family = AF_INET;
    local_address.sin_addr.s_addr = INADDR_ANY;
    local_address.sin_port = htons(cfg->sport);
    printf("Bind to %s:%d\n", inet_ntoa(local_address.sin_addr), ntohs(local_address.sin_port));
    TRY(bind(s, (struct sockaddr*)&local_address, sizeof(local_address)));

    memset(&remote_address, 0, sizeof(struct sockaddr_in));
    remote_address.sin_family = AF_INET;
    remote_address.sin_port = htons(cfg->dport);
    printf("Send to %s:%d\n", cfg->slave_ip, cfg->dport);
    remote_address.sin_addr.s_addr = inet_addr(cfg->slave_ip);

    if (cfg->protocol == IPPROTO_TCP) {
        TRY(connect(s, (struct sockaddr*)&remote_address, sizeof(remote_address)));
        printf("Tcp socket connected to %s:%d\n", inet_ntoa(remote_address.sin_addr), ntohs(remote_address.sin_port));
    }
    return s;
}

void* send_packets(void* arg)
{
    struct thread_args* thread_args = (struct thread_args*)arg;
    int fd = thread_args->fd;
    struct configuration* cfg = thread_args->cfg;
    // self defined header
    unsigned char payload[PAYLOAD_SIZE];
    memset(payload, 'A', PAYLOAD_SIZE); // for debugging?

    struct mdelayhdr mdelayhdr;
    memset(&mdelayhdr, 0, sizeof(mdelayhdr));

    struct sockaddr_in sa; // for udp socket use
    if (cfg->protocol == IPPROTO_UDP) {
        memset(&sa, 0, sizeof(sa));
        sa.sin_family = AF_INET;
        sa.sin_port = htons(cfg->dport);
        sa.sin_addr.s_addr = inet_addr(cfg->slave_ip);
    }
    for (int i = 0; i < cfg->max_packets; i++) {
        usleep(200);
        mdelayhdr.seq = htonl(i);
        struct timeval tv;
        gettimeofday(&tv, NULL);
        uint64_t timestamp_nanos = tv.tv_sec * 1000000000ULL + tv.tv_usec * 1000ULL;
        mdelayhdr.t1 = hton64(timestamp_nanos);
        // memcpy(buffer + 42, &timestamp_nanos, sizeof(timestamp_nanos));
        memcpy(payload, &mdelayhdr, sizeof(mdelayhdr));
        printf("Sending packet %d\n", i);
        if (cfg->protocol == IPPROTO_TCP) {
            TRY(send(fd, payload, PAYLOAD_SIZE, 0) < 0);
        } else {
            TRY(sendto(fd, payload, PAYLOAD_SIZE, 0, (struct sockaddr*)&sa, sizeof(sa)) < 0);
        }
    }
    return NULL;
}

static int do_recv(int sock, struct configuration* cfg)
{
    struct msghdr msg;
    struct iovec iov;
    struct sockaddr_in host_address;
    char buffer[2048];
    char control[1024];
    int got;

    /* recvmsg header structure */
    // make_address(0, &host_address);
    memset(&host_address, 0, sizeof(struct sockaddr_in));
    host_address.sin_family = AF_INET;
    host_address.sin_port = htons(cfg->dport);
    iov.iov_base = buffer;
    iov.iov_len = 2048;
    msg.msg_iov = &iov;
    msg.msg_iovlen = 1;
    msg.msg_name = &host_address;
    msg.msg_namelen = sizeof(struct sockaddr_in);
    msg.msg_control = control;
    msg.msg_controllen = 1024;

    /* block for message */
    got = recvmsg(sock, &msg, 0);
    if (!got)
        return 0;
    struct mdelayhdr mdelayhdr;
    memcpy(&mdelayhdr, buffer, sizeof(mdelayhdr));
    uint64_t received_timestamp = ntoh64(mdelayhdr.t1);
    uint32_t seq = ntohl(mdelayhdr.seq);
    printf("Echo seq: %u\n", seq);
    return got;
};

int main(int argc, char** argv)
{
    struct configuration cfg;
    parse_options(argc, argv, &cfg);
    int sock;
    sock = create_send_socket(&cfg);
    pthread_t thread;
    struct thread_args arg = { sock, &cfg };
    TRY(pthread_create(&thread, NULL, send_packets, &arg));

    int echo;
    while (echo = do_recv(sock, &cfg) && echo > 0);
    pthread_join(thread, NULL);
    close(sock);
    return 0;
}