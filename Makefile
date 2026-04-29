# EE450 project — Ubuntu: make all, then run servers in order per Phase 1A.

CC      = gcc
CFLAGS  = -Wall -Wextra -std=c99 -pedantic -Iinclude
LDFLAGS =
USC_SUFFIX ?= 994
DEFS    = -DEE450_USC_SUFFIX=$(USC_SUFFIX)

HOSP    = hospital_server/hospital_server
AUTH    = authentication_server/authentication_server
APPT    = appointment_server/appointment_server
PRES    = prescription_server/prescription_server
CLI     = client/client

.PHONY: all clean

all: $(HOSP) $(AUTH) $(APPT) $(PRES) $(CLI)

$(HOSP): hospital_server/hospital_server.c include/project_ports.h
	$(CC) $(CFLAGS) $(DEFS) -o $@ hospital_server/hospital_server.c $(LDFLAGS)

$(AUTH): authentication_server/authentication_server.c include/project_ports.h
	$(CC) $(CFLAGS) $(DEFS) -o $@ authentication_server/authentication_server.c $(LDFLAGS)

$(APPT): appointment_server/appointment_server.c include/project_ports.h
	$(CC) $(CFLAGS) $(DEFS) -o $@ appointment_server/appointment_server.c $(LDFLAGS)

$(PRES): prescription_server/prescription_server.c include/project_ports.h
	$(CC) $(CFLAGS) $(DEFS) -o $@ prescription_server/prescription_server.c $(LDFLAGS)

$(CLI): client/client.c
	$(CC) $(CFLAGS) -o $@ client/client.c $(LDFLAGS)

clean:
	rm -f $(HOSP) $(AUTH) $(APPT) $(PRES) $(CLI)
