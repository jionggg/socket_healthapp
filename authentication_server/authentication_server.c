/*
 * Authentication Server — Phase 1B: UDP auth against users.txt.
 * Socket patterns adapted from Beej's Guide to Network Programming (beej.us/guide/bgnet/).
 */

#include <arpa/inet.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

#include "../include/project_proto.h"
#include "../include/project_ports.h"

static uint16_t local_port(int sockfd) {
    struct sockaddr_in sin;
    socklen_t len = sizeof sin;
    if (getsockname(sockfd, (struct sockaddr *)&sin, &len) < 0) {
        perror("authentication_server: getsockname");
        exit(1);
    }
    return ntohs(sin.sin_port);
}

static void hash_suffix(const char *hex64, char out[6]) {
    memcpy(out, hex64 + 59, 5);
    out[5] = '\0';
}

static FILE *open_users_file(void) {
    FILE *f = fopen("users.txt", "r");
    if (f)
        return f;
    f = fopen("authentication_server/users.txt", "r");
    return f;
}

static int credentials_valid(const char *user_hex, const char *pass_hex) {
    FILE *f = open_users_file();
    if (!f)
        return 0;

    char line[512];
    int ok = 0;
    while (fgets(line, sizeof line, f)) {
        char uh[65], ph[65];
        if (sscanf(line, "%64s %64s", uh, ph) != 2)
            continue;
        if (strcmp(uh, user_hex) == 0 && strcmp(ph, pass_hex) == 0) {
            ok = 1;
            break;
        }
    }
    fclose(f);
    return ok;
}

int main(void) {
    const char *host = "127.0.0.1";
    int sockfd;
    struct sockaddr_in addr;

    if ((sockfd = socket(AF_INET, SOCK_DGRAM, 0)) < 0) {
        perror("authentication_server: socket");
        exit(1);
    }

    int yes = 1;
    if (setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof yes) < 0) {
        perror("authentication_server: setsockopt");
        exit(1);
    }

    memset(&addr, 0, sizeof addr);
    addr.sin_family = AF_INET;
    addr.sin_port = htons(PROJECT_AUTH_UDP_PORT);
    if (inet_pton(AF_INET, host, &addr.sin_addr) <= 0) {
        fprintf(stderr, "authentication_server: bad address\n");
        exit(1);
    }

    if (bind(sockfd, (struct sockaddr *)&addr, sizeof addr) < 0) {
        perror("authentication_server: bind");
        exit(1);
    }

    uint16_t port = local_port(sockfd);
    printf("Authentication Server is up and running using UDP on port %u.\n",
           (unsigned)port);

    for (;;) {
        char buf[512];
        struct sockaddr_storage src;
        socklen_t srclen = sizeof src;
        ssize_t n = recvfrom(sockfd, buf, sizeof buf - 1, 0,
                             (struct sockaddr *)&src, &srclen);
        if (n <= 0)
            continue;
        buf[n] = '\0';

        char user_hex[65], pass_hex[65];
        if (sscanf(buf, "%64s %64s", user_hex, pass_hex) != 2)
            continue;

        char suf[6];
        hash_suffix(user_hex, suf);
        printf("Authentication Server has received an authentication request for a user with hash suffix: %s.\n",
               suf);

        int valid = credentials_valid(user_hex, pass_hex);
        if (valid) {
            printf("Authentication succeeded for a user with hash suffix: %s.\n", suf);
        } else {
            printf("Authentication failed for a user with hash suffix: %s.\n", suf);
        }

        const char *reply = valid ? PROJECT_AUTH_REPLY_OK : PROJECT_AUTH_REPLY_FAIL;
        if (sendto(sockfd, reply, strlen(reply), 0,
                   (struct sockaddr *)&src, srclen) < 0) {
            perror("authentication_server: sendto");
            continue;
        }

        printf("The Authentication Server has sent the authentication result to the Hospital Server.\n");
    }

    close(sockfd);
    return 0;
}
