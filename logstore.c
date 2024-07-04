#include "rdma.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>

int main(int argc, char *argv[]) {
    if (argc != 2) {
        fprintf(stderr, "Usage: %s <port>\n", argv[0]);
        return 1;
    }

    int port = atoi(argv[1]);

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    addr.sin_addr.s_addr = INADDR_ANY;

    int sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0) {
        die("Failed to create socket");
    }

    if (bind(sockfd, (struct sockaddr *)&addr, sizeof(addr))) {
        die("Failed to bind socket");
    }

    if (listen(sockfd, 1)) {
        die("Failed to listen on socket");
    }

    printf("LogStore is ready to receive Xlogs.\n");

    int connfd = accept(sockfd, NULL, NULL);
    if (connfd < 0) {
        die("Failed to accept connection");
    }

    struct rdma_context ctx;
    create_rdma_context(&ctx);

    for (int i = 0; i < 10; i++) {
        poll_completion(&ctx);
        printf("Received Xlog: %s\n", (char *)ctx.buffer);
    }

    printf("LogStore has received all Xlogs.\n");

    destroy_rdma_context(&ctx);

    close(connfd);
    close(sockfd);

    return 0;
}