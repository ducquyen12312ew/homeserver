#ifndef SERVER_H
#define SERVER_H

#include <pthread.h>
#include <stdbool.h>

#define PORT 8888
#define MAX_CONN 50
#define BUF_SIZE 4096

typedef struct {
    int sock;
    char id[32];
    char ip[16];
    int port;
    bool is_dev;
    pthread_t tid;
} Conn;

int srv_init(void);
void srv_start(void);
void srv_stop(void);

#endif

