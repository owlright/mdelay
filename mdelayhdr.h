#ifndef mdelayhdr_h
#define mdelayhdr_h
#include <stdint.h>

#define DELAY_REQ 0
#define DELAY_REQ_FOLLOW_UP 1
#define DELAY_RESP 2
#define DELAY_RESP_FOLLOW_UP 3

struct mdelayhdr {
    uint32_t seq;
    uint8_t type;
    uint64_t t1;
    uint64_t t2;
    uint64_t t3;
    uint64_t t4;
};

#endif // mdelayhdr_h