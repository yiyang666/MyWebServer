
#include "priorityTimer.h"

timer_node::timer_node(int ns)
{
    this->expire_ = Clock::now() + SEC(ns);
    this->deleted_ = false;
}

timer_node::~timer_node(){}

bool timer_node::isVaild()
{
    if (this->expire_ <= Clock::now()){
        return false;
    }
    return true;
}

// 时间堆类，使用优先队列实现
timerQueue::timerQueue() {}
timerQueue::~timerQueue() {}

SPTNode timerQueue::add_timer(int ns)
{
    //SPTNode new_node(new timer_node(user_data,ns));// 下面这种方式更高效
    SPTNode new_node = std::make_shared<timer_node>(ns);
    timer_queue.push(new_node);

    return new_node;
}

void timerQueue::tick() {
    while (!timer_queue.empty())
    {
        // 临时的sharedptr会在作用域外自动销毁，引用计数先+1后-1
        SPTNode temp_timer = timer_queue.top(); 
        SPHttp temp_conn = temp_timer->user_data.lock();

        if (!temp_timer->isVaild())
        {
            // 如果未被标记，断开连接，标记删除，更新为容忍时间，期间不删除定时器和连接信息
            if (!temp_timer->isDeleted())
            {
                temp_conn->close_conn();
                LOG_INFO("Normally close to client(%s) cfd(%d)", 
                    inet_ntoa(temp_conn->get_address()->sin_addr), temp_conn->get_sockfd());
                break;
            }
            // 如果已经被标记了，说明容忍时间已经到了，释放定时器和连接对象资源
            temp_conn->release_conn();
            // 出队自动维护最小堆，定时器的引用计数-1为0，定时器开始析构
            timer_queue.pop(); 
            continue;
        }
        else
            break;
    }
}




