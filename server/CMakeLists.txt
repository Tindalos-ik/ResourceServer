cmake_minimum_required(VERSION 3.10)
project(ResourceServer)

add_compile_options(/utf-8 /wd4819) #忽略编码警告

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ==================== 项目源文件 ====================
set(JSONCPP_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/jsoncpp/jsoncpp.cpp
)

set(SOURCE_FILES
    ${CMAKE_CURRENT_SOURCE_DIR}/src/Base64.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/LogicSystem.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/CServer.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/ConfigMgr.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/AsioIOServicePool.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/CSession.cpp
)

link_directories("D:/cppsoft/vcpkg/installed/x64-windows/lib") # 添加 vcpkg 的 lib 路径，库文件搜索路径

# ==================== 可执行文件 ====================
add_executable(${PROJECT_NAME} 
    ResourceServer.cpp
    ${JSONCPP_SOURCES}  
    ${SOURCE_FILES}
)

# ==================== 头文件搜索路径 ====================
target_include_directories(${PROJECT_NAME} PRIVATE 
    "C:/local/boost_1_91_0"
    ${CMAKE_CURRENT_SOURCE_DIR}/jsoncpp/json
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    "D:/cppsoft/vcpkg/installed/x64-windows/include"   # 添加 vcpkg 的 include 路径
)


if(WIN32)
    target_link_libraries(${PROJECT_NAME} 
        ws2_32
        mswsock
    )
endif()

# 配置文件，也就是将config.ini文件拷贝到可执行文件所在目录
configure_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/config.ini
    ${CMAKE_CURRENT_BINARY_DIR}/Debug/config.ini
    COPYONLY
)


