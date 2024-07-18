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

struct xlog_entry {
    uint32_t flag;
    char data[XLOG_SIZE];
};

int main(int argc, char *argv[]) {
    struct resources res;
    struct sockaddr_in addr;

    if (argc != 3) {
        fprintf(stderr, "Usage: %s <logstore_ip> <port>\n", argv[0]);
        fprintf(stderr, "Example: %s 192.168.100.2 5555\n", argv[0]);
        return 1;
    }

    config.tcp_port = atoi(argv[2]);

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

    struct xlog_entry *xlog_buffer = (struct xlog_entry *)res.buf;

    for (int i = 0; i < NUM_XLOGS; i++) {
        snprintf(xlog_buffer[i].data, XLOG_SIZE, "Xlog-%d", i);
        xlog_buffer[i].flag = 1;

        printf("Sending Xlog: %s\n", xlog_buffer[i].data);

        if (rdma_write(&res, i * sizeof(struct xlog_entry), sizeof(struct xlog_entry)) != 0) {
            fprintf(stderr, "Failed to perform RDMA Write for Xlog: %s\n", xlog_buffer[i].data);
            continue;
        }

        printf("Xlog sent successfully: %s\n", xlog_buffer[i].data);

        usleep(10000);  // 10ms between sends
    }

    printf("All Xlogs sent. Cleaning up...\n");

    if (resources_destroy(&res) != 0) {
        fprintf(stderr, "Failed to destroy resources\n");
        return 1;
    }

    printf("Resources destroyed. Exiting.\n");
    return 0;
}