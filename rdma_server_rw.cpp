#include "rdma_common.hpp"

//初始化服务端并开始监听
int init_server() {
    //创建socket
    int sock_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (sock_fd < 0) {
        std::cerr << "Socket creation failed" << std::endl;
        return -1;
    }

    //绑定IP与端口号
    struct sockaddr_in server_addr;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(PORT);
    if (bind(sock_fd, (const sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        std::cerr << "Bind failed" << std::endl;
        close(sock_fd);
        return -1;
    }

    //监听
    if (listen(sock_fd, 5) < 0) {
        std::cerr << "Listen failed" << std::endl;
        close(sock_fd);
        return -1;
    }
    std::cout << "Server is listening on port " << PORT << std::endl;

    return sock_fd;
}


//RDMA传输
int rdma_server_trans_rw(rdma_context* _ctx, int sock_fd) {
    //接受客户端连接
    struct sockaddr_in client_addr;
    socklen_t addr_len = sizeof(client_addr);
    int client_fd = accept(sock_fd, (struct sockaddr *)&client_addr, &addr_len);
    if (client_fd < 0) {
        std::cerr << "Accept failed" << std::endl;
        close(sock_fd);
        return -1;
    }
    std::cout << "Client connected" << std::endl;

    //设置RDMA
    memset(_ctx, 0, sizeof(*_ctx));
    _ctx->ctx = ibv_open_device(ibv_get_device_list(NULL)[0]);
    if (!_ctx->ctx) {
        std::cerr << "Failed to open device" << std::endl;
        close(client_fd);
        return -1;
    }
    _ctx->pd = ibv_alloc_pd(_ctx->ctx);
    if (!_ctx->pd) {
        std::cerr << "Failed to allocate PD" << std::endl;
        close(client_fd);
        ibv_close_device(_ctx->ctx);  // 释放已分配的设备资源
        return -1;
    }
    _ctx->channel = ibv_create_comp_channel(_ctx->ctx);
    if (!_ctx->channel) {
        std::cerr <<"Failed to create completion channel" << std::endl;
        return -1;
    }
    _ctx->cq = ibv_create_cq(_ctx->ctx, 10, NULL, _ctx->channel, 0);
    if (!_ctx->cq) {
        std::cerr << "Failed to create CQ" << std::endl;
        return -1;
    }
    _ctx->buffer = (char *)malloc(BUFFER_SIZE);
    if (!_ctx->buffer) {
        std::cerr << "Failed to allocate buffer" << std::endl;
        return -1;
    }
    _ctx->mr = ibv_reg_mr(_ctx->pd, _ctx->buffer, BUFFER_SIZE, IBV_ACCESS_LOCAL_WRITE | IBV_ACCESS_REMOTE_READ | IBV_ACCESS_REMOTE_WRITE);
    if (!_ctx->mr) {
        std::cerr << "Failed to register MR" << std::endl;
        return -1;
    }

    // 初始化qp
    struct ibv_qp_init_attr qp_attr;
    memset(&qp_attr, 0, sizeof(qp_attr));    //将结构体 qp_attr 清零
    qp_attr.send_cq = _ctx->cq;
    qp_attr.recv_cq = _ctx->cq;
    qp_attr.qp_type = IBV_QPT_RC;
    qp_attr.cap.max_send_wr = 10;
    qp_attr.cap.max_recv_wr = 10;
    qp_attr.cap.max_send_sge = 1;       //发送队列最大SGE（散播-聚集元素）数
    qp_attr.cap.max_recv_sge = 1;           
    _ctx->qp = ibv_create_qp(_ctx->pd, &qp_attr);
    if (!_ctx->qp) {
        std::cerr << "Failed to create QP" << std::endl;
        return -1;
    }

    // 修改队列属性
    struct ibv_qp_attr mod_attr;
    memset(&mod_attr, 0, sizeof(mod_attr));  //将结构体 mod_attr 清零
    mod_attr.qp_state = IBV_QPS_INIT;        //设置队列对状态为初始化（INIT）
    mod_attr.pkey_index = 0;                 //设置PKey索引
    mod_attr.port_num = 1;                   //设置端口号
    mod_attr.qp_access_flags = IBV_ACCESS_LOCAL_WRITE | IBV_ACCESS_REMOTE_READ | IBV_ACCESS_REMOTE_WRITE; //对访问权限为本地写、远程读、远程写
 
    if (ibv_modify_qp(_ctx->qp, &mod_attr, IBV_QP_STATE | IBV_QP_PKEY_INDEX | IBV_QP_PORT | IBV_QP_ACCESS_FLAGS)) {
        std::cerr << "Failed to modify QP to INIT" << std::endl;
        return -1;
    }

    //获取LID
    struct ibv_port_attr port_attr;
    if (ibv_query_port(_ctx->ctx, 1, &port_attr)) {
        std::cerr << "Failed to query port" << std::endl;
        return -1;
    }
    //获取GID
    union ibv_gid gid;
    if (ibv_query_gid(_ctx->ctx, 1, 0, &gid)) {
        std::cerr << "Failed to query GID" << std::endl;
        return -1;
    }
    //准备QP信息
    qp_info local_qp_info, remote_qp_info;
    local_qp_info.qp_num = _ctx->qp->qp_num;
    local_qp_info.lid = port_attr.lid;
    memcpy(local_qp_info.gid, &gid, sizeof(gid));
    local_qp_info.rkey = _ctx->mr->rkey;
    local_qp_info.addr = (uintptr_t)_ctx->buffer;
    // 交换QP信息
    if (exchange_qp_info(client_fd, &local_qp_info, &remote_qp_info) < 0) {
        std::cerr << "Failed to exchange QP info" << std::endl;
        return -1;
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
    if (ibv_modify_qp(_ctx->qp, &mod_attr, IBV_QP_STATE | IBV_QP_AV | IBV_QP_PATH_MTU | IBV_QP_DEST_QPN | IBV_QP_RQ_PSN | IBV_QP_MAX_DEST_RD_ATOMIC | IBV_QP_MIN_RNR_TIMER)) {
        std::cerr << "Failed to modify QP to RTR" << std::endl;
        return -1;
    }

    // 修改QP状态为RTS
    memset(&mod_attr, 0, sizeof(mod_attr));
    mod_attr.qp_state = IBV_QPS_RTS;
    mod_attr.timeout = 14;        // 超时值
    mod_attr.retry_cnt = 7;       // 重试计数器
    mod_attr.rnr_retry = 7;       // 接收不可用重试计数器
    mod_attr.sq_psn = 0;          // 发送队列的初始PSN
    mod_attr.max_rd_atomic = 1;   // 最大发起的原子操作请求数
    if (ibv_modify_qp(_ctx->qp, &mod_attr, IBV_QP_STATE | IBV_QP_TIMEOUT | IBV_QP_RETRY_CNT | IBV_QP_RNR_RETRY | IBV_QP_SQ_PSN | IBV_QP_MAX_QP_RD_ATOMIC)) {
        std::cerr << "Failed to modify QP to RTS" << std::endl;
        return -1;
    }

    // 准备接收消息
    // 定义并初始化ibv_sge结构体
    struct ibv_sge sge;
    sge.addr = (uintptr_t)_ctx->buffer;      // 设置SGE的地址为RDMA内存缓冲区的地址
    sge.length = BUFFER_SIZE;
    sge.lkey = _ctx->mr->lkey;               // 设置SGE的本地键（lkey），用于内存区域的访问

    struct ibv_recv_wr recv_wr, *bad_recv_wr; //bad_recv_wr是用于接收可能发生错误的工作请求的指针
    memset(&recv_wr, 0, sizeof(recv_wr));
    recv_wr.sg_list = &sge;                 // 设置接收工作请求的SGE列表指针
    recv_wr.num_sge = 1;

    if (ibv_post_recv(_ctx->qp, &recv_wr, &bad_recv_wr)) {   // 将接收工作请求发布到队列对
        std::cerr << "Failed to post receive request" << std::endl;
        return -1;
    }  

    // RDMA读操作
    memset(_ctx->buffer, 0, BUFFER_SIZE);     // 清空缓冲区
    struct ibv_send_wr wr, *bad_wr;    
    memset(&wr, 0, sizeof(wr));
    wr.opcode = IBV_WR_RDMA_READ;
    wr.sg_list = &sge;
    wr.num_sge = 1;
    wr.wr.rdma.remote_addr = remote_qp_info.addr; // 远程地址
    wr.wr.rdma.rkey = remote_qp_info.rkey;        // 远程键
    wr.send_flags = IBV_SEND_SIGNALED;

    if (ibv_post_send(_ctx->qp, &wr, &bad_wr)) {   //将发送工作请求发布到指定的队列对（QP），以发送消息到远程端。
        std::cerr << "Failed to post send request" << std::endl;
        return -1;
    }

    //轮询直到至少有一个工作完成。
    struct ibv_wc wc;
    while (ibv_poll_cq(_ctx->cq, 1, &wc) == 0);
    if (wc.status == IBV_WC_SUCCESS) {
        std::cout << "RDMA Read completed, received: " << _ctx->buffer << std::endl;
    } else {
        std::cerr << "RDMA Read failed with status " << wc.status << std::endl;
    }

    // RDMA写
    strcpy(_ctx->buffer, "Hello from server");
    wr.opcode = IBV_WR_RDMA_WRITE;

    if (ibv_post_send(_ctx->qp, &wr, &bad_wr)) {
        std::cerr << "Failed to post RDMA write request" << std::endl;
        return -1;
    }

    //等待完成
    while (ibv_poll_cq(_ctx->cq, 1, &wc) < 1);
    if (wc.status == IBV_WC_SUCCESS) {
        std::cout << "RDMA Write completed" << std::endl;
    } else {
        std::cerr << "RDMA Write failed with status " << wc.status << std::endl;
    }

    close(client_fd);
    return 0;
}


int main() {
    int server_fd = init_server();
    if (server_fd < 0) {
        return -1;
    }
    struct rdma_context ctx;
    if (rdma_server_trans_rw(&ctx, server_fd) < 0) {
        std::cerr << "RDMA transaction failed" << std::endl;
        return -1;
    }
    close(server_fd);

    //清理RDMA资源
    ibv_dereg_mr(ctx.mr);
    free(ctx.buffer);
    ibv_destroy_qp(ctx.qp);
    ibv_destroy_cq(ctx.cq);
    ibv_dealloc_pd(ctx.pd);
    ibv_close_device(ctx.ctx);

    return 0;
}