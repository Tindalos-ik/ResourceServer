# jsoncpp

在cpp网关服务中，网络通信底层boost.beast处理的是HTTP请求的原始字节流，而业务逻辑层需要的是结构化的数据

所以需要jsoncpp



**解析请求 (反序列化)**：前端、小程序或其他服务发来的请求，其 Body 部分通常是 JSON 格式的字符串。C++ 网关服务使用 `jsoncpp` 可以将这段原始的字符串，解析成方便 C++ 代码直接访问和操作的对象（如 `Json::Value`），从而高效地读取其中的参数。

**构造响应 (序列化)**：当网关处理完业务逻辑，需要返回数据给客户端时，`jsoncpp` 能把 C++ 的数据结构（如 `map`、`vector` 或自定义对象）重新打包成标准的 JSON 字符串，并通过 HTTP 响应发送回去。



如何配置？

首先下载jsoncpp源码 [地址](https://github.com/open-source-parsers/jsoncpp)

然后运行`amalgamate.py` ，生成dist文件夹，json目录下就是文件，还有一个cpp，把源码放进去项目即可，然后在CMakeLists.txt中修改一下

```
# 收集 JsonCpp 的源文件
set(JSONCPP_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/jsoncpp/jsoncpp.cpp
)

# 添加可执行文件，把 JsonCpp 源文件也加进去
add_executable(${PROJECT_NAME} 
    main.cpp
    ${JSONCPP_SOURCES}  
)

# 指定头文件搜索路径
target_include_directories(${PROJECT_NAME} PRIVATE 
    ${CMAKE_CURRENT_SOURCE_DIR}/jsoncpp/json  # JsonCpp 的路径
)
```

