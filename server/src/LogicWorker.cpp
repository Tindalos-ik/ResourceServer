#include "LogicWorker.h"
#include <json.h>
#include <sstream>
#include <iostream>
#include <cctype>
#include "ConfigMgr.h"
#include "Base64.h"
#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <mutex>

namespace {

// 同一个 upload_id 可能在断线重连后被另一条会话继续上传。该锁保证查询进度、
// 追加写入和完成重命名不会相互穿插，避免同一临时文件被并发写坏。
std::mutex g_upload_file_mutex;

bool IsSafeUploadId(const std::string& upload_id) {
    if (upload_id.empty() || upload_id.size() > 160) {
        return false;
    }
    return std::all_of(upload_id.begin(), upload_id.end(), [](unsigned char ch) {
        return std::isalnum(ch) || ch == '_' || ch == '-';
    });
}

// Json::Value::asUInt64() 会把负数转换成很大的无符号数；先单独拒绝负数，
// 防止恶意请求绕过 offset/total_size 的范围校验。
bool IsNonNegativeInteger(const Json::Value& value) {
    return value.isUInt() || value.isUInt64() ||
           (value.isInt() && value.asInt() >= 0) ||
           (value.isInt64() && value.asInt64() >= 0);
}

bool ReadJsonFile(const std::filesystem::path& path, Json::Value& value) {
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        return false;
    }
    Json::CharReaderBuilder reader;
    std::string errors;
    return Json::parseFromStream(reader, input, &value, &errors);
}

bool WriteJsonFile(const std::filesystem::path& path, const Json::Value& value) {
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    if (!output) {
        return false;
    }
    output << value.toStyledString();
    return static_cast<bool>(output);
}

bool IsSameTask(const Json::Value& meta, const std::string& md5,
                const std::string& name, Json::UInt64 total_size) {
    return meta["md5"].asString() == md5 &&
           meta["name"].asString() == name &&
           meta["total_size"].asUInt64() == total_size;
}

void FillUploadResponse(Json::Value& response, ErrorCodes error,
                        const std::string& upload_id, Json::UInt64 total_size,
                        Json::UInt64 confirmed_offset, bool completed) {
    response["error"] = error;
    response["upload_id"] = upload_id;
    response["total_size"] = total_size;
    response["confirmed_offset"] = confirmed_offset;
    response["completed"] = completed;
}

} // namespace


LogicWorker::LogicWorker(): _b_stop(false), _p_server(nullptr) {
    RegisterCallBacks(); // 注册消息处理函数
    // 启动工作线程，专门消费消息队列，业务逻辑与IO线程分离
    _worker_thread = std::thread(&LogicWorker::DealMsg, this);
}

LogicWorker::~LogicWorker()
{
    _b_stop = true;        // 置停止标志
    _consume.notify_one(); // 唤醒工作线程，让它处理完剩余消息后退出
    _worker_thread.join();
}

// LogicSystem 将任务分发给 worker
void LogicWorker::PostTask(std::shared_ptr<LogicNode> task) {
    std::unique_lock<std::mutex> unique_lk(_mutex);
    _msg_que.push(task);
    if (_msg_que.size() == 1) {
        // 队列从空变成非空，需要唤醒正在等待的工作线程
        unique_lk.unlock();
        _consume.notify_one();
    }
}

void LogicWorker::SetServer(std::shared_ptr<CServer> pserver)
{
    _p_server = pserver;
}

// 工作线程主循环：等待消息 -> 按消息id分发到处理函数
void LogicWorker::DealMsg() {
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
        _msg_que.pop();
        unique_lk.unlock(); // 释放锁，避免阻塞其他线程

        auto call_back_iter = _fun_callbacks.find(msg_node->_recvnode->_msg_id);
        if (call_back_iter == _fun_callbacks.end()) {
            std::cout << "msg id [" << msg_node->_recvnode->_msg_id << "] handler not found" << std::endl;
            continue;
        }

        // 调用对应的处理函数（登录、搜索好友、聊天、心跳等）
        call_back_iter->second(msg_node->_session, msg_node->_recvnode->_msg_id,
            std::string(msg_node->_recvnode->_data, msg_node->_recvnode->_cur_len));
    }
}


// 注册 消息id -> 处理函数 的映射 诸如 登录，搜索好友，聊天，心跳等服务都在这里注册
void LogicWorker::RegisterCallBacks() {
   _fun_callbacks[ID_TEST_MSG_REQ] = [this](std::shared_ptr<CSession> session, const short &msg_id, const std::string &msg_data) {
        HandleTestMsg(session, msg_id, msg_data);
   };
   _fun_callbacks[ID_UPLOAD_FILE_REQ] = [this](std::shared_ptr<CSession> session, const short &msg_id, const std::string &msg_data) {
        HandleUploadFile(session, msg_id, msg_data);
   };
   _fun_callbacks[ID_SYNC_FILE_REQ] = [this](std::shared_ptr<CSession> session, const short &msg_id, const std::string &msg_data) {
        HandleSyncFile(session, msg_id, msg_data);
   };
}


void LogicWorker::HandleTestMsg(std::shared_ptr<CSession> session, const short &msg_id, const std::string &msg_data)
{   
    // 服务器原样返回即可
    std::string return_str = msg_data;
    session->Send(return_str, ID_TEST_MSG_RSP); 
}

// 查询或创建上传任务。任务目录以稳定的 upload_id 命名，而不是 session_id 或文件名，
// 因此同一文件即使断线后重新建立 TCP 连接，也能找到之前的 .part 文件继续上传。
void LogicWorker::HandleSyncFile(std::shared_ptr<CSession> session, const short &msg_id,
                                 const std::string &msg_data)
{
    Json::CharReaderBuilder reader;
    Json::Value root;
    std::istringstream input(msg_data);
    std::string errors;
    Json::Value response;
    Defer defer([session, &response]() {
        session->Send(response.toStyledString(), ID_SYNC_FILE_RSP);
    });

    if (!Json::parseFromStream(reader, input, &root, &errors)) {
        response["error"] = ErrorCodes::Error_Json;
        return;
    }

    const std::string upload_id = root["upload_id"].asString();
    const std::string md5 = root["md5"].asString();
    const std::string name = root["name"].asString();
    const Json::UInt64 total_size = root["total_size"].asUInt64();
    const Json::UInt64 chunk_size = root["chunk_size"].asUInt64();
    const auto file_name = std::filesystem::u8path(name).filename();

    if (!IsNonNegativeInteger(root["total_size"]) || !IsNonNegativeInteger(root["chunk_size"]) ||
        !IsSafeUploadId(upload_id) || md5.empty() || file_name.empty() ||
        chunk_size == 0) {
        response["error"] = ErrorCodes::Error_Json;
        return;
    }

    std::lock_guard<std::mutex> lock(g_upload_file_mutex);
    const std::filesystem::path task_dir = ConfigMgr::Inst().GetFilePath() / upload_id;
    const std::filesystem::path meta_path = task_dir / "meta.json";
    const std::filesystem::path part_path = task_dir / "data.part";
    const std::filesystem::path final_path = task_dir / file_name;
    std::error_code ec;
    std::filesystem::create_directories(task_dir, ec);
    if (ec) {
        FillUploadResponse(response, UploadFileError, upload_id, total_size, 0, false);
        return;
    }

    Json::Value meta;
    if (std::filesystem::exists(meta_path)) {
        if (!ReadJsonFile(meta_path, meta) || !IsSameTask(meta, md5, name, total_size) ||
            meta["chunk_size"].asUInt64() != chunk_size) {
            FillUploadResponse(response, UploadTaskConflict, upload_id, total_size, 0, false);
            return;
        }
    } else {
        // 元数据把 upload_id 和原始文件属性绑定在一起，防止客户端借同一目录覆盖别的任务。
        meta["upload_id"] = upload_id;
        meta["md5"] = md5;
        meta["name"] = name;
        meta["total_size"] = total_size;
        meta["chunk_size"] = chunk_size;
        meta["completed"] = false;
        if (!WriteJsonFile(meta_path, meta)) {
            FillUploadResponse(response, UploadFileError, upload_id, total_size, 0, false);
            return;
        }
    }

    if (std::filesystem::exists(final_path)) {
        const auto final_size = std::filesystem::file_size(final_path, ec);
        if (!ec && final_size == total_size) {
            FillUploadResponse(response, Success, upload_id, total_size, total_size, true);
            return;
        }
        FillUploadResponse(response, UploadTaskConflict, upload_id, total_size, 0, false);
        return;
    }

    Json::UInt64 confirmed_offset = 0;
    if (std::filesystem::exists(part_path)) {
        confirmed_offset = std::filesystem::file_size(part_path, ec);
        if (ec || confirmed_offset > total_size) {
            FillUploadResponse(response, UploadTaskConflict, upload_id, total_size, 0, false);
            return;
        }
    }
    FillUploadResponse(response, Success, upload_id, total_size, confirmed_offset, false);
}

void LogicWorker::HandleUploadFile(std::shared_ptr<CSession> session, const short &msg_id, const std::string &msg_data)
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

    const std::string upload_id = root["upload_id"].asString();
    const std::string md5 = root["md5"].asString();
    const std::string name = root["name"].asString();
    const Json::UInt64 total_size = root["total_size"].asUInt64();
    const Json::UInt64 offset = root["offset"].asUInt64();
    const bool is_last = root["is_last"].asBool();
    const auto file_name = std::filesystem::u8path(name).filename();
    if (!IsNonNegativeInteger(root["total_size"]) || !IsNonNegativeInteger(root["offset"]) ||
        !IsSafeUploadId(upload_id) || md5.empty() || file_name.empty() ||
        offset > total_size || decoded_data.size() > total_size - offset ||
        is_last != (offset + decoded_data.size() == total_size)) {
        rtvalue["error"] = ErrorCodes::Error_Json;
        return;
    }

    std::lock_guard<std::mutex> lock(g_upload_file_mutex);
    const std::filesystem::path task_dir = ConfigMgr::Inst().GetFilePath() / upload_id;
    const std::filesystem::path meta_path = task_dir / "meta.json";
    const std::filesystem::path part_path = task_dir / "data.part";
    const std::filesystem::path final_path = task_dir / file_name;
    Json::Value meta;
    std::error_code ec;
    if (!ReadJsonFile(meta_path, meta) || !IsSameTask(meta, md5, name, total_size)) {
        FillUploadResponse(rtvalue, UploadTaskConflict, upload_id, total_size, 0, false);
        return;
    }

    // 完成文件保存在任务目录中；重发最后一个包时直接返回完成，保持请求幂等。
    if (std::filesystem::exists(final_path)) {
        const auto final_size = std::filesystem::file_size(final_path, ec);
        if (!ec && final_size == total_size) {
            FillUploadResponse(rtvalue, Success, upload_id, total_size, total_size, true);
            return;
        }
        FillUploadResponse(rtvalue, UploadTaskConflict, upload_id, total_size, 0, false);
        return;
    }

    Json::UInt64 confirmed_offset = 0;
    if (std::filesystem::exists(part_path)) {
        confirmed_offset = std::filesystem::file_size(part_path, ec);
        if (ec || confirmed_offset > total_size) {
            FillUploadResponse(rtvalue, UploadTaskConflict, upload_id, total_size, 0, false);
            return;
        }
    }

    // 已确认位置之前的包是断线后的重传，不重复写入；未来位置的包要求客户端先重新同步。
    if (offset < confirmed_offset) {
        FillUploadResponse(rtvalue, Success, upload_id, total_size, confirmed_offset, false);
        return;
    }
    if (offset > confirmed_offset) {
        FillUploadResponse(rtvalue, UploadOffsetMismatch, upload_id, total_size, confirmed_offset, false);
        return;
    }

    std::ofstream outfile(part_path, std::ios::binary | std::ios::app);
    if (!outfile) {
        FillUploadResponse(rtvalue, UploadFileError, upload_id, total_size, confirmed_offset, false);
        return;
    }
    outfile.write(decoded_data.data(), static_cast<std::streamsize>(decoded_data.size()));
    outfile.close();
    if (!outfile) {
        FillUploadResponse(rtvalue, UploadFileError, upload_id, total_size, confirmed_offset, false);
        return;
    }

    confirmed_offset += decoded_data.size();
    if (is_last) {
        // rename 只会在全部字节落盘后执行，外部使用者不会读到半成品文件。
        std::filesystem::rename(part_path, final_path, ec);
        if (ec) {
            FillUploadResponse(rtvalue, UploadFileError, upload_id, total_size, confirmed_offset, false);
            return;
        }
        meta["completed"] = true;
        if (!WriteJsonFile(meta_path, meta)) {
            FillUploadResponse(rtvalue, UploadFileError, upload_id, total_size, confirmed_offset, false);
            return;
        }
    }

    FillUploadResponse(rtvalue, Success, upload_id, total_size, confirmed_offset, is_last);
}
