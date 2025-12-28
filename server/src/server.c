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

typedef struct {
    Timer timers[MAX_TIMERS];
    int cnt;
    int next_id;
    pthread_mutex_t mtx;
} TimerList;

typedef struct {
    ActivityLog logs[MAX_LOGS];
    int cnt;
    int next_id;
    pthread_mutex_t mtx;
    FILE *log_file;
} LogManager;

static ConnList list = {.cnt = 0, .mtx = PTHREAD_MUTEX_INITIALIZER};
static TimerList timer_list = {.cnt = 0, .next_id = 1, .mtx = PTHREAD_MUTEX_INITIALIZER};
static LogManager log_mgr = {.cnt = 0, .next_id = 1, .mtx = PTHREAD_MUTEX_INITIALIZER, .log_file = NULL};

static void* handle_conn(void *arg);
static void* timer_thread(void *arg);
static void handle_msg(Conn *c, const char *json);
static void route_msg(Message *m);
static void handle_list_devices(Conn *c);
static void handle_set_timer(Conn *c, Message *m);
static void handle_list_timers(Conn *c, Message *m);
static void handle_delete_timer(Conn *c, Message *m);
static void handle_get_logs(Conn *c, Message *m);
static void send_error_response(Conn *c, const char *action, const char *error_msg);
static void execute_timer(Timer *t);
static void log_activity(const char *actor, const char *action, const char *target, const char *details, bool success);
static void init_log_file(void);
static void close_log_file(void);

static void init_log_file(void) {
    log_mgr.log_file = fopen("server_activity.log", "a");
    if (!log_mgr.log_file) {
        fprintf(stderr, "Warning: Cannot open log file\n");
    }
}

static void close_log_file(void) {
    if (log_mgr.log_file) {
        fclose(log_mgr.log_file);
        log_mgr.log_file = NULL;
    }
}

static void log_activity(const char *actor, const char *action, const char *target, const char *details, bool success) {
    pthread_mutex_lock(&log_mgr.mtx);
    
    if (log_mgr.cnt >= MAX_LOGS) {
        memmove(&log_mgr.logs[0], &log_mgr.logs[1], (MAX_LOGS - 1) * sizeof(ActivityLog));
        log_mgr.cnt = MAX_LOGS - 1;
    }
    
    ActivityLog *log = &log_mgr.logs[log_mgr.cnt];
    log->log_id = log_mgr.next_id++;
    log->timestamp = time(NULL);
    strncpy(log->actor, actor, sizeof(log->actor) - 1);
    log->actor[sizeof(log->actor) - 1] = '\0';
    strncpy(log->action, action, sizeof(log->action) - 1);
    log->action[sizeof(log->action) - 1] = '\0';
    strncpy(log->target, target, sizeof(log->target) - 1);
    log->target[sizeof(log->target) - 1] = '\0';
    strncpy(log->details, details, sizeof(log->details) - 1);
    log->details[sizeof(log->details) - 1] = '\0';
    log->success = success;
    
    log_mgr.cnt++;
    
    if (log_mgr.log_file) {
        char time_str[64];
        struct tm *tm_info = localtime(&log->timestamp);
        strftime(time_str, sizeof(time_str), "%Y-%m-%d %H:%M:%S", tm_info);
        
        fprintf(log_mgr.log_file, "[%s] ID=%d | %s | %s -> %s | %s | %s\n",
                time_str, log->log_id, log->success ? "SUCCESS" : "FAILED",
                log->actor, log->target, log->action, log->details);
        fflush(log_mgr.log_file);
    }
    
    pthread_mutex_unlock(&log_mgr.mtx);
    
    printf("[LOG] %s: %s -> %s (%s)\n", action, actor, target, success ? "OK" : "FAIL");
}

int srv_init(void) {
    if (pthread_mutex_init(&list.mtx, NULL) != 0) {
        fprintf(stderr, "Mutex init failed\n");
        return -1;
    }
    
    if (pthread_mutex_init(&timer_list.mtx, NULL) != 0) {
        fprintf(stderr, "Timer mutex init failed\n");
        return -1;
    }

    if (pthread_mutex_init(&log_mgr.mtx, NULL) != 0) {
        fprintf(stderr, "Log mutex init failed\n");
        return -1;
    }
    
    init_log_file();

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
    printf("Default password: %s\n", admin_password);
    printf("Log file: server_activity.log\n\n");
    
    log_activity("server", "startup", "system", "Server started", true);
    return 0;
}

void srv_start(void) {
    running = true;
    printf("Server started\n\n");
    
    pthread_t timer_tid;
    pthread_create(&timer_tid, NULL, timer_thread, NULL);
    pthread_detach(timer_tid);

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
        
        char details[128];
        snprintf(details, sizeof(details), "from %s:%d", c->ip, c->port);
        log_activity(c->id, "connect", "server", details, true);

        if (pthread_create(&c->tid, NULL, handle_conn, c) != 0) {
            perror("pthread_create");
            close(csock);
            free(c);
            continue;
        }
        pthread_detach(c->tid);
    }
}

static void* timer_thread(void *arg) {
    (void)arg;
    printf("[TIMER] Background thread started\n");
    
    while (running) {
        time_t now = time(NULL);
        
        pthread_mutex_lock(&timer_list.mtx);
        for (int i = 0; i < timer_list.cnt; i++) {
            if (timer_list.timers[i].active && timer_list.timers[i].execute_time <= now) {
                printf("[TIMER] Executing timer #%d for %s\n", 
                       timer_list.timers[i].timer_id, 
                       timer_list.timers[i].device_id);
                
                char details[128];
                snprintf(details, sizeof(details), "timer#%d: %s", 
                         timer_list.timers[i].timer_id,
                         timer_list.timers[i].state ? "ON" : "OFF");
                log_activity("server", "timer_execute", timer_list.timers[i].device_id, details, true);
                
                execute_timer(&timer_list.timers[i]);
                timer_list.timers[i].active = false;
            }
        }
        pthread_mutex_unlock(&timer_list.mtx);
        
        sleep(5);
    }
    return NULL;
}

static void execute_timer(Timer *t) {
    Message m = {0};
    m.type = MSG_REQUEST;
    strncpy(m.from, "server", sizeof(m.from) - 1);
    strncpy(m.to, t->device_id, sizeof(m.to) - 1);
    m.action = ACT_CONTROL;
    m.timestamp = time(NULL);
    
    struct json_object *data = json_object_new_object();
    json_object_object_add(data, "device_type", json_object_new_string("light"));
    json_object_object_add(data, "state", json_object_new_boolean(t->state));
    json_object_object_add(data, "triggered_by", json_object_new_string("timer"));
    m.data = data;
    
    route_msg(&m);
    
    json_object_put(data);
}

static void* handle_conn(void *arg) {
    Conn *c = (Conn*)arg;
    char buf[BUF_SIZE];

    while (running) {
        int n = recv(c->sock, buf, BUF_SIZE - 1, 0);

        if (n <= 0) {
            printf("[DISCONNECT] %s\n", c->id);
            log_activity(c->id, "disconnect", "server", "Connection closed", true);
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
    strncpy(r->to, c->id, sizeof(r->to) - 1);
    
    if (strcmp(action, "register") == 0) r->action = ACT_REGISTER;
    else if (strcmp(action, "login") == 0) r->action = ACT_LOGIN;
    else if (strcmp(action, "set_timer") == 0) r->action = ACT_SET_TIMER;
    else if (strcmp(action, "list_timers") == 0) r->action = ACT_LIST_TIMERS;
    else if (strcmp(action, "delete_timer") == 0) r->action = ACT_DELETE_TIMER;
    else if (strcmp(action, "get_logs") == 0) r->action = ACT_GET_LOGS;
    else r->action = ACT_CONTROL;
    
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
        c->is_dev = true;
        c->logged_in = true;

        struct json_object *data = (struct json_object*)m->data;
        struct json_object *dev_type;
        if (json_object_object_get_ex(data, "device_type", &dev_type)) {
            strncpy(c->device_type, json_object_get_string(dev_type), 
                    sizeof(c->device_type) - 1);
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

        char details[128];
        snprintf(details, sizeof(details), "type=%s", c->device_type);
        log_activity(c->id, "register", "server", details, true);

        Message *r = calloc(1, sizeof(Message));
        if (r) {
            r->type = MSG_RESPONSE;
            strncpy(r->from, "server", sizeof(r->from) - 1);
            strncpy(r->to, m->from, sizeof(r->to) - 1);
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
            log_activity(m->from, "login", "server", "wrong password", false);
            send_error_response(c, "login", "wrong_password");
            free_msg(m);
            return;
        }

        strncpy(c->id, m->from, sizeof(c->id) - 1);
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

        log_activity(c->id, "login", "server", "authenticated", true);

        Message *r = calloc(1, sizeof(Message));
        if (r) {
            r->type = MSG_RESPONSE;
            strncpy(r->from, "server", sizeof(r->from) - 1);
            strncpy(r->to, m->from, sizeof(r->to) - 1);
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
            log_activity(c->id, "change_password", "server", "not authenticated", false);
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
                json_object_object_add(res, "status", json_object_new_string("success"));
                log_activity(c->id, "change_password", "server", "password changed", true);
                printf("[CHANGE_PASSWORD] Password changed by %s\n", c->id);
                c->logged_in = false;
            } else {
                json_object_object_add(res, "status", json_object_new_string("wrong_password"));
                log_activity(c->id, "change_password", "server", "wrong old password", false);
            }
        } else {
            json_object_object_add(res, "status", json_object_new_string("invalid_request"));
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
            send_error_response(c, "list_devices", "not_authenticated");
            free_msg(m);
            return;
        }
        log_activity(c->id, "list_devices", "server", "", true);
        handle_list_devices(c);
    }
    else if (m->action == ACT_SET_TIMER) {
        if (!c->logged_in) {
            send_error_response(c, "set_timer", "not_authenticated");
            free_msg(m);
            return;
        }
        handle_set_timer(c, m);
    }
    else if (m->action == ACT_LIST_TIMERS) {
        if (!c->logged_in) {
            send_error_response(c, "list_timers", "not_authenticated");
            free_msg(m);
            return;
        }
        handle_list_timers(c, m);
    }
    else if (m->action == ACT_DELETE_TIMER) {
        if (!c->logged_in) {
            send_error_response(c, "delete_timer", "not_authenticated");
            free_msg(m);
            return;
        }
        handle_delete_timer(c, m);
    }
    else if (m->action == ACT_GET_LOGS) {
        if (!c->logged_in) {
            send_error_response(c, "get_logs", "not_authenticated");
            free_msg(m);
            return;
        }
        handle_get_logs(c, m);
    }
    else if (m->action == ACT_HEARTBEAT) {
        printf("[HEARTBEAT] From %s\n", m->from);
    }
    else if (m->action == ACT_CONTROL) {
        if (!c->logged_in && !c->is_dev) {
            send_error_response(c, "control", "not_authenticated");
            free_msg(m);
            return;
        }
        
        struct json_object *data = (struct json_object*)m->data;
        struct json_object *state_obj;
        if (json_object_object_get_ex(data, "state", &state_obj)) {
            bool state = json_object_get_boolean(state_obj);
            char details[128];
            snprintf(details, sizeof(details), "state=%s", state ? "ON" : "OFF");
            log_activity(m->from, "control", m->to, details, true);
        }
        route_msg(m);
    }
    else {
        if (!c->logged_in && !c->is_dev) {
            send_error_response(c, "control", "not_authenticated");
            free_msg(m);
            return;
        }
        route_msg(m);
    }

    free_msg(m);
}

static void handle_set_timer(Conn *c, Message *m) {
    struct json_object *data = (struct json_object*)m->data;
    struct json_object *dev_id, *delay_sec, *st, *lbl;
    
    if (!json_object_object_get_ex(data, "device_id", &dev_id) ||
        !json_object_object_get_ex(data, "delay_seconds", &delay_sec) ||
        !json_object_object_get_ex(data, "state", &st)) {
        send_error_response(c, "set_timer", "missing_fields");
        return;
    }
    
    pthread_mutex_lock(&timer_list.mtx);
    
    if (timer_list.cnt >= MAX_TIMERS) {
        pthread_mutex_unlock(&timer_list.mtx);
        send_error_response(c, "set_timer", "timer_limit_reached");
        return;
    }
    
    Timer *t = &timer_list.timers[timer_list.cnt];
    t->timer_id = timer_list.next_id++;
    strncpy(t->device_id, json_object_get_string(dev_id), sizeof(t->device_id) - 1);
    strncpy(t->owner, c->id, sizeof(t->owner) - 1);
    t->execute_time = time(NULL) + json_object_get_int(delay_sec);
    t->state = json_object_get_boolean(st);
    t->active = true;
    
    if (json_object_object_get_ex(data, "label", &lbl)) {
        strncpy(t->label, json_object_get_string(lbl), sizeof(t->label) - 1);
    } else {
        snprintf(t->label, sizeof(t->label), "Timer #%d", t->timer_id);
    }
    
    timer_list.cnt++;
    
    printf("[SET_TIMER] ID=%d, Device=%s, Delay=%ds, State=%s\n",
           t->timer_id, t->device_id, json_object_get_int(delay_sec),
           t->state ? "ON" : "OFF");
    
    char details[128];
    snprintf(details, sizeof(details), "timer#%d delay=%ds state=%s", 
             t->timer_id, json_object_get_int(delay_sec), t->state ? "ON" : "OFF");
    log_activity(c->id, "set_timer", t->device_id, details, true);
    
    pthread_mutex_unlock(&timer_list.mtx);
    
    Message *r = calloc(1, sizeof(Message));
    r->type = MSG_RESPONSE;
    strncpy(r->from, "server", sizeof(r->from) - 1);
    strncpy(r->to, c->id, sizeof(r->to) - 1);
    r->action = ACT_SET_TIMER;
    r->timestamp = time(NULL);
    
    struct json_object *resp = json_object_new_object();
    json_object_object_add(resp, "status", json_object_new_string("success"));
    json_object_object_add(resp, "timer_id", json_object_new_int(t->timer_id));
    json_object_object_add(resp, "execute_time", json_object_new_int64(t->execute_time));
    r->data = resp;
    
    char *js = create_msg(r);
    send(c->sock, js, strlen(js), 0);
    send(c->sock, "\n", 1, 0);
    free(js);
    free_msg(r);
}

static void handle_list_timers(Conn *c, Message *m) {
    (void)m;
    
    Message *r = calloc(1, sizeof(Message));
    r->type = MSG_RESPONSE;
    strncpy(r->from, "server", sizeof(r->from) - 1);
    strncpy(r->to, c->id, sizeof(r->to) - 1);
    r->action = ACT_LIST_TIMERS;
    r->timestamp = time(NULL);
    
    struct json_object *timers = json_object_new_array();
    
    pthread_mutex_lock(&timer_list.mtx);
    for (int i = 0; i < timer_list.cnt; i++) {
        if (timer_list.timers[i].active) {
            struct json_object *t = json_object_new_object();
            json_object_object_add(t, "timer_id", json_object_new_int(timer_list.timers[i].timer_id));
            json_object_object_add(t, "device_id", json_object_new_string(timer_list.timers[i].device_id));
            json_object_object_add(t, "owner", json_object_new_string(timer_list.timers[i].owner));
            json_object_object_add(t, "execute_time", json_object_new_int64(timer_list.timers[i].execute_time));
            json_object_object_add(t, "state", json_object_new_boolean(timer_list.timers[i].state));
            json_object_object_add(t, "label", json_object_new_string(timer_list.timers[i].label));
            json_object_array_add(timers, t);
        }
    }
    pthread_mutex_unlock(&timer_list.mtx);
    
    struct json_object *resp = json_object_new_object();
    json_object_object_add(resp, "timers", timers);
    r->data = resp;
    
    char *js = create_msg(r);
    send(c->sock, js, strlen(js), 0);
    send(c->sock, "\n", 1, 0);
    
    printf("[LIST_TIMERS] Sent %zu timers to %s\n", json_object_array_length(timers), c->id);
    
    free(js);
    free_msg(r);
}

static void handle_delete_timer(Conn *c, Message *m) {
    struct json_object *data = (struct json_object*)m->data;
    struct json_object *tid;
    
    if (!json_object_object_get_ex(data, "timer_id", &tid)) {
        send_error_response(c, "delete_timer", "missing_timer_id");
        return;
    }
    
    int timer_id = json_object_get_int(tid);
    bool found = false;
    
    pthread_mutex_lock(&timer_list.mtx);
for (int i = 0; i < timer_list.cnt; i++) {
if (timer_list.timers[i].timer_id == timer_id && timer_list.timers[i].active) {
timer_list.timers[i].active = false;
found = true;
printf("[DELETE_TIMER] ID=%d deleted by %s\n", timer_id, c->id);
        char details[128];
        snprintf(details, sizeof(details), "timer#%d", timer_id);
        log_activity(c->id, "delete_timer", timer_list.timers[i].device_id, details, true);
        break;
    }
}
pthread_mutex_unlock(&timer_list.mtx);

Message *r = calloc(1, sizeof(Message));
r->type = MSG_RESPONSE;
strncpy(r->from, "server", sizeof(r->from) - 1);
strncpy(r->to, c->id, sizeof(r->to) - 1);
r->action = ACT_DELETE_TIMER;
r->timestamp = time(NULL);

struct json_object *resp = json_object_new_object();
json_object_object_add(resp, "status", json_object_new_string(found ? "success" : "not_found"));
r->data = resp;

char *js = create_msg(r);
send(c->sock, js, strlen(js), 0);
send(c->sock, "\n", 1, 0);
free(js);
free_msg(r);
}
static void handle_get_logs(Conn *c, Message *m) {
(void)m;
log_activity(c->id, "get_logs", "server", "", true);

Message *r = calloc(1, sizeof(Message));
r->type = MSG_RESPONSE;
strncpy(r->from, "server", sizeof(r->from) - 1);
strncpy(r->to, c->id, sizeof(r->to) - 1);
r->action = ACT_GET_LOGS;
r->timestamp = time(NULL);

struct json_object *logs = json_object_new_array();

pthread_mutex_lock(&log_mgr.mtx);
for (int i = 0; i < log_mgr.cnt; i++) {
    struct json_object *log = json_object_new_object();
    json_object_object_add(log, "log_id", json_object_new_int(log_mgr.logs[i].log_id));
    json_object_object_add(log, "timestamp", json_object_new_int64(log_mgr.logs[i].timestamp));
    json_object_object_add(log, "actor", json_object_new_string(log_mgr.logs[i].actor));
    json_object_object_add(log, "action", json_object_new_string(log_mgr.logs[i].action));
    json_object_object_add(log, "target", json_object_new_string(log_mgr.logs[i].target));
    json_object_object_add(log, "details", json_object_new_string(log_mgr.logs[i].details));
    json_object_object_add(log, "success", json_object_new_boolean(log_mgr.logs[i].success));
    json_object_array_add(logs, log);
}
pthread_mutex_unlock(&log_mgr.mtx);

struct json_object *resp = json_object_new_object();
json_object_object_add(resp, "logs", logs);
r->data = resp;

char *js = create_msg(r);
send(c->sock, js, strlen(js), 0);
send(c->sock, "\n", 1, 0);

printf("[GET_LOGS] Sent %zu logs to %s\n", json_object_array_length(logs), c->id);

free(js);
free_msg(r);
}
static void handle_list_devices(Conn *c) {
Message *r = calloc(1, sizeof(Message));
if (!r) return;
r->type = MSG_RESPONSE;
strncpy(r->from, "server", sizeof(r->from) - 1);
strncpy(r->to, c->id, sizeof(r->to) - 1);
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
log_activity("server", "shutdown", "system", "Server stopping", true);

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
pthread_mutex_destroy(&timer_list.mtx);
pthread_mutex_destroy(&log_mgr.mtx);

close_log_file();

printf("Server stopped\n");
}
