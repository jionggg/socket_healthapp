/*
 * EE450 static port bases (Table 2). Replace EE450_USC_SUFFIX via Makefile -D
 * or edit the default to the last three digits of your USC ID.
 */
#ifndef PROJECT_PORTS_H
#define PROJECT_PORTS_H

#ifndef EE450_USC_SUFFIX
#define EE450_USC_SUFFIX 0
#endif

#define EE450_AUTH_UDP_PORT   (21000 + EE450_USC_SUFFIX)
#define EE450_PRES_UDP_PORT   (22000 + EE450_USC_SUFFIX)
#define EE450_APPT_UDP_PORT   (23000 + EE450_USC_SUFFIX)
#define EE450_HOSP_UDP_PORT   (25000 + EE450_USC_SUFFIX)
#define EE450_HOSP_TCP_PORT   (26000 + EE450_USC_SUFFIX)

#endif
