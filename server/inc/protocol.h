#ifndef PROTOCOL_H
#define PROTOCOL_H

#include <stdint.h>
#include <json-c/json.h>

typedef enum {
    MSG_REQUEST,
    MSG_RESPONSE,
    MSG_NOTIFY
} MsgType;

typedef enum {
    ACT_REGISTER,
    ACT_LOGIN,
    ACT_CONTROL,
    ACT_STATUS,
    ACT_HEARTBEAT,
    ACT_LIST_DEVICES,
    ACT_CHANGE_PASSWORD
} Action;

typedef struct {
    MsgType type;
    char from[32];
    char to[32];
    Action action;
    uint64_t timestamp;
    void *data;
} Message;

Message* parse_msg(const char *json);
char* create_msg(Message *m);
void free_msg(Message *m);
const char* type_str(MsgType t);
const char* action_str(Action a);

#endif
