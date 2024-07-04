#include "rdma.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <netdb.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

void die(const char *message) {
    perror(message);
    exit(1);
}

void create_rdma_context(struct rdma_context *ctx) {
    ctx->ctx = ibv_open_device(ibv_get_device_list(NULL)[0]);
    if (!ctx->ctx) {
        die("Failed to open RDMA device");
    }

    ctx->pd = ibv_alloc_pd(ctx->ctx);
    if (!ctx->pd) {
        die("Failed to allocate protection domain");
    }

    ctx->buffer_size = RDMA_BUFFER_SIZE;
    ctx->buffer = malloc(ctx->buffer_size);
    if (!ctx->buffer) {
        die("Failed to allocate buffer");
    }

    ctx->mr = ibv_reg_mr(ctx->pd, ctx->buffer, ctx->buffer_size,
                         IBV_ACCESS_LOCAL_WRITE | IBV_ACCESS_REMOTE_READ | IBV_ACCESS_REMOTE_WRITE | IBV_ACCESS_REMOTE_ATOMIC);
    if (!ctx->mr) {
        die("Failed to register memory region");
    }

    ctx->cq = ibv_create_cq(ctx->ctx, 1, NULL, NULL, 0);
    if (!ctx->cq) {
        die("Failed to create completion queue");
    }

    struct ibv_qp_init_attr qp_init_attr = {
        .send_cq = ctx->cq,
        .recv_cq = ctx->cq,
        .qp_type = IBV_QPT_RC,
        .cap = {
            .max_send_wr = 1,
            .max_recv_wr = 1,
            .max_send_sge = 1,
            .max_recv_sge = 1,
        },
    };

    ctx->qp = ibv_create_qp(ctx->pd, &qp_init_attr);
    if (!ctx->qp) {
        die("Failed to create queue pair");
    }
}

void destroy_rdma_context(struct rdma_context *ctx) {
    ibv_destroy_qp(ctx->qp);
    ibv_destroy_cq(ctx->cq);
    ibv_dereg_mr(ctx->mr);
    free(ctx->buffer);
    ibv_dealloc_pd(ctx->pd);
    ibv_close_device(ctx->ctx);
}

void post_rdma_write(struct rdma_context *ctx, void *local_addr, uint32_t length, uint64_t remote_addr, uint32_t rkey) {
    struct ibv_send_wr wr = {
        .wr_id = 0,
        .opcode = IBV_WR_RDMA_WRITE,
        .send_flags = IBV_SEND_SIGNALED,
        .wr.rdma.remote_addr = remote_addr,
        .wr.rdma.rkey = rkey,
    };

    struct ibv_sge sge = {
        .addr = (uintptr_t)local_addr,
        .length = length,
        .lkey = ctx->mr->lkey,
    };

    wr.sg_list = &sge;
    wr.num_sge = 1;

    struct ibv_send_wr *bad_wr;
    if (ibv_post_send(ctx->qp, &wr, &bad_wr)) {
        die("Failed to post RDMA write");
    }
}

void post_rdma_read(struct rdma_context *ctx, void *local_addr, uint32_t length, uint64_t remote_addr, uint32_t rkey) {
    struct ibv_send_wr wr = {
        .wr_id = 0,
        .opcode = IBV_WR_RDMA_READ,
        .send_flags = IBV_SEND_SIGNALED,
        .wr.rdma.remote_addr = remote_addr,
        .wr.rdma.rkey = rkey,
    };

    struct ibv_sge sge = {
        .addr = (uintptr_t)local_addr,
        .length = length,
        .lkey = ctx->mr->lkey,
    };

    wr.sg_list = &sge;
    wr.num_sge = 1;

    struct ibv_send_wr *bad_wr;
    if (ibv_post_send(ctx->qp, &wr, &bad_wr)) {
        die("Failed to post RDMA read");
    }
}

void post_rdma_atomic(struct rdma_context *ctx, uint64_t remote_addr, uint32_t rkey, uint64_t compare, uint64_t swap) {
    struct ibv_send_wr wr = {
        .wr_id = 0,
        .opcode = IBV_WR_ATOMIC_CMP_AND_SWP,
        .send_flags = IBV_SEND_SIGNALED,
        .wr.atomic.remote_addr = remote_addr,
        .wr.atomic.rkey = rkey,
        .wr.atomic.compare_add = compare,
        .wr.atomic.swap = swap,
    };

    struct ibv_send_wr *bad_wr;
    if (ibv_post_send(ctx->qp, &wr, &bad_wr)) {
        die("Failed to post RDMA atomic");
    }
}

void poll_completion(struct rdma_context *ctx) {
    struct ibv_wc wc;
    int ret = ibv_poll_cq(ctx->cq, 1, &wc);
    if (ret < 0) {
        die("Failed to poll completion queue");
    } else if (ret == 0) {
        die("Unexpected completion");
    }

    if (wc.status != IBV_WC_SUCCESS) {
        die("Failed RDMA operation");
    }
}

// Add this function to rdma.c
void connect_rdma(struct rdma_context *ctx, const char *server_name, int port) {
    struct addrinfo *addr;
    struct addrinfo hints = {
        .ai_family = AF_INET,
        .ai_socktype = SOCK_STREAM
    };

    char service[16];
    snprintf(service, sizeof(service), "%d", port);

    if (getaddrinfo(server_name, service, &hints, &addr)) {
        die("Failed to resolve server address");
    }

    ctx->sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (ctx->sockfd < 0) {
        die("Failed to create socket");
    }

    if (connect(ctx->sockfd, addr->ai_addr, addr->ai_addrlen)) {
        die("Failed to connect to server");
    }

    freeaddrinfo(addr);
}