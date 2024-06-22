#include "rdma_common.hpp"

//初始化服务端
void init_server(rdma_context *ctx) {
    /* TCP连接 */
    // 创建套接字 
    int sock_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (sock_fd < 0) {  //
        perror("Socket creation failed");
        exit(EXIT_FAILURE);
    }

    // 配置服务器地址
    struct sockaddr_in server_addr;
    server_addr.sin_family = AF_INET;       // 设置地址族为 IPv4
    server_addr.sin_port = htons(PORT);     // 设置端口号为 PORT，并使用 htons 函数将端口号从主机字节序转换为网络字节序
    server_addr.sin_addr.s_addr = htonl(INADDR_ANY);  //监听所有可用的网络接口上的所有IP地址
    
    // 绑定套接字到指定的IP和端口号
    if (bind(sock_fd, (const sockaddr*)& server_addr, sizeof(server_addr)) < 0) {
         perror("Bind failed");
         close(sock_fd);
         exit(EXIT_FAILURE);
    }

    // 监听连接请求
    if (listen(sock_fd, 5) < 0) {    //同时最多监听五个用户
        perror("Listen failed");
        close(sock_fd);
        exit(EXIT_FAILURE);
    }
    std::cout << "Server is listening on port " << PORT << std::endl;

    // 接受客户端连接
    struct sockaddr_in client_addr;
    socklen_t client_socklen = sizeof(client_addr);    
    int client_fd = accept(sock_fd, (struct sockaddr*)&client_addr, &client_socklen);
    if (client_fd < 0) {
        perror("Accept failed");
        exit(EXIT_FAILURE);
    }
    std::cout << "Client connected" << std::endl;


    /* RDMA传输 */
    // 设置RDMA
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
    memset(&qp_attr, 0, sizeof(qp_attr));    //将结构体 qp_attr 清零
    qp_attr.send_cq = ctx->cq;
    qp_attr.recv_cq = ctx->cq;
    qp_attr.qp_type = IBV_QPT_RC;
    qp_attr.cap.max_send_wr = 10;
    qp_attr.cap.max_recv_wr = 10;
    qp_attr.cap.max_send_sge = 1;       //发送队列最大SGE（散播-聚集元素）数
    qp_attr.cap.max_recv_sge = 1;           
    ctx->qp = ibv_create_qp(ctx->pd, &qp_attr);
    if (!ctx->qp) {
        perror("Failed to create QP");
        exit(EXIT_FAILURE);
    }
    
    // 修改队列属性
    struct ibv_qp_attr mod_attr;
    memset(&mod_attr, 0, sizeof(mod_attr));  //将结构体 mod_attr 清零
    mod_attr.qp_state = IBV_QPS_INIT;        //设置队列对状态为初始化（INIT）
    mod_attr.pkey_index = 0;                 //设置PKey索引
    mod_attr.port_num = 1;                   //设置端口号
    mod_attr.qp_access_flags = IBV_ACCESS_REMOTE_WRITE; //对访问权限为远程写。
 
    if (ibv_modify_qp(ctx->qp, &mod_attr, IBV_QP_STATE | IBV_QP_PKEY_INDEX | IBV_QP_PORT | IBV_QP_ACCESS_FLAGS)) {
        perror("Failed to modify QP to INIT");
        exit(EXIT_FAILURE);
    }

    // 获取本地LID与GID
    // int ibv_query_port(struct ibv_context *context, uint8_t port_num, struct ibv_port_attr *port_attr);
    // 上下文
    // 要查询的端口号
    // 指向struct ibv_port_attr结构体的指针
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

    //准备QP信息
    qp_info local_qp_info, remote_qp_info;
    local_qp_info.qp_num = ctx->qp->qp_num;
    local_qp_info.lid = port_attr.lid;
    memcpy(local_qp_info.gid, &gid, sizeof(gid));

    // 交换QP信息
    if (exchange_qp_info(client_fd, &local_qp_info, &remote_qp_info) < 0) {
        perror("Failed to exchange QP info");
        exit(EXIT_FAILURE);
    }

    // 输出调试信息
    std::cout << "Local QP Info - QP Num: " << local_qp_info.qp_num << ", LID: " << local_qp_info.lid << std::endl;
    std::cout << "Remote QP Info - QP Num: " << remote_qp_info.qp_num << ", LID: " << remote_qp_info.lid << std::endl;


    // 修改QP状态为RTR
    memset(&mod_attr, 0, sizeof(mod_attr));
    mod_attr.qp_state = IBV_QPS_RTR;
    mod_attr.path_mtu = IBV_MTU_1024;    // 设置路径MTU为1024字节
    mod_attr.dest_qp_num = remote_qp_info.qp_num;
    mod_attr.rq_psn = 0;                 // 接收队列的初始PSN
    mod_attr.max_dest_rd_atomic = 1;     // 最大目的地原子操作请求数
    mod_attr.min_rnr_timer = 12;         // 最小重传请求计时器
    mod_attr.ah_attr.is_global = 1;      // 地址句柄属性
    memcpy(&mod_attr.ah_attr.grh.dgid, remote_qp_info.gid, 16);  // 设置GID
    mod_attr.ah_attr.grh.sgid_index = 0;        // 设置全局路由头 (GRH) 的源GID索引
    mod_attr.ah_attr.grh.hop_limit = 1;         // 设置全局路由头 (GRH) 的跳数限制
    mod_attr.ah_attr.dlid = remote_qp_info.lid; // 设置目标局域标识符 (DLID) 为远程QP信息的LID
    mod_attr.ah_attr.sl = 0;                    // 设置服务级别 (SL)，这里设置为0
    mod_attr.ah_attr.src_path_bits = 0;         // 设置源路径位 (Source Path Bits)，这里设置为0
    mod_attr.ah_attr.port_num = 1;              // 设置端口号，这里设置为端口1
    if (ibv_modify_qp(ctx->qp, &mod_attr, IBV_QP_STATE | IBV_QP_AV | IBV_QP_PATH_MTU | IBV_QP_DEST_QPN | IBV_QP_RQ_PSN | IBV_QP_MAX_DEST_RD_ATOMIC | IBV_QP_MIN_RNR_TIMER)) {
        perror("Failed to modify QP to RTR");
        exit(EXIT_FAILURE);
    }

    // 修改QP状态为RTS
    memset(&mod_attr, 0, sizeof(mod_attr));
    mod_attr.qp_state = IBV_QPS_RTS;
    mod_attr.timeout = 14;        // 超时值
    mod_attr.retry_cnt = 7;       // 重试计数器
    mod_attr.rnr_retry = 7;       // 接收不可用重试计数器
    mod_attr.sq_psn = 0;          // 发送队列的初始PSN
    mod_attr.max_rd_atomic = 1;   // 最大发起的原子操作请求数
    if (ibv_modify_qp(ctx->qp, &mod_attr, IBV_QP_STATE | IBV_QP_TIMEOUT | IBV_QP_RETRY_CNT | IBV_QP_RNR_RETRY | IBV_QP_SQ_PSN | IBV_QP_MAX_QP_RD_ATOMIC)) {
        perror("Failed to modify QP to RTS");
        exit(EXIT_FAILURE);
    }

    // 准备接收消息
    // 定义并初始化ibv_sge结构体
    struct ibv_sge sge;
    sge.addr = (uintptr_t)ctx->buffer;      // 设置SGE的地址为RDMA内存缓冲区的地址
    sge.length = BUFFER_SIZE;
    sge.lkey = ctx->mr->lkey;               // 设置SGE的本地键（lkey），用于内存区域的访问

    struct ibv_recv_wr recv_wr, *bad_recv_wr; //bad_recv_wr是用于接收可能发生错误的工作请求的指针
    memset(&recv_wr, 0, sizeof(recv_wr));
    recv_wr.sg_list = &sge;                 // 设置接收工作请求的SGE列表指针
    recv_wr.num_sge = 1;

    if (ibv_post_recv(ctx->qp, &recv_wr, &bad_recv_wr)) {   // 将接收工作请求发布到队列对
        perror("Failed to post receive request");
        exit(EXIT_FAILURE);
    }  

    // 等待接收消息
    struct ibv_wc wc;
    //参数ctx->cq指定要轮询的完成队列
    //参数1指定要检索的最大工作完成数
    //参数&wc用于接收完成的工作请求信息
    while (ibv_poll_cq(ctx->cq, 1, &wc) == 0);  //轮询完成队列 (CQ)，等待接收操作完成。
    if (wc.status == IBV_WC_SUCCESS) {
        std::cout << "Received message: " << ctx->buffer << std::endl;
    } else {
        std::cerr << "Receive failed with status " << wc.status << std::endl;
    }

    // 发送响应消息
    strcpy(ctx->buffer, "Pong");
    struct ibv_send_wr send_wr, *bad_send_wr;
    memset(&send_wr, 0, sizeof(send_wr));
    send_wr.opcode = IBV_WR_SEND;       // 设置操作码为发送操作
    send_wr.sg_list = &sge;             // 设置发送工作请求的SGE列表指针
    send_wr.num_sge = 1;                // 设置SGE列表中的元素数量为1
    send_wr.send_flags = IBV_SEND_SIGNALED;  // 设置发送标志，表示发送完成时生成完成队列元素（CQE）。
    //send_wr.send_flags = IBV_SEND_SIGNALED | IBV_SEND_INLINE;  // 设置发送标志，包括内联数据,此时无需buffer

    if (ibv_post_send(ctx->qp, &send_wr, &bad_send_wr)) {   //将发送工作请求发布到指定的队列对（QP），以发送消息到远程端。
        perror("Failed to post send request");
        exit(EXIT_FAILURE);
    }

    while (ibv_poll_cq(ctx->cq, 1, &wc) == 0);    //轮询直到至少有一个工作完成。
    if (wc.status == IBV_WC_SUCCESS) {
        std::cout << "Sent response to client" << std::endl;
    } else {
        std::cerr << "Send failed with status " << wc.status << std::endl;
    }

    close(client_fd);
    close(sock_fd);
}

int main() {
    rdma_context ctx;
    init_server(&ctx);

    //清理RDMA资源
    ibv_dereg_mr(ctx.mr);
    free(ctx.buffer);
    ibv_destroy_qp(ctx.qp);
    ibv_destroy_cq(ctx.cq);
    ibv_dealloc_pd(ctx.pd);
    ibv_close_device(ctx.ctx);

    return 0;
}