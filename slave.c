#include "mdelayhdr.h"
#include "util.h"
#include <arpa/inet.h>
#include <getopt.h>
#include <netinet/tcp.h>
#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <unistd.h>
#define PAYLOAD_SIZE 900
static uint64_t total_received = 0;

struct p2pdelay {
    uint64_t sent_tt; // tt is shortcut for timestamp
    uint64_t recv_tt;
};

static struct p2pdelay* p2pdelay_measurements = NULL;

struct configuration {
    int protocol; /* IPPROTO_TCP or IPPROTO_UDP */
    int number_measure;
    unsigned short port;
    /* below are context */
    const char* remote_ip;
    unsigned short remote_port; // record the udp source port for sendto function, tcp send doesn't need this
};

void parse_options(int argc, char** argv, struct configuration* cfg)
{
    const char* optstring = "up:n:";
    int opt = getopt(argc, argv, optstring);
    memset(cfg, 0, sizeof(struct configuration));
    cfg->protocol = IPPROTO_TCP;
    cfg->port = 9337;
    while (opt != -1) {
        switch (opt) {
        case 'n':
            cfg->number_measure = atoi(optarg);
            break;
        case 'u':
            cfg->protocol = IPPROTO_UDP;
            break;
        case 'p':
            cfg->port = atoi(optarg);
            break;
        default:
            fprintf(stderr, "wrong option!\n");
            exit(EXIT_FAILURE);
        }
        opt = getopt(argc, argv, optstring);
    }
}

static int create_listen_socket(struct configuration* cfg)
{
    int s;
    struct sockaddr_in host_address;
    int domain = SOCK_DGRAM;
    if (cfg->protocol == IPPROTO_TCP)
        domain = SOCK_STREAM;

    memset(&host_address, 0, sizeof(struct sockaddr_in));
    host_address.sin_family = AF_INET;
    host_address.sin_port = htons(cfg->port);
    host_address.sin_addr.s_addr = INADDR_ANY;
    s = socket(AF_INET, domain, cfg->protocol);
    TEST(s >= 0);
    TRY(bind(s, (struct sockaddr*)&host_address, sizeof(host_address)));
    printf("Socket created, listening on port %d\n", cfg->port);
    return s;
}

static int accept_child(int parent, struct configuration* cfg)
{
    int child;
    socklen_t clilen;
    struct sockaddr_in cli_addr;
    clilen = sizeof(cli_addr);

    TRY(listen(parent, 1));
    child = accept(parent, (struct sockaddr*)&cli_addr, &clilen);
    TEST(child >= 0);

    printf("Accept child %s:%d\n", inet_ntoa(cli_addr.sin_addr), ntohs(cli_addr.sin_port));
    if (cfg->protocol == IPPROTO_TCP) {
        TRY(setsockopt(child, IPPROTO_TCP, TCP_NODELAY, &(int) { 1 }, sizeof(int)));
    }
    // ! no use, but still record the remote ip and port
    cfg->remote_ip = inet_ntoa(cli_addr.sin_addr);
    cfg->remote_port = ntohs(cli_addr.sin_port);
    return child;
}

static void echo(int sock, unsigned char* buf, int buflen, struct configuration* cfg)
{
    if (cfg->protocol == IPPROTO_TCP) {
        TRY(send(sock, buf, buflen, 0));
    } else {
        struct sockaddr_in remote_address;
        memset(&remote_address, 0, sizeof(struct sockaddr_in));
        remote_address.sin_family = AF_INET;
        remote_address.sin_addr.s_addr = inet_addr(cfg->remote_ip);
        remote_address.sin_port = htons(cfg->remote_port);
        TRY(sendto(sock, buf, buflen, 0, (struct sockaddr*)&remote_address, sizeof(remote_address)));
    }
}

/* Processing */
static void print_time(struct timespec* ts)
{
    if (ts == NULL) {
        printf("no timestamp\n");
        return;
    }
    /* Hardware timestamping provides three timestamps -
     *   system (software)
     *   transformed (hw converted to sw)
     *   raw (hardware)
     * in that order - though depending on socket option, you may have 0 in
     * some of them.
     */
    // printf("timestamps " TIME_FMT TIME_FMT TIME_FMT "\n",
    //   (uint64_t)ts[0].tv_sec, (uint64_t)ts[0].tv_nsec,
    //   (uint64_t)ts[1].tv_sec, (uint64_t)ts[1].tv_nsec,
    //   (uint64_t)ts[2].tv_sec, (uint64_t)ts[2].tv_nsec );

    struct timeval time_user;
    gettimeofday(&time_user, NULL);
    // printf("time_user : %d.%06d\n", (int) time_user.tv_sec,
    //                                 (int) time_user.tv_usec);

    static uint64_t diff_nic_kernel = 0;
    static uint64_t diff_nic_user = 0;
    static uint64_t diff_kernel_user = 0;
    static int64_t nic_kernel_total_diff = 0;
    uint64_t old_diff_nic_kernel = diff_nic_kernel;

    uint64_t nanoseconds_nic = ts[2].tv_sec * 1000000000 + ts[2].tv_nsec;
    uint64_t nanoseconds_kernel = ts[0].tv_sec * 1000000000 + ts[0].tv_nsec;
    uint64_t nanoseconds_user = time_user.tv_sec * 1000000000 + time_user.tv_usec * 1000;

    printf("nic: %ld, kernel: %ld, user: %ld\n", nanoseconds_nic, nanoseconds_kernel, nanoseconds_user);

    diff_nic_kernel = (ts[0].tv_sec - ts[2].tv_sec) * 1000000000 + (ts[0].tv_nsec - ts[2].tv_nsec);

    // nic_kernel_latency_numbers[total_received++] = diff_nic_kernel; // all latency numbers are in nanoseconds

    if (old_diff_nic_kernel != 0) {
        nic_kernel_total_diff += diff_nic_kernel - old_diff_nic_kernel;
    }
    diff_kernel_user = (time_user.tv_sec - ts[0].tv_sec) * 1000000000 + (time_user.tv_usec * 1000 - ts[0].tv_nsec);
    diff_nic_user = (time_user.tv_sec - ts[2].tv_sec) * 1000000000 + (time_user.tv_usec * 1000 - ts[2].tv_nsec);

    // nic_user_latency_numbers[total_received] = diff_nic_user; // all latency numbers are in nanoseconds
    // kernel_user_latency_numbers[total_received] = diff_kernel_user; // all latency numbers are in nanoseconds

    // printf("Kernel timestamp %lds %ldns\n", ts[0].tv_sec, ts[0].tv_nsec);
    // printf("Kernel timestamp %lds %ldns\n", ts[2].tv_sec, ts[2].tv_nsec);

    // printf("Difference NIC->Kernel: %ld, change of diff_nic_kernel: %ld, at
    // %ld\n", diff_nic_kernel, diff_nic_kernel-old_diff_nic_kernel,
    // nic_kernel_total_diff);

    // printf("Difference NIC->User: %ld\n", diff_nic_user);

    // printf("Difference Kernel->User: %ld\n", diff_kernel_user);
}

/* Given a packet, extract the timestamp(s) */
static void handle_time(struct msghdr* msg, struct configuration* cfg)
{
    struct timespec* ts = NULL;
    struct cmsghdr* cmsg;

    for (cmsg = CMSG_FIRSTHDR(msg); cmsg; cmsg = CMSG_NXTHDR(msg, cmsg)) {
        if (cmsg->cmsg_level != SOL_SOCKET)
            continue;

        switch (cmsg->cmsg_type) {
        case SO_TIMESTAMPNS:
            ts = (struct timespec*)CMSG_DATA(cmsg);
            break;
        case SO_TIMESTAMPING:
            ts = (struct timespec*)CMSG_DATA(cmsg);
            break;
        default:
            /* Ignore other cmsg options */
            break;
        }
    }

    print_time(ts);
}

static int do_recv(int sock, struct configuration* cfg)
{
    struct msghdr msg;
    struct iovec iov;
    struct sockaddr_in host_address;
    char buffer[PAYLOAD_SIZE];
    char control[1024];
    int got;

    /* recvmsg header structure */
    iov.iov_base = buffer;
    iov.iov_len = PAYLOAD_SIZE;
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
    uint32_t pktseq;
    uint64_t t1, t2, t3, t4;
    memcpy(&mdelayhdr, buffer, sizeof(mdelayhdr));
    pktseq = ntohl(mdelayhdr.seq);
    t1 = ntoh64(mdelayhdr.t1);
    t2 = ntoh64(mdelayhdr.t2);
    t3 = ntoh64(mdelayhdr.t3);
    t4 = ntoh64(mdelayhdr.t4);

    struct timespec* ts_tmp = retrieve_timestamp(&msg);
    // handle_time(&msg, cfg);
    if (total_received == 0) { // todo: total_received is always 0
        cfg->remote_ip = inet_ntoa(host_address.sin_addr);
        cfg->remote_port = ntohs(host_address.sin_port);
    }
    printf("Packet %d - %d bytes type: %u\n", pktseq, got, mdelayhdr.type);
    switch (mdelayhdr.type) {
        case DELAY_REQ:
            p2pdelay_measurements[pktseq].recv_tt = ts_tmp->tv_sec * 1000000000ULL + ts_tmp->tv_nsec;
            break;
        case DELAY_REQ_FOLLOW_UP:
            p2pdelay_measurements[pktseq].sent_tt = t2;
            printf("p2p delay is %lu ns.\n", p2pdelay_measurements[pktseq].recv_tt - p2pdelay_measurements[pktseq].sent_tt);
            break;
        default:
            fprintf(stderr, "wrong packet type.\n");
            exit(EXIT_FAILURE);
    }

    // echo(sock, buffer, got, cfg);
    return got;
};

int main(int argc, char** argv)
{
    struct configuration cfg;
    parse_options(argc, argv, &cfg);
    int parent, sock;
    p2pdelay_measurements = calloc(cfg.number_measure, sizeof(struct p2pdelay));
    if (cfg.protocol == IPPROTO_TCP) {
        parent = create_listen_socket(&cfg);
        sock = accept_child(parent, &cfg);
        close(parent);
    } else {
        sock = create_listen_socket(&cfg);
    }
    do_ts_sockopt(sock);
    int got;
    while (got = do_recv(sock, &cfg) && got > 0)
        ;
    close(sock);
    free(p2pdelay_measurements);
    return 0;
}