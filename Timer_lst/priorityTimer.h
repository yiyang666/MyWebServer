#ifndef PRIORITYTIMER_H
#define PRIORITYTIMER_H

#include <iostream>
#include <vector>
#include <queue>
#include <memory>
#include <functional>   // 管理回调函数
#include <arpa/inet.h>  
#include <time.h>
#include <chrono>
#include"../http/http_conn.h"

// 前向声明
class http_conn;

// 智能指针的别名:全局作用域
class timer_node;
using SPTNode = std::shared_ptr<timer_node>;

// 统一规范化时间
using Clock = std::chrono::system_clock;// 获取当前时间
using time_p = Clock::time_point;       // 统一时间数据类型
using SEC = std::chrono::seconds;       // 把int转换为秒

// 定时器类
class timer_node {
public:
    // 构造函数，初始化参数列表
    timer_node(int ns);

    // 定时器的析构函数很关键
    ~timer_node();

    bool isVaild();
    void upadte(int ns) { this->expire_ = Clock::now() + SEC(ns); } // 更新超时时间
    time_p getExpire() const { return this->expire_; }// 获取超时时间

public:
    std::weak_ptr<http_conn> user_data; // 对该连接对象进行弱引用，定时器删除时销毁原来的强引用
    
private:
    time_p expire_;     // 超时时间
};

struct TimerCmp {
    bool operator()(const SPTNode& a, 
                    const SPTNode& b) const {
        return a->getExpire() > b->getExpire(); // 小根堆：过期时间小的优先级高
    }
};

// 时间堆类，使用优先队列实现
class timerQueue {
public:
    timerQueue();
    ~timerQueue();

    
    SPTNode add_timer(int ns);// 添加定时器，返回这个定时器
    void tick();// 心搏函数,根据定时器超时时间，清理超时连接

private:
    // 使用优先队列实现定时器堆，入队自动调整，保持最小堆，出队也会自动调整，同时定时器sharedptr会把
    std::priority_queue<SPTNode, std::vector<SPTNode>,TimerCmp> timer_queue;
};

#endif

