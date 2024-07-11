#include "rdma.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <netdb.h>
#include <arpa/inet.h>

#define NUM_XLOGS 10
#define XLOG_SIZE 256

int main(int argc, char *argv[]) {
    if (argc != 3) {
        fprintf(stderr, "Usage: %s <logstore_server> <port>\n", argv[0]);
        fprintf(stderr, "Example: %s db3.cs.purdue.edu 5555\n", argv[0]);
        return 1;
    }

    const char *logstore_server = argv[1];
    int port = atoi(argv[2]);

    // Resolve the hostname to an IP address
    struct hostent *he = gethostbyname(logstore_server);
    if (he == NULL) {
        fprintf(stderr, "Could not resolve hostname: %s\n", logstore_server);
        return 1;
    }
    char ip_address[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, he->h_addr, ip_address, INET_ADDRSTRLEN);

    printf("Connecting to LogStore at %s (%s):%d\n", logstore_server, ip_address, port);

    struct rdma_context ctx;
    printf("Creating RDMA context...\n");
    create_rdma_context(&ctx);
    
    printf("Connecting RDMA...\n");
    connect_rdma(&ctx, ip_address, port);

    printf("RDMA connection established.\n");

    uint64_t local_lsn = 0;
    uint64_t flush_lsn = 0;

    for (int i = 0; i < NUM_XLOGS; i++) {
        char xlog[XLOG_SIZE];
        snprintf(xlog, XLOG_SIZE, "Xlog-%d", i);
        local_lsn++;

        printf("Preparing Xlog: %s (LSN: %lu)\n", xlog, local_lsn);

        // Append Xlog to Memory (simulated by writing to the RDMA buffer)
        memcpy(ctx.buffer, xlog, strlen(xlog) + 1);

        // RDMA Write to flush Xlog to LogStore
        printf("Performing RDMA Write...\n");
        post_rdma_write(&ctx, ctx.buffer, strlen(xlog) + 1, i * XLOG_SIZE, ctx.remote_mr.rkey);
        poll_completion(&ctx);

        printf("Sent Xlog: %s (LSN: %lu)\n", xlog, local_lsn);

        // Asynchronously check flush-LSN
        printf("Checking Flush-LSN...\n");
        post_rdma_read(&ctx, &flush_lsn, sizeof(flush_lsn), ctx.remote_mr.addr, ctx.remote_mr.rkey);
        poll_completion(&ctx);

        if (flush_lsn >= local_lsn) {
            printf("Commit successful: Flush-LSN (%lu) >= Local LSN (%lu)\n", flush_lsn, local_lsn);
        } else {
            printf("Waiting for commit: Flush-LSN (%lu) < Local LSN (%lu)\n", flush_lsn, local_lsn);
        }

        usleep(100000); // Sleep for 100ms between sends
    }

    printf("All Xlogs sent. Cleaning up...\n");
    destroy_rdma_context(&ctx);

    printf("Done.\n");
    return 0;
}