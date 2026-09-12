#include "CSession.h"
#include "CServer.h"
#include "LogicSystem.h"
#include <iostream>
#include <sstream>
#include <iomanip>
#include <random>
#include <cstring>

using namespace std;

// 生成一个32位的十六进制随机字符串作为会话id（效果等同uuid，保证每个连接唯一）
static std::string generate_uuid() {
    static std::random_device rd;
    static std::mt19937_64 gen(rd());
    static std::uniform_int_distribution<uint64_t> dis;

    std::stringstream ss;
    ss << std::hex << std::setfill('0');
    for (int i = 0; i < 4; ++i) {
        ss << std::setw(8) << dis(gen);
        if (i < 3) ss << "-";
    }
    return ss.str();
}

CSession::CSession(boost::asio::io_context &io_context, CServer *server)
    : _socket(io_context), _server(server), _b_close(false), _user_uid(0) {
    _session_id = generate_uuid(); // 每个会话分配一个唯一id，服务器用它管理会话
    _recv_head_node = std::make_shared<MsgNode>(HEAD_TOTAL_LEN); // 包头固定6字节
    std::cout << "session created, id = " << _session_id << std::endl;
}

CSession::~CSession() {
    std::cout << "session destroyed, id = " << _session_id << std::endl;
}

tcp::socket &CSession::GetSocket() {
    return _socket;
}

std::string &CSession::GetSessionId() {
    return _session_id;
}

void CSession::SetUserId(int uid) {
    _user_uid = uid;
}

int CSession::GetUserId() {
    return _user_uid;
}

// 连接建立后开始接收数据：先读6字节包头
void CSession::Start() {
    ReadHead(HEAD_TOTAL_LEN);
}

// 发送消息（std::string版本）
void CSession::Send(std::string msg, short msgid) {
    std::lock_guard<std::mutex> lock(_send_lock);
    std::size_t send_que_size = _send_que.size();
    // 发送队列已满，说明对端消费能力不足，丢弃本次消息防止内存无限膨胀
    if (send_que_size > MAX_SENDQUE) {
        std::cout << "session: " << _session_id << " send que fulled, size is " << MAX_SENDQUE << endl;
        return;
    }

    if (msg.length() > MAX_LENGTH) {
        std::cout << "session: " << _session_id << " message too large, length is "
                  << msg.length() << endl;
        return;
    }

    // 封装成发送节点（构造时自动打包头部：2字节id + 4字节长度）
    _send_que.push(std::make_shared<SendNode>(msg.c_str(),
        static_cast<std::uint32_t>(msg.length()), msgid));

    // 队列里已经有消息在发送中，等前面发完再发，避免数据乱序
    if (send_que_size > 0) {
        return;
    }

    // 队列之前是空的，说明当前没有异步写在进行，立刻启动第一个写
    auto &msgnode = _send_que.front();
    boost::asio::async_write(_socket, boost::asio::buffer(msgnode->_data, msgnode->_total_len),
        std::bind(&CSession::HandleWrite, this, std::placeholders::_1, SharedSelf()));
}

// 发送消息（char*版本）
void CSession::Send(char *msg, short max_length, short msgid) {
    std::lock_guard<std::mutex> lock(_send_lock);
    std::size_t send_que_size = _send_que.size();
    if (send_que_size > MAX_SENDQUE) {
        std::cout << "session: " << _session_id << " send que fulled, size is " << MAX_SENDQUE << endl;
        return;
    }

    _send_que.push(std::make_shared<SendNode>(msg, max_length, msgid));
    if (send_que_size > 0) {
        return;
    }

    auto &msgnode = _send_que.front();
    boost::asio::async_write(_socket, boost::asio::buffer(msgnode->_data, msgnode->_total_len),
        std::bind(&CSession::HandleWrite, this, std::placeholders::_1, SharedSelf()));
}

// 关闭连接：关闭socket后，进行中的异步读写会以error结束，从而触发清理流程
void CSession::Close() {
    std::lock_guard<std::mutex> lock(_session_mtx);
    _b_close = true;
    boost::system::error_code ec;
    _socket.close(ec); // 重复close不会抛异常，安全
}

std::shared_ptr<CSession> CSession::SharedSelf() {
    return shared_from_this();
}

// 写完成回调：弹出队首消息，若队列还有消息则继续发送下一条
void CSession::HandleWrite(const boost::system::error_code &error, std::shared_ptr<CSession> shared_self) {
    try {
        if (!error) {
            std::lock_guard<std::mutex> lock(_send_lock);
            _send_que.pop();
            if (!_send_que.empty()) {
                auto &msgnode = _send_que.front();
                boost::asio::async_write(_socket, boost::asio::buffer(msgnode->_data, msgnode->_total_len),
                    std::bind(&CSession::HandleWrite, this, std::placeholders::_1, shared_self));
            }
        } else {
            std::cout << "handle write failed, error is " << error.message() << endl;
            Close();
            _server->ClearSession(_session_id); // 发送失败说明连接已不可用，清理会话
        }
    } catch (std::exception &e) {
        std::cerr << "Exception code : " << e.what() << endl;
    }
}

// 读包头：包头固定6字节 = [消息id(2字节)] [消息体长度(4字节)]
void CSession::ReadHead(int head_len) {
    auto self = shared_from_this();
    asyncReadFull(head_len, [self, this](const boost::system::error_code &ec, std::size_t bytes_transfered) {
        try {
            if (ec) {
                std::cout << "handle read failed, error is " << ec.message() << endl;
                Close();
                _server->ClearSession(_session_id);
                return;
            }
            if (bytes_transfered < HEAD_TOTAL_LEN) {
                std::cout << "read length not match, read [" << bytes_transfered << "] , total ["
                          << HEAD_TOTAL_LEN << "]" << endl;
                Close();
                _server->ClearSession(_session_id);
                return;
            }

            _recv_head_node->Clear(); // 复用包头节点
            memcpy(_recv_head_node->_data, _data, bytes_transfered);

            // 解析消息id（网络字节序 -> 主机字节序）
            short msg_id = 0;
            memcpy(&msg_id, _recv_head_node->_data, HEAD_ID_LEN);
            msg_id = boost::asio::detail::socket_ops::network_to_host_short(msg_id);

            // 解析4字节消息体长度（网络字节序 -> 主机字节序）
            std::uint32_t msg_len = 0;
            memcpy(&msg_len, _recv_head_node->_data + HEAD_ID_LEN, HEAD_DATA_LEN);
            msg_len = boost::asio::detail::socket_ops::network_to_host_long(msg_len);

            // 非法长度直接断开，防止恶意包导致内存越界
            if (msg_id <= 0 || msg_id > MAX_LENGTH || msg_len <= 0 || msg_len > MAX_LENGTH) {
                std::cout << "invalid msg id [" << msg_id << "] or length [" << msg_len << "]" << endl;
                Close();
                _server->ClearSession(_session_id);
                return;
            }

            // 根据包体长度创建接收节点，继续读包体
            _recv_msg_node = std::make_shared<RecvNode>(static_cast<short>(msg_len), msg_id);
            ReadBody(static_cast<int>(msg_len));
        } catch (std::exception &e) {
            std::cout << "read head exception : " << e.what() << endl;
            Close();
            _server->ClearSession(_session_id);
        }
    });
}

// 读包体：读满 body_len 字节后解析消息并投递到逻辑层
void CSession::ReadBody(int body_len) {
    auto self = shared_from_this();
    asyncReadFull(body_len, [self, this, body_len](const boost::system::error_code &ec, std::size_t bytes_transfered) {
        try {
            if (ec) {
                std::cout << "handle read failed, error is " << ec.message() << endl;
                Close();
                _server->ClearSession(_session_id);
                return;
            }
            if (bytes_transfered < static_cast<std::size_t>(body_len)) {
                std::cout << "read length not match, read [" << bytes_transfered << "] , total ["
                          << body_len << "]" << endl;
                Close();
                _server->ClearSession(_session_id);
                return;
            }

            // 把包体拷贝进接收节点，并补一个'\0'方便以字符串形式打印
            memcpy(_recv_msg_node->_data, _data, bytes_transfered);
            _recv_msg_node->_cur_len += static_cast<short>(bytes_transfered);
            _recv_msg_node->_data[_recv_msg_node->_total_len] = '\0';
            std::cout << "receive data is " << _recv_msg_node->_data << endl;

            // 封装成逻辑节点投递给逻辑层处理
            LogicSystem::GetInstance()->PostMsgToQue(
                std::make_shared<LogicNode>(shared_from_this(), _recv_msg_node));

            // 处理完一条消息，继续读下一条的包头（长连接循环接收）
            ReadHead(HEAD_TOTAL_LEN);
        } catch (std::exception &e) {
            std::cout << "read body exception : " << e.what() << endl;
            Close();
            _server->ClearSession(_session_id);
        }
    });
}

// 清空缓冲区，并从0开始读满 maxLength 字节
void CSession::asyncReadFull(std::size_t maxLength,
                             std::function<void(const boost::system::error_code &, std::size_t)> handler) {
    ::memset(_data, 0, MAX_LENGTH);
    asyncReadLen(0, maxLength, handler);
}

// 循环读取，直到累计读满 total_len 字节才回调（解决TCP粘包/半包问题）
void CSession::asyncReadLen(std::size_t read_len, std::size_t total_len,
                            std::function<void(const boost::system::error_code &, std::size_t)> handler) {
    auto self = shared_from_this();
    _socket.async_read_some(boost::asio::buffer(_data + read_len, total_len - read_len),
        [read_len, total_len, handler, self](const boost::system::error_code &ec, std::size_t bytes_transfered) {
            if (ec) {
                handler(ec, read_len + bytes_transfered); // 出错直接回调，由上层统一处理
                return;
            }
            if (read_len + bytes_transfered >= total_len) {
                handler(ec, read_len + bytes_transfered); // 长度够了，回调
                return;
            }
            // 半包：继续读剩下的字节
            self->asyncReadLen(read_len + bytes_transfered, total_len, handler);
        });
}

LogicNode::LogicNode(std::shared_ptr<CSession> session, std::shared_ptr<RecvNode> recvnode)
    : _session(session), _recvnode(recvnode) {
}
