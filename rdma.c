#include "rdma.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <inttypes.h>
#include <endian.h>
#include <byteswap.h>
#include <getopt.h>
#include <sys/time.h>
#include <arpa/inet.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <sys/resource.h>
#include <errno.h>
#include <infiniband/verbs.h>

// Add this define
#ifndef ibv_link_layer_str
#define ibv_link_layer_str(link_layer) ((link_layer == IBV_LINK_LAYER_INFINIBAND) ? "InfiniBand" : \
                                        (link_layer == IBV_LINK_LAYER_ETHERNET) ? "Ethernet" : "Unknown")
#endif

#define MAX_POLL_CQ_TIMEOUT 10000

// Utility functions for 64-bit host to network byte order conversion
uint64_t htonll(uint64_t x) {
    return ((1==htonl(1)) ? (x) : ((uint64_t)htonl((x) & 0xFFFFFFFF) << 32) | htonl((x) >> 32));
}

uint64_t ntohll(uint64_t x) {
    return ((1==ntohl(1)) ? (x) : ((uint64_t)ntohl((x) & 0xFFFFFFFF) << 32) | ntohl((x) >> 32));
}

struct config_t config = {
    NULL,  /* dev_name */
    19875, /* tcp_port */
    1,     /* ib_port */
    -1     /* gid_idx */
};



void print_port_info(struct ibv_context *context, int port_num) {
    struct ibv_port_attr port_attr;
    if (ibv_query_port(context, port_num, &port_attr) == 0) {
        printf("Port Info:\n");
        printf("  State: %d (%s)\n", port_attr.state, ibv_port_state_str(port_attr.state));
        printf("  Max MTU: %d\n", port_attr.max_mtu);
        printf("  Active MTU: %d\n", port_attr.active_mtu);
        printf("  LID: %d\n", port_attr.lid);
        printf("  Link Layer: %s\n", ibv_link_layer_str(port_attr.link_layer));
    } else {
        fprintf(stderr, "Failed to query port attributes\n");
    }
}

int sock_connect(const char *servername, int port)
{
    fprintf(stdout, "Entering function: %s\n", __func__);
	struct addrinfo *resolved_addr = NULL;
	struct addrinfo *iterator;
	char service[6];
	int sockfd = -1;
	int listenfd = 0;
	int tmp;
	struct addrinfo hints =
		{
			.ai_flags = AI_PASSIVE,
			.ai_family = AF_INET,
			.ai_socktype = SOCK_STREAM};
	if (sprintf(service, "%d", port) < 0)
		goto sock_connect_exit;
	/* Resolve DNS address, use sockfd as temp storage */
	sockfd = getaddrinfo(servername, service, &hints, &resolved_addr);
	if (sockfd < 0)
	{
		fprintf(stderr, "%s for %s:%d\n", gai_strerror(sockfd), servername, port);
		goto sock_connect_exit;
	}
	/* Search through results and find the one we want */
	for (iterator = resolved_addr; iterator; iterator = iterator->ai_next)
	{
		sockfd = socket(iterator->ai_family, iterator->ai_socktype, iterator->ai_protocol);
		if (sockfd >= 0)
		{
			if (servername){
				/* Client mode. Initiate connection to remote */
				if ((tmp = connect(sockfd, iterator->ai_addr, iterator->ai_addrlen)))
				{
					fprintf(stdout, "failed connect \n");
					close(sockfd);
					sockfd = -1;
				}
            }
			else
			{
					/* Server mode. Set up listening socket an accept a connection */
					listenfd = sockfd;
					sockfd = -1;
					if (bind(listenfd, iterator->ai_addr, iterator->ai_addrlen))
						goto sock_connect_exit;
					listen(listenfd, 1);
					sockfd = accept(listenfd, NULL, 0);
			}
		}
	}
sock_connect_exit:
	if (listenfd)
		close(listenfd);
	if (resolved_addr)
		freeaddrinfo(resolved_addr);
	if (sockfd < 0)
	{
		if (servername)
			fprintf(stderr, "Couldn't connect to %s:%d\n", servername, port);
		else
		{
			perror("server accept");
			fprintf(stderr, "accept() failed\n");
		}
	}
	return sockfd;
}

static int sock_sync_data(int sock, int xfer_size, char *local_data, char *remote_data)
{
    fprintf(stdout, "Entering function: %s\n", __func__);
    int rc;
    int read_bytes = 0;
    int total_read_bytes = 0;

    rc = write(sock, local_data, xfer_size);
    if (rc < xfer_size)
        fprintf(stderr, "Failed writing data during sock_sync_data\n");
    else
        rc = 0;

    while (!rc && total_read_bytes < xfer_size) {
        read_bytes = read(sock, remote_data, xfer_size);
        if (read_bytes > 0)
            total_read_bytes += read_bytes;
        else
            rc = read_bytes;
    }
    return rc;
}

int poll_completion(struct resources *res)
{
    fprintf(stdout, "Entering function: %s\n", __func__);
    struct ibv_wc wc;
    unsigned long start_time_msec;
    unsigned long cur_time_msec;
    struct timeval cur_time;
    int poll_result;
    int rc = 0;

    gettimeofday(&cur_time, NULL);
    start_time_msec = (cur_time.tv_sec * 1000) + (cur_time.tv_usec / 1000);

    do {
        poll_result = ibv_poll_cq(res->cq, 1, &wc);
        gettimeofday(&cur_time, NULL);
        cur_time_msec = (cur_time.tv_sec * 1000) + (cur_time.tv_usec / 1000);
    } while ((poll_result == 0) && ((cur_time_msec - start_time_msec) < MAX_POLL_CQ_TIMEOUT));

    if (poll_result < 0) {
        fprintf(stderr, "poll CQ failed\n");
        rc = 1;
    } else if (poll_result == 0) {
        fprintf(stderr, "completion wasn't found in the CQ after timeout\n");
        rc = 1;
    } else {
        fprintf(stdout, "completion was found in CQ with status 0x%x\n", wc.status);
        if (wc.status != IBV_WC_SUCCESS) {
            fprintf(stderr, "got bad completion with status: 0x%x, vendor syndrome: 0x%x\n", wc.status, wc.vendor_err);
            fprintf(stderr, "Completion error description: %s\n", ibv_wc_status_str(wc.status));
            rc = 1;
        }
    }
    return rc;
}

int post_send(struct resources *res, int opcode)
{
    fprintf(stdout, "Entering function: %s\n", __func__);
    struct ibv_send_wr sr;
    struct ibv_sge sge;
    struct ibv_send_wr *bad_wr = NULL;
    int rc;

    memset(&sge, 0, sizeof(sge));
    sge.addr = (uintptr_t)res->buf;
    sge.length = MSG_SIZE;
    sge.lkey = res->mr->lkey;

    memset(&sr, 0, sizeof(sr));
    sr.next = NULL;
    sr.wr_id = 0;
    sr.sg_list = &sge;
    sr.num_sge = 1;
    sr.opcode = opcode;
    sr.send_flags = IBV_SEND_SIGNALED;

    if (opcode != IBV_WR_SEND) {
        sr.wr.rdma.remote_addr = res->remote_props.addr;
        sr.wr.rdma.rkey = res->remote_props.rkey;
    }

    rc = ibv_post_send(res->qp, &sr, &bad_wr);
    //-----------Debug-----------------

    if (rc) {
        fprintf(stderr, "failed to post SR, error: %d\n", rc);
    } else {
        fprintf(stdout, "SR was posted successfully\n");
        fprintf(stdout, "RDMA operation details:\n");
        fprintf(stdout, "  opcode: %d\n", opcode);
        fprintf(stdout, "  send_flags: 0x%x\n", sr.send_flags);
        fprintf(stdout, "  addr: 0x%lx\n", (unsigned long)sge.addr);
        fprintf(stdout, "  length: %u\n", sge.length);
        fprintf(stdout, "  lkey: 0x%x\n", sge.lkey);
        if (opcode != IBV_WR_SEND) {
            fprintf(stdout, "  remote_addr: 0x%lx\n", (unsigned long)sr.wr.rdma.remote_addr);
            fprintf(stdout, "  rkey: 0x%x\n", sr.wr.rdma.rkey);
        }
    }

    //-----------Debug-----------------
    if (rc) {
        fprintf(stderr, "ibv_post_send failed with error: %d\n", rc);
    }
    else {
        switch (opcode) {
        case IBV_WR_SEND:
            fprintf(stdout, "Send Request was posted\n");
            break;
        case IBV_WR_RDMA_READ:
            fprintf(stdout, "RDMA Read Request was posted\n");
            break;
        case IBV_WR_RDMA_WRITE:
            fprintf(stdout, "RDMA Write Request was posted\n");
            break;
        default:
            fprintf(stdout, "Unknown Request was posted\n");
            break;
        }
    }
    return rc;
}

int post_receive(struct resources *res)
{
    fprintf(stdout, "Entering function: %s\n", __func__);
    struct ibv_recv_wr rr;
    struct ibv_sge sge;
    struct ibv_recv_wr *bad_wr;
    int rc;

    memset(&sge, 0, sizeof(sge));
    sge.addr = (uintptr_t)res->buf;
    sge.length = MSG_SIZE;
    sge.lkey = res->mr->lkey;

    memset(&rr, 0, sizeof(rr));
    rr.next = NULL;
    rr.wr_id = 0;
    rr.sg_list = &sge;
    rr.num_sge = 1;

    rc = ibv_post_recv(res->qp, &rr, &bad_wr);
    if (rc) {
        fprintf(stderr, "ibv_post_recv failed with error: %d\n", rc);
    }
    else
        fprintf(stdout, "Receive Request was posted\n");
    return rc;
}



void resources_init(struct resources *res)
{
    fprintf(stdout, "Entering function: %s\n", __func__);
    memset(res, 0, sizeof *res);
    res->sock = -1;
}

int resources_create(struct resources *res)
{
    fprintf(stdout, "Entering function: %s\n", __func__);
    struct rlimit rlim;
    if (getrlimit(RLIMIT_NOFILE, &rlim) == 0) {
        fprintf(stdout, "Current file descriptor limit: %lu\n", (unsigned long)rlim.rlim_cur);
    } else {
        fprintf(stderr, "Failed to get resource limits: %s\n", strerror(errno));
    }
    struct ibv_device **dev_list = NULL;
    struct ibv_qp_init_attr qp_init_attr;
    struct ibv_device *ib_dev = NULL;
    size_t size;
    int i;
    int mr_flags = 0;
    int cq_size = 0;
    int num_devices;
    int rc = 0;

    dev_list = ibv_get_device_list(&num_devices);
    if (!dev_list) {
        fprintf(stderr, "failed to get IB devices list\n");
        rc = 1;
        goto resources_create_exit;
    }

    if (!num_devices) {
        fprintf(stderr, "found %d device(s)\n", num_devices);
        rc = 1;
        goto resources_create_exit;
    }

    for (i = 0; i < num_devices; i++) {
        if (!config.dev_name) {
            config.dev_name = strdup(ibv_get_device_name(dev_list[i]));
            fprintf(stdout, "device not specified, using first one found: %s\n", config.dev_name);
        }
        if (!strcmp(ibv_get_device_name(dev_list[i]), config.dev_name)) {
            ib_dev = dev_list[i];
            break;
        }
    }

    if (!ib_dev) {
        fprintf(stderr, "IB device %s wasn't found\n", config.dev_name);
        rc = 1;
        goto resources_create_exit;
    }

    res->ib_ctx = ibv_open_device(ib_dev);
    if (!res->ib_ctx) {
        fprintf(stderr, "failed to open device %s\n", config.dev_name);
        rc = 1;
        goto resources_create_exit;
    }

    if (ibv_query_port(res->ib_ctx, config.ib_port, &res->port_attr)) {
        fprintf(stderr, "ibv_query_port on port %u failed\n", config.ib_port);
        rc = 1;
        goto resources_create_exit;
    }

    print_port_info(res->ib_ctx, config.ib_port);

    res->pd = ibv_alloc_pd(res->ib_ctx);
    if (!res->pd) {
        fprintf(stderr, "ibv_alloc_pd failed\n");
        rc = 1;
        goto resources_create_exit;
    }

    cq_size = 10;
    res->cq = ibv_create_cq(res->ib_ctx, cq_size, NULL, NULL, 0);
    if (!res->cq) {
        fprintf(stderr, "failed to create CQ with %u entries\n", cq_size);
        rc = 1;
        goto resources_create_exit;
    }

    size = MSG_SIZE;
    res->buf = (char *)malloc(size);
    if (!res->buf) {
        fprintf(stderr, "failed to malloc %zu bytes to memory buffer\n", size);
        rc = 1;
        goto resources_create_exit;
    }

    memset(res->buf, 0, size);

    //store the buffer size
    res->buf_size = (uint32_t)size;

    fprintf(stdout, "Buffer initialized to zero, size: %zu bytes\n", size);

    mr_flags = IBV_ACCESS_LOCAL_WRITE | IBV_ACCESS_REMOTE_READ | IBV_ACCESS_REMOTE_WRITE;

    // Add this debug print
    fprintf(stdout, "Registering MR with size: %zu bytes\n", size);

    
    res->mr = ibv_reg_mr(res->pd, res->buf, size, mr_flags);

    if (res->port_attr.state != IBV_PORT_ACTIVE) {
        fprintf(stderr, "Port is not in active state (state: %d - %s)\n", 
            res->port_attr.state, 
            ibv_port_state_str(res->port_attr.state));
        fprintf(stderr, "This may be normal for RoCE environments. Continuing...\n");
    }

    if (!res->mr) {
        fprintf(stderr, "ibv_reg_mr failed with mr_flags=0x%x\n", mr_flags);
        rc = 1;
        goto resources_create_exit;
    }

    memset(&qp_init_attr, 0, sizeof(qp_init_attr));
    qp_init_attr.qp_type = IBV_QPT_RC;
    qp_init_attr.sq_sig_all = 1;
    qp_init_attr.send_cq = res->cq;
    qp_init_attr.recv_cq = res->cq;
    qp_init_attr.cap.max_send_wr = 10;
    qp_init_attr.cap.max_recv_wr = 10;
    qp_init_attr.cap.max_send_sge = 1;
    qp_init_attr.cap.max_recv_sge = 1;

    fprintf(stdout, "Creating QP with max_send_wr: %d, max_recv_wr: %d\n", 
        qp_init_attr.cap.max_send_wr, qp_init_attr.cap.max_recv_wr);


    res->qp = ibv_create_qp(res->pd, &qp_init_attr);
    if (!res->qp) {
        fprintf(stderr, "failed to create QP\n");
        rc = 1;
        goto resources_create_exit;
    }

resources_create_exit:
    if (rc) {
        if (res->qp) {
            ibv_destroy_qp(res->qp);
            res->qp = NULL;
        }
        if (res->mr) {
            ibv_dereg_mr(res->mr);
            res->mr = NULL;
        }
        if (res->buf) {
            free(res->buf);
            res->buf = NULL;
        }
        if (res->cq) {
            ibv_destroy_cq(res->cq);
            res->cq = NULL;
        }
        if (res->pd) {
            ibv_dealloc_pd(res->pd);
            res->pd = NULL;
        }
        if (res->ib_ctx) {
            ibv_close_device(res->ib_ctx);
            res->ib_ctx = NULL;
        }
        if (dev_list) {
            ibv_free_device_list(dev_list);
            dev_list = NULL;
        }
    }
    return rc;
}

int resources_destroy(struct resources *res)
{
    fprintf(stdout, "Entering function: %s\n", __func__);
    int rc = 0;

    if (res->qp)
        if (ibv_destroy_qp(res->qp)) {
            fprintf(stderr, "failed to destroy QP\n");
            rc = 1;
        }

    if (res->mr)
        if (ibv_dereg_mr(res->mr)) {
            fprintf(stderr, "failed to deregister MR\n");
            rc = 1;
        }

    if (res->buf)
        free(res->buf);

    if (res->cq)
        if (ibv_destroy_cq(res->cq)) {
            fprintf(stderr, "failed to destroy CQ\n");
            rc = 1;
        }

    if (res->pd)
        if (ibv_dealloc_pd(res->pd)) {
            fprintf(stderr, "failed to deallocate PD\n");
            rc = 1;
        }

    if (res->ib_ctx)
        if (ibv_close_device(res->ib_ctx)) {
            fprintf(stderr, "failed to close device context\n");
            rc = 1;
        }

    if (res->sock >= 0)
        if (close(res->sock)) {
            fprintf(stderr, "failed to close socket\n");
            rc = 1;
        }

    return rc;
}

static int modify_qp_to_init(struct ibv_qp *qp)
{
    fprintf(stdout, "Entering function: %s\n", __func__);
    struct ibv_qp_attr attr;
    int flags;
    int rc;

    memset(&attr, 0, sizeof(attr));
    attr.qp_state = IBV_QPS_INIT;
    attr.port_num = config.ib_port;
    attr.pkey_index = 0;
    attr.qp_access_flags = IBV_ACCESS_LOCAL_WRITE | IBV_ACCESS_REMOTE_READ | IBV_ACCESS_REMOTE_WRITE;

    flags = IBV_QP_STATE | IBV_QP_PKEY_INDEX | IBV_QP_PORT | IBV_QP_ACCESS_FLAGS;

    rc = ibv_modify_qp(qp, &attr, flags);
    if (rc)
        fprintf(stderr, "failed to modify QP state to INIT\n");
    return rc;
}

static int modify_qp_to_rtr(struct ibv_qp *qp, uint32_t remote_qpn, uint16_t dlid, uint8_t *dgid)
{
    fprintf(stdout, "Entering function: %s\n", __func__);
    struct ibv_qp_attr attr;
    int flags;
    int rc;

    memset(&attr, 0, sizeof(attr));
    attr.qp_state = IBV_QPS_RTR;
    attr.path_mtu = IBV_MTU_256;
    attr.dest_qp_num = remote_qpn;
    attr.rq_psn = 0;
    attr.max_dest_rd_atomic = 1;
    attr.min_rnr_timer = 0x12;
    attr.ah_attr.is_global = 0;
    attr.ah_attr.dlid = dlid;
    attr.ah_attr.sl = 0;
    attr.ah_attr.src_path_bits = 0;
    attr.ah_attr.port_num = config.ib_port;

    if (config.gid_idx >= 0) {
        attr.ah_attr.is_global = 1;
        attr.ah_attr.port_num = config.ib_port;
        memcpy(&attr.ah_attr.grh.dgid, dgid, 16);
        attr.ah_attr.grh.flow_label = 0;
        attr.ah_attr.grh.hop_limit = 1;
        attr.ah_attr.grh.sgid_index = config.gid_idx;
        attr.ah_attr.grh.traffic_class = 0;
    }

    flags = IBV_QP_STATE | IBV_QP_AV | IBV_QP_PATH_MTU | IBV_QP_DEST_QPN |
            IBV_QP_RQ_PSN | IBV_QP_MAX_DEST_RD_ATOMIC | IBV_QP_MIN_RNR_TIMER;

    rc = ibv_modify_qp(qp, &attr, flags);
    if (rc) {
        fprintf(stderr, "failed to modify QP state to RTR, error: %d\n", rc);
        return rc;
    }
    fprintf(stdout, "QP modified to RTR state successfully\n");

    fprintf(stdout, "Modifying QP to RTR with remote QP: %u, remote LID: %u\n", remote_qpn, dlid);
    if (config.gid_idx >= 0) {
        fprintf(stdout, "Using GID index: %d\n", config.gid_idx);
        fprintf(stdout, "Remote GID: ");
        for (int i = 0; i < 16; i++) {
            fprintf(stdout, "%02x", dgid[i]);
        }
        fprintf(stdout, "\n");
    }
    return rc;
}

static int modify_qp_to_rts(struct ibv_qp *qp)
{
    fprintf(stdout, "Entering function: %s\n", __func__);
    struct ibv_qp_attr attr;
    int flags;
    int rc;

    memset(&attr, 0, sizeof(attr));
    attr.qp_state = IBV_QPS_RTS;
    attr.timeout = 0x12;
    attr.retry_cnt = 6;
    attr.rnr_retry = 0;
    attr.sq_psn = 0;
    attr.max_rd_atomic = 1;

    flags = IBV_QP_STATE | IBV_QP_TIMEOUT | IBV_QP_RETRY_CNT |
            IBV_QP_RNR_RETRY | IBV_QP_SQ_PSN | IBV_QP_MAX_QP_RD_ATOMIC;

    rc = ibv_modify_qp(qp, &attr, flags);
    if (rc) {
        fprintf(stderr, "failed to modify QP state to RTS, error: %d\n", rc);
        return rc;
    }
    fprintf(stdout, "QP modified to RTS state successfully\n");
    return rc;
}

int connect_qp(struct resources *res)
{
    fprintf(stdout, "Entering function: %s\n", __func__);
    struct cm_con_data_t local_con_data;
    struct cm_con_data_t remote_con_data;
    struct cm_con_data_t tmp_con_data;
    int rc = 0;
    char temp_char;
    union ibv_gid my_gid;

    if (config.gid_idx >= 0) {
        rc = ibv_query_gid(res->ib_ctx, config.ib_port, config.gid_idx, &my_gid);
        if (rc) {
            fprintf(stderr, "could not get gid for port %d, index %d\n", config.ib_port, config.gid_idx);
            return rc;
        }
    } else
        memset(&my_gid, 0, sizeof my_gid);

    local_con_data.addr = htonll((uintptr_t)res->buf);
    local_con_data.rkey = htonl(res->mr->rkey);
    local_con_data.qp_num = htonl(res->qp->qp_num);
    local_con_data.lid = htons(res->port_attr.lid);
    memcpy(local_con_data.gid, &my_gid, 16);
    local_con_data.size = htonl(res->buf_size);  // Add this line

    fprintf(stdout, "Local QP information:\n");
    fprintf(stdout, "  QP number: %u\n", res->qp->qp_num);
    fprintf(stdout, "  LID: %u\n", res->port_attr.lid);
    fprintf(stdout, "Local GID: ");
    for (int i = 0; i < 16; i++) {
        fprintf(stdout, "%02x", my_gid.raw[i]);
    }
    fprintf(stdout, "\n");

    if (sock_sync_data(res->sock, sizeof(struct cm_con_data_t), (char *)&local_con_data, (char *)&tmp_con_data) < 0) {
        fprintf(stderr, "failed to exchange connection data between sides\n");
        rc = 1;
        goto connect_qp_exit;
    }

    remote_con_data.addr = ntohll(tmp_con_data.addr);
    remote_con_data.rkey = ntohl(tmp_con_data.rkey);
    remote_con_data.qp_num = ntohl(tmp_con_data.qp_num);
    remote_con_data.lid = ntohs(tmp_con_data.lid);
    memcpy(remote_con_data.gid, tmp_con_data.gid, 16);
    remote_con_data.size = ntohl(tmp_con_data.size);  // Add this line

    res->remote_props = remote_con_data;


    fprintf(stdout, "Remote QP information:\n");
    fprintf(stdout, "  QP number: %u\n", remote_con_data.qp_num);
    fprintf(stdout, "  LID: %u\n", remote_con_data.lid);
    fprintf(stdout, "  GID: ");
    for (int i = 0; i < 16; i++) {
        fprintf(stdout, "%02x", remote_con_data.gid[i]);
    }
    fprintf(stdout, "\n");

    if (modify_qp_to_init(res->qp)) {
        fprintf(stderr, "change QP state to INIT failed\n");
        goto connect_qp_exit;
    }

    if (res->port_attr.link_layer == IBV_LINK_LAYER_ETHERNET) {
        fprintf(stdout, "Detected Ethernet link layer (RoCE). Using GID-based addressing.\n");
        config.gid_idx = 0;  // You might need to adjust this value
    }

    fprintf(stdout, "Modifying QP to RTR with remote QP: %u, remote LID: %u\n", remote_con_data.qp_num, remote_con_data.lid);
    if (config.gid_idx >= 0) {
        fprintf(stdout, "Using GID index: %d\n", config.gid_idx);
        fprintf(stdout, "Remote GID: ");
        for (int i = 0; i < 16; i++) {
            fprintf(stdout, "%02x", remote_con_data.gid[i]);
        }
        fprintf(stdout, "\n");
    }

    if (modify_qp_to_rtr(res->qp, remote_con_data.qp_num, remote_con_data.lid, remote_con_data.gid)) {
        fprintf(stderr, "failed to modify QP state to RTR\n");
        goto connect_qp_exit;
    }

    if (modify_qp_to_rts(res->qp)) {
        fprintf(stderr, "failed to modify QP state to RTS\n");
        goto connect_qp_exit;
    }

    if (remote_con_data.lid == 65535) {
        fprintf(stderr, "Invalid remote LID. This might indicate a RoCE v2 setup.\n");
        fprintf(stderr, "Try setting GID index explicitly in the configuration.\n");
    }

    fprintf(stdout, "QP state was changed to RTS\n");

    // Add these debug prints and synchronization
    fprintf(stdout, "QP ready, waiting for peer...\n");
    if (sock_sync_data(res->sock, 1, "R", &temp_char)) {
        fprintf(stderr, "Sync error after QP RTS\n");
        rc = 1;
        goto connect_qp_exit;
    }
    fprintf(stdout, "Peer ready, beginning operations\n");
    

connect_qp_exit:
    return rc;
}

void usage(const char *argv0)
{
    fprintf(stdout, "Usage:\n");
    fprintf(stdout, "  %s start a server and wait for connection\n", argv0);
    fprintf(stdout, "  %s <host> connect to server at <host>\n", argv0);
    fprintf(stdout, "\n");
    fprintf(stdout, "Options:\n");
    fprintf(stdout, "  -p, --port <port> listen on/connect to port <port> (default 18515)\n");
    fprintf(stdout, "  -d, --ib-dev <dev> use IB device <dev> (default first device found)\n");
    fprintf(stdout, "  -i, --ib-port <port> use port <port> of IB device (default 1)\n");
    fprintf(stdout, "  -g, --gid_idx <git index> gid index to be used in GRH (default not used)\n");
}

void print_config(void)
{
    fprintf(stdout, " ------------------------------------------------\n");
    fprintf(stdout, " Device name : \"%s\"\n", config.dev_name);
    fprintf(stdout, " IB port : %u\n", config.ib_port);
    fprintf(stdout, " TCP port : %u\n", config.tcp_port);
    if (config.gid_idx >= 0)
        fprintf(stdout, " GID index : %u\n", config.gid_idx);
    fprintf(stdout, " ------------------------------------------------\n\n");
}

