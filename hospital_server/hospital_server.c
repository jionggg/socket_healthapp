/*
 * Hospital server — Phase 1B auth + Phase 2 (TCP + UDP).
 * Socket patterns adapted from Beej's Guide to Network Programming (beej.us/guide/bgnet/).
 */

#include <arpa/inet.h>
#include <ctype.h>
#include <errno.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include <netdb.h>

#include "../common/sha256.h"
#include "../include/project_proto.h"
#include "../include/project_ports.h"

#define BACKLOG 10
#define MAX_CLIENTS 32
#define LINE_MAX 512
#define UDP_BUF 8192

typedef struct {
    int fd;
    char user_hex[65];
    int is_doctor;
    char doctor_plain[128];
} ClientCtx;

static ClientCtx g_clients[MAX_CLIENTS];
static int g_ncli;

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
    int in_doctors = 0, is_doc = 0;
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

static void resolve_doctor_plain(const char *user_hex, char *out, size_t cap) {
    out[0] = '\0';
    FILE *f = open_hospital_file();
    if (!f)
        return;
    char line[512];
    int in_doctors = 0;
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
            strncpy(out, name, cap - 1);
            out[cap - 1] = '\0';
            break;
        }
    }
    fclose(f);
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

static void tcp_send_line(int fd, const char *s) {
    char b[LINE_MAX + 2];
    snprintf(b, sizeof b, "%s\n", s);
    (void)send(fd, b, strlen(b), 0);
}

static void tcp_end_message(int fd) {
    (void)send(fd, ".\n", 2, 0);
}

/* Every command that expects a reply must end with .\n so the client never blocks in recv. */
static void tcp_reply_done(ClientCtx *c, const char *first_line) {
    if (first_line && first_line[0])
        tcp_send_line(c->fd, first_line);
    tcp_end_message(c->fd);
}

static int appt_send(int udpfd, const char *req) {
    struct sockaddr_in appt;
    memset(&appt, 0, sizeof appt);
    appt.sin_family = AF_INET;
    appt.sin_port = htons(PROJECT_APPT_UDP_PORT);
    if (inet_pton(AF_INET, "127.0.0.1", &appt.sin_addr) <= 0)
        return -1;
    return sendto(udpfd, req, strlen(req), 0, (struct sockaddr *)&appt, sizeof appt) < 0 ? -1 : 0;
}

static int appt_recv(int udpfd, char *resp, size_t respcap) {
    struct sockaddr_storage src;
    socklen_t srclen = sizeof src;
    ssize_t n = recvfrom(udpfd, resp, respcap - 1, 0, (struct sockaddr *)&src, &srclen);
    (void)src;
    if (n < 0)
        return -1;
    resp[n] = '\0';
    return 0;
}

static int pres_send(int udpfd, const char *req) {
    struct sockaddr_in pres;
    memset(&pres, 0, sizeof pres);
    pres.sin_family = AF_INET;
    pres.sin_port = htons(PROJECT_PRES_UDP_PORT);
    if (inet_pton(AF_INET, "127.0.0.1", &pres.sin_addr) <= 0)
        return -1;
    return sendto(udpfd, req, strlen(req), 0, (struct sockaddr *)&pres, sizeof pres) < 0 ? -1 : 0;
}

static int pres_recv(int udpfd, char *resp, size_t respcap) {
    struct sockaddr_storage src;
    socklen_t srclen = sizeof src;
    ssize_t n = recvfrom(udpfd, resp, respcap - 1, 0, (struct sockaddr *)&src, &srclen);
    (void)src;
    if (n < 0)
        return -1;
    resp[n] = '\0';
    return 0;
}

static void username_to_user_hex(const char *username, char out_hex[65]) {
    sha256_easy_hash_hex(username, strlen(username), out_hex);
    out_hex[64] = '\0';
}

static int split_prescribe_args(const char *s, char *pat, size_t patcap, char *freq, size_t freqcap) {
    while (*s && isspace((unsigned char)*s))
        s++;
    if (*s == '\0')
        return -1;
    size_t i = 0;
    while (*s && !isspace((unsigned char)*s) && i + 1 < patcap)
        pat[i++] = *s++;
    pat[i] = '\0';
    while (*s && isspace((unsigned char)*s))
        s++;
    if (*s == '\0')
        return -1;
    strncpy(freq, s, freqcap - 1);
    freq[freqcap - 1] = '\0';
    size_t n = strlen(freq);
    while (n > 0 && isspace((unsigned char)freq[n - 1]))
        freq[--n] = '\0';
    return pat[0] ? 0 : -1;
}

static FILE *open_appt_file(void) {
    FILE *f = fopen("appointment_server/appointments.txt", "r");
    return f;
}

static void trim_end_local(char *s) {
    size_t n = strlen(s);
    while (n > 0 && (s[n - 1] == '\n' || s[n - 1] == '\r' || isspace((unsigned char)s[n - 1])))
        s[--n] = '\0';
}

static int find_illness_for_patient_doctor(const char *patient_hex, const char *doctor, char *ill, size_t illcap) {
    FILE *f = open_appt_file();
    if (!f)
        return 0;
    char line[512];
    int in_doc = 0;
    while (fgets(line, sizeof line, f)) {
        trim_end_local(line);
        if (line[0] == '\0')
            continue;
        if (strchr(line, ' ') == NULL && strchr(line, ':') == NULL) {
            in_doc = (strcmp(line, doctor) == 0);
            continue;
        }
        if (!in_doc)
            continue;
        char tok1[64], tok2[65], tok3[128];
        if (sscanf(line, "%63s %64s %127s", tok1, tok2, tok3) != 3)
            continue;
        if (!strchr(tok1, ':'))
            continue;
        if (strcmp(tok2, patient_hex) == 0) {
            strncpy(ill, tok3, illcap - 1);
            ill[illcap - 1] = '\0';
            fclose(f);
            return 1;
        }
    }
    fclose(f);
    return 0;
}

static int lookup_treatment_for_illness(const char *illness, char *trt, size_t trtcap) {
    FILE *f = open_hospital_file();
    if (!f)
        return 0;
    char line[512];
    int in_treat = 0;
    while (fgets(line, sizeof line, f)) {
        if (strncmp(line, "[Treatments]", 12) == 0) {
            in_treat = 1;
            continue;
        }
        if (!in_treat)
            continue;
        char ill[128], treatment[128];
        if (sscanf(line, "%127s %127s", ill, treatment) != 2)
            continue;
        if (strcmp(ill, illness) == 0) {
            strncpy(trt, treatment, trtcap - 1);
            trt[trtcap - 1] = '\0';
            fclose(f);
            return 1;
        }
    }
    fclose(f);
    return 0;
}

static int handle_auth_tcp(int clientfd, int udpfd, uint16_t hosp_udp_port,
                           uint16_t hosp_tcp_port, ClientCtx *out_ctx) {
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
    printf("Hospital Server received an authentication request from a user with hash suffix %s.\n", suf);
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
    ssize_t rn = recvfrom(udpfd, reply, sizeof reply - 1, 0, (struct sockaddr *)&src, &srclen);
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
        out_ctx->fd = clientfd;
        strncpy(out_ctx->user_hex, user_hex, sizeof out_ctx->user_hex - 1);
        out_ctx->user_hex[sizeof out_ctx->user_hex - 1] = '\0';
        out_ctx->is_doctor = doc;
        out_ctx->doctor_plain[0] = '\0';
        if (doc)
            resolve_doctor_plain(user_hex, out_ctx->doctor_plain, sizeof out_ctx->doctor_plain);
    } else {
        if (send(clientfd, PROJECT_CLI_REPLY_FAIL, strlen(PROJECT_CLI_REPLY_FAIL), 0) < 0)
            return -1;
    }
    printf("Hospital Server has sent the response from Authentication Server to the client using TCP over port %u.\n",
           (unsigned)hosp_tcp_port);
    return ok ? 0 : 1;
}

static void handle_client_cmd(ClientCtx *c, int udpfd, uint16_t hosp_udp_port,
                              uint16_t hosp_tcp_port, const char *line) {
    char suf[6];
    hash_suffix(c->user_hex, suf);
    char req[UDP_BUF], resp[UDP_BUF];

    if (strcmp(line, "lookup") == 0) {
        printf("Hospital Server received a lookup request from a user with a hash suffix %s over port %u.\n",
               suf, (unsigned)hosp_tcp_port);
        snprintf(req, sizeof req, "REQ|LIST");
        if (appt_send(udpfd, req) < 0) {
            tcp_end_message(c->fd);
            return;
        }
        printf("Hospital Server sent the doctor lookup request to the Appointment server.\n");
        if (appt_recv(udpfd, resp, sizeof resp) < 0) {
            tcp_end_message(c->fd);
            return;
        }
        printf("Hospital Server has received the response from Appointment Server using UDP over port %u.\n",
               (unsigned)hosp_udp_port);
        tcp_send_line(c->fd, "The following doctors are available:");
        FILE *hf = open_hospital_file();
        if (hf) {
            char hline[512];
            int in_doc = 0;
            while (fgets(hline, sizeof hline, hf)) {
                if (strncmp(hline, "[Doctors]", 9) == 0) {
                    in_doc = 1;
                    continue;
                }
                if (strncmp(hline, "[Treatments]", 12) == 0)
                    break;
                if (!in_doc)
                    continue;
                char name[128], hx[65];
                if (sscanf(hline, "%127s %64s", name, hx) != 2)
                    continue;
                tcp_send_line(c->fd, name);
            }
            fclose(hf);
        }
        tcp_end_message(c->fd);
        printf("Hospital Server has sent the doctor lookup to the client.\n");
        return;
    }

    if (strncmp(line, "lookup ", 7) == 0) {
        const char *doc = line + 7;
        printf("Hospital Server has received a lookup request from a user with hash suffix %s to lookup %s availability using TCP over port %u.\n",
               suf, doc, (unsigned)hosp_tcp_port);
        snprintf(req, sizeof req, "REQ|AVAIL|%s", doc);
        if (appt_send(udpfd, req) < 0) {
            tcp_end_message(c->fd);
            return;
        }
        printf("Hospital Server sent the doctor lookup request to the Appointment server.\n");
        if (appt_recv(udpfd, resp, sizeof resp) < 0) {
            tcp_end_message(c->fd);
            return;
        }
        printf("Hospital Server has received the response from Appointment Server using UDP over port %u.\n",
               (unsigned)hosp_udp_port);
        if (strncmp(resp, "RES|AVAIL|all", 13) == 0)
            tcp_send_line(c->fd, "AVAIL|all");
        else if (strncmp(resp, "RES|AVAIL|none", 14) == 0)
            tcp_send_line(c->fd, "AVAIL|none");
        else if (strncmp(resp, "RES|AVAIL|some", 14) == 0)
            tcp_send_line(c->fd, resp);
        tcp_end_message(c->fd);
        printf("The Hospital Server has sent the response to the client.\n");
        return;
    }

    if (strncmp(line, "schedule ", 9) == 0) {
        char doc[128], tim[32], ill[64];
        if (sscanf(line + 9, "%127s %31s %63s", doc, tim, ill) != 3) {
            tcp_reply_done(c, "RES|ERR|badcmd");
            return;
        }
        printf("Hospital Server has received a schedule request from a user with hash suffix: %s to book an appointment using TCP over port %u.\n",
               suf, (unsigned)hosp_tcp_port);
        snprintf(req, sizeof req, "REQ|SCHED|%s|%s|%s|%s", doc, tim, ill, c->user_hex);
        if (appt_send(udpfd, req) < 0) {
            tcp_reply_done(c, "RES|ERR|appt");
            return;
        }
        printf("Hospital Server has sent the schedule request to the appointment server.\n");
        if (appt_recv(udpfd, resp, sizeof resp) < 0) {
            tcp_reply_done(c, "RES|ERR|appt");
            return;
        }
        printf("Hospital Server has received the response from Appointment Server using UDP over port %u.\n",
               (unsigned)hosp_udp_port);
        tcp_send_line(c->fd, resp);
        tcp_end_message(c->fd);
        printf("The hospital server has sent the response to the client.\n");
        return;
    }

    if (strcmp(line, "view_appointment") == 0) {
        printf("Hospital server has received a view appointment request from a user with hash suffix %s to view their appointment details using TCP over port %u.\n",
               suf, (unsigned)hosp_tcp_port);
        snprintf(req, sizeof req, "REQ|VIEW1|%s", c->user_hex);
        if (appt_send(udpfd, req) < 0) {
            tcp_reply_done(c, "RES|ERR|appt");
            return;
        }
        printf("Hospital Server has sent the view appointments request to the Appointment Server.\n");
        if (appt_recv(udpfd, resp, sizeof resp) < 0) {
            tcp_reply_done(c, "RES|ERR|appt");
            return;
        }
        printf("Hospital Server has received the response from the appointment server using UDP over port %u.\n",
               (unsigned)hosp_udp_port);
        tcp_send_line(c->fd, resp);
        tcp_end_message(c->fd);
        printf("The hospital server has sent the response to the client.\n");
        return;
    }

    if (strcmp(line, "cancel") == 0) {
        printf("Hospital Server has received a cancel request from user with hash suffix: %s to cancel their appointment using TCP over port %u.\n",
               suf, (unsigned)hosp_tcp_port);
        snprintf(req, sizeof req, "REQ|CANCEL|%s", c->user_hex);
        if (appt_send(udpfd, req) < 0) {
            tcp_reply_done(c, "RES|ERR|appt");
            return;
        }
        printf("The hospital server has sent the cancel request to the appointment server.\n");
        if (appt_recv(udpfd, resp, sizeof resp) < 0) {
            tcp_reply_done(c, "RES|ERR|appt");
            return;
        }
        printf("Hospital Server has received the response from Appointment Server using UDP over port %u.\n",
               (unsigned)hosp_udp_port);
        tcp_send_line(c->fd, resp);
        tcp_end_message(c->fd);
        printf("The hospital server has sent the response to the client.\n");
        return;
    }

    if (strcmp(line, "view_appointments") == 0) {
        if (!c->is_doctor) {
            tcp_reply_done(c, "RES|ERR|forbidden");
            return;
        }
        printf("Hospital Server has received a view appointments request from %s to view their schedule details using TCP over port %u.\n",
               c->doctor_plain, (unsigned)hosp_tcp_port);
        snprintf(req, sizeof req, "REQ|VIEW_DOC|%s|%s", c->doctor_plain, c->user_hex);
        if (appt_send(udpfd, req) < 0) {
            tcp_reply_done(c, "RES|ERR|appt");
            return;
        }
        printf("The hospital server has sent the view appointments request to the Appointment Server.\n");
        if (appt_recv(udpfd, resp, sizeof resp) < 0) {
            tcp_reply_done(c, "RES|ERR|appt");
            return;
        }
        printf("Hospital server has received the response from the Appointment server using UDP over port %u.\n",
               (unsigned)hosp_udp_port);
        tcp_send_line(c->fd, resp);
        tcp_end_message(c->fd);
        printf("The hospital server has sent the response to the client.\n");
        return;
    }

    if (strncmp(line, "prescribe ", 10) == 0) {
        if (!c->is_doctor) {
            tcp_reply_done(c, "RES|ERR|forbidden");
            return;
        }
        char pat[128], freq[512];
        if (split_prescribe_args(line + 10, pat, sizeof pat, freq, sizeof freq) < 0) {
            tcp_reply_done(c, "RES|ERR|badcmd");
            return;
        }
        char pat_hex[65];
        username_to_user_hex(pat, pat_hex);
        char illness[128], treatment[128];
        if (!find_illness_for_patient_doctor(pat_hex, c->doctor_plain, illness, sizeof illness) ||
            !lookup_treatment_for_illness(illness, treatment, sizeof treatment)) {
            tcp_reply_done(c, "RES|PRESCRIBE|fail|treatment");
            return;
        }
        printf("Hospital Server has received a prescribe request from %s for patient %s over TCP port %u.\n",
               c->doctor_plain, pat, (unsigned)hosp_tcp_port);
        snprintf(req, sizeof req, "REQ|PRESCRIBE|%s|%s|%s|%s", pat_hex, c->doctor_plain, treatment, freq);
        if (pres_send(udpfd, req) < 0) {
            tcp_reply_done(c, "RES|ERR|pres");
            return;
        }
        printf("Hospital Server has sent the prescribe request to the Prescription Server.\n");
        if (pres_recv(udpfd, resp, sizeof resp) < 0) {
            tcp_reply_done(c, "RES|ERR|pres");
            return;
        }
        printf("Hospital Server has received the response from Prescription Server over UDP.\n");
        tcp_send_line(c->fd, resp);
        tcp_end_message(c->fd);
        printf("The hospital server has sent the response to the client.\n");
        return;
    }

    if (strncmp(line, "view_prescription ", 18) == 0) {
        if (!c->is_doctor) {
            tcp_reply_done(c, "RES|ERR|forbidden");
            return;
        }
        char pat[128];
        if (sscanf(line + 18, "%127s", pat) != 1) {
            tcp_reply_done(c, "RES|ERR|badcmd");
            return;
        }
        char pat_hex[65];
        username_to_user_hex(pat, pat_hex);
        printf("Hospital Server has received a view-prescription request from %s for patient %s over TCP port %u.\n",
               c->doctor_plain, pat, (unsigned)hosp_tcp_port);
        snprintf(req, sizeof req, "REQ|VIEW_RX|%s", pat_hex);
        if (pres_send(udpfd, req) < 0) {
            tcp_reply_done(c, "RES|ERR|pres");
            return;
        }
        printf("Hospital Server has sent the view-prescription request to the Prescription Server.\n");
        if (pres_recv(udpfd, resp, sizeof resp) < 0) {
            tcp_reply_done(c, "RES|ERR|pres");
            return;
        }
        printf("Hospital Server has received the response from Prescription Server over UDP.\n");
        tcp_send_line(c->fd, resp);
        tcp_end_message(c->fd);
        printf("The hospital server has sent the response to the client.\n");
        return;
    }

    if (strcmp(line, "view_prescription") == 0) {
        if (c->is_doctor) {
            tcp_reply_done(c, "RES|ERR|badcmd");
            return;
        }
        printf("Hospital Server has received a view-prescription request from user hash suffix %s over TCP port %u.\n",
               suf, (unsigned)hosp_tcp_port);
        snprintf(req, sizeof req, "REQ|VIEW_RX|%s", c->user_hex);
        if (pres_send(udpfd, req) < 0) {
            tcp_reply_done(c, "RES|ERR|pres");
            return;
        }
        printf("Hospital Server has sent the view-prescription request to the Prescription Server.\n");
        if (pres_recv(udpfd, resp, sizeof resp) < 0) {
            tcp_reply_done(c, "RES|ERR|pres");
            return;
        }
        printf("Hospital Server has received the response from Prescription Server over UDP.\n");
        tcp_send_line(c->fd, resp);
        tcp_end_message(c->fd);
        printf("The hospital server has sent the response to the client.\n");
        return;
    }

    tcp_reply_done(c, "RES|ERR|unknown");
}

static void remove_client(int idx) {
    close(g_clients[idx].fd);
    g_clients[idx] = g_clients[g_ncli - 1];
    g_ncli--;
}

int main(void) {
    const char *host = "127.0.0.1";
    int udpfd = bind_udp_listener(host, PROJECT_HOSP_UDP_PORT);
    int tcpfd = bind_tcp_listener(host, PROJECT_HOSP_TCP_PORT);
    uint16_t udp_port = local_port(udpfd);
    uint16_t tcp_port = local_port(tcpfd);
    printf("Hospital Server is up and running using UDP on port %u.\n", (unsigned)udp_port);

    for (;;) {
        fd_set readfds;
        FD_ZERO(&readfds);
        FD_SET(tcpfd, &readfds);
        int maxfd = tcpfd;
        for (int i = 0; i < g_ncli; ++i) {
            FD_SET(g_clients[i].fd, &readfds);
            if (g_clients[i].fd > maxfd)
                maxfd = g_clients[i].fd;
        }
        if (select(maxfd + 1, &readfds, NULL, NULL, NULL) < 0) {
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
            ClientCtx tmp;
            memset(&tmp, 0, sizeof tmp);
            int auth_rc = handle_auth_tcp(newfd, udpfd, udp_port, tcp_port, &tmp);
            if (auth_rc < 0) {
                close(newfd);
                continue;
            }
            if (auth_rc != 0) {
                close(newfd);
                continue;
            }
            if (g_ncli < MAX_CLIENTS)
                g_clients[g_ncli++] = tmp;
            else
                close(newfd);
        }
        for (int i = 0; i < g_ncli;) {
            if (!FD_ISSET(g_clients[i].fd, &readfds)) {
                i++;
                continue;
            }
            char line[LINE_MAX];
            if (read_line(g_clients[i].fd, line, sizeof line) < 0) {
                remove_client(i);
                continue;
            }
            if (strcasecmp(line, "quit") == 0 || strcasecmp(line, "exit") == 0 ||
                strcasecmp(line, "logout") == 0) {
                remove_client(i);
                continue;
            }
            handle_client_cmd(&g_clients[i], udpfd, udp_port, tcp_port, line);
            i++;
        }
    }
    close(udpfd);
    close(tcpfd);
    return 0;
}
