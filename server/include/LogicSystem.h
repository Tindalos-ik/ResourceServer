#pragma once
#ifndef LOGICSYSTEM_H
#define LOGICSYSTEM_H

#include "singleton.h"
#include "CSession.h"
#include "const.h"
#include <map>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <memory>
#include "LogicWorker.h"

#define LOGIC_WORKER_COUNT 4

class CServer;
class LogicWorker;
/*
 * LogicSystem 逻辑层（单例）
 * 负责调度LogicWorker
 */
class LogicSystem : public Singleton<LogicSystem> {
    friend class Singleton<LogicSystem>;
public:
    ~LogicSystem();
    // 新客户端连接时轮询绑定 session_id 到 worker
    void BindSession(const std::string& session_id);
    // 客户端会话断开连接时，将session_id从映射中删除
    void UnbindSession(const std::string& session_id);
    // 根据index将消息给到对应的worker线程
    void PostMsgToQue(std::shared_ptr<LogicNode> msg);
private:
    LogicSystem();
    std::vector<std::shared_ptr<LogicWorker>> _workers; // 工作线程池
    std::mutex _map_mutex; // session到worker的映射的锁
    std::unordered_map<std::string, std::size_t> _session_worker_map; // session到worker的映射
    std::atomic<std::size_t> _next_worker = 0; // 下一个worker，轮询处理
};

#endif // LOGICSYSTEM_H
