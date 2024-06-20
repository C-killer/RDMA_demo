#ifndef RDMA_COMMON_HPP
#define RDMA_COMMON_HPP


#include <infiniband/verbs.h>   // 用于RDMA的核心头文件
#include <netinet/in.h>         // 用于定义sockaddr_in结构体
#include <sys/socket.h>         // 用于socket函数和相关网络编程API
#include <arpa/inet.h>          // 用于inet_addr函数
#include <iostream>             // 用于标准输入输出
#include <cstring>              // 用于memset函数
#include <cstdlib>              // 用于exit函数
#include <unistd.h>             // 用于close函数

#define PORT 8888
#define BUFFER_SIZE 1024

struct rdma_context
{
    struct ibv_pd *pd;
    struct ibv_mr *mr;
    struct ibv_cq *cq;
    struct ibv_qp *qp;
    struct ibv_context *ctx;      // 硬件上下文
    struct ibv_comp_channel *channel;  //完成事件通道
    char *buffer;                 // 数据缓冲区
};

struct qp_info {
    uint32_t qp_num;     //QPN
    uint16_t lid;         
    uint8_t gid[16];
};


int exchange_qp_info(int sock_fd, qp_info* local_info, qp_info* remote_info) {
    // ssize_t send(int sockfd, const void *buf, size_t len, int flags);
    // 套接字文件描述符，用于标识要发送数据的目标套接字
    // 指向要发送的数据缓冲区
    // 要发送的数据的长度（字节数）
    // 发送标志，通常为 0
    // 成功时：返回发送的字节数。 失败时：返回 -1，并设置 errno 以指示错误。
    if (send(sock_fd, local_info, sizeof(*local_info), 0) != sizeof(*local_info)) {
        perror("Failed to send local QP info");
        return -1;
    }
    // 接收远程QP信息
    if (recv(sock_fd, remote_info, sizeof(*remote_info), 0) != sizeof(*remote_info)) {
        perror("Failed to receive remote QP info");
        return -1;
    }
    return 0;
}


#endif  // RDMA_COMMON.HPP


/*
    g++ -o rdma_server rdma_server.cpp -libverbs
    g++ -o rdma_client rdma_client.cpp -libverbs
*/