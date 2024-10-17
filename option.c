#include "option.h"
#include <netinet/in.h>
#include <stdio.h>
void parse_options(int argc, char** argv, struct configuration* cfg)
{
    int option_index = 0;
    int opt;
    static struct option long_options[] = {
        { "server", no_argument, 0, 's' },
        { "client", optional_argument, 0, 'c' },
        { "port", optional_argument, 0, 'p' },
        { "cport", required_argument, 0, 'C' },
        { "udp", no_argument, 0, 'u' },
        { "number", required_argument, 0, 'n' },
        { 0, no_argument, 0, 0 },
    };
    const char* optstring = "sc:p:C:un:";

    /* Defaults */
    memset(cfg, 0, sizeof(struct configuration));
    cfg->server_port = 9337;
    cfg->client_port = 9336;
    cfg->protocol = IPPROTO_TCP;
    cfg->max_packets = 100;

    opt = getopt_long(argc, argv, optstring, long_options, &option_index);
    while (opt != -1) {
        switch (opt) {
        case 's':
            cfg->server = true;
            break;
        case 'c':
            cfg->server = false;
            cfg->server_ip = optarg;
            break;
        case 'p':
            cfg->server_port = atoi(optarg);
            break;
        case 'u':
            cfg->protocol = IPPROTO_UDP;
            break;
        case 'n':
            cfg->max_packets = atoi(optarg);
            break;
        case 'C':
            cfg->client_port = atoi(optarg);
            break;
        default:
            exit(EXIT_FAILURE);
        }
        opt = getopt_long(argc, argv, optstring, long_options, &option_index);
    }
}