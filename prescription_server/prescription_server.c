/*
 * Prescription server — Phase 3: UDP + prescriptions.txt.
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

#define UDP_BUF 8192
#define MAX_RX 512
#define PHEX_LEN 65
#define DOC_LEN 128
#define TRT_LEN 128
#define FREQ_LEN 512

typedef struct {
    char patient_hex[PHEX_LEN];
    char doctor[DOC_LEN];
    char treatment[TRT_LEN];
    char frequency[FREQ_LEN];
} RxRow;

static RxRow g_rx[MAX_RX];
static int g_nrx;

static void trim_end(char *s) {
    size_t n = strlen(s);
    while (n > 0 && (s[n - 1] == '\n' || s[n - 1] == '\r' || isspace((unsigned char)s[n - 1])))
        s[--n] = '\0';
}

static FILE *open_rx_file(const char *mode) {
    FILE *f = fopen("prescription_server/prescriptions.txt", mode);
    return f;
}

static void hash_suffix(const char *hex64, char out[6]) {
    memcpy(out, hex64 + 59, 5);
    out[5] = '\0';
}

static int hex64_valid(const char *h) {
    if (strlen(h) != 64)
        return 0;
    for (int i = 0; i < 64; ++i) {
        char c = h[i];
        if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F')))
            return 0;
    }
    return 1;
}

static void load_prescriptions(void) {
    FILE *f = open_rx_file("r");
    if (!f) {
        g_nrx = 0;
        return;
    }
    g_nrx = 0;
    char line[1024];
    while (fgets(line, sizeof line, f)) {
        trim_end(line);
        if (line[0] == '\0')
            continue;
        char phex[PHEX_LEN], doc[DOC_LEN], trt[TRT_LEN], freq[FREQ_LEN];
        const char *d1 = strchr(line, '|');
        if (!d1)
            continue;
        size_t plen = (size_t)(d1 - line);
        if (plen >= sizeof phex)
            continue;
        memcpy(phex, line, plen);
        phex[plen] = '\0';
        const char *d2 = strchr(d1 + 1, '|');
        if (!d2)
            continue;
        size_t dlen = (size_t)(d2 - (d1 + 1));
        if (dlen == 0 || dlen >= sizeof doc)
            continue;
        memcpy(doc, d1 + 1, dlen);
        doc[dlen] = '\0';
        const char *d3 = strchr(d2 + 1, '|');
        if (!d3) {
            /* Backward compatibility with old format: phex|doctor|frequency */
            strncpy(trt, "Unknown", sizeof trt - 1);
            trt[sizeof trt - 1] = '\0';
            strncpy(freq, d2 + 1, sizeof freq - 1);
            freq[sizeof freq - 1] = '\0';
        } else {
            size_t tlen = (size_t)(d3 - (d2 + 1));
            if (tlen == 0 || tlen >= sizeof trt)
                continue;
            memcpy(trt, d2 + 1, tlen);
            trt[tlen] = '\0';
            strncpy(freq, d3 + 1, sizeof freq - 1);
            freq[sizeof freq - 1] = '\0';
        }
        trim_end(doc);
        trim_end(trt);
        trim_end(freq);
        trim_end(phex);
        if (!hex64_valid(phex) || doc[0] == '\0')
            continue;
        if (g_nrx >= MAX_RX)
            break;
        strncpy(g_rx[g_nrx].patient_hex, phex, sizeof g_rx[g_nrx].patient_hex - 1);
        g_rx[g_nrx].patient_hex[sizeof g_rx[g_nrx].patient_hex - 1] = '\0';
        strncpy(g_rx[g_nrx].doctor, doc, sizeof g_rx[g_nrx].doctor - 1);
        g_rx[g_nrx].doctor[sizeof g_rx[g_nrx].doctor - 1] = '\0';
        strncpy(g_rx[g_nrx].treatment, trt, sizeof g_rx[g_nrx].treatment - 1);
        g_rx[g_nrx].treatment[sizeof g_rx[g_nrx].treatment - 1] = '\0';
        strncpy(g_rx[g_nrx].frequency, freq, sizeof g_rx[g_nrx].frequency - 1);
        g_rx[g_nrx].frequency[sizeof g_rx[g_nrx].frequency - 1] = '\0';
        g_nrx++;
    }
    fclose(f);
}

static void save_prescriptions(void) {
    FILE *f = open_rx_file("w");
    if (!f) {
        perror("prescription_server: fopen write");
        return;
    }
    for (int i = 0; i < g_nrx; ++i)
        fprintf(f, "%s|%s|%s|%s\n", g_rx[i].patient_hex, g_rx[i].doctor, g_rx[i].treatment, g_rx[i].frequency);
    fclose(f);
}

static void udp_reply(int sock, struct sockaddr *dst, socklen_t dlen, const char *msg) {
    (void)sendto(sock, msg, strlen(msg), 0, dst, dlen);
}

static uint16_t local_port(int sockfd) {
    struct sockaddr_in sin;
    socklen_t len = sizeof sin;
    if (getsockname(sockfd, (struct sockaddr *)&sin, &len) < 0) {
        perror("prescription_server: getsockname");
        exit(1);
    }
    return ntohs(sin.sin_port);
}

/* Parse REQ|PRESCRIBE|<64hex>|<doctor>|<treatment>|<frequency...> */
static int parse_prescribe_payload(const char *buf, char *phex, char *doc, char *trt, char *freq, size_t doccap,
                                   size_t trtcap, size_t freqcap) {
    const char *prefix = "REQ|PRESCRIBE|";
    if (strncmp(buf, prefix, strlen(prefix)) != 0)
        return -1;
    const char *a = buf + strlen(prefix);
    const char *d1 = strchr(a, '|');
    if (!d1)
        return -1;
    size_t plen = (size_t)(d1 - a);
    if (plen != 64)
        return -1;
    memcpy(phex, a, 64);
    phex[64] = '\0';
    if (!hex64_valid(phex))
        return -1;
    const char *b = d1 + 1;
    const char *d2 = strchr(b, '|');
    if (!d2)
        return -1;
    size_t dlen = (size_t)(d2 - b);
    if (dlen == 0 || dlen >= doccap)
        return -1;
    memcpy(doc, b, dlen);
    doc[dlen] = '\0';
    trim_end(doc);
    const char *c = d2 + 1;
    const char *d3 = strchr(c, '|');
    if (!d3)
        return -1;
    size_t tlen = (size_t)(d3 - c);
    if (tlen == 0 || tlen >= trtcap)
        return -1;
    memcpy(trt, c, tlen);
    trt[tlen] = '\0';
    trim_end(trt);
    strncpy(freq, d3 + 1, freqcap - 1);
    freq[freqcap - 1] = '\0';
    trim_end(freq);
    if (trt[0] == '\0' || freq[0] == '\0')
        return -1;
    if (strchr(doc, '|') || strchr(trt, '|') || strchr(freq, '|'))
        return -1;
    return 0;
}

static void handle_udp(int sock, char *buf, struct sockaddr *dst, socklen_t dlen) {
    trim_end(buf);

    if (strncmp(buf, "REQ|PRESCRIBE|", 14) == 0) {
        char phex[PHEX_LEN], doc[DOC_LEN], trt[TRT_LEN], freq[FREQ_LEN];
        if (parse_prescribe_payload(buf, phex, doc, trt, freq, sizeof doc, sizeof trt, sizeof freq) < 0) {
            printf("Prescription Server received an invalid prescribe request.\n");
            udp_reply(sock, dst, dlen, "RES|PRESCRIBE|fail|bad");
            return;
        }
        char suf[6];
        hash_suffix(phex, suf);
        printf("Prescription Server has received a prescribe request for patient hash suffix %s from %s.\n",
               suf, doc);
        /* Upsert: at most one row per (patient_hex, doctor). */
        int slot = -1;
        for (int i = 0; i < g_nrx; ++i) {
            if (strcmp(g_rx[i].patient_hex, phex) == 0 && strcmp(g_rx[i].doctor, doc) == 0) {
                slot = i;
                break;
            }
        }
        if (slot < 0) {
            if (g_nrx >= MAX_RX) {
                printf("Prescription Server: storage full; cannot add prescription.\n");
                udp_reply(sock, dst, dlen, "RES|PRESCRIBE|fail|full");
                return;
            }
            slot = g_nrx++;
            strncpy(g_rx[slot].patient_hex, phex, sizeof g_rx[slot].patient_hex - 1);
            g_rx[slot].patient_hex[sizeof g_rx[slot].patient_hex - 1] = '\0';
            strncpy(g_rx[slot].doctor, doc, sizeof g_rx[slot].doctor - 1);
            g_rx[slot].doctor[sizeof g_rx[slot].doctor - 1] = '\0';
        }
        strncpy(g_rx[slot].treatment, trt, sizeof g_rx[slot].treatment - 1);
        g_rx[slot].treatment[sizeof g_rx[slot].treatment - 1] = '\0';
        strncpy(g_rx[slot].frequency, freq, sizeof g_rx[slot].frequency - 1);
        g_rx[slot].frequency[sizeof g_rx[slot].frequency - 1] = '\0';
        save_prescriptions();
        printf("Prescription recorded for patient hash suffix %s (prescriber %s).\n", suf, doc);
        udp_reply(sock, dst, dlen, "RES|PRESCRIBE|ok");
        printf("Prescription Server has sent the prescribe result to the Hospital Server.\n");
        return;
    }

    if (strncmp(buf, "REQ|VIEW_RX|", 12) == 0) {
        char phex[PHEX_LEN];
        if (sscanf(buf + 12, "%64s", phex) != 1 || !hex64_valid(phex)) {
            udp_reply(sock, dst, dlen, "RES|VIEW_RX|fail|bad");
            return;
        }
        char suf[6];
        hash_suffix(phex, suf);
        printf("Prescription Server has received a view-prescription request for patient hash suffix %s.\n", suf);
        char out[UDP_BUF];
        size_t pos = (size_t)snprintf(out, sizeof out, "RES|VIEW_RX|ok");
        int any = 0;
        for (int i = 0; i < g_nrx; ++i) {
            if (strcmp(g_rx[i].patient_hex, phex) != 0)
                continue;
            any = 1;
            int n = snprintf(out + pos, sizeof out - pos, "|%s|%s|%s",
                             g_rx[i].doctor, g_rx[i].treatment, g_rx[i].frequency);
            if (n > 0 && (size_t)n < sizeof out - pos)
                pos += (size_t)n;
        }
        if (!any) {
            printf("No prescriptions on file for patient hash suffix %s.\n", suf);
            udp_reply(sock, dst, dlen, "RES|VIEW_RX|none");
        } else {
            printf("Returning prescription list for patient hash suffix %s.\n", suf);
            udp_reply(sock, dst, dlen, out);
        }
        printf("Prescription Server has sent the view-prescription result to the Hospital Server.\n");
        return;
    }
}

int main(void) {
    const char *host = "127.0.0.1";
    load_prescriptions();

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
    addr.sin_port = htons(PROJECT_PRES_UDP_PORT);
    if (inet_pton(AF_INET, host, &addr.sin_addr) <= 0) {
        fprintf(stderr, "prescription_server: bad address\n");
        exit(1);
    }

    if (bind(sockfd, (struct sockaddr *)&addr, sizeof addr) < 0) {
        perror("prescription_server: bind");
        exit(1);
    }

    uint16_t port = local_port(sockfd);
    printf("Prescription Server is up and running using UDP on port %u.\n", (unsigned)port);

    for (;;) {
        char buf[UDP_BUF];
        struct sockaddr_storage src;
        socklen_t srclen = sizeof src;
        ssize_t n = recvfrom(sockfd, buf, sizeof buf - 1, 0, (struct sockaddr *)&src, &srclen);
        if (n <= 0)
            continue;
        buf[n] = '\0';
        handle_udp(sockfd, buf, (struct sockaddr *)&src, srclen);
    }

    close(sockfd);
    return 0;
}
