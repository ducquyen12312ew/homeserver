#include "server.h"
#include <stdio.h>
#include <signal.h>
#include <stdlib.h>

void sig_handler(int sig) {
    (void)sig;
    printf("\nStop\n");
    srv_stop();
    exit(0);
}

int main(void) {
    printf("============================\n");
    printf("  Smart Home Server v1.0\n");
    printf("  C11 - GCC - Ubuntu\n");
    printf("============================\n\n");
    
    signal(SIGINT, sig_handler);
    signal(SIGTERM, sig_handler);
    
    if (srv_init() < 0) {
        fprintf(stderr, "Init fail\n");
        return 1;
    }
    
    srv_start();
    return 0;
}
