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
    if (cfg->protocol == IPPROTO_TCP) {
        TRY(setsockopt(s, IPPROTO_TCP, TCP_NODELAY, &(int) { 1 }, sizeof(int)));
    }
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

void* send_packets(void* arg)
{
    struct thread_args* thread_args = (struct thread_args*)arg;
    int fd = thread_args->fd;
    struct configuration* cfg = thread_args->cfg;

    struct msghdr msg;
    struct iovec iov;
    struct sockaddr_in host_address;

    // self defined header
    unsigned char payload[PAYLOAD_SIZE];
    memset(payload, 'A', PAYLOAD_SIZE); // for debugging?
    char control[1024];
    iov.iov_base = payload;
    iov.iov_len = PAYLOAD_SIZE;
    msg.msg_iov = &iov;
    msg.msg_iovlen = 1;
    msg.msg_name = NULL;
    msg.msg_namelen = 0;
    msg.msg_control = control;
    msg.msg_controllen = 1024;
    struct sockaddr_in sa; // for udp socket use
    if (cfg->protocol == IPPROTO_UDP) {
        memset(&sa, 0, sizeof(sa));
        sa.sin_family = AF_INET;
        sa.sin_port = htons(cfg->dport);
        sa.sin_addr.s_addr = inet_addr(cfg->slave_ip);
        msg.msg_name = &sa;
        msg.msg_namelen = sizeof(sa);
    } else {
        msg.msg_name = NULL;
        msg.msg_namelen = 0;
    }

    struct mdelayhdr mdelayhdr;
    memset(&mdelayhdr, 0, sizeof(mdelayhdr));
    for (int i = 0; i < cfg->max_packets; i++) {
        usleep(200);
        mdelayhdr.seq = htonl(i);
        struct timeval tv;
        gettimeofday(&tv, NULL);
        uint64_t timestamp_nanos = tv.tv_sec * 1000000000ULL + tv.tv_usec * 1000ULL;
        mdelayhdr.t1 = hton64(timestamp_nanos);
        memcpy(payload, &mdelayhdr, sizeof(mdelayhdr));
        printf("Sending packet %d\n", i);
        TRY(sendmsg(fd, &msg, 0));
        handle_time(&msg, cfg);
        // if (cfg->protocol == IPPROTO_TCP) {
        //     TRY(send(fd, payload, PAYLOAD_SIZE, 0) < 0);
        // } else {
        //     TRY(sendto(fd, payload, PAYLOAD_SIZE, 0, (struct sockaddr*)&sa, sizeof(sa)) < 0);
        // }
    }
    return NULL;
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
    memset(&mdelayhdr, 0, sizeof(mdelayhdr));
    memcpy(&mdelayhdr, buffer, sizeof(mdelayhdr));
    printf("Packet %d - %d bytes\n", ntohl(mdelayhdr.seq), got);
    return got;
};

int main(int argc, char** argv)
{
    struct configuration cfg;
    parse_options(argc, argv, &cfg);
    int sock;
    sock = create_send_socket(&cfg);
    TEST(sock >= 0);
    do_ts_sockopt(sock);
    pthread_t thread;
    struct thread_args arg = { sock, &cfg };
    TRY(pthread_create(&thread, NULL, send_packets, &arg));

    int echo;
    while (echo = do_recv(sock, &cfg) && echo > 0)
        ;
    pthread_join(thread, NULL);
    close(sock);
    return 0;
}