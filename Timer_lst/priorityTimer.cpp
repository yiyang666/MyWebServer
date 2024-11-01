

#include "priorityTimer.h"

timer_node::timer_node(int ns)
{
    this->expire_ = Clock::now() + SEC(ns);
}

// 定时器的析构函数很关键，指向定时器的shared被列队pop后，没有其它强引用了
// 定时器会调用析构函数，所以这里把当前绑定的连接对象也销毁
// 具体地，如果连接对象还存在，expired返回fasle，那么就获取强引用去销毁掉
// 此时连接对象的引用计数-1，没有其它强引用了，所以整个连接对象就随着定时器一起被释放了~
timer_node::~timer_node()
{
    if(!user_data.expired())
        user_data.lock().reset();
}

bool timer_node::isVaild()
{
    if (this->expire_ <= Clock::now())
    {
        return false;
    }
    else
    {
        return true;
    }
}

// 时间堆类，使用优先队列实现
timerQueue::timerQueue() {}
timerQueue::~timerQueue() {}

    // 添加定时器
SPTNode timerQueue::add_timer(int ns)
{
    //SPTNode new_node(new timer_node(user_data,ns));// 下面这种方式更高效
    SPTNode new_node = std::make_shared<timer_node>(ns);
    timer_queue.push(new_node);

    return new_node;
}

// 心跳函数,根据定时器超时时间，清理超时连接，
void timerQueue::tick() {
    while (!timer_queue.empty())
    {
        // 临时的sharedptr在作用域外会自动销毁，引用计数先+1后-1
        SPTNode temp_timer = timer_queue.top(); 
        // 超时了就要被无条件清理
        if (!temp_timer->isVaild())
        {
            temp_timer->user_data.lock()->close_conn(true);
            // 出队自动维护最小堆，引用计数-1为0，定时器开始析构
            timer_queue.pop(); 
            continue;
        }
        else
            break;
    }
}




