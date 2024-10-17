#include "mdelayhdr.h"
#include "option.h"
#include "util.h"
#include <arpa/inet.h>
#include <getopt.h>
#include <pthread.h>
#include <sys/time.h>
#include <unistd.h>

#define PAYLOAD_SIZE 900
static uint64_t total_received = 0;
static uint64_t* nic_kernel_latency_numbers = NULL;
static uint64_t* nic_user_latency_numbers = NULL;
static uint64_t* kernel_user_latency_numbers = NULL;

static int create_listen_socket(struct configuration* cfg)
{
    int s;
    struct sockaddr_in host_address;
    int domain = SOCK_DGRAM;
    if (cfg->protocol == IPPROTO_TCP)
        domain = SOCK_STREAM;

    memset(&host_address, 0, sizeof(struct sockaddr_in));
    host_address.sin_family = AF_INET;
    host_address.sin_port = htons(cfg->server_port);
    host_address.sin_addr.s_addr = INADDR_ANY;
    s = socket(AF_INET, domain, cfg->protocol);
    TEST(s >= 0);
    if (cfg->server) {
        TRY(bind(s, (struct sockaddr*)&host_address, sizeof(host_address)));
        printf("Socket created, listening on port %d\n", cfg->server_port);
    } else {
        /* If I'm the client */
    }
    return s;
}

static int accept_child(int parent)
{
    int child;
    socklen_t clilen;
    struct sockaddr_in cli_addr;
    clilen = sizeof(cli_addr);

    TRY(listen(parent, 1));
    child = accept(parent, (struct sockaddr*)&cli_addr, &clilen);
    TEST(child >= 0);

    printf("Accept child %s:%d\n", inet_ntoa(cli_addr.sin_addr), ntohs(cli_addr.sin_port));
    return child;
}

static int create_send_socket(struct configuration* cfg)
{
    int s;
    struct sockaddr_in server_address, client_address;
    int domain = SOCK_DGRAM;
    if (cfg->protocol == IPPROTO_TCP)
        domain = SOCK_STREAM;

    s = socket(AF_INET, domain, cfg->protocol);

    memset(&client_address, 0, sizeof(struct sockaddr_in));
    client_address.sin_family = AF_INET;
    client_address.sin_addr.s_addr = INADDR_ANY;
    client_address.sin_port = htons(cfg->client_port);
    TRY(bind(s, (struct sockaddr*)&client_address, sizeof(client_address)));

    memset(&server_address, 0, sizeof(struct sockaddr_in));
    server_address.sin_family = AF_INET;
    server_address.sin_port = htons(cfg->server_port);
    // printf("Will send to %s:%d\n", cfg->server_ip, cfg->client_port);
    server_address.sin_addr.s_addr = inet_addr(cfg->server_ip);

    if (cfg->protocol == IPPROTO_TCP) {
        TRY(connect(s, (struct sockaddr*)&server_address, sizeof(server_address)));
        printf("tcp socket connected to %s:%d\n", inet_ntoa(server_address.sin_addr), ntohs(server_address.sin_port) );
    }
    return s;
}
struct thread_args {
    int fd;
    struct configuration* cfg;
};

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
        sa.sin_port = htons(cfg->server_port);
        sa.sin_addr.s_addr = inet_addr(cfg->server_ip);
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
            if (send(fd, payload, PAYLOAD_SIZE, 0) < 0) {
                perror("send");
                exit(EXIT_FAILURE);
            }
        } else {
            if (sendto(fd, payload, PAYLOAD_SIZE, 0, (struct sockaddr*)&sa, sizeof(sa)) < 0) {
                perror("sendto");
                exit(EXIT_FAILURE);
            }
        }
    }
    return NULL;
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

    // printf("nic: %ld, kernel: %ld, user: %ld\n", nanoseconds_nic,
    //        nanoseconds_kernel, nanoseconds_user);

    diff_nic_kernel = (ts[0].tv_sec - ts[2].tv_sec) * 1000000000 + (ts[0].tv_nsec - ts[2].tv_nsec);

    nic_kernel_latency_numbers[total_received++] = diff_nic_kernel; // all latency numbers are in nanoseconds

    if (old_diff_nic_kernel != 0) {
        nic_kernel_total_diff += diff_nic_kernel - old_diff_nic_kernel;
    }
    diff_kernel_user = (time_user.tv_sec - ts[0].tv_sec) * 1000000000 + (time_user.tv_usec * 1000 - ts[0].tv_nsec);
    diff_nic_user = (time_user.tv_sec - ts[2].tv_sec) * 1000000000 + (time_user.tv_usec * 1000 - ts[2].tv_nsec);

    nic_user_latency_numbers[total_received] = diff_nic_user; // all latency numbers are in nanoseconds
    kernel_user_latency_numbers[total_received] = diff_kernel_user; // all latency numbers are in nanoseconds

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

    // print_time(ts);
}

void broadcast(const char* buffer, int buffer_len)
{
    static int sockfd = -1;
    static struct sockaddr_in server_addr;

    if (sockfd == -1) {
        // Create a UDP socket
        sockfd = socket(AF_INET, SOCK_DGRAM, 0);
        if (sockfd < 0) {
            perror("socket");
            exit(1);
        }
        // Enable broadcast option
        int broadcastEnable = 1;
        if (setsockopt(sockfd, SOL_SOCKET, SO_BROADCAST, &broadcastEnable, sizeof(broadcastEnable)) < 0) {
            perror("setsockopt");
            exit(1);
        }
        // Set up the server address structure
        memset(&server_addr, 0, sizeof(server_addr));
        server_addr.sin_family = AF_INET;
        server_addr.sin_port = htons(4200);
        server_addr.sin_addr.s_addr = inet_addr("255.255.255.255");
    }

    // Send the UDP packet
    if (sendto(sockfd, buffer, buffer_len, 0, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        perror("sendto");
        exit(1);
    }
}

/* Receive a packet, and print out the timestamps from it */
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
    host_address.sin_port = htons(cfg->server_port);
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
    if (!got && errno == EAGAIN)
        return 0;
    struct mdelayhdr mdelayhdr;
    memcpy(&mdelayhdr, buffer, sizeof(mdelayhdr));

    printf("Packet %d - %d bytes\n", ntohl(mdelayhdr.seq), got);
    // handle_time(&msg, cfg);
    // broadcast(buffer, got);
    return got;
};


int main(int argc, char** argv)
{
    struct configuration cfg;

    parse_options(argc, argv, &cfg);

    nic_user_latency_numbers = malloc(cfg.max_packets * sizeof(uint64_t));
    nic_kernel_latency_numbers = malloc(cfg.max_packets * sizeof(uint64_t));
    kernel_user_latency_numbers = malloc(cfg.max_packets * sizeof(uint64_t));

    char buffer[2048];
    struct sockaddr_in client_address;
    socklen_t client_address_len = sizeof(client_address);
    int sock;

    if (cfg.server) {
        int parent, got;
        parent = create_listen_socket(&cfg);
        sock = parent;
        if (cfg.protocol == IPPROTO_TCP)
            sock = accept_child(parent);

        while (got = do_recv(sock, &cfg) && got > 0) {
            // printf("Received %d bytes\n", got);

        }

    } else {
        sock = create_send_socket(&cfg);
        pthread_t thread;
        struct thread_args arg = { sock, &cfg };
        TRY(pthread_create(&thread, NULL, send_packets, &arg));
        int echo;
        while (got = recv)
        pthread_join(thread, NULL);
    }
    close(sock);
    return 0;
}