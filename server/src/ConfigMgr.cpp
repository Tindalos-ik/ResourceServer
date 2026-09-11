#include "ConfigMgr.h"
#include <filesystem>
#include <boost/property_tree/ptree.hpp>
#include <boost/property_tree/ini_parser.hpp>

ConfigMgr::ConfigMgr()
{
    // 使用 std::filesystem 替代 boost::filesystem 只有boost header-only 才无所谓什么编译器，需要依赖库的我现在的boost是mingw编译的，不行
    std::filesystem::path config_path = std::filesystem::current_path() / "config.ini"; //重载了/运算符
    
    // 检查文件是否存在
    if (!std::filesystem::exists(config_path)) {
        std::cerr << "Config file not found: " << config_path << std::endl;
        return;
    }
    
    std::cout << "config_path: " << config_path << std::endl;

    // 处理 ini 文件
    boost::property_tree::ptree pt;
    boost::property_tree::ini_parser::read_ini(config_path.string(), pt);

    // 读取配置
    for (const auto& [section_name, section_tree] : pt) {
        std::map<std::string, std::string> section_config;
        for (const auto& [key, value] : section_tree) {
            section_config[key] = value.get_value<std::string>();
        }
        SectionInfo section_info(section_config);
        _config_map[section_name] = section_info;
    }

    // 输出所有的 section 和键值对
    for (const auto& [section_name, section_info] : _config_map) {
        std::cout << "[" << section_name << "]" << std::endl;
        for (const auto& [key, value] : section_info._section_datas) {
            std::cout << key << " = " << value << std::endl;
        }
    }
}