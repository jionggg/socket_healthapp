/*
 * Hospital Server — Phase 1B: TCP clients, UDP to authentication server.
 * Socket patterns adapted from Beej's Guide to Network Programming (beej.us/guide/bgnet/).
 */

#include <arpa/inet.h>
#include <errno.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include <netdb.h>

#include "../include/project_proto.h"
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

static void hash_suffix(const char *hex64, char out[6]) {
    memcpy(out, hex64 + 59, 5);
    out[5] = '\0';
}

static FILE *open_hospital_file(void) {
    FILE *f = fopen("hospital.txt", "r");
    if (f)
        return f;
    return fopen("hospital_server/hospital.txt", "r");
}

static int user_is_doctor(const char *user_hex) {
    FILE *f = open_hospital_file();
    if (!f)
        return 0;

    char line[512];
    int in_doctors = 0;
    int is_doc = 0;
    while (fgets(line, sizeof line, f)) {
        if (strncmp(line, "[Doctors]", 9) == 0) {
            in_doctors = 1;
            continue;
        }
        if (strncmp(line, "[Treatments]", 12) == 0)
            break;
        if (!in_doctors)
            continue;

        char name[128], doc_hash[65];
        if (sscanf(line, "%127s %64s", name, doc_hash) != 2)
            continue;
        if (strcmp(doc_hash, user_hex) == 0) {
            is_doc = 1;
            break;
        }
    }
    fclose(f);
    return is_doc;
}

static int read_line(int fd, char *buf, size_t cap) {
    size_t i = 0;
    while (i + 1 < cap) {
        char c;
        ssize_t n = recv(fd, &c, 1, 0);
        if (n < 0)
            return -1;
        if (n == 0)
            return -1;
        if (c == '\n')
            break;
        buf[i++] = c;
    }
    buf[i] = '\0';
    return 0;
}

static int handle_auth_tcp(int clientfd, int udpfd, uint16_t hosp_udp_port,
                           uint16_t hosp_tcp_port) {
    char line[512];
    if (read_line(clientfd, line, sizeof line) < 0)
        return -1;

    if (strncmp(line, PROJECT_AUTH_PREFIX, PROJECT_AUTH_PREFIX_LEN) != 0)
        return -1;

    char user_hex[65], pass_hex[65];
    if (sscanf(line + PROJECT_AUTH_PREFIX_LEN, "%64s %64s", user_hex, pass_hex) != 2)
        return -1;

    char suf[6];
    hash_suffix(user_hex, suf);
    printf("Hospital Server received an authentication request from a user with hash suffix %s.\n",
           suf);

    char udp_payload[256];
    snprintf(udp_payload, sizeof udp_payload, "%s %s", user_hex, pass_hex);

    struct sockaddr_in auth_addr;
    memset(&auth_addr, 0, sizeof auth_addr);
    auth_addr.sin_family = AF_INET;
    auth_addr.sin_port = htons(PROJECT_AUTH_UDP_PORT);
    if (inet_pton(AF_INET, "127.0.0.1", &auth_addr.sin_addr) <= 0)
        return -1;

    if (sendto(udpfd, udp_payload, strlen(udp_payload), 0,
               (struct sockaddr *)&auth_addr, sizeof auth_addr) < 0) {
        perror("hospital_server: sendto auth");
        return -1;
    }

    printf("Hospital Server has sent an authentication request to the Authentication Server.\n");

    char reply[64];
    struct sockaddr_storage src;
    socklen_t srclen = sizeof src;
    ssize_t rn = recvfrom(udpfd, reply, sizeof reply - 1, 0,
                          (struct sockaddr *)&src, &srclen);
    if (rn < 0) {
        perror("hospital_server: recvfrom auth");
        return -1;
    }
    reply[rn] = '\0';
    reply[strcspn(reply, "\r\n")] = '\0';

    printf("Hospital server has received the response from the authentication server using UDP over port %u.\n",
           (unsigned)hosp_udp_port);

    int ok = (strcmp(reply, PROJECT_AUTH_REPLY_OK) == 0);

    if (ok) {
        printf("User with a hash suffix %s has been granted access to the system. Determining the access of the user.\n",
               suf);
        int doc = user_is_doctor(user_hex);
        if (doc) {
            printf("User with hash suffix %s will be granted doctor access.\n", suf);
            if (send(clientfd, PROJECT_CLI_REPLY_DOCTOR, strlen(PROJECT_CLI_REPLY_DOCTOR), 0) < 0)
                return -1;
        } else {
            printf("User with hash %s will be granted patient access.\n", suf);
            if (send(clientfd, PROJECT_CLI_REPLY_PATIENT, strlen(PROJECT_CLI_REPLY_PATIENT), 0) < 0)
                return -1;
        }
    } else {
        if (send(clientfd, PROJECT_CLI_REPLY_FAIL, strlen(PROJECT_CLI_REPLY_FAIL), 0) < 0)
            return -1;
    }

    printf("Hospital Server has sent the response from Authentication Server to the client using TCP over port %u.\n",
           (unsigned)hosp_tcp_port);

    return ok ? 0 : 1;
}

int main(void) {
    const char *host = "127.0.0.1";

    int udpfd = bind_udp_listener(host, PROJECT_HOSP_UDP_PORT);
    int tcpfd = bind_tcp_listener(host, PROJECT_HOSP_TCP_PORT);

    uint16_t udp_port = local_port(udpfd);
    uint16_t tcp_port = local_port(tcpfd);

    printf("Hospital Server is up and running using UDP on port %u.\n",
           (unsigned)udp_port);

    for (;;) {
        fd_set readfds;
        FD_ZERO(&readfds);
        FD_SET(tcpfd, &readfds);

        if (select(tcpfd + 1, &readfds, NULL, NULL, NULL) < 0) {
            if (errno == EINTR)
                continue;
            perror("hospital_server: select");
            break;
        }

        if (FD_ISSET(tcpfd, &readfds)) {
            struct sockaddr_storage their_addr;
            socklen_t sin_size = sizeof their_addr;
            int newfd = accept(tcpfd, (struct sockaddr *)&their_addr, &sin_size);
            if (newfd < 0) {
                perror("hospital_server: accept");
                continue;
            }

            int auth_rc = handle_auth_tcp(newfd, udpfd, udp_port, tcp_port);
            if (auth_rc < 0) {
                close(newfd);
                continue;
            }
            if (auth_rc != 0)
                close(newfd);
        }
    }

    close(udpfd);
    close(tcpfd);
    return 0;
}
