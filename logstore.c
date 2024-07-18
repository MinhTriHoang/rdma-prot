#include "rdma.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#define NUM_XLOGS 10
#define XLOG_SIZE 256

int main(int argc, char *argv[]) {
    if (argc != 2) {
        fprintf(stderr, "Usage: %s <port>\n", argv[0]);
        return 1;
    }

    int port = atoi(argv[1]);
    
    printf("LogStore starting on port %d\n", port);

    struct resources res;
    struct sockaddr_in addr;
    resources_init(&res);

    config.tcp_port = port;
    

    if (resources_create(&res) != 0) {
        fprintf(stderr, "Failed to create RDMA resources\n");
        return 1;
    }

    printf("Waiting for RDMA connection...\n");

    int sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0) {
        fprintf(stderr, "Failed to create socket\n");
        return 1;
    }

    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(port);

    if (bind(sockfd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        fprintf(stderr, "Failed to bind to port\n");
        return 1;
    }

    if (listen(sockfd, 1) < 0) {
        fprintf(stderr, "Failed to listen on socket\n");
        return 1;
    }

    res.sock = accept(sockfd, NULL, NULL);
    if (res.sock < 0) {
        fprintf(stderr, "Failed to accept connection\n");
        return 1;
    }

    if (connect_qp(&res) != 0) {
        fprintf(stderr, "Failed to connect QPs\n");
        return 1;
    }

    printf("RDMA connection established. Waiting for Xlogs...\n");

    // Receive xlogs
    // Replace the existing loop with this
    for (int i = 0; i < NUM_XLOGS; i++) {
        if (post_receive(&res) != 0) {
            fprintf(stderr, "Failed to post receive for Xlog %d\n", i);
            // Don't break, try to post the remaining receives
        } else {
            fprintf(stdout, "Posted receive for Xlog %d\n", i);
        }
    }

        printf("RDMA connection established. Waiting for Xlogs...\n");

    for (int i = 0; i < NUM_XLOGS; i++) {
        if (poll_completion(&res) != 0) {
            fprintf(stderr, "Error receiving Xlog %d\n", i);
        } else {
            fprintf(stdout, "Received Xlog %d: %s\n", i, res.buf);
        }
    }
    
    printf("All Xlogs received successfully.\n");



    resources_destroy(&res);

    return 0;
}