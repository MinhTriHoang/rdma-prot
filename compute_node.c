#include "rdma.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <sys/socket.h>
#include <netinet/in.h>


#define NUM_XLOGS 10
#define XLOG_SIZE 256

// Function to perform a simple RDMA read test
int test_rdma_read(struct resources *res) {
    char test_data[64] = "Test RDMA Read Data";
    memcpy(res->buf, test_data, strlen(test_data) + 1);

    fprintf(stdout, "Performing RDMA Write test\n");
    fprintf(stdout, "Local buffer content: %s\n", res->buf);
    fprintf(stdout, "Remote buffer address: 0x%lx\n", (unsigned long)res->remote_props.addr);
    fprintf(stdout, "Remote rkey: 0x%x\n", res->remote_props.rkey);

    if (post_send(res, IBV_WR_RDMA_WRITE) != 0 || poll_completion(res) != 0) {
        fprintf(stderr, "Failed to perform RDMA Write for test\n");
        return -1;
    }

    // Clear local buffer
    memset(res->buf, 0, XLOG_SIZE);

    fprintf(stdout, "Performing RDMA Read test\n");
    if (post_send(res, IBV_WR_RDMA_READ) != 0 || poll_completion(res) != 0) {
        fprintf(stderr, "Failed to perform RDMA Read for test\n");
        return -1;
    }

    fprintf(stdout, "RDMA Read result: %s\n", res->buf);

    // Verify the data
    if (strcmp(res->buf, test_data) != 0) {
        fprintf(stderr, "RDMA Read test failed: data mismatch\n");
        return -1;
    }

    printf("RDMA Read test passed successfully\n");
    return 0;
}

int main(int argc, char *argv[]) {
    struct resources res;
    struct sockaddr_in addr;
    

    if (argc != 3) {
        fprintf(stderr, "Usage: %s <logstore_ip> <port>\n", argv[0]);
        fprintf(stderr, "Example: %s 192.168.100.2 5555\n", argv[0]);
        return 1;
    }

    config.tcp_port = atoi(argv[2]);

    // Set up the IP address
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(config.tcp_port);
    if (inet_pton(AF_INET, argv[1], &addr.sin_addr) <= 0) {
        fprintf(stderr, "Invalid IP address\n");
        return 1;
    }

    printf("Connecting to LogStore at %s:%d\n", argv[1], config.tcp_port);

    resources_init(&res);
    if (resources_create(&res) != 0) {
        fprintf(stderr, "Failed to create RDMA resources\n");
        return 1;
    }

    res.sock = socket(AF_INET, SOCK_STREAM, 0);
    if (res.sock < 0) {
        fprintf(stderr, "Failed to create socket\n");
        return 1;
    }

    if (connect(res.sock, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        fprintf(stderr, "Failed to connect to server\n");
        return 1;
    }

    if (connect_qp(&res) != 0) {
        fprintf(stderr, "Failed to connect QPs\n");
        return 1;
    }

    printf("RDMA connection established.\n");
    

    int retry_count = 3;
    while (retry_count > 0) {
        if (test_rdma_read(&res) == 0) {
            break;
        }
        fprintf(stderr, "RDMA Read test failed, retrying... (%d attempts left)\n", retry_count - 1);
        retry_count--;
        sleep(1);  // Wait a second before retrying
    }

    if (retry_count == 0) {
        fprintf(stderr, "RDMA Read test failed after multiple attempts\n");
        resources_destroy(&res);
        return 1;
    }

    // Perform RDMA Read test
    if (test_rdma_read(&res) != 0) {
        fprintf(stderr, "RDMA Read test failed\n");
        resources_destroy(&res);
        return 1;
    }

    // Prepare and send xlogs
    for (int i = 0; i < NUM_XLOGS; i++) {
        char xlog[XLOG_SIZE];
        snprintf(xlog, XLOG_SIZE, "Xlog-%d", i);

        printf("Sending Xlog: %s\n", xlog);

        // Copy xlog to RDMA buffer
        memcpy(res.buf, xlog, strlen(xlog) + 1);

        // Perform RDMA Write
        if (post_send(&res, IBV_WR_RDMA_WRITE) != 0) {
            fprintf(stderr, "Failed to post RDMA Write for Xlog: %s\n", xlog);
            continue;
        }

        if (poll_completion(&res) != 0) {
            fprintf(stderr, "Error in RDMA Write for Xlog: %s\n", xlog);
            continue;
        }

        printf("Xlog sent successfully: %s\n", xlog);

        // Sleep for a short time between sends
        usleep(10000);  // 100ms
    }
    
    printf("All Xlogs sent. Cleaning up...\n");

    if (resources_destroy(&res) != 0) {
        fprintf(stderr, "Failed to destroy resources\n");
        return 1;
    }


    printf("Resources destroyed. Exiting.\n");
    return 0;
}