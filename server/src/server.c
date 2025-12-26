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
#include <pthread.h>
#include <stdbool.h>

static int srv_sock = -1;
static bool running = false;
static char admin_password[32] = "admin";

typedef struct {
    Conn conns[MAX_CONN];
    int cnt;
    pthread_mutex_t mtx;
} ConnList;

static ConnList list = {.cnt = 0, .mtx = PTHREAD_MUTEX_INITIALIZER};

static void* handle_conn(void *arg);
static void handle_msg(Conn *c, const char *json);
static void route_msg(Message *m);
static void handle_list_devices(Conn *c);
static void send_error_response(Conn *c, const char *action, const char *error_msg);

int srv_init(void) {
    if (pthread_mutex_init(&list.mtx, NULL) != 0) {
        fprintf(stderr, "Mutex init failed\n");
        return -1;
    }

    srv_sock = socket(AF_INET, SOCK_STREAM, 0);
    if (srv_sock < 0) {
        perror("socket");
        return -1;
    }

    int opt = 1;
    if (setsockopt(srv_sock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
        perror("setsockopt");
        close(srv_sock);
        return -1;
    }

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
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

    printf("Server listening on port %d\n", PORT);
    printf("Default password: %s\n\n", admin_password);
    return 0;
}

void srv_start(void) {
    running = true;
    printf("Server started\n\n");

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
        if (!c) {
            fprintf(stderr, "malloc failed for Conn\n");
            close(csock);
            continue;
        }
        memset(c, 0, sizeof(Conn));
        c->sock = csock;
        strncpy(c->ip, inet_ntoa(caddr.sin_addr), sizeof(c->ip) - 1);
        c->ip[sizeof(c->ip) - 1] = '\0';
        c->port = ntohs(caddr.sin_port);
        snprintf(c->id, sizeof(c->id), "tmp_%d", csock);
        c->online = true;
        c->is_dev = false;
        c->logged_in = false;
        c->device_type[0] = '\0';

        printf("[CONNECT] %s:%d\n", c->ip, c->port);

        if (pthread_create(&c->tid, NULL, handle_conn, c) != 0) {
            perror("pthread_create");
            close(csock);
            free(c);
            continue;
        }
        pthread_detach(c->tid);
    }
}

static void* handle_conn(void *arg) {
    Conn *c = (Conn*)arg;
    char buf[BUF_SIZE];

    while (running) {
        int n = recv(c->sock, buf, BUF_SIZE - 1, 0);

        if (n <= 0) {
            printf("[DISCONNECT] %s\n", c->id);
            c->online = false;
            break;
        }

        buf[n] = '\0';

        if (n < 5) {
            continue;
        }

        printf("\n[RX] %s:\n%s\n", c->id, buf);
        handle_msg(c, buf);
    }

    close(c->sock);

    pthread_mutex_lock(&list.mtx);
    for (int i = 0; i < list.cnt; i++) {
        if (strcmp(list.conns[i].id, c->id) == 0) {
            list.conns[i].online = false;
            break;
        }
    }
    pthread_mutex_unlock(&list.mtx);

    free(c);
    return NULL;
}

static void send_error_response(Conn *c, const char *action, const char *error_msg) {
    Message *r = calloc(1, sizeof(Message));
    if (!r) return;

    r->type = MSG_RESPONSE;
    strncpy(r->from, "server", sizeof(r->from) - 1);
    r->from[sizeof(r->from) - 1] = '\0';
    strncpy(r->to, c->id, sizeof(r->to) - 1);
    r->to[sizeof(r->to) - 1] = '\0';
    
    if (strcmp(action, "register") == 0) {
        r->action = ACT_REGISTER;
    } else if (strcmp(action, "login") == 0) {
        r->action = ACT_LOGIN;
    } else if (strcmp(action, "change_password") == 0) {
        r->action = ACT_CHANGE_PASSWORD;
    } else {
        r->action = ACT_CONTROL;
    }
    
    r->timestamp = time(NULL);

    struct json_object *d = json_object_new_object();
    json_object_object_add(d, "status", json_object_new_string("error"));
    json_object_object_add(d, "message", json_object_new_string(error_msg));
    r->data = d;

    char *js = create_msg(r);
    if (js) {
        send(c->sock, js, strlen(js), 0);
        send(c->sock, "\n", 1, 0);
        free(js);
    }
    free_msg(r);
}

static void handle_msg(Conn *c, const char *json) {
    Message *m = parse_msg(json);
    if (!m) {
        printf("[ERROR] Parse failed\n");
        return;
    }

    printf("[MSG] %s | %s | %s -> %s\n",
        type_str(m->type), action_str(m->action), m->from, m->to);

    if (m->action == ACT_REGISTER) {
        strncpy(c->id, m->from, sizeof(c->id) - 1);
        c->id[sizeof(c->id) - 1] = '\0';
        c->is_dev = true;
        c->logged_in = true;

        struct json_object *data = (struct json_object*)m->data;
        struct json_object *dev_type;
        if (json_object_object_get_ex(data, "device_type", &dev_type)) {
            strncpy(c->device_type, json_object_get_string(dev_type), 
                    sizeof(c->device_type) - 1);
            c->device_type[sizeof(c->device_type) - 1] = '\0';
        }

        pthread_mutex_lock(&list.mtx);
        bool found = false;
        for (int i = 0; i < list.cnt; i++) {
            if (strcmp(list.conns[i].id, c->id) == 0) {
                list.conns[i] = *c;
                found = true;
                break;
            }
        }
        if (!found && list.cnt < MAX_CONN) {
            list.conns[list.cnt++] = *c;
        }
        pthread_mutex_unlock(&list.mtx);

        Message *r = calloc(1, sizeof(Message));
        if (r) {
            r->type = MSG_RESPONSE;
            strncpy(r->from, "server", sizeof(r->from) - 1);
            r->from[sizeof(r->from) - 1] = '\0';
            strncpy(r->to, m->from, sizeof(r->to) - 1);
            r->to[sizeof(r->to) - 1] = '\0';
            r->action = ACT_REGISTER;
            r->timestamp = time(NULL);

            struct json_object *d = json_object_new_object();
            json_object_object_add(d, "status", json_object_new_string("success"));
            json_object_object_add(d, "device_id", json_object_new_string(c->id));
            r->data = d;

            char *js = create_msg(r);
            if (js) {
                send(c->sock, js, strlen(js), 0);
                send(c->sock, "\n", 1, 0);
                free(js);
            }
            free_msg(r);
        }
        printf("[REGISTER] Device: %s (%s)\n", c->id, c->device_type);
    }
    else if (m->action == ACT_LOGIN) {
        struct json_object *data = (struct json_object*)m->data;
        struct json_object *pass_obj;
        
        const char *provided_password = NULL;
        if (json_object_object_get_ex(data, "password", &pass_obj)) {
            provided_password = json_object_get_string(pass_obj);
        }

        if (!provided_password || strcmp(provided_password, admin_password) != 0) {
            printf("[LOGIN] FAILED - wrong password from %s\n", m->from);
            send_error_response(c, "login", "wrong_password");
            free_msg(m);
            return;
        }

        strncpy(c->id, m->from, sizeof(c->id) - 1);
        c->id[sizeof(c->id) - 1] = '\0';
        c->is_dev = false;
        c->logged_in = true;
        
        pthread_mutex_lock(&list.mtx);
        bool found = false;
        for (int i = 0; i < list.cnt; i++) {
            if (strcmp(list.conns[i].id, c->id) == 0) {
                list.conns[i] = *c;
                found = true;
                break;
            }
        }
        if (!found && list.cnt < MAX_CONN) {
            list.conns[list.cnt++] = *c;
        }
        pthread_mutex_unlock(&list.mtx);

        Message *r = calloc(1, sizeof(Message));
        if (r) {
            r->type = MSG_RESPONSE;
            strncpy(r->from, "server", sizeof(r->from) - 1);
            r->from[sizeof(r->from) - 1] = '\0';
            strncpy(r->to, m->from, sizeof(r->to) - 1);
            r->to[sizeof(r->to) - 1] = '\0';
            r->action = ACT_LOGIN;
            r->timestamp = time(NULL);

            struct json_object *d = json_object_new_object();
            json_object_object_add(d, "status", json_object_new_string("success"));
            json_object_object_add(d, "token", json_object_new_string("token123"));
            r->data = d;

            char *js = create_msg(r);
            if (js) {
                send(c->sock, js, strlen(js), 0);
                send(c->sock, "\n", 1, 0);
                free(js);
            }
            free_msg(r);
        }
        printf("[LOGIN] SUCCESS - Client: %s\n", c->id);
    }
    else if (m->action == ACT_CHANGE_PASSWORD) {
        if (!c->logged_in) {
            printf("[CHANGE_PASSWORD] Rejected - not authenticated\n");
            send_error_response(c, "change_password", "not_authenticated");
            free_msg(m);
            return;
        }

        struct json_object *data = (struct json_object*)m->data;
        struct json_object *oldp, *newp;

        Message *r = calloc(1, sizeof(Message));
        r->type = MSG_RESPONSE;
        strcpy(r->from, "server");
        strcpy(r->to, m->from);
        r->action = ACT_CHANGE_PASSWORD;
        r->timestamp = time(NULL);

        struct json_object *res = json_object_new_object();

        if (json_object_object_get_ex(data, "old_password", &oldp) &&
            json_object_object_get_ex(data, "new_password", &newp)) {

            const char *oldpw = json_object_get_string(oldp);
            const char *newpw = json_object_get_string(newp);

            if (strcmp(oldpw, admin_password) == 0) {
                strncpy(admin_password, newpw, sizeof(admin_password) - 1);
                admin_password[sizeof(admin_password) - 1] = '\0';
                json_object_object_add(res, "status",
                    json_object_new_string("success"));
                printf("[CHANGE_PASSWORD] Password changed by %s\n", c->id);
                c->logged_in = false;
            } else {
                json_object_object_add(res, "status",
                    json_object_new_string("wrong_password"));
                printf("[CHANGE_PASSWORD] Wrong old password from %s\n", c->id);
            }
        } else {
            json_object_object_add(res, "status",
                json_object_new_string("invalid_request"));
        }

        r->data = res;

        char *js = create_msg(r);
        send(c->sock, js, strlen(js), 0);
        send(c->sock, "\n", 1, 0);

        free(js);
        free_msg(r);
    }
    else if (m->action == ACT_LIST_DEVICES) {
        if (!c->logged_in) {
            printf("[LIST_DEVICES] Rejected - not authenticated\n");
            send_error_response(c, "list_devices", "not_authenticated");
            free_msg(m);
            return;
        }
        handle_list_devices(c);
    }
    else if (m->action == ACT_HEARTBEAT) {
        printf("[HEARTBEAT] From %s\n", m->from);
    }
    else {
        if (!c->logged_in && !c->is_dev) {
            printf("[CONTROL] Rejected - not authenticated\n");
            send_error_response(c, "control", "not_authenticated");
            free_msg(m);
            return;
        }
        route_msg(m);
    }

    free_msg(m);
}

static void handle_list_devices(Conn *c) {
    Message *r = calloc(1, sizeof(Message));
    if (!r) return;

    r->type = MSG_RESPONSE;
    strncpy(r->from, "server", sizeof(r->from) - 1);
    r->from[sizeof(r->from) - 1] = '\0';
    strncpy(r->to, c->id, sizeof(r->to) - 1);
    r->to[sizeof(r->to) - 1] = '\0';
    r->action = ACT_LIST_DEVICES;
    r->timestamp = time(NULL);

    struct json_object *devices = json_object_new_array();

    pthread_mutex_lock(&list.mtx);
    for (int i = 0; i < list.cnt; i++) {
        if (list.conns[i].is_dev && list.conns[i].online) {
            struct json_object *dev = json_object_new_object();
            json_object_object_add(dev, "id", json_object_new_string(list.conns[i].id));
            json_object_object_add(dev, "type", json_object_new_string(list.conns[i].device_type));
            json_object_object_add(dev, "ip", json_object_new_string(list.conns[i].ip));
            json_object_array_add(devices, dev);
        }
    }
    pthread_mutex_unlock(&list.mtx);

    struct json_object *d = json_object_new_object();
    json_object_object_add(d, "devices", devices);
    r->data = d;

    char *js = create_msg(r);
    if (js) {
        send(c->sock, js, strlen(js), 0);
        send(c->sock, "\n", 1, 0);
        free(js);
    }

    printf("[LIST] Sent %zu devices to %s\n", json_object_array_length(devices), c->id);

    free_msg(r);
}

static void route_msg(Message *m) {
    pthread_mutex_lock(&list.mtx);

    bool found = false;
    for (int i = 0; i < list.cnt; i++) {
        if (strcmp(list.conns[i].id, m->to) == 0 && list.conns[i].online) {
            char *js = create_msg(m);
            if (js) {
                send(list.conns[i].sock, js, strlen(js), 0);
                send(list.conns[i].sock, "\n", 1, 0);
                free(js);
            }
            printf("[ROUTE] %s -> %s\n", m->from, m->to);
            found = true;
            break;
        }
    }

    pthread_mutex_unlock(&list.mtx);

    if (!found) {
        printf("[ERROR] Destination not found: %s\n", m->to);
    }
}

void srv_stop(void) {
    running = false;
    if (srv_sock >= 0) close(srv_sock);

    pthread_mutex_lock(&list.mtx);
    for (int i = 0; i < list.cnt; i++) {
        if (list.conns[i].online) {
            close(list.conns[i].sock);
            list.conns[i].online = false;
        }
    }
    pthread_mutex_unlock(&list.mtx);

    pthread_mutex_destroy(&list.mtx);
    printf("Server stopped\n");
}
