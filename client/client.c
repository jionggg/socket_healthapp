/*
 * Client — Phase 1B: SHA-256 credentials, TCP to hospital server.
 * TCP connect adapted from Beej's Guide to Network Programming (beej.us/guide/bgnet/).
 */

#include <arpa/inet.h>
#include <errno.h>
#include <netdb.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>
#include <sys/wait.h>

#include "../common/sha256.h"
#include "../include/project_proto.h"
#include "../include/project_ports.h"

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

int main(int argc, char *argv[]) {
    if (argc != 3) {
        return 1;
    }

    const char *username = argv[1];
    const char *password = argv[2];

    printf("The client is up and running.\n");

    char user_hex[65];
    char pass_hex[65];
    sha256_easy_hash_hex(username, strlen(username), user_hex);
    sha256_easy_hash_hex(password, strlen(password), pass_hex);
    user_hex[64] = '\0';
    pass_hex[64] = '\0';

    int sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0) {
        perror("client: socket");
        return 1;
    }

    struct sockaddr_in serv;
    memset(&serv, 0, sizeof serv);
    serv.sin_family = AF_INET;
    serv.sin_port = htons(PROJECT_HOSP_TCP_PORT);
    if (inet_pton(AF_INET, "127.0.0.1", &serv.sin_addr) <= 0) {
        fprintf(stderr, "client: inet_pton\n");
        close(sockfd);
        return 1;
    }

    if (connect(sockfd, (struct sockaddr *)&serv, sizeof serv) < 0) {
        perror("client: connect");
        close(sockfd);
        return 1;
    }

    printf("%s sent an authentication request to the hospital server.\n", username);

    char out[256];
    snprintf(out, sizeof out, "%s%s %s\n", PROJECT_AUTH_PREFIX, user_hex, pass_hex);
    if (send(sockfd, out, strlen(out), 0) < 0) {
        perror("client: send");
        close(sockfd);
        return 1;
    }

    char reply[64];
    if (read_line(sockfd, reply, sizeof reply) < 0) {
        close(sockfd);
        return 1;
    }

    reply[strcspn(reply, "\r\n")] = '\0';

    if (strcmp(reply, "FAIL") == 0) {
        printf("The credentials are incorrect. Please try again.\n");
        close(sockfd);
        return 1;
    }

    if (strcmp(reply, "PATIENT") == 0) {
        printf("%s received the authentication result.\n", username);
        printf("Authentication successful. You have been granted patient access.\n");
    } else if (strcmp(reply, "DOCTOR") == 0) {
        printf("%s received the authentication result.\n", username);
        printf("Authentication successful. You have been granted doctor access.\n");
    } else {
        close(sockfd);
        return 1;
    }

    for (;;) {
        if (getchar() == EOF)
            break;
    }

    close(sockfd);
    return 0;
}
