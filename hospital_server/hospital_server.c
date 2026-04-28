/*
 * EE450 Hospital Server — Phase 1A: bind UDP + TCP, boot message, event loop.
 * Socket setup adapted from Beej's Guide to Network Programming (beej.us/guide/bgnet/).
 */

#include <arpa/inet.h>
#include <errno.h>
#include <netdb.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include "../include/project_ports.h"

#define BACKLOG 10

static int bind_udp_listener(const char *host, uint16_t port) {
    int sockfd;
    struct sockaddr_in addr;

    if ((sockfd = socket(AF_INET, SOCK_DGRAM, 0)) < 0) {
        perror("hospital_server: UDP socket");
        exit(1);
    }

    memset(&addr, 0, sizeof addr);
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    if (inet_pton(AF_INET, host, &addr.sin_addr) <= 0) {
        fprintf(stderr, "hospital_server: bad bind address %s\n", host);
        exit(1);
    }

    int yes = 1;
    if (setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof yes) < 0) {
        perror("hospital_server: setsockopt UDP");
        exit(1);
    }

    if (bind(sockfd, (struct sockaddr *)&addr, sizeof addr) < 0) {
        perror("hospital_server: bind UDP");
        exit(1);
    }
    return sockfd;
}

static int bind_tcp_listener(const char *host, uint16_t port) {
    int sockfd;
    struct sockaddr_in addr;

    if ((sockfd = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
        perror("hospital_server: TCP socket");
        exit(1);
    }

    memset(&addr, 0, sizeof addr);
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    if (inet_pton(AF_INET, host, &addr.sin_addr) <= 0) {
        fprintf(stderr, "hospital_server: bad bind address %s\n", host);
        exit(1);
    }

    int yes = 1;
    if (setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof yes) < 0) {
        perror("hospital_server: setsockopt TCP");
        exit(1);
    }

    if (bind(sockfd, (struct sockaddr *)&addr, sizeof addr) < 0) {
        perror("hospital_server: bind TCP");
        exit(1);
    }

    if (listen(sockfd, BACKLOG) < 0) {
        perror("hospital_server: listen");
        exit(1);
    }
    return sockfd;
}

static uint16_t local_port(int sockfd) {
    struct sockaddr_in sin;
    socklen_t len = sizeof sin;
    if (getsockname(sockfd, (struct sockaddr *)&sin, &len) < 0) {
        perror("hospital_server: getsockname");
        exit(1);
    }
    return ntohs(sin.sin_port);
}

int main(void) {
    const char *host = "127.0.0.1";

    int udpfd = bind_udp_listener(host, EE450_HOSP_UDP_PORT);
    int tcpfd = bind_tcp_listener(host, EE450_HOSP_TCP_PORT);

    uint16_t udp_port = local_port(udpfd);
    (void)local_port(tcpfd);

    printf("Hospital Server is up and running using UDP on port %u.\n",
           (unsigned)udp_port);

    for (;;) {
        fd_set readfds;
        FD_ZERO(&readfds);
        FD_SET(udpfd, &readfds);
        FD_SET(tcpfd, &readfds);
        int maxfd = udpfd > tcpfd ? udpfd : tcpfd;

        if (select(maxfd + 1, &readfds, NULL, NULL, NULL) < 0) {
            if (errno == EINTR)
                continue;
            perror("hospital_server: select");
            break;
        }

        if (FD_ISSET(udpfd, &readfds)) {
            char buf[2048];
            struct sockaddr_storage src;
            socklen_t srclen = sizeof src;
            (void)recvfrom(udpfd, buf, sizeof buf, 0,
                           (struct sockaddr *)&src, &srclen);
        }

        if (FD_ISSET(tcpfd, &readfds)) {
            struct sockaddr_storage their_addr;
            socklen_t sin_size = sizeof their_addr;
            int newfd = accept(tcpfd, (struct sockaddr *)&their_addr, &sin_size);
            if (newfd >= 0)
                close(newfd);
        }
    }

    close(udpfd);
    close(tcpfd);
    return 0;
}
