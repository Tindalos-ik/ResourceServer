#ifndef GLOBAL_H
#define GLOBAL_H

#pragma once
#include <functional>


enum ErrorCodes {
    Success = 0,
    Error_Json = 1001,  //Json解析错误
    FileNotExists = 1012
};


// Defer类
class Defer {
public:
    // 接受一个lambda表达式或者函数指针
    Defer(std::function<void()> func) : func_(func) {}

    // 析构函数中执行传入的函数
    ~Defer() {
        func_();
    }

private:
    std::function<void()> func_;
};

#define MAX_LENGTH  1024*4
//头部总长度
#define HEAD_TOTAL_LEN 6
//头部id长度1
#define HEAD_ID_LEN 2
//头部数据长度
#define HEAD_DATA_LEN 4
// 接受队列最大个数
#define MAX_RECVQUE  2000000
// 发送队列最大个数
#define MAX_SENDQUE 2000000


enum ReqId {
    ID_TEST_MSG_REQ = 1001,       //测试消息
    ID_TEST_MSG_RSP = 1002,       //测试消息回包
    ID_UPLOAD_FILE_REQ = 1003,    //发送文件请求
    ID_UPLOAD_FILE_RSP = 1004,    //发送文件回复
    ID_SYNC_FILE_REQ = 1005,      //同步文件信息请求
    ID_SYNC_FILE_RSP = 1006,      //同步文件回复回复
};



#endif // GLOBAL_H
