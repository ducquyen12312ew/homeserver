#define _POSIX_C_SOURCE 200809L
#include "message_builder.h"
#include <stdlib.h>
#include <string.h>
#include <time.h>
MessageBuilder* msg_builder_create(const char *type, const char *from, const char *to, const char *action) {
    MessageBuilder *mb = malloc(sizeof(MessageBuilder));
    if (!mb) return NULL;
    mb->root = json_object_new_object();
    if (!mb->root) { free(mb); return NULL; }
    json_object_object_add(mb->root, "type", json_object_new_string(type));
    json_object_object_add(mb->root, "from", json_object_new_string(from));
    json_object_object_add(mb->root, "to", json_object_new_string(to));
    json_object_object_add(mb->root, "action", json_object_new_string(action));
    json_object_object_add(mb->root, "timestamp", json_object_new_int64((int64_t)time(NULL)));
    json_object_object_add(mb->root, "data", json_object_new_object());
    return mb;
}
void msg_builder_add_string(MessageBuilder *mb, const char *key, const char *value) {
    if (!mb || !mb->root) return;
    struct json_object *data;
    if (json_object_object_get_ex(mb->root, "data", &data))
        json_object_object_add(data, key, json_object_new_string(value));
}
void msg_builder_add_int(MessageBuilder *mb, const char *key, int value) {
    if (!mb || !mb->root) return;
    struct json_object *data;
    if (json_object_object_get_ex(mb->root, "data", &data))
        json_object_object_add(data, key, json_object_new_int(value));
}
void msg_builder_add_bool(MessageBuilder *mb, const char *key, bool value) {
    if (!mb || !mb->root) return;
    struct json_object *data;
    if (json_object_object_get_ex(mb->root, "data", &data))
        json_object_object_add(data, key, json_object_new_boolean(value));
}
char* msg_builder_build(MessageBuilder *mb) {
    if (!mb || !mb->root) return NULL;
    return strdup(json_object_to_json_string(mb->root));
}
void msg_builder_free(MessageBuilder *mb) {
    if (mb) {
        if (mb->root) json_object_put(mb->root);
        free(mb);
    }
}
ResponseParser* response_parse(const char *json_str) {
    if (!json_str) return NULL;
    ResponseParser *rp = calloc(1, sizeof(ResponseParser));
    if (!rp) return NULL;
    struct json_object *root = json_tokener_parse(json_str);
    if (!root) { strcpy(rp->error_msg, "Invalid JSON"); return rp; }
    struct json_object *data;
    if (json_object_object_get_ex(root, "data", &data)) {
        rp->data = json_object_get(data);
        struct json_object *status;
        if (json_object_object_get_ex(data, "status", &status)) {
            if (strcmp(json_object_get_string(status), "success") == 0) rp->success = true;
            else { rp->success = false; strncpy(rp->error_msg, json_object_get_string(status), 255); }
        } else rp->success = true;
    } else { rp->success = false; strcpy(rp->error_msg, "No data field"); }
    json_object_put(root);
    return rp;
}
bool response_is_success(ResponseParser *rp) { return rp && rp->success; }
const char* response_get_string(ResponseParser *rp, const char *key) {
    if (!rp || !rp->data) return NULL;
    struct json_object *field;
    if (json_object_object_get_ex(rp->data, key, &field)) return json_object_get_string(field);
    return NULL;
}
int response_get_int(ResponseParser *rp, const char *key) {
    if (!rp || !rp->data) return 0;
    struct json_object *field;
    if (json_object_object_get_ex(rp->data, key, &field)) return json_object_get_int(field);
    return 0;
}
void response_free(ResponseParser *rp) {
    if (rp) {
        if (rp->data) json_object_put(rp->data);
        free(rp);
    }
}
