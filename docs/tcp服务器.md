# TCP 服务器：从连接到业务回包

本文对应当前 `server` 目录中的实现，重点解释下面五个类如何协作：

- `CServer`：监听端口、接受连接、管理会话。
- `CSession`：代表一条 TCP 连接，负责拆包、收包和顺序发包。
- `AsioIOServicePool`：将不同连接的 IO 事件轮询分到多个线程。
- `LogicSystem`：用一个业务队列把网络 IO 和业务处理解耦。
- `MsgNode`：保存一条待收或待发的完整协议消息。

## 1. 总体结构

```text
                         主线程：io_context.run()
                                      │
客户端 ── TCP 连接 ──> CServer / acceptor
                                      │  轮询选择一个 io_context
                                      ▼
                    AsioIOServicePool 的某个 IO 线程
                                      │
                                      ▼
                            CSession：读包头、读包体
                                      │
                           LogicNode(session, recvNode)
                                      │
                                      ▼
                    LogicSystem 的消息队列与业务工作线程
                                      │
                          按 msg_id 调用处理函数
                                      │
                                      ▼
                           session->Send(回包)
                                      │
                                      ▼
                     CSession 发送队列 + async_write
```

这里的线程职责很明确：

| 位置 | 运行内容 | 不应该做什么 |
| --- | --- | --- |
| 主 `io_context` | `accept` 新连接、响应退出信号 | 处理耗时业务 |
| IO 线程池 | 某个连接的异步读写回调 | JSON 解析、磁盘写文件等慢操作 |
| `LogicSystem` 工作线程 | 分发消息、处理上传等业务 | 阻塞等待网络数据 |

`LogicSystem` 当前只有一个业务线程，因此它已经把业务从 IO 线程移开，但还不是多业务线程池。

## 2. 协议：约定边界，解决粘包和半包

当前协议定义在 `server/include/const.h`：

```cpp
#define HEAD_TOTAL_LEN 6
#define HEAD_ID_LEN 2
#define HEAD_DATA_LEN 4
#define MAX_LENGTH 1024 * 4
```

一条消息在网络上的布局是：

```text
┌──────────────────┬──────────────────────┬──────────────────┐
│ 消息 ID，2 字节   │ 消息体长度，4 字节    │ 消息体，n 字节    │
└──────────────────┴──────────────────────┴──────────────────┘
         大端序                 大端序
```

`MsgNode::SendNode` 在构造时完成打包：

```cpp
short msg_id_network = boost::asio::detail::socket_ops::host_to_network_short(msg_id);
std::uint32_t len_network = boost::asio::detail::socket_ops::host_to_network_long(max_len);

memcpy(_data, &msg_id_network, HEAD_ID_LEN);
memcpy(_data + HEAD_ID_LEN, &len_network, HEAD_DATA_LEN);
memcpy(_data + HEAD_TOTAL_LEN, msg, max_len);
```

虽然长度字段有 4 字节、理论上可表达更大数字，但当前 `CSession::ReadHead` 会拒绝 `msg_len > MAX_LENGTH` 的消息。因此**当前单个 TCP 业务包的最大消息体是 4096 字节，而不是 4 GB**。文件上传通过客户端分片来适应这个限制。

TCP 是字节流，不保留应用层消息边界：一次 `read_some` 可能只读到包头的一部分，也可能读到多条消息的一部分。因此不能把“一次 read”误认为“一条业务消息”。本项目通过“固定长度包头 + 长度字段 + 读满循环”恢复边界。

## 3. 服务启动：`ResourceServer.cpp`

服务启动流程如下：

```cpp
auto& config = ConfigMgr::Inst();
auto pool = AsioIOServicePool::GetInstance();

boost::asio::io_context io_context;
boost::asio::signal_set signals(io_context, SIGINT, SIGTERM);
signals.async_wait([&io_context, pool](auto, auto) {
    io_context.stop();
    pool->Stop();
});

CServer server(io_context, std::stoi(config["ResourceServer"]["port"]));
io_context.run();
```

这里有两类 `io_context`：

1. `ResourceServer.cpp` 中的主 `io_context`：只承载 `CServer` 的监听器和退出信号。
2. `AsioIOServicePool` 内部的多个 `io_context`：每个对应一个线程，用于具体 `CSession` 的收发。

信号到来后先停止主 `io_context`，再调用 `pool->Stop()` 停止 IO 池并 `join` 工作线程，避免进程直接退出时留下运行中的线程。

## 4. IO 线程池：`AsioIOServicePool`

构造函数按照 `std::thread::hardware_concurrency()` 创建多个 `io_context`。每个 `io_context` 有一个 `work_guard` 和一个对应线程：

```cpp
_workGuards.emplace_back(boost::asio::make_work_guard(_ioServices[i]));
_threads.emplace_back([this, i]() {
    _ioServices[i].run();
});
```

`work_guard` 的作用是：即使暂时没有 IO 事件，`run()` 也不要立刻返回。否则线程池会在服务刚启动、还没有客户端连接时就结束。

### 4.1 轮询分配

`GetIOService()` 使用原子下标轮询选择 IO 上下文：

```cpp
boost::asio::io_context& AsioIOServicePool::GetIOService() {
    auto& service = _ioServices[_nextIOService++ % _ioServices.size()];
    return service;
}
```

若池中有 3 个 IO 线程，新连接的分配顺序是：

```text
连接 A → IO 0
连接 B → IO 1
连接 C → IO 2
连接 D → IO 0
```

轮询分配的是**连接**，不是单个文件分片。这样同一 `CSession` 创建出的 socket、读回调和写回调都在同一个 `io_context` 上执行，模型更简单，也有利于保证单条 TCP 连接的处理顺序。

## 5. 接受连接与会话管理：`CServer`

`CServer` 的 `acceptor` 属于主 `io_context`。每次准备接受新连接时，先从 IO 池取出一个上下文来构造会话：

```cpp
void CServer::StartAccept() {
    auto& io_context = AsioIOServicePool::GetInstance()->GetIOService();
    auto new_session = std::make_shared<CSession>(io_context, this);

    _acceptor.async_accept(new_session->GetSocket(),
        std::bind(&CServer::HandleAccept, this, new_session,
                  std::placeholders::_1));
}
```

`new_session` 被绑定到回调中，因此异步 `accept` 尚未完成时对象也不会提前析构。接受成功后：

```cpp
void CServer::HandleAccept(std::shared_ptr<CSession> new_session,
                           const boost::system::error_code& error) {
    if (!error) {
        new_session->Start();
        std::lock_guard<std::mutex> lock(_mutex);
        _sessions.insert({new_session->GetSessionId(), new_session});
    }
    StartAccept();
}
```

两个重点：

- `Start()` 启动这一连接的收包状态机。
- 无论本次接受成功还是失败，都要再次调用 `StartAccept()`；异步接受只接受一次，不重新注册就不会再接收后续连接。

`_sessions` 以随机 session id 保存 `shared_ptr<CSession>`，既可管理在线会话，也延长会话生命周期。读写失败时 `CSession` 会调用 `CServer::ClearSession()` 删除对应项目。由于接受回调和关闭回调可能运行在不同线程，访问 map 使用 `_mutex` 保护。

## 6. `CSession`：一条连接的收包状态机

每个 `CSession` 只代表一个 socket。它有两个连续阶段：

```text
ReadHead(6)
   │  解析 msg_id 和 msg_len
   ▼
ReadBody(msg_len)
   │  投递 LogicNode
   └───────────────> ReadHead(6)，等待下一条消息
```

### 6.1 读满包头和包体

`Start()` 从固定长度包头开始：

```cpp
void CSession::Start() {
    ReadHead(HEAD_TOTAL_LEN);
}
```

`ReadHead()` 读取 6 字节，使用网络序转主机序，然后验证 id 和长度：

```cpp
short msg_id = 0;
memcpy(&msg_id, _recv_head_node->_data, HEAD_ID_LEN);
msg_id = boost::asio::detail::socket_ops::network_to_host_short(msg_id);

std::uint32_t msg_len = 0;
memcpy(&msg_len, _recv_head_node->_data + HEAD_ID_LEN, HEAD_DATA_LEN);
msg_len = boost::asio::detail::socket_ops::network_to_host_long(msg_len);

if (msg_id <= 0 || msg_id > MAX_LENGTH ||
    msg_len <= 0 || msg_len > MAX_LENGTH) {
    Close();
    _server->ClearSession(_session_id);
    return;
}
```

长度检查是必要的防御：如果直接信任客户端给出的长度并分配内存，恶意客户端可以构造很大的长度导致内存耗尽。这里还顺带限制了非法消息 ID。

### 6.2 为什么 `asyncReadLen` 使用递归

底层使用的是 `async_read_some`，它只保证“读到一些数据”，不保证读满请求长度。`asyncReadLen` 因此记录已读长度；如果仍未读满，就在回调里继续发起下一次异步读取：

```cpp
void CSession::asyncReadLen(
    std::size_t read_len, std::size_t total_len,
    std::function<void(const boost::system::error_code&, std::size_t)> handler) {
    _socket.async_read_some(
        boost::asio::buffer(_data + read_len, total_len - read_len),
        [read_len, total_len, handler, self](auto ec, std::size_t transferred) {
            if (ec) {
                handler(ec, read_len + transferred);
                return;
            }
            if (read_len + transferred >= total_len) {
                handler(ec, read_len + transferred);
                return;
            }
            self->asyncReadLen(read_len + transferred, total_len, handler);
        });
}
```

这是一种**异步递归续读**：本次函数在注册 `async_read_some` 后已经返回，下一次调用发生在未来的 IO 回调中，因此不会像普通同步递归那样不断压深当前调用栈。它解决的是 TCP 半包问题，不是把业务任务递归切分到多个线程。

读满包体后，`ReadBody()` 会将数据封装为：

```cpp
LogicSystem::GetInstance()->PostMsgToQue(
    std::make_shared<LogicNode>(shared_from_this(), _recv_msg_node));
```

随后立刻调用 `ReadHead(HEAD_TOTAL_LEN)` 等待下一条包。业务尚未处理完也不妨碍 IO 线程继续接收数据；两层之间由消息队列隔离。

### 6.3 `shared_from_this()` 为什么重要

异步操作在函数返回后才完成。如果回调只保存裸 `this`，会话可能先被 `ClearSession()` 删除，回调触发时就会访问悬空对象。

所以 `CSession` 继承 `std::enable_shared_from_this<CSession>`，在读写回调中持有 `shared_ptr`：

```cpp
auto self = shared_from_this();
// 回调捕获 self，直到异步操作结束。
```

这样只要读写尚未完成，`CSession` 就仍存活。

## 7. `MsgNode`：收发消息的内存载体

`MsgNode` 管理一块动态字符数组，保存长度、当前读写位置和数据指针：

```cpp
class MsgNode {
public:
    short _cur_len;
    short _total_len;
    char* _data;
};
```

它有两个子类：

| 类型 | 用途 | 保存内容 |
| --- | --- | --- |
| `RecvNode` | 接收一条包体 | `msg_id` 和包体数据 |
| `SendNode` | 发送一条消息 | 已打包的 `[id][length][body]` |

`SendNode` 在进入发送队列前就完成协议打包，因此 `async_write` 能直接发送一整段连续内存；`RecvNode` 则让 `LogicSystem` 能同时拿到消息来源会话和对应的消息 ID。

## 8. 业务线程：`LogicSystem`

`LogicSystem` 是单例。构造时注册 `msg_id -> 处理函数` 的映射，并启动一个工作线程：

```cpp
LogicSystem::LogicSystem() : _b_stop(false), _p_server(nullptr) {
    RegisterCallBacks();
    _worker_thread = std::thread(&LogicSystem::DealMsg, this);
}
```

会话层投递数据时，仅做入队和唤醒：

```cpp
void LogicSystem::PostMsgToQue(std::shared_ptr<LogicNode> msg) {
    std::unique_lock<std::mutex> lock(_mutex);
    _msg_que.push(msg);
    if (_msg_que.size() == 1) {
        lock.unlock();
        _consume.notify_one();
    }
}
```

工作线程等待条件变量、取出队首任务、按 ID 分发：

```cpp
while (_msg_que.empty() && !_b_stop) {
    _consume.wait(lock);
}

auto msg_node = _msg_que.front();
auto iter = _fun_callbacks.find(msg_node->_recvnode->_msg_id);
if (iter != _fun_callbacks.end()) {
    iter->second(msg_node->_session, msg_node->_recvnode->_msg_id,
                 std::string(msg_node->_recvnode->_data,
                             msg_node->_recvnode->_cur_len));
}
_msg_que.pop();
```

现在已注册的处理逻辑包括：

```cpp
_fun_callbacks[ID_TEST_MSG_REQ] = ... HandleTestMsg(...);
_fun_callbacks[ID_UPLOAD_FILE_REQ] = ... HandleUploadFile(...);
```

这种写法的意义是：IO 线程只负责尽快收完整报文，不等待 JSON 解析、Base64 解码或磁盘写入完成。业务函数执行结束后可用原来的 `session` 发送回包。

## 9. 回包为什么不会乱序：`CSession::Send`

多个地方可能同时调用同一个会话的 `Send()`：例如业务线程处理请求后回包，其他业务通知也可能到来。直接对同一 socket 同时 `async_write` 会造成缓冲区生命周期和消息顺序问题。

当前代码用 `_send_que` 和 `_send_lock` 串行化发送：

```cpp
std::lock_guard<std::mutex> lock(_send_lock);
std::size_t send_que_size = _send_que.size();
_send_que.push(std::make_shared<SendNode>(...));

if (send_que_size > 0) {
    return; // 前面已有 async_write 在进行
}

auto& node = _send_que.front();
boost::asio::async_write(_socket, boost::asio::buffer(node->_data, node->_total_len), ...);
```

写完成后 `HandleWrite()` 弹出队首；如果队列不为空，才启动下一次写。这保证同一 `CSession` 的回包顺序与发送队列顺序一致，并且队列中的 `shared_ptr<SendNode>` 确保异步写期间缓冲区不被释放。

## 10. 上传请求走一遍

以 `ID_UPLOAD_FILE_REQ` 为例：

1. 客户端将一个文件分片 Base64 编码，并组装 `name`、`seq`、`trans_size`、`total_size`、`data` 等 JSON 字段。
2. 客户端将 JSON 放进协议包体，前面附加 2 字节 ID 和 4 字节长度。
3. `CSession` 读满包头，得知 ID 为 `1003`，再读满 JSON 包体。
4. `CSession` 创建 `LogicNode(session, recvNode)`，投递给 `LogicSystem`。
5. `LogicSystem` 找到 `ID_UPLOAD_FILE_REQ` 对应的 `HandleUploadFile`。
6. 业务函数解析 JSON、Base64 解码，用 `std::filesystem::u8path` 处理 UTF-8 中文文件名，再以二进制方式写入配置目录。
7. 函数结束时通过 `Defer` 生成 `ID_UPLOAD_FILE_RSP` 回包，其中带有服务端确认的 `trans_size` 和 `total_size`。
8. `session->Send()` 将回包排入该连接的发送队列；客户端据此更新进度条。

这里进度条使用服务端回包的 `trans_size`，而不是客户端“已调用 write 的字节数”，所以代表的是服务端确认落盘的进度。

## 11. 总结

`CServer` 接连接，`AsioIOServicePool` 分 IO 线程，`CSession` 把 TCP 字节流还原成完整协议包，`LogicSystem` 在业务线程按消息 ID 执行处理函数，`MsgNode` 负责保存收发数据；处理结果再经 `CSession` 的发送队列有序回到客户端。
