#ifndef PROTOCOL_H
#define PROTOCOL_H

#include <stdint.h>
#include <stdbool.h>

typedef enum {
    MSG_REQUEST = 0,
    MSG_RESPONSE = 1,
    MSG_NOTIFY = 2
} MsgType;

typedef enum {
    ACT_REGISTER = 0,
    ACT_LOGIN = 1,
    ACT_CONTROL = 2,
    ACT_STATUS = 3,
    ACT_HEARTBEAT = 4
} Action;

typedef enum {
    DEV_LIGHT = 0,
    DEV_FAN = 1,
    DEV_AC = 2
} DevType;

typedef struct {
    MsgType type;
    char from[32];
    char to[32];
    Action action;
    uint64_t timestamp;
    void *data;
} Message;

Message* parse_msg(const char *json);
char* create_msg(Message *msg);
void free_msg(Message *msg);
const char* action_str(Action a);
const char* type_str(MsgType t);

#endif
