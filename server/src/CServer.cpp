#include "CServer.h"
#include "AsioIOServicePool.h"
#include <iostream>
#include "ConfigMgr.h"
#include "LogicSystem.h"

using boost::asio::ip::tcp;

CServer::CServer(boost::asio::io_context &io_context, short port)
    : _io_context(io_context), _port(port),
      _acceptor(io_context, tcp::endpoint(tcp::v4(), port)) {
    std::cout << "Server start success, listen on port : " << _port << std::endl;
    StartAccept(); // 构造完成即开始监听
}

CServer::~CServer() {
    std::cout << "Server destruct, listen on port : " << _port << std::endl;
    // 服务器析构时清空会话容器，会话对象由各自所在的IO线程持有引用
    std::lock_guard<std::mutex> lock(_mutex);
    _sessions.clear();
}

// 发起一次异步接受：
// 先从IO线程池轮询取出一个 io_context 创建会话（该会话后续的所有读写都跑在这个IO线程上），
// 再让 acceptor 在主 io_context 上异步接受新连接，socket 交给会话保管。
void CServer::StartAccept() {
    auto &io_context = AsioIOServicePool::GetInstance()->GetIOService();
    std::shared_ptr<CSession> new_session = std::make_shared<CSession>(io_context, this);
    _acceptor.async_accept(new_session->GetSocket(),
        std::bind(&CServer::HandleAccept, this, new_session, std::placeholders::_1));
}

// 接受连接回调：error 为空表示成功接入一个新连接
void CServer::HandleAccept(std::shared_ptr<CSession> new_session,
                           const boost::system::error_code &error) {
    if (!error) {
        // 给新会话绑定一个工作线程
        LogicSystem::GetInstance()->BindSession(new_session->GetSessionId());
        // 让会话开始接收数据（先读包头）
        new_session->Start();
        // 把会话登记到map中，维持其生命周期，防止被提前析构
        std::lock_guard<std::mutex> lock(_mutex);
        _sessions.insert(std::make_pair(new_session->GetSessionId(), new_session));
    } else {
        std::cout << "session accept failed, error is " << error.message() << std::endl;
    }
    // 无论成功失败都继续接受下一个连接（长连接服务器，循环监听）
    StartAccept();
}

// 会话断开/异常时调用：从 map 中移除会话，并把本服务器在线人数减一
void CServer::ClearSession(std::string session_id) {
    int uid = 0;
    {
        std::lock_guard<std::mutex> lock(_mutex);
        auto iter = _sessions.find(session_id);
        if (iter != _sessions.end()) {
            _sessions.erase(iter);
        }
    }
    LogicSystem::GetInstance()->UnbindSession(session_id);
}
