#include "LogicSystem.h"
#include <json.h>
#include <sstream>
#include <iostream>
#include <cctype>
#include "ConfigMgr.h"
#include "Base64.h"
#include <fstream>

using namespace std;

LogicSystem::LogicSystem(){
    for(int i=0; i< LOGIC_WORKER_COUNT; i++){
        _workers.push_back(std::make_shared<LogicWorker>());
    }
}

LogicSystem::~LogicSystem() {

}

void LogicSystem::PostMsgToQue(std::shared_ptr<LogicNode> msg)
{
    auto session_id = msg->_session->GetSessionId();
    auto worker_index = _session_worker_map[session_id];
    _workers[worker_index]->PostTask(msg);
}

void LogicSystem::BindSession(const std::string &session_id)
{
    std::lock_guard<std::mutex> lock(_map_mutex);
    std::size_t index = _next_worker++ % _workers.size();
    _session_worker_map[session_id] = index;
}

void LogicSystem::UnbindSession(const std::string &session_id)
{
    std::lock_guard<std::mutex> lock(_map_mutex);
    auto iter = _session_worker_map.find(session_id);
    if(iter != _session_worker_map.end()){
        _session_worker_map.erase(iter);
    }
}
