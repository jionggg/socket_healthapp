/*
 * Appointment server — Phase 2: UDP + appointments.txt.
 * Socket setup adapted from Beej's Guide to Network Programming (beej.us/guide/bgnet/).
 */

#include <arpa/inet.h>
#include <ctype.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

#include "../include/project_ports.h"

#define SLOT_CT 8
#define MAX_DOCS 32
#define UDP_BUF 8192

static const char *const SLOT_TIMES[SLOT_CT] = {
    "9:00", "10:00", "11:00", "12:00", "13:00", "14:00", "15:00", "16:00"
};

typedef struct {
    char doctor[128];
    char user_hex[SLOT_CT][65];
    char illness[SLOT_CT][64];
} DocBlock;

static DocBlock g_docs[MAX_DOCS];
static int g_ndocs;

static void trim_end(char *s) {
    size_t n = strlen(s);
    while (n > 0 && (s[n - 1] == '\n' || s[n - 1] == '\r' || isspace((unsigned char)s[n - 1])))
        s[--n] = '\0';
}

static FILE *open_appt_file(const char *mode) {
    FILE *f = fopen("appointment_server/appointments.txt", mode);
    return f;
}

static int time_to_idx(const char *t) {
    for (int i = 0; i < SLOT_CT; ++i) {
        if (strcmp(SLOT_TIMES[i], t) == 0)
            return i;
    }
    return -1;
}

static void load_appointments(void) {
    FILE *f = open_appt_file("r");
    if (!f) {
        g_ndocs = 0;
        return;
    }
    g_ndocs = 0;
    char line[512];
    int cur = -1;
    while (fgets(line, sizeof line, f)) {
        trim_end(line);
        if (line[0] == '\0')
            continue;
        char tok1[128], tok2[256], tok3[256];
        int n = sscanf(line, "%127s %255s %255s", tok1, tok2, tok3);
        if (cur >= 0 && n >= 1 && time_to_idx(tok1) >= 0) {
            int idx = time_to_idx(tok1);
            if (n == 1) {
                g_docs[cur].user_hex[idx][0] = '\0';
                g_docs[cur].illness[idx][0] = '\0';
            } else if (n == 3 && strlen(tok2) == 64) {
                strncpy(g_docs[cur].user_hex[idx], tok2, 64);
                g_docs[cur].user_hex[idx][64] = '\0';
                strncpy(g_docs[cur].illness[idx], tok3, 63);
                g_docs[cur].illness[idx][63] = '\0';
            }
            continue;
        }
        if (g_ndocs >= MAX_DOCS)
            continue;
        cur = g_ndocs++;
        memset(&g_docs[cur], 0, sizeof g_docs[cur]);
        strncpy(g_docs[cur].doctor, tok1, sizeof g_docs[cur].doctor - 1);
        g_docs[cur].doctor[sizeof g_docs[cur].doctor - 1] = '\0';
    }
    fclose(f);
}

static void save_appointments(void) {
    FILE *f = open_appt_file("w");
    if (!f) {
        perror("appointment_server: fopen write");
        return;
    }
    for (int d = 0; d < g_ndocs; ++d) {
        fprintf(f, "%s\n", g_docs[d].doctor);
        for (int i = 0; i < SLOT_CT; ++i) {
            if (g_docs[d].user_hex[i][0] == '\0')
                fprintf(f, "%s\n", SLOT_TIMES[i]);
            else
                fprintf(f, "%s %s %s\n", SLOT_TIMES[i], g_docs[d].user_hex[i], g_docs[d].illness[i]);
        }
        fprintf(f, "\n");
    }
    fclose(f);
}

static int find_doc(const char *doctor) {
    for (int i = 0; i < g_ndocs; ++i) {
        if (strcmp(g_docs[i].doctor, doctor) == 0)
            return i;
    }
    return -1;
}

static void hash_suffix(const char *hex64, char suf[6]) {
    memcpy(suf, hex64 + 59, 5);
    suf[5] = '\0';
}

static void print_lookup_recv(void) {
    printf("The Appointment Server has received a doctor availability request.\n");
}

static void print_lookup_sent(void) {
    printf("The Appointment Server has sent the lookup result to the Hospital Server.\n");
}

static void udp_reply(int sock, struct sockaddr *dst, socklen_t dlen, const char *msg) {
    (void)sendto(sock, msg, strlen(msg), 0, dst, dlen);
}

static uint16_t local_port(int sockfd) {
    struct sockaddr_in sin;
    socklen_t len = sizeof sin;
    if (getsockname(sockfd, (struct sockaddr *)&sin, &len) < 0) {
        perror("appointment_server: getsockname");
        exit(1);
    }
    return ntohs(sin.sin_port);
}

static void handle_udp(int sock, char *buf, struct sockaddr_storage *src, socklen_t srclen) {
    struct sockaddr *dst = (struct sockaddr *)src;

    if (strncmp(buf, "REQ|LIST", 8) == 0) {
        print_lookup_recv();
        udp_reply(sock, dst, srclen, "RES|LIST_OK");
        print_lookup_sent();
        return;
    }

    if (strncmp(buf, "REQ|AVAIL|", 10) == 0) {
        const char *doctor = buf + 10;
        print_lookup_recv();
        int di = find_doc(doctor);
        if (di < 0) {
            udp_reply(sock, dst, srclen, "RES|AVAIL|none");
            print_lookup_sent();
            return;
        }
        int booked = 0;
        for (int i = 0; i < SLOT_CT; ++i) {
            if (g_docs[di].user_hex[i][0] != '\0')
                booked++;
        }
        if (booked == 0) {
            printf("All time blocks are available for %s.\n", doctor);
            udp_reply(sock, dst, srclen, "RES|AVAIL|all");
        } else if (booked == SLOT_CT) {
            printf("%s has no time slots available.\n", doctor);
            udp_reply(sock, dst, srclen, "RES|AVAIL|none");
        } else {
            printf("%s has some time slots available.\n", doctor);
            char out[UDP_BUF];
            snprintf(out, sizeof out, "RES|AVAIL|some");
            for (int i = 0; i < SLOT_CT; ++i) {
                if (g_docs[di].user_hex[i][0] == '\0')
                    snprintf(out + strlen(out), sizeof out - strlen(out), "|%s", SLOT_TIMES[i]);
            }
            udp_reply(sock, dst, srclen, out);
        }
        print_lookup_sent();
        return;
    }

    if (strncmp(buf, "REQ|SCHED|", 10) == 0) {
        char doctor[128], time[32], ill[64], uhex[65];
        if (sscanf(buf + 10, "%127[^|]|%31[^|]|%63[^|]|%64s", doctor, time, ill, uhex) != 4)
            return;
        char suf[6];
        hash_suffix(uhex, suf);
        printf("Appointment scheduling request received (time: %s, doctor: %s, patient hash suffix: %s, illness: %s).\n",
               time, doctor, suf, ill);
        int di = find_doc(doctor);
        if (di < 0) {
            udp_reply(sock, dst, srclen, "RES|SCHED|bad");
            return;
        }
        int ti = time_to_idx(time);
        if (ti < 0) {
            char out[UDP_BUF];
            snprintf(out, sizeof out, "RES|SCHED|badtime");
            for (int i = 0; i < SLOT_CT; ++i) {
                if (g_docs[di].user_hex[i][0] == '\0')
                    snprintf(out + strlen(out), sizeof out - strlen(out), "|%s", SLOT_TIMES[i]);
            }
            udp_reply(sock, dst, srclen, out);
            return;
        }
        int allfull = 1;
        for (int i = 0; i < SLOT_CT; ++i) {
            if (g_docs[di].user_hex[i][0] == '\0') {
                allfull = 0;
                break;
            }
        }
        if (allfull) {
            printf("The requested appointment time is not available.\n");
            udp_reply(sock, dst, srclen, "RES|SCHED|full");
            return;
        }
        if (g_docs[di].user_hex[ti][0] != '\0') {
            printf("The requested appointment time is not available.\n");
            char outb[UDP_BUF];
            snprintf(outb, sizeof outb, "RES|SCHED|busy");
            for (int i = 0; i < SLOT_CT; ++i) {
                if (g_docs[di].user_hex[i][0] == '\0')
                    snprintf(outb + strlen(outb), sizeof outb - strlen(outb), "|%s", SLOT_TIMES[i]);
            }
            udp_reply(sock, dst, srclen, outb);
            return;
        }
        strncpy(g_docs[di].user_hex[ti], uhex, 64);
        g_docs[di].user_hex[ti][64] = '\0';
        strncpy(g_docs[di].illness[ti], ill, 63);
        g_docs[di].illness[ti][63] = '\0';
        save_appointments();
        printf("Appointment has been scheduled successfully for user %s with %s.\n", suf, doctor);
        udp_reply(sock, dst, srclen, "RES|SCHED|ok");
        return;
    }

    if (strncmp(buf, "REQ|VIEW1|", 10) == 0) {
        char uhex[65];
        if (sscanf(buf + 10, "%64s", uhex) != 1)
            return;
        char suf[6];
        hash_suffix(uhex, suf);
        printf("Appointment Server has received a view appointment command for the user with hash suffix %s.\n", suf);
        char out[UDP_BUF];
        size_t pos = (size_t)snprintf(out, sizeof out, "RES|VIEW1|ok");
        int any = 0;
        for (int d = 0; d < g_ndocs; ++d) {
            for (int i = 0; i < SLOT_CT; ++i) {
                if (strcmp(g_docs[d].user_hex[i], uhex) == 0) {
                    int n = snprintf(out + pos, sizeof out - pos, "|%s|%s|%s",
                                     g_docs[d].doctor, SLOT_TIMES[i], g_docs[d].illness[i]);
                    if (n > 0 && (size_t)n < sizeof out - pos)
                        pos += (size_t)n;
                    any = 1;
                }
            }
        }
        if (!any) {
            printf("The user with hash suffix %s has no appointment in the system.\n", suf);
            udp_reply(sock, dst, srclen, "RES|VIEW1|none");
            return;
        }
        printf("Returning details regarding the appointment for the user with hash suffix %s.\n", suf);
        udp_reply(sock, dst, srclen, out);
        return;
    }

    if (strncmp(buf, "REQ|CANCEL|", 11) == 0) {
        char uhex[65];
        if (sscanf(buf + 11, "%64s", uhex) != 1)
            return;
        char suf[6];
        hash_suffix(uhex, suf);
        printf("Appointment Server has received a cancel appointment command for the user with hash suffix: %s.\n", suf);
        char out[UDP_BUF];
        size_t pos = (size_t)snprintf(out, sizeof out, "RES|CANCEL|ok");
        int ncancel = 0;
        for (int d = 0; d < g_ndocs; ++d) {
            for (int i = 0; i < SLOT_CT; ++i) {
                if (strcmp(g_docs[d].user_hex[i], uhex) == 0) {
                    int n = snprintf(out + pos, sizeof out - pos, "|%s|%s", g_docs[d].doctor, SLOT_TIMES[i]);
                    if (n > 0 && (size_t)n < sizeof out - pos)
                        pos += (size_t)n;
                    g_docs[d].user_hex[i][0] = '\0';
                    g_docs[d].illness[i][0] = '\0';
                    ncancel++;
                }
            }
        }
        if (ncancel == 0) {
            printf("Error: Failed to find appointment.\n");
            udp_reply(sock, dst, srclen, "RES|CANCEL|fail");
            return;
        }
        save_appointments();
        printf("Successfully cancelled appointment.\n");
        udp_reply(sock, dst, srclen, out);
        return;
    }

    if (strncmp(buf, "REQ|VIEW_DOC|", 13) == 0) {
        char doctor[128], uhex[65];
        if (sscanf(buf + 13, "%127[^|]|%64s", doctor, uhex) != 2)
            return;
        printf("Appointment Server has received a request to view appointments scheduled for %s.\n", doctor);
        int di = find_doc(doctor);
        if (di < 0) {
            printf("No appointments have been made for %s.\n", doctor);
            udp_reply(sock, dst, srclen, "RES|VIEW_DOC|none");
            return;
        }
        int any = 0;
        for (int i = 0; i < SLOT_CT; ++i) {
            if (g_docs[di].user_hex[i][0] != '\0')
                any = 1;
        }
        if (!any) {
            printf("No appointments have been made for %s.\n", doctor);
            udp_reply(sock, dst, srclen, "RES|VIEW_DOC|none");
            return;
        }
        printf("Returning the scheduled appointments for %s.\n", doctor);
        char out[UDP_BUF];
        snprintf(out, sizeof out, "RES|VIEW_DOC|ok");
        for (int i = 0; i < SLOT_CT; ++i) {
            if (g_docs[di].user_hex[i][0] != '\0')
                snprintf(out + strlen(out), sizeof out - strlen(out), "|%s", SLOT_TIMES[i]);
        }
        udp_reply(sock, dst, srclen, out);
    }
}

int main(void) {
    const char *host = "127.0.0.1";
    load_appointments();

    int sockfd;
    struct sockaddr_in addr;
    if ((sockfd = socket(AF_INET, SOCK_DGRAM, 0)) < 0) {
        perror("appointment_server: socket");
        exit(1);
    }
    int yes = 1;
    if (setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof yes) < 0) {
        perror("appointment_server: setsockopt");
        exit(1);
    }
    memset(&addr, 0, sizeof addr);
    addr.sin_family = AF_INET;
    addr.sin_port = htons(PROJECT_APPT_UDP_PORT);
    if (inet_pton(AF_INET, host, &addr.sin_addr) <= 0) {
        fprintf(stderr, "appointment_server: bad address\n");
        exit(1);
    }
    if (bind(sockfd, (struct sockaddr *)&addr, sizeof addr) < 0) {
        perror("appointment_server: bind");
        exit(1);
    }
    uint16_t port = local_port(sockfd);
    printf("Appointment Server is up and running using UDP on port %u.\n", (unsigned)port);

    for (;;) {
        char buf[UDP_BUF];
        struct sockaddr_storage src;
        socklen_t srclen = sizeof src;
        ssize_t n = recvfrom(sockfd, buf, sizeof buf - 1, 0, (struct sockaddr *)&src, &srclen);
        if (n <= 0)
            continue;
        buf[n] = '\0';
        handle_udp(sockfd, buf, &src, srclen);
    }
    close(sockfd);
    return 0;
}
