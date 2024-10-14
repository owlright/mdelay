#ifndef mdelayhdr_h
#define mdelayhdr_h
#include <stdint.h>

struct mdelayhdr {
    uint32_t seq;
    uint64_t t1;
    uint64_t t2;
    uint64_t t3;
    uint64_t t4;
};

#endif // mdelayhdr_h