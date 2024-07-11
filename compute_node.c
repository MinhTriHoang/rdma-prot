#include "rdma.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

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

    uint64_t local_lsn = 0;
    uint64_t flush_lsn = 0;

    for (int i = 0; i < NUM_XLOGS; i++) {
        char xlog[XLOG_SIZE];
        snprintf(xlog, XLOG_SIZE, "Xlog-%d", i);
        local_lsn++;

        // Append Xlog to Memory (simulated by writing to the RDMA buffer)
        memcpy(ctx.buffer, xlog, strlen(xlog) + 1);

        // RDMA Write to flush Xlog to LogStore
        post_rdma_write(&ctx, ctx.buffer, strlen(xlog) + 1, i * XLOG_SIZE, ctx.remote_mr.rkey);
        poll_completion(&ctx);

        printf("Sent Xlog: %s (LSN: %lu)\n", xlog, local_lsn);

        // Asynchronously check flush-LSN
        post_rdma_read(&ctx, &flush_lsn, sizeof(flush_lsn), ctx.remote_mr.addr, ctx.remote_mr.rkey);
        poll_completion(&ctx);

        if (flush_lsn >= local_lsn) {
            printf("Commit successful: Flush-LSN (%lu) >= Local LSN (%lu)\n", flush_lsn, local_lsn);
        }

        usleep(100000); // Sleep for 100ms between sends
    }

    destroy_rdma_context(&ctx);

    return 0;
}