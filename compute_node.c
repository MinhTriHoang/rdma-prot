#include "rdma.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define NUM_XLOGS 10
#define XLOG_SIZE 256

int main(int argc, char *argv[]) {
    if (argc != 3) {
        fprintf(stderr, "Usage: %s <logstore_server> <port>\n", argv[0]);
        return 1;
    }

    const char *logstore_server = argv[1];
    int port = atoi(argv[2]);

    struct rdma_context ctx;
    create_rdma_context(&ctx);
    connect_rdma(&ctx, logstore_server, port);

    char xlogs[NUM_XLOGS][XLOG_SIZE];
    for (int i = 0; i < NUM_XLOGS; i++) {
        snprintf(xlogs[i], XLOG_SIZE, "Xlog-%d", i);
    }

    for (int i = 0; i < NUM_XLOGS; i++) {
        memcpy(ctx.buffer, xlogs[i], strlen(xlogs[i]) + 1);
        post_rdma_write(&ctx, ctx.buffer, strlen(xlogs[i]) + 1, 0, ctx.mr->rkey);
        poll_completion(&ctx);
        printf("Sent Xlog: %s\n", xlogs[i]);
    }

    destroy_rdma_context(&ctx);

    return 0;
}