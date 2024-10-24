#ifndef util_h
#define util_h
#include <errno.h>
#include <linux/net_tstamp.h>
#include <netinet/in.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>
/* Assert-like macros */
#define TEST(x)                                                                                                        \
    do {                                                                                                               \
        if (!(x)) {                                                                                                    \
            fprintf(stderr, "ERROR: '%s' failed\n", #x);                                                               \
            fprintf(stderr, "ERROR: at %s:%d\n", __FILE__, __LINE__);                                                  \
            exit(1);                                                                                                   \
        }                                                                                                              \
    } while (0)

#define TRY(x)                                                                                                         \
    do {                                                                                                               \
        int __rc = (x);                                                                                                \
        if (__rc < 0) {                                                                                                \
            fprintf(stderr, "ERROR: TRY(%s) failed\n", #x);                                                            \
            fprintf(stderr, "ERROR: at %s:%d\n", __FILE__, __LINE__);                                                  \
            fprintf(stderr, "ERROR: rc=%d errno=%d (%s)\n", __rc, errno, strerror(errno));                             \
            exit(1);                                                                                                   \
        }                                                                                                              \
    } while (0)

#define REASON(x)                                                                                                      \
    do {                                                                                                               \
        if ((x) < 0) {                                                                                                 \
            fprintf(stderr, "ERROR: %s\n", #x);                                                                        \
            fprintf(stderr, "ERROR: at %s:%d\n", __FILE__, __LINE__);                                                  \
            fprintf(stderr, "ERROR: errno=%d (%s)\n", errno, strerror(errno));                                         \
            exit(1);                                                                                                   \
        }                                                                                                              \
    } while (0)

struct p2pdelay {
    uint64_t sent_tt; // tt is shortcut for timestamp
    uint64_t recv_tt;
};

uint64_t hton64(uint64_t value);
uint64_t ntoh64(uint64_t value);
void do_ts_sockopt(int sock);
struct timespec* retrieve_timestamp(struct msghdr* msg);
void send_udp_packets_timestamp(int sock, const struct sockaddr_in* dsa, int pkttype, int pktsize, int N, int seq);
#endif