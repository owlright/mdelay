#include <arpa/inet.h>
#include "util.h"

uint64_t hton64(uint64_t value)
{
    if (htonl(1) != 1) {
        return ((uint64_t)htonl(value & 0xFFFFFFFF) << 32) | htonl(value >> 32);
    } else {
        return value;
    }
}

uint64_t ntoh64(uint64_t value)
{
    if (ntohl(1) != 1) {
        return ((uint64_t)ntohl(value & 0xFFFFFFFF) << 32) | ntohl(value >> 32);
    } else {
        return value;
    }
}

/* This routine selects the correct socket option to enable timestamping. */
void do_ts_sockopt(int sock)
{
    printf("Selecting hardware timestamping mode.\n");

    {
        /* https://www.kernel.org/doc/html/v6.3/networking/timestamping.html
        SOF_TIMESTAMPING_OPT_TX_SWHW:
                Request both hardware and software timestamps for outgoing packets when
                SOF_TIMESTAMPING_TX_HARDWARE and SOF_TIMESTAMPING_TX_SOFTWARE are enabled at the same time.
                If both timestamps are generated, two separate messages will be looped to the socket’s error queue,
                each containing just one timestamp.
        */
        int enable = SOF_TIMESTAMPING_TX_SOFTWARE | SOF_TIMESTAMPING_TX_HARDWARE | SOF_TIMESTAMPING_RX_HARDWARE | SOF_TIMESTAMPING_RAW_HARDWARE
            | SOF_TIMESTAMPING_SOFTWARE | SOF_TIMESTAMPING_OPT_TX_SWHW;
        TRY(setsockopt(sock, SOL_SOCKET, SO_TIMESTAMPING, &enable, sizeof(int)));
        printf("enabled timestamping sockopt\n");
    }
}
