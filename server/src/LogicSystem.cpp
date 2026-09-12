#include "LogicSystem.h"
#include <json.h>
#include <sstream>
#include <iostream>
#include <cctype>
#include "ConfigMgr.h"
#include "Base64.h"
#include <fstream>

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
   _fun_callbacks[ID_UPLOAD_FILE_REQ] = [this](std::shared_ptr<CSession> session, const short &msg_id, const std::string &msg_data) {
        HandleUploadFile(session, msg_id, msg_data);
   };
}

void LogicSystem::HandleTestMsg(std::shared_ptr<CSession> session, const short &msg_id, const std::string &msg_data)
{   
    // 服务器原样返回即可
    std::string return_str = msg_data;
    session->Send(return_str, ID_TEST_MSG_RSP); 
}

void LogicSystem::HandleUploadFile(std::shared_ptr<CSession> session, const short &msg_id, const std::string &msg_data)
{
    Json::CharReaderBuilder reader;
    Json::Value root;
    std::istringstream ss(msg_data);
    std::string errs;
    bool parse_success = Json::parseFromStream(reader, ss, &root, &errs);
    if (!parse_success) {
        std::cout << "Failed to parse JSON data" << std::endl;
        std::cout << errs << std::endl;
        return;
    }

    Json::Value rtvalue;
    Defer defer([this, session, &rtvalue]() {
        std::string return_str = rtvalue.toStyledString();
        session->Send(return_str, ID_UPLOAD_FILE_RSP);
    });

    std::string data = root["data"].asString();
    // 客户端上传的是 Base64 文本，保存前必须还原为原始二进制字节。
    std::string decoded_data;
    if (!Base64Decode(data, decoded_data)) {
        std::cout << "Failed to decode Base64 data" << std::endl;
        rtvalue["error"] = ErrorCodes::Error_Json;
        return;
    }

    auto seq = root["seq"].asInt();
    auto name = root["name"].asString();
    auto total_size = root["total_size"].asInt();
    auto trans_size = root["trans_size"].asInt();
    auto file_path = ConfigMgr::Inst().GetFilePath();
    // JSON 中的中文文件名为 UTF-8，u8path 可避免 Windows 按本地代码页解释而乱码。
    // filename() 只保留名称，防止客户端传入 "../" 等路径越界写文件。
    auto file_name = std::filesystem::u8path(name).filename();
    if (file_name.empty()) {
        std::cout << "Invalid file name" << std::endl;
        rtvalue["error"] = ErrorCodes::Error_Json;
        return;
    }
    auto file_path_str = file_path / file_name;
    std::cout << "file_path: " << file_path_str << std::endl;
    std::ofstream outfile;
    if(seq == 1){
        // 第一个包需要创建
        // 打开文件，如果存在则清空，不存在则创建
        outfile.open(file_path_str, std::ios::binary | std::ios::trunc);
    }else{
        // 保存为文件
        outfile.open(file_path_str, std::ios::binary | std::ios::app);
    }
    if(!outfile){
        std::cout << "Failed to open file" << std::endl;
        return;
    }
    outfile.write(decoded_data.data(), decoded_data.size());
    if(!outfile){
        std::cout << "Failed to write file" << std::endl;
        return;
    }
    outfile.close();
    std::cout << "Write file success" << name << std::endl;

    rtvalue["error"] = ErrorCodes::Success;
    rtvalue["seq"] = seq;
    rtvalue["name"] = name;
    rtvalue["total_size"] = total_size;
    rtvalue["trans_size"] = trans_size;
  

}
