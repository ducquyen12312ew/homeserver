#ifndef SERVER_H
#define SERVER_H

#include <pthread.h>
#include <stdbool.h>
#include <time.h>

#define PORT 6666
#define MAX_CONN 10
#define BUF_SIZE 4096
#define MAX_TIMERS 50
#define MAX_LOGS 500

typedef struct {
    int sock;
    char id[32];
    char ip[32];
    int port;
    pthread_t tid;
    bool online;
    bool is_dev;
    bool logged_in;
    char device_type[32];
} Conn;

typedef struct {
    int timer_id;
    char device_id[32];
    char owner[32];
    time_t execute_time;
    bool state;
    bool active;
    char label[64];
} Timer;

typedef struct {
    int log_id;
    time_t timestamp;
    char actor[32];
    char action[32];
    char target[32];
    char details[128];
    bool success;
} ActivityLog;

int srv_init(void);
void srv_start(void);
void srv_stop(void);

#endif
