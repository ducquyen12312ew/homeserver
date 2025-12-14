#ifndef NETWORK_HELPER_H
#define NETWORK_HELPER_H
#include "message_builder.h"
typedef struct { int sock; char client_id[32]; } NetContext;
NetContext* net_context_create(const char *client_id);
int net_connect(NetContext *ctx, const char *server_ip, int port);
char* net_send_receive(NetContext *ctx, MessageBuilder *mb);
void net_context_free(NetContext *ctx);
#endif

//{
  //"type": "response",
  //"from": "server",
  //"to": "client",
  //"action": "control",
  //"data": {
   // "status": "success",
   // "state": "on",
  //  "power": 10
 // }
//}

//{
  //"type": "request",
 // "from": "gtk_client",
 // "to": "light_001",      
 // "action": "control",
 // "timestamp": 1234567890,
 // "data": {}
}
