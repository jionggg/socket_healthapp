/* Protocol constants shared by client, hospital, and authentication servers. */

#ifndef PROJECT_PROTO_H
#define PROJECT_PROTO_H

/* One-line auth request from client to hospital (TCP). */
#define PROJECT_AUTH_PREFIX "AUTH "
#define PROJECT_AUTH_PREFIX_LEN 5

/* UDP auth payload hospital <-> authentication server (no prefix). */
#define PROJECT_UDP_AUTH_MAX 200

/* Reply codes authentication -> hospital (UDP). */
#define PROJECT_AUTH_REPLY_OK "OK"
#define PROJECT_AUTH_REPLY_FAIL "FAIL"

/* Hospital -> client (TCP) after auth + role check. */
#define PROJECT_CLI_REPLY_FAIL "FAIL\n"
#define PROJECT_CLI_REPLY_PATIENT "PATIENT\n"
#define PROJECT_CLI_REPLY_DOCTOR "DOCTOR\n"

#endif
