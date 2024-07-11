#include "rdma.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>

#define NUM_XLOGS 10
#define XLOG_SIZE 256

struct logstore_context {
    struct rdma_context rdma_ctx;
    uint64_t flush_lsn;
    pthread_mutex_t flush_lsn_mutex;
};

void *background_flush(void *arg) {
    struct logstore_context *ctx = (struct logstore_context *)arg;

    while (1) {
        sleep(5); // Simulate periodic flushing

        pthread_mutex_lock(&ctx->flush_lsn_mutex);
        ctx->flush_lsn++;
        printf("Flushed LSN %lu to disk\n", ctx->flush_lsn);
        pthread_mutex_unlock(&ctx->flush_lsn_mutex);
    }

    return NULL;
}

int main(int argc, char *argv[]) {
    if (argc != 2) {
        fprintf(stderr, "Usage: %s <port>\n", argv[0]);
        return 1;
    }

    int port = atoi(argv[1]);
    printf("LogStore starting on port %d\n", port);

    struct logstore_context ctx;
    ctx.flush_lsn = 0;
    pthread_mutex_init(&ctx.flush_lsn_mutex, NULL);

    printf("Creating RDMA context...\n");
    create_rdma_context(&ctx.rdma_ctx);
    
    printf("Listening for RDMA connection...\n");
    listen_rdma(&ctx.rdma_ctx, port);

    printf("RDMA connection established.\n");

    pthread_t flush_thread;
    pthread_create(&flush_thread, NULL, background_flush, &ctx);

    printf("LogStore is ready to receive Xlogs.\n");

    for (int i = 0; i < NUM_XLOGS; i++) {
        printf("Waiting for Xlog %d...\n", i+1);
        poll_completion(&ctx.rdma_ctx);
        printf("Received Xlog: %s\n", (char *)ctx.rdma_ctx.buffer + (i * XLOG_SIZE));

        pthread_mutex_lock(&ctx.flush_lsn_mutex);
        ctx.flush_lsn++;
        pthread_mutex_unlock(&ctx.flush_lsn_mutex);
    }

    printf("LogStore has received all Xlogs.\n");

    // Wait for the background flush thread to finish (it won't in this case)
    pthread_join(flush_thread, NULL);

    destroy_rdma_context(&ctx.rdma_ctx);
    pthread_mutex_destroy(&ctx.flush_lsn_mutex);

    return 0;
}