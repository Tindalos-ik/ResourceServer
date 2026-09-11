
#pragma once
#ifndef ASIO_IO_SERVICE_POOL_H
#define ASIO_IO_SERVICE_POOL_H

#include <vector>
#include <thread>
#include <memory>
#include <atomic>
#include <boost/asio.hpp>
#include "singleton.h"

/* Asio IO 服务线程池
 * 管理多个 io_context，每个 io_context 运行在独立的线程中。
 * 使用 round-robin 方式分发任务，实现负载均衡。*/
class AsioIOServicePool : public Singleton<AsioIOServicePool>
{
    friend class Singleton<AsioIOServicePool>;

public:
    using IOService = boost::asio::io_context; 
    // 只要 work_guard 还存在，io_context::run() 就会保持运行，这是一种工作保持机制
    // 如果没有这个，在 io_context 没有绑定任何读写事件的适合，io_context::run() 会立即返回，导致 io_context 退出
    using WorkGuard = boost::asio::executor_work_guard<IOService::executor_type>;

    ~AsioIOServicePool();
    
    AsioIOServicePool(const AsioIOServicePool&) = delete;
    AsioIOServicePool& operator=(const AsioIOServicePool&) = delete;
    
    /*使用 round-robin（轮询）方式获取一个 io_context
     * 每次调用返回下一个 io_context，循环使用。
     * 适合将连接或任务均匀分配到不同的 IO 线程。*/
    boost::asio::io_context& GetIOService();
    
    /*停止整个 IO 服务池
     * 1. 重置所有 work_guard（允许 io_context 退出）
     * 2. 停止所有 io_context（强制结束正在进行的异步操作）
     * 3. 等待所有线程结束*/
    void Stop();

private:
    /*私有构造函数（单例模式）
     * 创建指定数量的 io_context 和对应的 work_guard，
     * 并为每个 io_context 创建一个线程。
     *io_context 的数量，默认等于 CPU 核心数*/
    AsioIOServicePool(std::size_t size = std::thread::hardware_concurrency());
    
    std::vector<IOService> _ioServices;      // IO 服务容器，有默认构造函数
    std::vector<WorkGuard> _workGuards;      // 工作保持器容器（只支持移动，不支持拷贝）
    std::vector<std::thread> _threads;       // 线程容器
    std::atomic<std::size_t> _nextIOService; // 轮询索引（原子操作，线程安全）
};

#endif // ASIO_IO_SERVICE_POOL_H
