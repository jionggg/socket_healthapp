/*
 * Client — Phase 1B auth + Phase 2 commands (TCP to hospital).
 * Socket patterns adapted from Beej's Guide to Network Programming (beej.us/guide/bgnet/).
 */

#include <arpa/inet.h>
#include <ctype.h>
#include <errno.h>
#include <netdb.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include "../common/sha256.h"
#include "../include/project_proto.h"
#include "../include/project_ports.h"

#define LINE_MAX 1024

static void trim_cmd(char *s) {
    char *end;
    while (*s && isspace((unsigned char)*s))
        memmove(s, s + 1, strlen(s));
    end = s + strlen(s);
    while (end > s && isspace((unsigned char)end[-1]))
        *--end = '\0';
}

static int read_line_sock(int fd, char *buf, size_t cap) {
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

static uint16_t client_tcp_port(int sockfd) {
    struct sockaddr_in sin;
    socklen_t len = sizeof sin;
    if (getsockname(sockfd, (struct sockaddr *)&sin, &len) < 0)
        return 0;
    return ntohs(sin.sin_port);
}

static void read_until_dot(int sockfd) {
    for (;;) {
        char line[LINE_MAX];
        if (read_line_sock(sockfd, line, sizeof line) < 0)
            return;
        if (strcmp(line, ".") == 0)
            return;
        printf("%s\n", line);
    }
}

static void skip_pipe_fields(char *s, int nskip) {
    char *p = s;
    for (int i = 0; i < nskip; ++i) {
        p = strchr(p, '|');
        if (!p) {
            s[0] = '\0';
            return;
        }
        p++;
    }
    memmove(s, p, strlen(p) + 1);
}

static void read_until_dot_lookup_doctor(int sockfd, const char *doctor_username) {
    char line[LINE_MAX];
    if (read_line_sock(sockfd, line, sizeof line) < 0)
        return;
    if (strcmp(line, ".") == 0)
        return;
    uint16_t p = client_tcp_port(sockfd);
    if (strcmp(line, "AVAIL|all") == 0) {
        printf("The client received the response from the hospital server\n");
        printf("using TCP over port %u.\n", (unsigned)p);
        printf("All time blocks are available for %s.\n", doctor_username);
    } else if (strcmp(line, "AVAIL|none") == 0) {
        printf("The client received the response from the Hospital Server\n");
        printf("using TCP over port %u.\n", (unsigned)p);
        printf("%s has no time slots available.\n", doctor_username);
    } else if (strncmp(line, "RES|AVAIL|some", 14) == 0) {
        printf("The client received the response from the Hospital Server\n");
        printf("using TCP over port %u.\n", (unsigned)p);
        printf("%s is available at times:\n", doctor_username);
        skip_pipe_fields(line, 3);
        char *save = NULL;
        for (char *tok = strtok_r(line, "|", &save); tok; tok = strtok_r(NULL, "|", &save))
            printf("%s\n", tok);
    }
    (void)read_line_sock(sockfd, line, sizeof line);
}

static void handle_schedule_response(int sockfd, const char *patient_username,
                                     const char *doctor, const char *time_sel) {
    char line[LINE_MAX];
    if (read_line_sock(sockfd, line, sizeof line) < 0)
        return;
    if (strcmp(line, ".") == 0)
        return;
    uint16_t p = client_tcp_port(sockfd);
    if (strcmp(line, "RES|SCHED|ok") == 0) {
        printf("The client received the response from the Hospital Server\n");
        printf("using TCP over port %u\n", (unsigned)p);
        printf("An appointment has been successfully scheduled for patient %s with %s at %s.\n",
               patient_username, doctor, time_sel);
    } else if (strcmp(line, "RES|SCHED|full") == 0) {
        printf("The client received the response from the hospital server\n");
        printf("using TCP over port %u\n", (unsigned)p);
        printf("Unable to schedule an appointment with %s at this time, as all time blocks have been taken up.\n",
               doctor);
    } else if (strncmp(line, "RES|SCHED|busy", 14) == 0) {
        printf("The client received the response from the hospital server\n");
        printf("using TCP over port %u\n", (unsigned)p);
        printf("Unable to schedule an appointment with %s at %s. Other available time blocks are\n",
               doctor, time_sel);
        char tmp[LINE_MAX];
        strncpy(tmp, line, sizeof tmp - 1);
        tmp[sizeof tmp - 1] = '\0';
        skip_pipe_fields(tmp, 3);
        char *save = NULL;
        for (char *tok = strtok_r(tmp, "|", &save); tok; tok = strtok_r(NULL, "|", &save))
            printf("%s\n", tok);
    } else if (strncmp(line, "RES|SCHED|badtime", 17) == 0) {
        printf("The client received the response from the hospital server\n");
        printf("using TCP over port %u\n", (unsigned)p);
        printf("Unable to schedule an appointment with %s at %s. Other available time blocks are\n",
               doctor, time_sel);
        char tmp[LINE_MAX];
        strncpy(tmp, line, sizeof tmp - 1);
        tmp[sizeof tmp - 1] = '\0';
        skip_pipe_fields(tmp, 3);
        char *save = NULL;
        for (char *tok = strtok_r(tmp, "|", &save); tok; tok = strtok_r(NULL, "|", &save))
            printf("%s\n", tok);
    } else if (strncmp(line, "RES|ERR", 7) == 0) {
        printf("The client received the response from the hospital server\n");
        printf("using TCP over port %u\n", (unsigned)p);
        printf("The hospital server could not complete that request.\n");
    }
    (void)read_line_sock(sockfd, line, sizeof line);
}

static void handle_view_appt(int sockfd) {
    char line[LINE_MAX];
    if (read_line_sock(sockfd, line, sizeof line) < 0)
        return;
    if (strcmp(line, ".") == 0)
        return;
    uint16_t p = client_tcp_port(sockfd);
    if (strncmp(line, "RES|VIEW1|ok|", 13) == 0) {
        char work[LINE_MAX];
        strncpy(work, line + 13, sizeof work - 1);
        work[sizeof work - 1] = '\0';
        char *save = NULL;
        for (char *doc = strtok_r(work, "|", &save); doc; doc = strtok_r(NULL, "|", &save)) {
            char *tim = strtok_r(NULL, "|", &save);
            char *ill = strtok_r(NULL, "|", &save);
            if (!tim || !ill)
                break;
            printf("The client received the response from the hospital server\n");
            printf("using TCP over port %u\n", (unsigned)p);
            printf("You have an appointment scheduled with %s at %s.\n", doc, tim);
        }
    } else if (strcmp(line, "RES|VIEW1|none") == 0) {
        printf("The client received the response from the hospital server\n");
        printf("using TCP over client port %u\n", (unsigned)p);
        printf("You do not have an appointment today.\n");
    } else if (strncmp(line, "RES|ERR", 7) == 0) {
        printf("The client received the response from the hospital server\n");
        printf("using TCP over port %u\n", (unsigned)p);
        printf("The hospital server could not complete that request.\n");
    }
    (void)read_line_sock(sockfd, line, sizeof line);
}

static void handle_cancel(int sockfd) {
    char line[LINE_MAX];
    if (read_line_sock(sockfd, line, sizeof line) < 0)
        return;
    if (strcmp(line, ".") == 0)
        return;
    uint16_t p = client_tcp_port(sockfd);
    if (strncmp(line, "RES|CANCEL|ok|", 14) == 0) {
        char work[LINE_MAX];
        strncpy(work, line + 14, sizeof work - 1);
        work[sizeof work - 1] = '\0';
        char *save = NULL;
        for (char *doc = strtok_r(work, "|", &save); doc; doc = strtok_r(NULL, "|", &save)) {
            char *tim = strtok_r(NULL, "|", &save);
            if (!tim)
                break;
            printf("The client received the response from the Hospital Server\n");
            printf("using TCP over port %u\n", (unsigned)p);
            printf("You have successfully cancelled your appointment with %s at %s.\n", doc, tim);
        }
    } else if (strcmp(line, "RES|CANCEL|fail") == 0) {
        printf("The client received the response from the Hospital Server\n");
        printf("using TCP over port %u\n", (unsigned)p);
        printf("You have no appointments available to cancel.\n");
    } else if (strncmp(line, "RES|ERR", 7) == 0) {
        printf("The client received the response from the Hospital Server\n");
        printf("using TCP over port %u\n", (unsigned)p);
        printf("The hospital server could not complete that request.\n");
    }
    (void)read_line_sock(sockfd, line, sizeof line);
}

static void handle_view_doc(int sockfd, const char *doctor_username) {
    char line[LINE_MAX];
    if (read_line_sock(sockfd, line, sizeof line) < 0)
        return;
    if (strcmp(line, ".") == 0)
        return;
    uint16_t p = client_tcp_port(sockfd);
    if (strcmp(line, "RES|VIEW_DOC|none") == 0) {
        printf("The client received the response from the hospital server\n");
        printf("using TCP over port %u\n", (unsigned)p);
        printf("You do not have any appointments scheduled.\n");
    } else if (strncmp(line, "RES|VIEW_DOC|ok", 15) == 0) {
        printf("The client received the response from the hospital server\n");
        printf("using TCP over port %u\n", (unsigned)p);
        printf("%s is scheduled at times:\n", doctor_username);
        char tmp[LINE_MAX];
        strncpy(tmp, line, sizeof tmp - 1);
        tmp[sizeof tmp - 1] = '\0';
        skip_pipe_fields(tmp, 3);
        char *save = NULL;
        for (char *tok = strtok_r(tmp, "|", &save); tok; tok = strtok_r(NULL, "|", &save))
            printf("%s\n", tok);
    } else if (strncmp(line, "RES|ERR", 7) == 0) {
        printf("The client received the response from the hospital server\n");
        printf("using TCP over port %u\n", (unsigned)p);
        printf("The hospital server could not complete that request.\n");
    }
    (void)read_line_sock(sockfd, line, sizeof line);
}

int main(int argc, char *argv[]) {
    if (argc != 3)
        return 1;
    const char *username = argv[1];
    const char *password = argv[2];

    printf("The client is up and running.\n");
    char user_hex[65], pass_hex[65];
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
        close(sockfd);
        return 1;
    }
    char reply[64];
    if (read_line_sock(sockfd, reply, sizeof reply) < 0) {
        close(sockfd);
        return 1;
    }
    reply[strcspn(reply, "\r\n")] = '\0';
    if (strcmp(reply, "FAIL") == 0) {
        printf("The credentials are incorrect. Please try again.\n");
        close(sockfd);
        return 1;
    }
    int is_doctor = 0;
    if (strcmp(reply, "PATIENT") == 0) {
        printf("%s received the authentication result.\n", username);
        printf("Authentication successful. You have been granted patient access.\n");
    } else if (strcmp(reply, "DOCTOR") == 0) {
        is_doctor = 1;
        printf("%s received the authentication result.\n", username);
        printf("Authentication successful. You have been granted doctor access.\n");
    } else {
        close(sockfd);
        return 1;
    }

    char cmdbuf[LINE_MAX];
    for (;;) {
        if (!fgets(cmdbuf, sizeof cmdbuf, stdin))
            break;
        cmdbuf[strcspn(cmdbuf, "\r\n")] = '\0';
        trim_cmd(cmdbuf);
        if (cmdbuf[0] == '\0')
            continue;

        if (strcasecmp(cmdbuf, "quit") == 0 || strcasecmp(cmdbuf, "exit") == 0 ||
            strcasecmp(cmdbuf, "logout") == 0) {
            send(sockfd, "quit\n", 5, 0);
            shutdown(sockfd, SHUT_WR);
            printf("You have successfully been logged out.\n");
            break;
        }
        if (strcmp(cmdbuf, "help") == 0) {
            if (is_doctor) {
                printf("Please enter the command:\n");
                printf("<view_appointments>,\n");
                printf("<prescribe <patient> <frequency>>,\n");
                printf("<view_prescription <patient>>,\n");
                printf("<quit>\n");
            } else {
                printf("Please enter the command:\n");
                printf("<lookup>,\n");
                printf("<lookup <doctor>>,\n");
                printf("<schedule <doctor> <start_time> <illness>>,\n");
                printf("<cancel>,\n");
                printf("<view_appointment>,\n");
                printf("<view_prescription>,\n");
                printf("<quit>\n");
            }
            continue;
        }
        if (strcmp(cmdbuf, "lookup") == 0) {
            printf("%s sent a lookup request to the hospital server.\n", username);
            send(sockfd, "lookup\n", 7, 0);
            printf("The client received the response from the hospital server\n");
            printf("using TCP over port %u.\n", (unsigned)client_tcp_port(sockfd));
            read_until_dot(sockfd);
            continue;
        }
        {
            char docname[128];
            if (sscanf(cmdbuf, "lookup %127s", docname) == 1) {
                printf("Patient %s sent a lookup request to the hospital server for %s.\n", username, docname);
                if (send(sockfd, cmdbuf, strlen(cmdbuf), 0) < 0 || send(sockfd, "\n", 1, 0) < 0)
                    break;
                read_until_dot_lookup_doctor(sockfd, docname);
                continue;
            }
        }
        if (strncmp(cmdbuf, "schedule ", 9) == 0) {
            char doc[128], tim[32], ill[64];
            if (sscanf(cmdbuf + 9, "%127s %31s %63s", doc, tim, ill) != 3)
                continue;
            printf("%s sent an appointment schedule request to the hospital server.\n", username);
            if (send(sockfd, cmdbuf, strlen(cmdbuf), 0) < 0 || send(sockfd, "\n", 1, 0) < 0)
                break;
            handle_schedule_response(sockfd, username, doc, tim);
            continue;
        }
        if (strcmp(cmdbuf, "view_appointment") == 0) {
            printf("%s sent a request to view their appointment to the Hospital Server.\n", username);
            send(sockfd, "view_appointment\n", 18, 0);
            handle_view_appt(sockfd);
            continue;
        }
        if (strcmp(cmdbuf, "cancel") == 0) {
            printf("%s sent a cancellation request to the Hospital Server.\n", username);
            send(sockfd, "cancel\n", 7, 0);
            handle_cancel(sockfd);
            continue;
        }
        if (strcmp(cmdbuf, "view_appointments") == 0) {
            if (!is_doctor)
                continue;
            printf("%s sent a request to view their scheduled appointments to the Hospital Server.\n", username);
            send(sockfd, "view_appointments\n", 19, 0);
            handle_view_doc(sockfd, username);
            continue;
        }
    }
    close(sockfd);
    return 0;
}
