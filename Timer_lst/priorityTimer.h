#ifndef PRIORITYTIMER_H
#define PRIORITYTIMER_H

#include <iostream>
#include <vector>
#include <queue>
#include <memory>
#include <arpa/inet.h>  
#include <time.h>
#include <chrono>

#include"../http/http_conn.h"

#define TIMESLOT 5  //时间间隔，每次请求延长超时间为此间隔的三倍

// 前向声明
class http_conn;
class timer_node;
// 给管理连接对象的sharedptr起别名
using SPHttp = std::shared_ptr<http_conn>;
// 给管理定时器类的sharedptr起别名
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
    // 指向定时器的sharedptr被列队pop后，引用计数为0，定时器会调用析构函数
    ~timer_node();
    // 删除标记，用来区分资源回收方式
    void setdeleted() { deleted_ = true; }
    // 判断是否标记删除
    bool isDeleted() { return deleted_; }
    void cancelDeleted() { deleted_ = false; }
    // 判断是否有效，超时返回false
    bool isVaild();
    // 更新超时时间
    void upadte(int ns) { this->expire_ = Clock::now() + SEC(ns); }
    // 获取超时时间
    time_p getExpire() const { return this->expire_; }

public:
    // 绑定连接对象，弱引用，防止循环引用
    std::weak_ptr<http_conn> user_data; 
    
private:
    time_p expire_;     // 超时时间
    bool deleted_;      // 删除标记
};

// 小根堆：过期时间小的优先级高
struct TimerCmp {
    bool operator()(const SPTNode& a, 
                    const SPTNode& b) const {
        return a->getExpire() > b->getExpire(); 
    }
};

// 时间堆类，使用优先队列实现
class timerQueue {
public:
    timerQueue();
    ~timerQueue();

    // 添加定时器，返回这个定时器
    SPTNode add_timer(int ns);
    // 心搏函数,根据定时器超时时间，清理超时连接
    void tick();

private:
    // 使用优先队列实现定时器堆，入队自动调整，保持最小堆，出队也会自动调整，同时定时器sharedptr会把
    std::priority_queue<SPTNode, std::vector<SPTNode>,TimerCmp> timer_queue;
};

#endif

