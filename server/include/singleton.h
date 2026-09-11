#pragma once
#ifndef SINGLETON_H
#define SINGLETON_H

#include <memory>
#include <iostream>
#include <mutex>

//模板类的实现只能在头文件里面写
template <typename T>
class Singleton {
protected:
    Singleton() = default;  // 单例类构造函数是protected的，这样子类去继承时可以构造这个基类
    Singleton(const Singleton& other) = delete;  // 不允许拷贝构造
    Singleton& operator=(const Singleton& other) = delete;  // 不允许拷贝赋值

    static std::shared_ptr<T> _instance;  // 静态成员变量，存储唯一实例
    static std::once_flag _once_flag;  // 用于线程安全的初始化（C++11），一个标记变量，记录 是否已经执行过

public:
    static std::shared_ptr<T> GetInstance() {
        //线程安全的懒汉式单例，避免初始化多次
        std::call_once(_once_flag, []() {
            _instance = std::shared_ptr<T>(new T());
        });
        return _instance;
    }
    ~Singleton(){
        std::cout <<  "this is singleton destruct" << std::endl;
    }

    void printAddress(){
        std::cout << _instance.get() << std::endl;
    }
};

// 静态成员变量需要在类外初始化，模板类放在头文件里面
template <typename T>
std::shared_ptr<T> Singleton<T>::_instance = nullptr;

template <typename T>
std::once_flag Singleton<T>::_once_flag;




#endif // SINGLETON_H
