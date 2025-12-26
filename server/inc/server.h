#ifndef SERVER_H
#define SERVER_H

#include <pthread.h>
#include <stdbool.h>

#define PORT 6666
#define MAX_CONN 10
#define BUF_SIZE 4096

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

int srv_init(void);
void srv_start(void);
void srv_stop(void);

#endif

//Message r = {
    //.type = MSG_RESPONSE,
   // .from = "server",
    //.to = "gtk_client",
    //.action = ACT_CHANGE_PASSWORD,
    //.timestamp = 1703123505,
    //.data = NULL 
//}
