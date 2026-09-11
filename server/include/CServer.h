#pragma once
#ifndef CSERVER_H
#define CSERVER_H

#include <boost/asio.hpp>
#include "CSession.h"
#include <memory>
#include <map>
#include <mutex>

/*
 * CServer 服务器类
 * 职责：
 *  1. 绑定端口并异步监听（acceptor 跑在主 io_context 上）
 *  2. 每来一个新连接，从 IO 线程池轮询取出一个 io_context 创建 CSession
 *  3. 用 map 管理所有存活会话，连接断开时负责清理
 */
class CServer {
public:
    CServer(boost::asio::io_context& io_context, short port);
    ~CServer();

    void ClearSession(std::string session_id); // 会话断开/异常时从 map 中移除并销毁

private:
    void StartAccept();   // 发起一次异步接受连接
    void HandleAccept(std::shared_ptr<CSession> new_session,
                      const boost::system::error_code& error); // 接受连接完成后的回调

    boost::asio::io_context& _io_context;           // 主IO上下文（acceptor 跑在这里）
    short _port;                                    // 监听端口
    boost::asio::ip::tcp::acceptor _acceptor;       // 异步监听器

    std::map<std::string, std::shared_ptr<CSession>> _sessions; // session_id -> 会话
    std::mutex _mutex; // 保护 _sessions 的线程安全（接受回调在主线程，清理在IO线程）
};

#endif // CSERVER_H
