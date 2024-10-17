#include "mdelayhdr.h"
#include "util.h"
#include <arpa/inet.h>
#include <linux/net_tstamp.h>
#include <getopt.h>
#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <unistd.h>

struct configuration {
    int protocol; /* IPPROTO_TCP or IPPROTO_UDP */
    unsigned short port;
    /* below are context */
    const char* remote_ip;
    unsigned short remote_port; // record the udp source port for sendto function, tcp send doesn't need this
};

void parse_options(int argc, char** argv, struct configuration* cfg)
{
    const char* optstring = "up:";
    int opt = getopt(argc, argv, optstring);
    memset(cfg, 0, sizeof(struct configuration));
    cfg->protocol = IPPROTO_TCP;
    cfg->port = 9337;
    while (opt != -1) {
        switch (opt) {
        case 'u':
            cfg->protocol = IPPROTO_UDP;
            break;
        case 'p':
            cfg->port = atoi(optarg);
            break;
        default:
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
        socklen_t remote_address_len = sizeof(remote_address);
        memset(&remote_address, 0, sizeof(struct sockaddr_in));
        remote_address.sin_family = AF_INET;
        remote_address.sin_addr.s_addr = inet_addr(cfg->remote_ip);
        remote_address.sin_port = htons(cfg->remote_port);
        TRY(sendto(sock, buf, buflen, 0, (struct sockaddr*)&remote_address, remote_address_len));
    }
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
    memset(&host_address, 0, sizeof(struct sockaddr_in));
    host_address.sin_family = AF_INET;
    host_address.sin_port = htons(cfg->port);
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
    echo(sock, buffer, got, cfg);
    return got;
};

/* This routine selects the correct socket option to enable timestamping. */
static void do_ts_sockopt(struct configuration* cfg, int sock)
{
    printf("Selecting hardware timestamping mode.\n");

    {
        int enable = SOF_TIMESTAMPING_RX_HARDWARE | SOF_TIMESTAMPING_RAW_HARDWARE | SOF_TIMESTAMPING_SYS_HARDWARE
            | SOF_TIMESTAMPING_SOFTWARE;
        TRY(setsockopt(sock, SOL_SOCKET, SO_TIMESTAMPING, &enable, sizeof(int)));
        printf("enabled timestamping sockopt\n");
    }
}

int main(int argc, char** argv)
{
    struct configuration cfg;
    parse_options(argc, argv, &cfg);
    int parent, sock;
    if (cfg.protocol == IPPROTO_TCP) {
        parent = create_listen_socket(&cfg);
        sock = accept_child(parent, &cfg);
        close(parent);
    } else {
        sock = create_listen_socket(&cfg);
    }
    do_ts_sockopt(&cfg, sock);
    int got;
    while (got = do_recv(sock, &cfg) && got > 0);
    close(sock);
    return 0;
}