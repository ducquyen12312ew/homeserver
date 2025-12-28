#ifndef MESSAGE_BUILDER_H
#define MESSAGE_BUILDER_H
#include <json-c/json.h>
#include <stdbool.h>
typedef struct { 
	struct json_object *root; 
} MessageBuilder;
MessageBuilder* msg_builder_create(
	const char *type, 
	const char *from, 
	const char *to, 
	const char *action
);
void msg_builder_add_string(MessageBuilder *mb, const char *key, const char *value);
void msg_builder_add_int(MessageBuilder *mb, const char *key, int value);
void msg_builder_add_bool(MessageBuilder *mb, const char *key, bool value);
char* msg_builder_build(MessageBuilder *mb);
void msg_builder_free(MessageBuilder *mb);
typedef struct { bool success; char error_msg[256]; struct json_object *data; } ResponseParser;
ResponseParser* response_parse(const char *json_str);
bool response_is_success(ResponseParser *rp);
const char* response_get_string(ResponseParser *rp, const char *key);
int response_get_int(ResponseParser *rp, const char *key);
void response_free(ResponseParser *rp);
#endif
