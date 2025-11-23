#include "server.h"
#include "protocol.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <errno.h>
#include <json-c/json.h>
#include <time.h>

static int srv_sock = -1;
static bool running = false;

typedef struct {
    Conn conns[MAX_CONN];
    int cnt;
    pthread_mutex_t mtx;
} ConnList;

static ConnList list = {.cnt = 0};

static void* handle_conn(void *arg);
static void handle_msg(Conn *c, const char *json);
static void route_msg(Message *m);

int srv_init(void) {
    pthread_mutex_init(&list.mtx, NULL);
    
    srv_sock = socket(AF_INET, SOCK_STREAM, 0);
    if (srv_sock < 0) {
        perror("socket");
        return -1;
    }
    
    int opt = 1;
    setsockopt(srv_sock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    
    struct sockaddr_in addr = {0};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(PORT);
    
    if (bind(srv_sock, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        perror("bind");
        close(srv_sock);
        return -1;
    }
    
    if (listen(srv_sock, MAX_CONN) < 0) {
        perror("listen");
        close(srv_sock);
        return -1;
    }
    
    printf("✓ Server on port %d\n", PORT);
    return 0;
}

void srv_start(void) {
    running = true;
    printf("✓ Waiting...\n\n");
    
    while (running) {
        struct sockaddr_in caddr;
        socklen_t len = sizeof(caddr);
        
        int csock = accept(srv_sock, (struct sockaddr*)&caddr, &len);
        if (csock < 0) {
            if (errno == EINTR) continue;
            perror("accept");
            continue;
        }
        
        Conn *c = malloc(sizeof(Conn));
        c->sock = csock;
        strncpy(c->ip, inet_ntoa(caddr.sin_addr), 15);
        c->port = ntohs(caddr.sin_port);
        snprintf(c->id, 31, "tmp_%d", csock);
        
        printf("→ New: %s:%d\n", c->ip, c->port);
        
        pthread_create(&c->tid, NULL, handle_conn, c);
        pthread_detach(c->tid);
    }
}

static void* handle_conn(void *arg) {
    Conn *c = (Conn*)arg;
    char buf[BUF_SIZE];
    
    while (running) {
        memset(buf, 0, BUF_SIZE);
        int n = recv(c->sock, buf, BUF_SIZE - 1, 0);
        
        if (n <= 0) {
            printf("← %s disc\n", c->id);
            break;
        }
        
        printf("\n📨 %s:\n%s\n", c->id, buf);
        handle_msg(c, buf);
    }
    
    close(c->sock);
    
    pthread_mutex_lock(&list.mtx);
    for (int i = 0; i < list.cnt; i++) {
        if (strcmp(list.conns[i].id, c->id) == 0) {
            for (int j = i; j < list.cnt - 1; j++) {
                list.conns[j] = list.conns[j + 1];
            }
            list.cnt--;
            break;
        }
    }
    pthread_mutex_unlock(&list.mtx);
    
    free(c);
    return NULL;
}

static void handle_msg(Conn *c, const char *json) {
    Message *m = parse_msg(json);
    if (!m) {
        printf("✗ Parse fail\n");
        return;
    }
    
    printf("  %s | %s | %s → %s\n", type_str(m->type), action_str(m->action), m->from, m->to);
    
    if (m->action == ACT_REGISTER) {
        strncpy(c->id, m->from, 31);
        c->is_dev = true;
        
        pthread_mutex_lock(&list.mtx);
        if (list.cnt < MAX_CONN) {
            list.conns[list.cnt++] = *c;
        }
        pthread_mutex_unlock(&list.mtx);
        
        printf("✓ Device: %s\n", c->id);
        
        Message *r = calloc(1, sizeof(Message));
        r->type = MSG_RESPONSE;
        strncpy(r->from, "server", 31);
        strncpy(r->to, m->from, 31);
        r->action = ACT_REGISTER;
        r->timestamp = time(NULL);
        
        struct json_object *d = json_object_new_object();
        json_object_object_add(d, "status", json_object_new_string("success"));
        json_object_object_add(d, "device_id", json_object_new_string(c->id));
        r->data = d;
        
        char *js = create_msg(r);
        send(c->sock, js, strlen(js), 0);
        
        free(js);
        free_msg(r);
    }
    else if (m->action == ACT_LOGIN) {
        strncpy(c->id, m->from, 31);
        c->is_dev = false;
        
        pthread_mutex_lock(&list.mtx);
        if (list.cnt < MAX_CONN) {
            list.conns[list.cnt++] = *c;
        }
        pthread_mutex_unlock(&list.mtx);
        
        printf("✓ Client: %s\n", c->id);
        
        Message *r = calloc(1, sizeof(Message));
        r->type = MSG_RESPONSE;
        strncpy(r->from, "server", 31);
        strncpy(r->to, m->from, 31);
        r->action = ACT_LOGIN;
        r->timestamp = time(NULL);
        
        struct json_object *d = json_object_new_object();
        json_object_object_add(d, "status", json_object_new_string("success"));
        json_object_object_add(d, "token", json_object_new_string("abc123"));
        r->data = d;
        
        char *js = create_msg(r);
        send(c->sock, js, strlen(js), 0);
        
        free(js);
        free_msg(r);
    }
    else {
        route_msg(m);
    }
    
    free_msg(m);
}

static void route_msg(Message *m) {
    pthread_mutex_lock(&list.mtx);
    
    bool found = false;
    for (int i = 0; i < list.cnt; i++) {
        if (strcmp(list.conns[i].id, m->to) == 0) {
            char *js = create_msg(m);
            send(list.conns[i].sock, js, strlen(js), 0);
            printf("✓ Route: %s → %s\n", m->from, m->to);
            free(js);
            found = true;
            break;
        }
    }
    
    pthread_mutex_unlock(&list.mtx);
    
    if (!found) {
        printf("✗ Not found: %s\n", m->to);
    }
}

void srv_stop(void) {
    running = false;
    if (srv_sock >= 0) close(srv_sock);
}
