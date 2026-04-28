/*
 * EE450 Prescription Server — Phase 1A: UDP bind, boot message, recv loop.
 * Socket setup adapted from Beej's Guide to Network Programming (beej.us/guide/bgnet/).
 */

#include <arpa/inet.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

#include "../include/project_ports.h"

static uint16_t local_port(int sockfd) {
    struct sockaddr_in sin;
    socklen_t len = sizeof sin;
    if (getsockname(sockfd, (struct sockaddr *)&sin, &len) < 0) {
        perror("prescription_server: getsockname");
        exit(1);
    }
    return ntohs(sin.sin_port);
}

int main(void) {
    const char *host = "127.0.0.1";
    int sockfd;
    struct sockaddr_in addr;

    if ((sockfd = socket(AF_INET, SOCK_DGRAM, 0)) < 0) {
        perror("prescription_server: socket");
        exit(1);
    }

    int yes = 1;
    if (setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof yes) < 0) {
        perror("prescription_server: setsockopt");
        exit(1);
    }

    memset(&addr, 0, sizeof addr);
    addr.sin_family = AF_INET;
    addr.sin_port = htons(EE450_PRES_UDP_PORT);
    if (inet_pton(AF_INET, host, &addr.sin_addr) <= 0) {
        fprintf(stderr, "prescription_server: bad address\n");
        exit(1);
    }

    if (bind(sockfd, (struct sockaddr *)&addr, sizeof addr) < 0) {
        perror("prescription_server: bind");
        exit(1);
    }

    uint16_t port = local_port(sockfd);
    printf("Prescription Server is up and running using UDP on port %u.\n",
           (unsigned)port);

    for (;;) {
        char buf[2048];
        struct sockaddr_storage src;
        socklen_t srclen = sizeof src;
        (void)recvfrom(sockfd, buf, sizeof buf, 0,
                        (struct sockaddr *)&src, &srclen);
    }

    close(sockfd);
    return 0;
}
