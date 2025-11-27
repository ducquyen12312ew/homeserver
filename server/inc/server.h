#ifndef SERVER_H
#define SERVER_H

#include <stdbool.h>
#include <pthread.h>

#define PORT 6666
#define MAX_CONN 50
#define BUF_SIZE 4096

typedef struct {
    int sock;
    char id[32];
    char ip[16];
    int port;
    bool is_dev;
    char device_type[16];
    bool online;
    pthread_t tid;
} Conn;

int srv_init(void);
void srv_start(void);
void srv_stop(void);
void get_device_list(char *json_out, size_t max_len);

#endif
