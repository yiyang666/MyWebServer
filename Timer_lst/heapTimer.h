#ifndef HEAPTIMER_H
#define HEAPTIMER_H

// 定时器------最小堆的数组实现

#include <iostream>
#include <arpa/inet.h>
#include <time.h>
#include <assert.h>
#include "../logs/log.h"

using namespace std;
// 向前声明
class heap_timer;

// 用户连接数据结构
struct client_data
{
    sockaddr_in address;  // 客户端socket地址
    int sockfd;           // socket文件描述符
    heap_timer* timer;    // 定时器
};


// 定时器类
class heap_timer
{
public:
    heap_timer(){}
    ~heap_timer(){}
public:
    time_t expire;   // 任务超时时间，这里使用绝对时间
    void (*call_back)( client_data* ); // 任务回调函数，回调函数处理的客户数据，由定时器的执行者传递给回调函数
    client_data* user_data; 
};

// 时间堆类,控制整个堆的构建
class time_heap
{

public:
    // 构造函数1,初始化一个大小为cap的空堆
    time_heap(int cap);

    // 构造函数2,用已有数组来初始化堆
    time_heap(heap_timer **init_array, int size, int capacity);

    // 销毁时间堆
    ~time_heap();

public:
    // 添加目标定时器timer
    void add_timer(heap_timer *timer);

    // 删除目标定时器timer
    void del_timer(heap_timer *timer);
    /*
        这里说明一下 const 在函数返回值前和函数名后的区别
        const 在返回值前表示该函数的返回值只能被读取，不能被修改
        const 在函数名后表示该函数内部只能访问类成员，而不能修改类成员
     */
    // 获得堆顶部的定时器
    heap_timer *top() const;

    // 删除堆顶部的定时器
    void pop_timer();

    // 调整目标定时器位置
    void adj_timer(heap_timer *timer);

    // 心搏函数
    void tick();

    // 判断堆是否为空
    bool empty() const { return cur_size == 0; }

private:
    //下滤操作,维护数组的最小堆性质
    void percolate_down(int hole);

    // 将堆数组扩大一倍
    void resize();

private:
    heap_timer **array; //堆数组
    int capacity;       //堆数组容量
    int cur_size;     //堆数组当前元素个数
};

#endif