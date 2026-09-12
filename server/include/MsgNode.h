#pragma once
#ifndef MSGNODE_H
#define MSGNODE_H

#include <memory>
#include <cstdint>
#include <cstring>
#include <boost/asio.hpp>
#include "const.h"

/*
 * MsgNode 消息节点基类
 * 负责维护一块动态内存，用来存放一条完整的消息。
 * 发送时内存布局：[消息id(2字节)] [消息体长度(4字节)] [消息体(n字节)] ['\0']
 *                  |----------- 头部 HEAD_TOTAL_LEN -----------|
 */
class MsgNode
{
    friend class CSession;
public:
    // 只分配内存、不填充数据的节点，用于接收数据
    explicit MsgNode(short max_len) : _total_len(max_len), _cur_len(0) {
        _data = new char[_total_len + 1](); // 多申请1个字节，用来存字符串结束符'\0'
        _data[_total_len] = '\0';
    }

    ~MsgNode() {
        delete[] _data;
    }

    // 清空缓冲区并重置读进度，复用节点对象，减少反复 new/delete 的开销
    void Clear() {
        memset(_data, 0, _total_len);
        _cur_len = 0;
    }

    short _cur_len;   // 已经读入/写出的字节数
    short _total_len; // 节点缓冲区总长度
    char* _data;      // 数据缓冲区
};

/*
 * RecvNode 接收节点（服务器收到的消息）
 * 包头解析完成后，根据消息id和消息体长度构造该节点，用于存放消息体
 */
class RecvNode : public MsgNode {
    friend class LogicWorker; // 逻辑层需要读取 _msg_id 做消息路由
public:
    // max_len: 消息体长度；msg_id: 消息id（由包头解析出来）
    RecvNode(short max_len, short msg_id) : MsgNode(max_len), _msg_id(msg_id) {
    }
private:
    short _msg_id; // 消息id，逻辑层根据它找到对应的处理函数
};

/*
 * SendNode 发送节点（服务器发给客户端的消息）
 * 构造时就把 [消息id][消息体长度][消息体] 按网络字节序打包好，
 * 发送时直接整块写出，无需在会话层再拼包。
 */
class SendNode : public MsgNode {
    friend class LogicWorker;
public:
    SendNode(const char* msg, std::uint32_t max_len, short msg_id)
        : MsgNode(static_cast<short>(max_len + HEAD_TOTAL_LEN)), _msg_id(msg_id) {
        // 1. 写入消息id（主机字节序 -> 网络字节序）
        short msg_id_host = boost::asio::detail::socket_ops::host_to_network_short(msg_id);
        memcpy(_data, &msg_id_host, HEAD_ID_LEN);

        // 2. 写入4字节消息体长度（主机字节序 -> 网络字节序）
        std::uint32_t max_len_host = boost::asio::detail::socket_ops::host_to_network_long(max_len);
        memcpy(_data + HEAD_ID_LEN, &max_len_host, HEAD_DATA_LEN);

        // 3. 写入消息体
        memcpy(_data + HEAD_TOTAL_LEN, msg, max_len);
    }
private:
    short _msg_id;
};

#endif // MSGNODE_H
