#include "AsioIOServicePool.h"
#include <iostream>

using namespace std;

/*构造函数：初始化 IO 服务池
 * 执行流程：
 * 1. 初始化 _ioServices 容器（大小等于 size）
 * 2. 预留 _workGuards 容量，避免多次重新分配
 * 3. 为每个 io_context 创建一个 WorkGuard，防止 run() 提前退出
 * 4. 为每个 io_context 创建一个线程，并在该线程中执行 ioc.run()
 *  size IO 服务的数量 */
AsioIOServicePool::AsioIOServicePool(std::size_t size)
    : _ioServices(size)      // 创建 size 个 io_context
    , _nextIOService(0)      // 初始轮询索引为 0
{
    // WorkGuard 只支持移动语义，不支持拷贝，所以不能用 _workGuards(size) 先创建空对象
    _workGuards.reserve(size);
    
    // 核心：为每个 io_context 创建 WorkGuard
    for (std::size_t i = 0; i < size; ++i) {
        // make_work_guard 创建一个工作保持器
        // 只要这个对象还存在，io_context::run() 就会一直运行
        // 即使当前没有任务也不会退出
        // 使用 emplace_back 就地构造，避免拷贝
        _workGuards.emplace_back(boost::asio::make_work_guard(_ioServices[i]));
    }

    // 遍历所有 io_context，为每个创建一个线程，让多个context跑在多个线程上
    for (std::size_t i = 0; i < _ioServices.size(); ++i) {
        _threads.emplace_back([this, i]() {
            // run() 会阻塞，直到以下条件之一满足：
            // 1. WorkGuard 被销毁且没有待处理的任务
            // 2. io_context 被显式 stop()
            _ioServices[i].run();
        });
    }
    
    cout << "AsioIOServicePool started with " << size << " threads" << endl;
}


AsioIOServicePool::~AsioIOServicePool() {
    Stop(); //RAII，资源自动释放
    cout << "AsioIOServicePool destroyed" << endl;
}

/*使用 round-robin 方式获取一个 io_context
 * 实现原理：
 * - 使用原子变量 _nextIOService 记录下一个要返回的索引
 * - 每次调用后索引自增，并对 size 取模，实现循环使用
 * - 原子操作保证了多线程调用时的线程安全      */
boost::asio::io_context& AsioIOServicePool::GetIOService() {
    auto& service = _ioServices[_nextIOService++ % _ioServices.size()];
    return service;
}

/*停止整个 IO 服务池
 * 执行步骤：
 * 1. reset() 所有 WorkGuard -> 释放工作保持，io_context 可以在无任务时退出
 * 2. stop() 所有 io_context -> 强制停止，处理已绑定的异步操作
 * 3. join() 所有线程 -> 等待线程安全结束
 * 注意：必须先 reset 再 stop，否则 run() 可能无法正常退出 */
void AsioIOServicePool::Stop() {
    // 第一步：重置所有 WorkGuard
    for (auto& guard : _workGuards) {
        guard.reset(); //work_guard 被销毁，io_context 可以在无任务时退出
    }
    
    // 第二步：手动停止所有 io_context
    // 这一步是必须的，因为可能有已绑定的事件监听
    // 如果不调用 stop()，run() 可能会因为等待 I/O 事件而一直阻塞
    for (auto& ioc : _ioServices) {
        ioc.stop(); //就算当前有事件，也会强制停止
    }
    
    // 第三步：等待所有线程结束
    for (auto& t : _threads) {
        if (t.joinable()) {
            t.join();    // join() 会阻塞直到线程函数返回
        }
    }
}