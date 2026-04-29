/*
 * Static port bases (Table 2). Override PROJECT_USC_SUFFIX via Makefile -D
 * or edit the default to the last three digits of your USC ID.
 */
#ifndef PROJECT_PORTS_H
#define PROJECT_PORTS_H

#ifndef PROJECT_USC_SUFFIX
#define PROJECT_USC_SUFFIX 0
#endif

#define PROJECT_AUTH_UDP_PORT   (21000 + PROJECT_USC_SUFFIX)
#define PROJECT_PRES_UDP_PORT   (22000 + PROJECT_USC_SUFFIX)
#define PROJECT_APPT_UDP_PORT   (23000 + PROJECT_USC_SUFFIX)
#define PROJECT_HOSP_UDP_PORT   (25000 + PROJECT_USC_SUFFIX)
#define PROJECT_HOSP_TCP_PORT   (26000 + PROJECT_USC_SUFFIX)

#endif
