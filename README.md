# RDMA_demo
RDMA的小demo
记录了Linux下用C++实现RDMA技术的过程以及相关函数的运用与讲解

# RDMA 下 SEND 和 RECV 的相关用法

在RDMA编程中，SEND和RECV是两种常见的通信操作。SEND用于发送数据，RECV用于接收数据。以下是这两种操作的相关用法：

## 发送 (SEND) 操作

### 基本要素

- **操作码 (Opcode)**：IBV_WR_SEND
- **发送标志 (Send Flags)**：常用的标志有IBV_SEND_SIGNALED（生成完成通知）和IBV_SEND_INLINE（内联数据）。
- **散列表项 (SGL)**：指定发送缓冲区的地址、长度和内存密钥。
- **发送请求 (Send WR)**：使用 ibv_send_wr 结构体配置发送操作，并通过ibv_post_send函数将请求发送到QP。

### 具体流程

1. **准备发送缓冲区**：将要发送的消息复制到发送缓冲区。
2. **设置散列表项 (SGL)**：配置 ibv_sge 结构体，指定发送数据缓冲区的地址、长度和内存密钥。
3. **设置发送请求 (WR)**：初始化并配置 ibv_send_wr 结构体，设置操作码、SGL 和发送标志。
4. **发送请求**：使用 ibv_post_send 函数将发送请求提交到队列对列 (QP)。
5. **轮询完成队列**：通过 ibv_poll_cq 轮询完成队列 (CQ)，等待发送操作完成。

## 接收 (RECV) 操作

### 基本要素

- **接收请求 (Recv WR)**：使用 ibv_recv_wr 结构体配置接收操作，并通过ibv_post_recv函数将请求发送到QP。
- **散列表项 (SGL)**：指定接收缓冲区的地址、长度和内存密钥。
- **完成队列 (CQ)**：通过 ibv_poll_cq 函数轮询CQ，检查操作是否完成。
- **完成元素 (CQE)**：当操作完成时，CQ中会生成一个CQE，其中包含操作的状态信息。

### 具体流程

1. **设置散列表项 (SGL)**：配置 ibv_sge 结构体，指定发送数据缓冲区的地址、长度和内存密钥。（通常 SEND 过程已经设置）
2. **设置接收请求 (WR)**：初始化并配置 ibv_recv_wr 结构体，指定 SGL。
3. **接收请求**：使用 ibv_post_recv 函数将接收请求提交到队列对列 (QP)。
4. **轮询完成队列**：通过 ibv_poll_cq 轮询完成队列 (CQ)，等待接收操作完成。

## 关闭套接字

关闭客户端和服务端套接字。

## 总结

首先通过 RECV 操作等待并接收来自客户端的消息，然后通过SEND操作发送响应消息给客户端。通过对CQ的轮询，服务器可以确认每个操作的完成状态，从而确保数据传输的可靠性和准确性。

## 散列表项 (SGL)

在RDMA编程中，散列表项 (Scatter/Gather List, SGL) 是一个重要的概念，用于描述一组数据缓冲区。这些缓冲区将用于数据传输操作，如发送 (SEND) 或接收 (RECV)。

### SGL 结构体

SGL 由一系列的 ibv_sge 结构体组成，每个结构体描述一个数据缓冲区。ibv_sge 结构体的定义如下：
SGL 被包含在发送和接收工作请求中，以指示哪些数据缓冲区参与了数据传输。例如，在发送操作中，SGL 指定了发送的数据缓冲区，而在接收操作中，SGL 指定了接收的数据缓冲区。

## IBV_SEND_INLINE（内联数据）

IBV_SEND_INLINE 是 RDMA 编程中的一个发送标志，用于指示发送的数据将被直接内联到发送请求中，而不是通过引用外部缓冲区。这种方式可以减少延迟，因为它避免了对数据缓冲区的额外处理。

- **低延迟**：由于数据直接内联到发送请求中，无需额外的内存复制或缓冲区管理，减少了延迟。
- **简单高效**：对于小数据包，内联数据可以简化发送操作，提供更高的效率。

## IBV_WR_SEND_WITH_IMM

IBV_WR_SEND_WITH_IMM 是 RDMA 编程中的一种发送操作类型，它与普通的 IBV_WR_SEND 操作类似，但附带一个即时 (Immediate) 数据。即时数据是一种特殊的32位数据，在发送操作完成后，这些数据会被传递到接收端，并可以立即被接收端处理。

### 使用场景

- **信号量或通知机制**
  - **控制信号**：在分布式系统中，IBV_WR_SEND_WITH_IMM 可用于发送控制信号，通知接收端某些状态的改变。例如，可以在发送数据块后附加一个信号，表示某个特定操作已经完成。
  - **同步机制**：用于多线程或多进程环境中的同步机制。例如，当一个线程完成某个数据传输任务后，可以使用即时数据通知其他线程。
- **附加元数据传递**
  - **数据描述信息**：可以在传输数据时附加一些描述信息，例如数据的类型、长度、校验和等，接收端可以根据这些信息进行相应的处理。
  - **数据分片信息**：在传输大数据块时，可以使用即时数据传递数据分片的序号，帮助接收端正确地重组数据。
- **应用层协议实现**
  - **协议标志**：在自定义的应用层协议中，可以使用即时数据作为协议标志，指示不同类型的消息。例如，使用不同的即时数据值来表示不同的消息类型或请求类型。
  - **状态信息**：传递当前的状态信息或会话信息。例如，在文件传输协议中，可以使用即时数据传递当前的传输进度或状态。
- **高性能计算和存储系统**
  - **数据传输控制**：在高性能计算和存储系统中，IBV_WR_SEND_WITH_IMM 可以用于控制复杂的数据传输过程。例如，在分布式存储系统中，可以用即时数据传递写操作的确认信息。
  - **内存操作管理**：用于远程内存操作的管理和控制。例如，在进行远程直接内存访问（RDMA）操作时，可以用即时数据传递内存操作的元数据。

# RDMA 下 READ 和 WRITE 的相关用法

在RDMA编程中，RDMA_READ 和 RDMA_WRITE 是两种常见的远程内存访问操作。RDMA_READ 用于从远程节点读取数据，RDMA_WRITE 用于将数据写入远程节点。以下是这两种操作的相关用法：

## 远程内存写 (RDMA_WRITE) 操作

### 基本要素

- **操作码 (Opcode)**：IBV_WR_RDMA_WRITE
- **发送标志 (Send Flags)**：常用的标志有 IBV_SEND_SIGNALED（生成完成通知）。
- **散列表项 (SGL)**：指定发送缓冲区的地址、长度和内存密钥。
- **发送请求 (Send WR)**：使用 ibv_send_wr 结构体配置写操作，并通过 ibv_post_send 函数将请求发送到 QP。

### 具体流程

1. **准备发送缓冲区**：将要写入的消息复制到发送缓冲区。
2. **设置散列表项 (SGL)**：配置 ibv_sge 结构体，指定发送数据缓冲区的地址、长度和内存密钥。
3. **设置发送请求 (WR)**：初始化并配置 ibv_send_wr 结构体，设置操作码、SGL 和发送标志。
4. **发送请求**：使用 ibv_post_send 函数将写请求提交到队列对列 (QP)。
5. **轮询完成队列**：通过 ibv_poll_cq 轮询完成队列 (CQ)，等待写操作完成。

## 远程内存读 (RDMA_READ) 操作

### 基本要素

- **操作码 (Opcode)**：IBV_WR_RDMA_READ
- **发送标志 (Send Flags)**：常用的标志有 IBV_SEND_SIGNALED（生成完成通知）。
- **散列表项 (SGL)**：指定接收缓冲区的地址、长度和内存密钥。
- **发送请求 (Send WR)**：使用 ibv_send_wr 结构体配置读操作，并通过 ibv_post_send 函数将请求发送到 QP。

### 具体流程

1. **设置散列表项 (SGL)**：配置 ibv_sge 结构体，指定接收数据缓冲区的地址、长度和内存密钥。
2. **设置发送请求 (WR)**：初始化并配置 ibv_send_wr 结构体，设置操作码、SGL 和发送标志。
3. **发送请求**：使用 ibv_post_send 函数将读请求提交到队列对列 (QP)。
4. **轮询完成队列**：通过 ibv_poll_cq 轮询完成队列 (CQ)，等待读操作完成。

## 总结

首先通过 RECV 操作等待并接收来自客户端的消息，然后通过 RDMA_WRITE 操作发送响应消息给客户端。通过对 CQ 的轮询，服务器可以确认每个操作的完成状态，从而确保数据传输的可靠性和准确性。

# RDMA 中的特殊操作码解释

## 1. IBV_WR_RDMA_WRITE_WITH_IMM

### 用途

IBV_WR_RDMA_WRITE_WITH_IMM 是 RDMA 写操作的一种变体，除了执行普通的 RDMA 写操作外，它还发送一个立即数据（immediate data）。这种操作在目标端的接收完成队列中会生成一个完成队列元素（CQE），其中包含立即数据。该立即数据可以用于携带额外的信号信息或控制数据。

```cpp
struct ibv_send_wr wr, *bad_wr;
memset(&wr, 0, sizeof(wr));
wr.wr_id = 0;
wr.opcode = IBV_WR_RDMA_WRITE_WITH_IMM;
wr.sg_list = &sge;
wr.num_sge = 1;
wr.send_flags = IBV_SEND_SIGNALED;
wr.wr.rdma.remote_addr = remote_addr;  // 远程地址
wr.wr.rdma.rkey = remote_rkey;         // 远程键
wr.imm_data = htonl(12345);            // 设置立即数据
```

## 2. IBV_WR_SEND_WITH_INV

### 用途

IBV_WR_SEND_WITH_INV 操作用于发送数据并同时无效化远程内存区域的注册。该操作可以用于通知远程节点特定的内存区域已经不再有效，确保接收方不会再对该区域执行访问。

```cpp
struct ibv_send_wr wr, *bad_wr;
memset(&wr, 0, sizeof(wr));
wr.wr_id = 0;
wr.opcode = IBV_WR_SEND_WITH_INV;
wr.sg_list = &sge;
wr.num_sge = 1;
wr.send_flags = IBV_SEND_SIGNALED;
wr.invalidate_rkey = remote_rkey;  // 设置要无效化的远程键
```

## 3. IBV_WR_ATOMIC_WRITE

### 用途

IBV_WR_ATOMIC_WRITE 操作用于执行远程的原子写操作。原子操作确保对共享数据的访问和修改是同步且一致的，这在需要多节点协调更新单一数据项时非常重要。

```cpp
struct ibv_send_wr wr, *bad_wr;
memset(&wr, 0, sizeof(wr));
wr.wr_id = 0;
wr.opcode = IBV_WR_ATOMIC_WRITE;
wr.send_flags = IBV_SEND_SIGNALED;
wr.wr.atomic.remote_addr = remote_addr;  // 远程地址
wr.wr.atomic.rkey = remote_rkey;         // 远程键
wr.wr.atomic.compare_add = 1;            // 比较值, 用于CAS
wr.wr.atomic.swap = new_value;           // 要写入的值
```
