#define _POSIX_C_SOURCE 200809L
#include "network_helper.h"
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
NetContext* net_context_create(const char *client_id) {
    NetContext *ctx = calloc(1, sizeof(NetContext));
    if (!ctx) return NULL;
    ctx->sock = -1;
    strncpy(ctx->client_id, client_id ? client_id : "client", 31);
    return ctx;
}
int net_connect(NetContext *ctx, const char *server_ip, int port) {
    if (!ctx) return -1;
    if (ctx->sock >= 0) close(ctx->sock);
    ctx->sock = socket(AF_INET, SOCK_STREAM, 0);
    if (ctx->sock < 0) return -1;
    struct sockaddr_in addr;
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    addr.sin_addr.s_addr = inet_addr(server_ip);
    if (connect(ctx->sock, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        close(ctx->sock); ctx->sock = -1; return -1;
    }
    return 0;
}
char* net_send_receive(NetContext *ctx, MessageBuilder *mb) {
    if (!ctx || ctx->sock < 0 || !mb) return NULL;
    char *msg_str = msg_builder_build(mb);
    if (!msg_str) return NULL;
    send(ctx->sock, msg_str, strlen(msg_str), 0);
    send(ctx->sock, "\n", 1, 0);
    free(msg_str);
    char buffer[4096] = {0};
    int n = recv(ctx->sock, buffer, 4095, 0);
    if (n <= 0) return NULL;
    return strdup(buffer);
}
void net_context_free(NetContext *ctx) {
    if (ctx) {
        if (ctx->sock >= 0) close(ctx->sock);
        free(ctx);
    }
}
