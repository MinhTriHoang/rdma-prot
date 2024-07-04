#ifndef RDMA_H
#define RDMA_H

#include <infiniband/verbs.h>
#include <stdint.h>

#define RDMA_BUFFER_SIZE 1024

struct rdma_context {
    struct ibv_context *ctx;
    struct ibv_pd *pd;
    struct ibv_mr *mr;
    struct ibv_cq *cq;
    struct ibv_qp *qp;
    void *buffer;
    size_t buffer_size;
    int sockfd;
};

void die(const char *message);
void create_rdma_context(struct rdma_context *ctx);
void destroy_rdma_context(struct rdma_context *ctx);
void post_rdma_write(struct rdma_context *ctx, void *local_addr, uint32_t length, uint64_t remote_addr, uint32_t rkey);
void post_rdma_read(struct rdma_context *ctx, void *local_addr, uint32_t length, uint64_t remote_addr, uint32_t rkey);
void post_rdma_atomic(struct rdma_context *ctx, uint64_t remote_addr, uint32_t rkey, uint64_t compare, uint64_t swap);
void poll_completion(struct rdma_context *ctx);
void connect_rdma(struct rdma_context *ctx, const char *server_name, int port);

#endif