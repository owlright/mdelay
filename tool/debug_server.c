#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <signal.h>

#define PORT 9337
#define BUFFER_SIZE 1024

int sockfd;

void handle_sigint(int sig) {
    printf("Caught signal %d, closing socket and exiting...\n", sig);
    close(sockfd);
    exit(0);
}

int main() {
    struct sockaddr_in server_addr, client_addr;
    char buffer[BUFFER_SIZE];
    socklen_t addr_len = sizeof(client_addr);
    ssize_t recv_len;

    // Create UDP socket
    if ((sockfd = socket(AF_INET, SOCK_DGRAM, 0)) < 0) {
        perror("socket creation failed");
        exit(EXIT_FAILURE);
    }
    /* Avoid Address already in use problem */
    if (setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &(int) { 1 }, sizeof(int)) < 0) {
        perror("setsockopt(SO_REUSEADDR)");
        close(sockfd);
        exit(EXIT_FAILURE);
    }
    // Set up server address structure
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(PORT);

    // Bind the socket to the port
    if (bind(sockfd, (const struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        perror("bind failed");
        close(sockfd);
        exit(EXIT_FAILURE);
    }

    // Set up signal handler for Ctrl+C
    signal(SIGINT, handle_sigint);

    printf("UDP server listening on port %d...\n", PORT);

    // Main loop to receive data
    while (1) {
        recv_len = recvfrom(sockfd, buffer, BUFFER_SIZE, 0, (struct sockaddr *)&client_addr, &addr_len);
        printf("Received from IP: %s, Port: %d\n", inet_ntoa(client_addr.sin_addr), ntohs(client_addr.sin_port));
        if (recv_len < 0) {
            perror("recvfrom failed");
            break;
        }

        buffer[recv_len] = '\0';
        printf("Received message: %s\n", buffer);
    }

    // Close the socket
    close(sockfd);
    return 0;
}