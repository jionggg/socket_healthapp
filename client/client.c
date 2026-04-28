/*
 * EE450 Client — Phase 1A: boot message, then wait for user (Phase 1B auth).
 */

#include <stdio.h>
#include <stdlib.h>

int main(int argc, char *argv[]) {
    (void)argc;
    (void)argv;

    printf("The client is up and running.\n");

    /* Phase 1A: stay running until the user ends the process (Phase 1B adds auth). */
    for (;;) {
        if (getchar() == EOF)
            break;
    }
    return 0;
}
