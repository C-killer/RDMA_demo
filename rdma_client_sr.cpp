#include "rdma_common.hpp"


void init_client(rdma_context *ctx) {
    /* TCP连接 */
    int sock_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (sock_fd < 0) {
        perror("Socket creation failed");
        exit(EXIT_FAILURE);
    }

    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(PORT);
    server_addr.sin_addr.s_addr = inet_addr("10.211.55.3");

    // 连接到服务器
    if (connect(sock_fd, (const sockaddr *)&server_addr, sizeof(sockaddr)) < 0) {
        perror("Connect failed");
        exit(EXIT_FAILURE);
    }
    std::cout << "Connected to server" << std::endl;

    /* 设置RDMA资源 */
    struct ibv_device **dev_list = ibv_get_device_list(NULL);
    ctx->ctx = ibv_open_device(dev_list[0]);
    if (!ctx->ctx) {
        perror("Failed to open device");
        exit(EXIT_FAILURE);
    }
    ibv_free_device_list(dev_list);
    ctx->pd = ibv_alloc_pd(ctx->ctx);
    if (!ctx->pd) {
        perror("Failed to allocate PD");
        exit(EXIT_FAILURE);
    }
    ctx->channel = ibv_create_comp_channel(ctx->ctx);
    if (!ctx->channel) {
        perror("Failed to create completion channel");
        exit(EXIT_FAILURE);
    }
    ctx->cq = ibv_create_cq(ctx->ctx, 10, NULL, ctx->channel, 0);
    if (!ctx->cq) {
        perror("Failed to create CQ");
        exit(EXIT_FAILURE);
    }
    ctx->buffer = (char *)malloc(BUFFER_SIZE);
    if (!ctx->buffer) {
        perror("Failed to allocate buffer");
        exit(EXIT_FAILURE);
    }
    ctx->mr = ibv_reg_mr(ctx->pd, ctx->buffer, BUFFER_SIZE, IBV_ACCESS_LOCAL_WRITE | IBV_ACCESS_REMOTE_WRITE);
    if (!ctx->mr) {
        perror("Failed to register MR");
        exit(EXIT_FAILURE);
    }

    // 初始化qp
    struct ibv_qp_init_attr qp_attr;
    memset(&qp_attr, 0, sizeof(qp_attr));
    qp_attr.send_cq = ctx->cq;
    qp_attr.recv_cq = ctx->cq;
    qp_attr.qp_type = IBV_QPT_RC;
    qp_attr.cap.max_send_wr = 10;
    qp_attr.cap.max_recv_wr = 10;
    qp_attr.cap.max_send_sge = 1;
    qp_attr.cap.max_recv_sge = 1;
    ctx->qp = ibv_create_qp(ctx->pd, &qp_attr);
    if (!ctx->qp) {
        perror("Failed to create QP");
        exit(EXIT_FAILURE);
    }

    struct ibv_qp_attr mod_attr;
    memset(&mod_attr, 0, sizeof(mod_attr));
    mod_attr.qp_state = IBV_QPS_INIT;
    mod_attr.pkey_index = 0;
    mod_attr.port_num = 1;
    mod_attr.qp_access_flags = IBV_ACCESS_REMOTE_WRITE;
    if (ibv_modify_qp(ctx->qp, &mod_attr, IBV_QP_STATE | IBV_QP_PKEY_INDEX | IBV_QP_PORT | IBV_QP_ACCESS_FLAGS)) {
        perror("Failed to modify QP to INIT");
        exit(EXIT_FAILURE);
    }

    // 获取本地LID
    struct ibv_port_attr port_attr;
    if (ibv_query_port(ctx->ctx, 1, &port_attr)) {
        perror("Failed to query port");
        exit(EXIT_FAILURE);
    }

    union ibv_gid gid;
    if (ibv_query_gid(ctx->ctx, 1, 0, &gid)) {
        perror("Failed to query GID");
        exit(EXIT_FAILURE);
    }

    // 准备本地QP信息
    qp_info local_qp_info, remote_qp_info;
    local_qp_info.qp_num = ctx->qp->qp_num;
    local_qp_info.lid = port_attr.lid;
    memcpy(local_qp_info.gid, &gid, sizeof(gid));

    // 交换QP信息
    if (exchange_qp_info(sock_fd, &local_qp_info, &remote_qp_info) < 0) {
        perror("Failed to exchange QP info");
        exit(EXIT_FAILURE);
    }

    // 输出调试信息
    std::cout << "Local QP Info - QP Num: " << local_qp_info.qp_num << ", LID: " << local_qp_info.lid << std::endl;
    std::cout << "Remote QP Info - QP Num: " << remote_qp_info.qp_num << ", LID: " << remote_qp_info.lid << std::endl;

    // 修改QP状态为RTR
    memset(&mod_attr, 0, sizeof(mod_attr));
    mod_attr.qp_state = IBV_QPS_RTR;
    mod_attr.path_mtu = IBV_MTU_1024;
    mod_attr.dest_qp_num = remote_qp_info.qp_num;
    mod_attr.rq_psn = 0;
    mod_attr.max_dest_rd_atomic = 1;
    mod_attr.min_rnr_timer = 12;
    mod_attr.ah_attr.is_global = 1;
    memcpy(&mod_attr.ah_attr.grh.dgid, remote_qp_info.gid, 16);  // 设置GID
    mod_attr.ah_attr.grh.sgid_index = 0;
    mod_attr.ah_attr.grh.hop_limit = 1;
    mod_attr.ah_attr.dlid = remote_qp_info.lid;
    mod_attr.ah_attr.sl = 0;
    mod_attr.ah_attr.src_path_bits = 0;
    mod_attr.ah_attr.port_num = 1;
    ibv_modify_qp(ctx->qp, &mod_attr, IBV_QP_STATE | IBV_QP_AV | IBV_QP_PATH_MTU | IBV_QP_DEST_QPN | IBV_QP_RQ_PSN | IBV_QP_MAX_DEST_RD_ATOMIC | IBV_QP_MIN_RNR_TIMER);

    // 修改QP状态为RTS
    memset(&mod_attr, 0, sizeof(mod_attr));
    mod_attr.qp_state = IBV_QPS_RTS;
    mod_attr.timeout = 14;
    mod_attr.retry_cnt = 7;
    mod_attr.rnr_retry = 7;
    mod_attr.sq_psn = 0;
    mod_attr.max_rd_atomic = 1;
    ibv_modify_qp(ctx->qp, &mod_attr, IBV_QP_STATE | IBV_QP_TIMEOUT | IBV_QP_RETRY_CNT | IBV_QP_RNR_RETRY | IBV_QP_SQ_PSN | IBV_QP_MAX_QP_RD_ATOMIC);

    // 发送消息到服务器
    strcpy(ctx->buffer, "Ping");        //将字符串 "Ping" 复制到发送缓冲区中
    struct ibv_sge sge;  //散列表项 (SGL)
    sge.addr = (uintptr_t)ctx->buffer;  //发送数据缓冲区的地址
    sge.length = BUFFER_SIZE;
    sge.lkey = ctx->mr->lkey;           //内存密钥

    struct ibv_send_wr send_wr, *bad_send_wr;  //发送请求 (WR)
    memset(&send_wr, 0, sizeof(send_wr));
    send_wr.opcode = IBV_WR_SEND;            // 设置操作码为发送操作
    //send_wr.opcode = IBV_WR_SEND_WITH_IMM;  // 发送操作类型
    //send_wr.imm_data = htonl(0x12345678);   // 设置即时数据，使用网络字节序
    send_wr.sg_list = &sge;                  // 设置发送工作请求的SGE列表指针
    send_wr.num_sge = 1;                     // 设置SGE列表中的元素数量为1
    send_wr.send_flags = IBV_SEND_SIGNALED;  // 设置发送标志，表示发送完成时生成完成队列元素（CQE）。
    
    if (ibv_post_send(ctx->qp, &send_wr, &bad_send_wr)) {
        perror("Failed to post send request");
        exit(EXIT_FAILURE);
    }

    struct ibv_wc wc;
    while (ibv_poll_cq(ctx->cq, 1, &wc) == 0); 
    if (wc.status == IBV_WC_SUCCESS) {
        std::cout << "Sent message to server" << std::endl;
        // if (wc.wc_flags & IBV_WC_WITH_IMM) {
        //     uint32_t imm_data = ntohl(wc.imm_data);  // 转换为主机字节序
        //     std::cout << "Received message with immediate data: " << imm_data << std::endl;
        // }
    } else {
        std::cerr << "Send failed with status " << wc.status << std::endl;
    }

    // 接收服务器响应
    struct ibv_recv_wr recv_wr, *bad_recv_wr;
    memset(&recv_wr, 0, sizeof(recv_wr));
    recv_wr.sg_list = &sge;
    recv_wr.num_sge = 1;

    if (ibv_post_recv(ctx->qp, &recv_wr, &bad_recv_wr)) {
        perror("Failed to post receive request");
        exit(EXIT_FAILURE);
    }

    while (ibv_poll_cq(ctx->cq, 1, &wc) == 0);
    if (wc.status == IBV_WC_SUCCESS) {
        std::cout << "Received response: " << ctx->buffer << std::endl;
    } else {
        std::cerr << "Receive failed with status " << wc.status << std::endl;
    }

    close(sock_fd);
}

int main() {
    rdma_context ctx;
    init_client(&ctx);

    // 清理RDMA资源
    ibv_dereg_mr(ctx.mr);
    free(ctx.buffer);
    ibv_destroy_qp(ctx.qp);
    ibv_destroy_cq(ctx.cq);
    ibv_dealloc_pd(ctx.pd);
    ibv_close_device(ctx.ctx);
    
    return 0;
}