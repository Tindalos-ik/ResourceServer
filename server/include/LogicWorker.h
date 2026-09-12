#ifndef LOGICWORKER_H
#define LOGICWORKER_H

#include "CSession.h"
#include "const.h"
#include <queue>
#include <map>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <functional>
#include <memory>
#include "MsgNode.h"

typedef std::function<void(std::shared_ptr<CSession>, const short &msg_id, const std::string &msg_data)> FunCallBack;

class LogicWorker{
public:
    LogicWorker();
    ~LogicWorker();
    void PostTask(std::shared_ptr<LogicNode> task);
    void SetServer(std::shared_ptr<CServer> pserver);  // 保存服务器指针（后续多会话操作使用）
private:
    void DealMsg();              // 工作线程：循环消费消息队列
    void RegisterCallBacks();    // 注册 消息id -> 处理函数 的映射

    void HandleTestMsg(std::shared_ptr<CSession> session, const short &msg_id, const std::string &msg_data); // 处理测试消息
    void HandleUploadFile(std::shared_ptr<CSession> session, const short &msg_id, const std::string &msg_data); // 处理上传文件

    std::thread _worker_thread;                    // 消费队列的工作线程
    std::queue<std::shared_ptr<LogicNode>> _msg_que; // 消息队列
    std::mutex _mutex;                             // 保护消息队列
    std::condition_variable _consume;              // 队列非空时唤醒工作线程
    bool _b_stop;                                  // 停止标志
    std::map<short, FunCallBack> _fun_callbacks;   // 消息id -> 处理函数
    std::shared_ptr<CServer> _p_server;            // 服务器指针
    
};

#endif // LOGICWORKER_H