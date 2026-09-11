#include "LogicSystem.h"
#include <json.h>
#include <sstream>
#include <iostream>
#include <cctype>

using namespace std;

LogicSystem::LogicSystem() : _b_stop(false), _p_server(nullptr) {
    RegisterCallBacks(); // 注册消息处理函数
    // 启动工作线程，专门消费消息队列，业务逻辑与IO线程分离
    _worker_thread = std::thread(&LogicSystem::DealMsg, this);
}

LogicSystem::~LogicSystem() {
    _b_stop = true;        // 置停止标志
    _consume.notify_one(); // 唤醒工作线程，让它处理完剩余消息后退出
    _worker_thread.join();
}

// 会话层把解析好的消息投递到队列，并唤醒工作线程
void LogicSystem::PostMsgToQue(std::shared_ptr<LogicNode> msg) {
    std::unique_lock<std::mutex> unique_lk(_mutex);
    _msg_que.push(msg);
    if (_msg_que.size() == 1) {
        // 队列从空变成非空，需要唤醒正在等待的工作线程
        unique_lk.unlock();
        _consume.notify_one();
    }
}

void LogicSystem::SetServer(std::shared_ptr<CServer> pserver) {
    _p_server = pserver;
}

// 工作线程主循环：等待消息 -> 按消息id分发到处理函数
void LogicSystem::DealMsg() {
    for (;;) {
        std::unique_lock<std::mutex> unique_lk(_mutex);
        // 队列为空且没有停止请求时，挂起等待
        while (_msg_que.empty() && !_b_stop) {
            _consume.wait(unique_lk);
        }

        // 收到停止请求：把队列剩余消息处理完再退出
        if (_b_stop) {
            while (!_msg_que.empty()) {
                auto msg_node = _msg_que.front();
                auto call_back_iter = _fun_callbacks.find(msg_node->_recvnode->_msg_id);
                if (call_back_iter != _fun_callbacks.end()) {
                    call_back_iter->second(msg_node->_session, msg_node->_recvnode->_msg_id,
                        std::string(msg_node->_recvnode->_data, msg_node->_recvnode->_cur_len));
                }
                _msg_que.pop();
            }
            break;
        }

        // 取出队首消息
        auto msg_node = _msg_que.front();
        auto call_back_iter = _fun_callbacks.find(msg_node->_recvnode->_msg_id);
        if (call_back_iter == _fun_callbacks.end()) {
            _msg_que.pop();
            std::cout << "msg id [" << msg_node->_recvnode->_msg_id << "] handler not found" << std::endl;
            continue;
        }

        // 调用对应的处理函数（登录、搜索好友、聊天、心跳等）
        call_back_iter->second(msg_node->_session, msg_node->_recvnode->_msg_id,
            std::string(msg_node->_recvnode->_data, msg_node->_recvnode->_cur_len));
        _msg_que.pop();
    }
}

// 注册 消息id -> 处理函数 的映射 诸如 登录，搜索好友，聊天，心跳等服务都在这里注册
void LogicSystem::RegisterCallBacks() {
   _fun_callbacks[ID_TEST_MSG_REQ] = [this](std::shared_ptr<CSession> session, const short &msg_id, const std::string &msg_data) {
        HandleTestMsg(session, msg_id, msg_data);
   };
}

void LogicSystem::HandleTestMsg(std::shared_ptr<CSession> session, const short &msg_id, const std::string &msg_data)
{   
    // 服务器原样返回即可
    std::string return_str = msg_data;
    session->Send(return_str, ID_TEST_MSG_RSP); 
}
