#include "protocol.h"
#include <json-c/json.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

const char* type_str(MsgType t) {
    switch(t) {
        case MSG_REQUEST: return "request";
        case MSG_RESPONSE: return "response";
        case MSG_NOTIFY: return "notify";
        default: return "unknown";
    }
}

const char* action_str(Action a) {
    switch(a) {
        case ACT_REGISTER: return "register";
        case ACT_LOGIN: return "login";
        case ACT_CONTROL: return "control";
        case ACT_STATUS: return "status";
        case ACT_HEARTBEAT: return "heartbeat";
        case ACT_LIST_DEVICES: return "list_devices";
        case ACT_CHANGE_PASSWORD: return "change_password";
        default: return "unknown";
    }
}

static MsgType str_to_type(const char *s) {
    if (strcmp(s, "request") == 0) return MSG_REQUEST;
    if (strcmp(s, "response") == 0) return MSG_RESPONSE;
    return MSG_NOTIFY;
}

static Action str_to_action(const char *s) {
    if (strcmp(s, "register") == 0) return ACT_REGISTER;
    if (strcmp(s, "login") == 0) return ACT_LOGIN;
    if (strcmp(s, "control") == 0) return ACT_CONTROL;
    if (strcmp(s, "status") == 0) return ACT_STATUS;
    if (strcmp(s, "heartbeat") == 0) return ACT_HEARTBEAT;
    if (strcmp(s, "list_devices") == 0) return ACT_LIST_DEVICES;
    if (strcmp(s, "change_password") == 0) return ACT_CHANGE_PASSWORD;
    return ACT_REGISTER;
}

Message* parse_msg(const char *json) {
    if (!json) return NULL;
    
    struct json_object *root = json_tokener_parse(json);
    if (!root) return NULL;
    
    Message *m = calloc(1, sizeof(Message));
    
    struct json_object *type;
    if (json_object_object_get_ex(root, "type", &type)) {
        m->type = str_to_type(json_object_get_string(type));
    }
    
    struct json_object *from;
    if (json_object_object_get_ex(root, "from", &from)) {
        strncpy(m->from, json_object_get_string(from), 31);
    }
    
    struct json_object *to;
    if (json_object_object_get_ex(root, "to", &to)) {
        strncpy(m->to, json_object_get_string(to), 31);
    }
    
    struct json_object *act;
    if (json_object_object_get_ex(root, "action", &act)) {
        m->action = str_to_action(json_object_get_string(act));
    }
    
    struct json_object *ts;
    if (json_object_object_get_ex(root, "timestamp", &ts)) {
        m->timestamp = (uint64_t)json_object_get_int64(ts);
    } else {
        m->timestamp = (uint64_t)time(NULL);
    }
    
    struct json_object *data;
    if (json_object_object_get_ex(root, "data", &data)) {
        m->data = json_object_get(data);
    }
    
    json_object_put(root);
    return m;
}

char* create_msg(Message *m) {
    if (!m) return NULL;
    
    struct json_object *root = json_object_new_object();
    
    json_object_object_add(root, "type", json_object_new_string(type_str(m->type)));
    json_object_object_add(root, "from", json_object_new_string(m->from));
    json_object_object_add(root, "to", json_object_new_string(m->to));
    json_object_object_add(root, "action", json_object_new_string(action_str(m->action)));
    json_object_object_add(root, "timestamp", json_object_new_int64(m->timestamp));
    
    if (m->data) {
        json_object_object_add(root, "data", json_object_get((struct json_object*)m->data));
    } else {
        json_object_object_add(root, "data", json_object_new_object());
    }
    
    const char *json_str = json_object_to_json_string_ext(root, JSON_C_TO_STRING_PLAIN);
    char *result = strdup(json_str);
    
    json_object_put(root);
    return result;
}

void free_msg(Message *m) {
    if (m) {
        if (m->data) json_object_put((struct json_object*)m->data);
        free(m);
    }
}

