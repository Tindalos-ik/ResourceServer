#include "LogicSystem.h"
#include "AsioIOServicePool.h"
#include "CServer.h"
#include "ConfigMgr.h"
#include <csignal>
#include <thread>
#include <mutex>
#include <condition_variable>

bool bstop = false;

std::condition_variable cond_quit; // 退出条件变量
std::mutex mutex_quit; // 退出互斥锁


int main(){
    try{
        auto& config = ConfigMgr::Inst();
        auto server_name = config["ResourceServer"]["name"];

        auto pool = AsioIOServicePool::GetInstance();

        boost::asio::io_context io_context; //主线程用于接收新的连接

        boost::asio::signal_set signals(io_context, SIGINT, SIGTERM); //定义信号集跑在主线程上，捕捉退出信号，实现优雅退出
        signals.async_wait([&io_context, pool](auto,auto){
            io_context.stop();
            pool->Stop(); //其实析构也会调用Stop，这里只是保险起见
        }); //捕捉到退出信号后，停止主线程和IO线程池
        
        auto port_str = config["ResourceServer"]["port"];
        CServer server(io_context, std::stoi(port_str));

        io_context.run(); //主线程开始运行，等待退出信号


    }catch (std::exception& e){
        std::cout << "Exception : " << e.what() << std::endl;
    }
}
