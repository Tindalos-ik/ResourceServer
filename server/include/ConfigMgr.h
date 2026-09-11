//造轮子，自己写一个读取ini配置文件的类
//或者用vcpkg安装一个第三方库inih

#pragma once //只能解决重复包含的问题，重复定义的问题无法解决
#ifndef CONFIGMGR_H
#define CONFIGMGR_H

#include <map>
#include <string>
#include <iostream>

//这个类也做成单例模式

struct SectionInfo{
    SectionInfo(){}

    SectionInfo(const std::map<std::string, std::string>& section_datas){
        _section_datas = section_datas;
    }
    
    ~SectionInfo(){
        _section_datas.clear();
    }

    SectionInfo(const SectionInfo& other){
        _section_datas = other._section_datas;
    }

    SectionInfo& operator=(const SectionInfo& other){
        if(this == &other){
            return *this;
        }
        _section_datas = other._section_datas;
        return *this;
    }

    std::map<std::string, std::string> _section_datas;

    //重载[]运算符，返回Section_datas
    std::string operator[](const std::string& key){
        if(_section_datas.find(key) == _section_datas.end()){
            return "";
        }
        return _section_datas[key];
    }
};

class ConfigMgr{
public:
    ~ConfigMgr(){
        _config_map.clear();
    }

    SectionInfo operator[](const std::string& section){
        if(_config_map.find(section) == _config_map.end()){
            return  SectionInfo();
        }
        return _config_map[section];
    }

    static ConfigMgr& Inst(){
        //静态局部变量，只会初始化一次，是线程安全的，生命周期为整个程序运行期间，但是可见范围为Inst()函数内部(c++11之后)
        static ConfigMgr cfg_mgr; 
        return cfg_mgr;
    }

    ConfigMgr(const ConfigMgr& other) = delete;
    ConfigMgr& operator=(const ConfigMgr& other) = delete;

private:
    ConfigMgr();  //构造函数内容比较多，要读文件初始化_config_map表，所以放在cpp文件中实现
    std::map<std::string, SectionInfo> _config_map;
};

#endif // CONFIGMGR_H