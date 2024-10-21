#ifndef util_h
#define util_h
#include <stdint.h>
#include <stdio.h>
#include <errno.h>
#include <linux/net_tstamp.h>
#include <string.h>
#include <stdlib.h>
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

uint64_t hton64(uint64_t value);
uint64_t ntoh64(uint64_t value);
void do_ts_sockopt(int sock);
#endif