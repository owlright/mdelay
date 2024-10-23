#include "util.h"
#include "mdelayhdr.h"
#include <arpa/inet.h>
#include <sys/time.h>
#include <time.h>

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
        int enable = SOF_TIMESTAMPING_TX_SOFTWARE | SOF_TIMESTAMPING_TX_HARDWARE | SOF_TIMESTAMPING_RX_HARDWARE
            | SOF_TIMESTAMPING_RAW_HARDWARE | SOF_TIMESTAMPING_SOFTWARE | SOF_TIMESTAMPING_OPT_TX_SWHW;
        TRY(setsockopt(sock, SOL_SOCKET, SO_TIMESTAMPING, &enable, sizeof(int)));
        printf("enabled timestamping sockopt\n");
    }
}

struct timespec* retrieve_timestamp(struct msghdr* msg)
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
    return ts;
}

/* Sends packets with timestamps followed pervious packet. So this function actually sent 2*N packets. */
void send_udp_packets_timestamp(int sock, const struct sockaddr_in* dsa, int pkttype, int pktsize, int N)
{
    unsigned char* payload = calloc(pktsize, 1);
    struct mdelayhdr mdelayhdr;
    memset(&mdelayhdr, 0, sizeof(mdelayhdr));

    char control[1024];
    struct iovec iov; // no need to set this when tx
    struct msghdr msg;
    memset(control, 0, sizeof(control));
    memset(&msg, 0, sizeof(msg));
    msg.msg_iov = &iov;
    msg.msg_iovlen = 1;
    msg.msg_name = NULL;
    msg.msg_namelen = 0;
    msg.msg_control = control;
    msg.msg_controllen = sizeof(control);

    for (int i = 0; i < N; i++) {
        usleep(200);
        memset(&mdelayhdr, 0, sizeof(mdelayhdr));
        mdelayhdr.seq = htonl(i);
        struct timeval tv;
        gettimeofday(&tv, NULL);
        uint64_t timestamp_nanos = tv.tv_sec * 1000000000ULL + tv.tv_usec * 1000ULL;

        switch (pkttype) {
        case DELAY_REQ:
            mdelayhdr.t1 = hton64(timestamp_nanos);
            mdelayhdr.type = DELAY_REQ;
            memset(payload, 'A', pktsize); // for debugging?
            printf("Sending DELAY_REQ packet %d\n", i);
            break;
        case DELAY_RESP:
            mdelayhdr.t3 = hton64(timestamp_nanos);
            mdelayhdr.type = DELAY_RESP;
            memset(payload, 'C', pktsize);
            printf("Sending DELAY_RESP packet %d\n", i);
            break;
        default:
            fprintf(stderr, "Unknown packet type\n");
            exit(EXIT_FAILURE);
        }

        memcpy(payload, &mdelayhdr, sizeof(mdelayhdr));
        TRY(sendto(sock, payload, pktsize, 0, (struct sockaddr*)dsa, sizeof(struct sockaddr_in)));
        /* Obtain the sent packet timestamp. */
        int got;
        struct timespec ts[3];
        struct timespec* ts_tmp;
        do {
            got = recvmsg(sock, &msg, MSG_ERRQUEUE);
        } while (got < 0 && errno == EAGAIN); // MSG_ERRQUEUE is non-blocking, make it blocking
        ts_tmp = retrieve_timestamp(&msg);
        memcpy(&ts[0], &ts_tmp[0], sizeof(struct timespec));
        printf("Kernel timestamp %lds %ldns\n", ts[0].tv_sec, ts[0].tv_nsec);

        do {
            got = recvmsg(sock, &msg, MSG_ERRQUEUE);
        } while (got < 0 && errno == EAGAIN);
        ts_tmp = retrieve_timestamp(&msg);
        memcpy(&ts[2], &ts_tmp[2], sizeof(struct timespec));
        printf("NIC timestamp %lds %ldns\n", ts[2].tv_sec, ts[2].tv_nsec);

        /* Send the follow-up packet.*/
        timestamp_nanos = ts[0].tv_sec * 1000000000ULL + ts[0].tv_nsec;
        switch (pkttype) {
        case DELAY_REQ:
            mdelayhdr.t1 = hton64(timestamp_nanos);
            mdelayhdr.type = DELAY_REQ_FOLLOW_UP;
            memset(payload, 'B', pktsize);
            printf("Sending DELAY_REQ_FOLLOW_UP packet %d\n\n", i);
            break;
        case DELAY_RESP:
            mdelayhdr.t3 = hton64(timestamp_nanos);
            mdelayhdr.type = DELAY_RESP_FOLLOW_UP;
            memset(payload, 'D', pktsize);
            printf("Sending DELAY_RESP_FOLLOW_UP packet %d\n\n", i);
            break;
        default:
            fprintf(stderr, "Unknown packet type\n");
            exit(EXIT_FAILURE);
        }
        TRY(sendto(sock, payload, pktsize, 0, (struct sockaddr*)dsa, sizeof(struct sockaddr_in)));
        // todo: code here is too ugly, need to be refactored
        // ! just consume the follow_up packets' timestamps which are not used
        do {
            got = recvmsg(sock, &msg, MSG_ERRQUEUE);
        } while (got < 0 && errno == EAGAIN);
        do {
            got = recvmsg(sock, &msg, MSG_ERRQUEUE);
        } while (got < 0 && errno == EAGAIN);
    }

    free(payload);
}
