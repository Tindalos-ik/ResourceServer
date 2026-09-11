#pragma once
#ifndef LOGICSYSTEM_H
#define LOGICSYSTEM_H

#include "singleton.h"
#include "CSession.h"
#include "const.h"
#include <queue>
#include <map>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <functional>
#include <memory>

class CServer;
typedef std::function<void(std::shared_ptr<CSession>, const short &msg_id, const std::string &msg_data)> FunCallBack;

/*
 * LogicSystem 逻辑层（单例）
 * 会话层解析完一条消息后调用 PostMsgToQue 投递到队列，
 * 本类的 worker 线程从队列取出消息，按消息id分发到对应的处理函数。
 * 网络层与业务逻辑通过队列解耦，避免在IO线程里做耗时操作。
 */
class LogicSystem : public Singleton<LogicSystem> {
    friend class Singleton<LogicSystem>;
public:
    ~LogicSystem();

    void PostMsgToQue(std::shared_ptr<LogicNode> msg); // 会话层投递消息到队列
    void SetServer(std::shared_ptr<CServer> pserver);  // 保存服务器指针（后续多会话操作使用）

private:
    LogicSystem();
    void DealMsg();              // 工作线程：循环消费消息队列
    void RegisterCallBacks();    // 注册 消息id -> 处理函数 的映射

    void HandleTestMsg(std::shared_ptr<CSession> session, const short &msg_id, const std::string &msg_data); // 处理测试消息

    std::thread _worker_thread;                    // 消费队列的工作线程
    std::queue<std::shared_ptr<LogicNode>> _msg_que; // 消息队列
    std::mutex _mutex;                             // 保护消息队列
    std::condition_variable _consume;              // 队列非空时唤醒工作线程
    bool _b_stop;                                  // 停止标志
    std::map<short, FunCallBack> _fun_callbacks;   // 消息id -> 处理函数
    std::shared_ptr<CServer> _p_server;            // 服务器指针
};

#endif // LOGICSYSTEM_H
