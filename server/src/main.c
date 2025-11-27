#include "server.h"
#include <stdio.h>
#include <signal.h>
#include <stdlib.h>

void sig_handler(int sig) {
    (void)sig;
    printf("\nStopping...\n");
    srv_stop();
    exit(0);
}

int main(void) {
    printf("C11 + GTK PROJECT\n\n");
    
    signal(SIGINT, sig_handler);
    signal(SIGTERM, sig_handler);
    
    if (srv_init() < 0) {
        fprintf(stderr, "Init failed\n");
        return 1;
    }
    
    srv_start();
    return 0;
}
