#ifndef option_h
#define option_h
#include <stdlib.h>
#include <string.h>
#include <getopt.h>
#include <stdbool.h>

#include <netinet/in.h>
struct configuration {
    bool server;
    int protocol; /* IPPROTO_TCP or IPPROTO_UDP */
    unsigned short server_port; /* listen port */
    unsigned short client_port;
    const char* server_ip; /* server ip */
    unsigned int max_packets; /* Stop after this many (0=forever) */
};

void parse_options(int argc, char** argv, struct configuration* cfg);

#endif