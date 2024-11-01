#ifndef BLOCK_DEQUE_H
#define BLOCK_DEQUE_H


#include <deque>        // 用于双端队列
#include <sys/time.h>   // 用于计时
#include <assert.h>     // 断言
#include "../lock/locker.h"     // 之前定义的锁类

// 模板类 BlockDeque，用于实现异步日志的阻塞队列
template<class T>
class BlockDeque {
public:
    // 1.初始化，构造函数，指定队列的最大容量
    BlockDeque(size_t max_size = 1000);

    // 2.析构函数，关闭队列
    ~BlockDeque();

    // 3.判断队列是否为空
    bool empty();

    // 4.判断队列是否已满
    bool full();

    // 5.获取队列头部元素
    T front();

    // 6.获取队列尾部元素
    T back();

    // 7.获取队列的大小
    size_t size();

    // 8.获取队列的容量
    size_t capacity();

    // 9.向队列尾部添加元素
    void push(const T &item);

    // // 10.向队列头部添加元素
    // void push_front(const T &item);

    // 10.从队列头部移除元素
    bool pop(T &item);

    // 11.从队列头部移除元素，带超时时间
    bool pop(T &item, int timeout);

    // // 14.通知消费者
    // void flush();

private:
    // 双端队列，用于存储数据
    std::deque<T> m_deq;

    // 队列的最大容量
    size_t m_capacity;

    // 互斥锁，用于保护队列数据
    locker m_mutex;
    //std::mutex mtx_;

    // 标记队列是否关闭
    bool isClose;

    // 条件变量，一个就够了
    cond m_cond;

    // // 条件变量，用于消费者等待
    // cond m_cond_consumer;

    // // 条件变量，用于生产者等待
    // cond m_cond_producer;
};


// 构造函数，初始化队列
template<class T>
BlockDeque<T>::BlockDeque(size_t max_size) :m_capacity(max_size) {

    assert(max_size > 0); // 断言容量大于0
    isClose = false;     // 初始化为未关闭状态
}

// 析构函数，关闭队列
template<class T>
BlockDeque<T>::~BlockDeque() {

    m_mutex.lock();    // 使用锁保护队列数据
    m_deq.clear();     // 清空队列
    isClose= true;     // 标记为已关闭
    m_mutex.unlock();  // 解锁

    m_cond.broadcast();
};

// 判断队列是否为空
template<class T>
bool BlockDeque<T>::empty() {
    m_mutex.lock();     // 使用锁保护队列数据
    if(m_deq.empty()) 
    {
        m_mutex.unlock();
        return true;
    }
    m_mutex.unlock();
    return false;  
}

// 判断队列是否已满
template<class T>
bool BlockDeque<T>::full(){
    m_mutex.lock(); // 使用锁保护队列数据
    if(m_deq.size()>= m_capacity)
    {
        m_mutex.unlock();
        return true;
    }
    m_mutex.unlock();
    return false;         // 返回队列是否已满
}

// 获取队列头部元素
template<class T>
T BlockDeque<T>::front() {
    T temp=NULL;
    m_mutex.lock();     // 使用锁保护队列数据
    temp=m_deq.front();
    m_mutex.unlock();
    return temp;        // 返回队列头部元素            
}

// 获取队列尾部元素
template<class T>
T BlockDeque<T>::back() {

    T temp=NULL;
    m_mutex.lock();     // 使用锁保护队列数据
    temp=m_deq.back();
    m_mutex.unlock();
    return temp;        // 返回队列尾部元素
}

// 获取队列大小
template<class T>
size_t BlockDeque<T>::size() {

    size_t temp=NULL;
    m_mutex.lock();     // 使用锁保护队列数据
    temp=m_deq.size();
    m_mutex.unlock();
    return temp; // 返回队列大小
}

// 获取队列容量
template<class T>
size_t BlockDeque<T>::capacity() {

    size_t temp=0;
    m_mutex.lock();     // 使用锁保护队列数据
    temp=m_capacity;
    m_mutex.unlock();
    return temp;   // 返回队列容量
}

// 向队列尾部添加元素
template<class T>
void BlockDeque<T>::push(const T &item) {
    m_mutex.lock();     // 使用独占锁保护队列数据
    // 满了就是同步写
    // while(m_deq.size() >= m_capacity) {     // 如果队列已满
    //     m_cond.wait(m_mutex.get());     // 生产者等待
    // }
    m_deq.push_back(item);            // 添加元素到队列尾部
    m_mutex.unlock();
    m_cond.signal();         // 通知一个或多个消费者
}

// // 向队列头部添加元素
// template<class T>
// void BlockDeque<T>::push_front(const T &item) {
//     std::unique_lock<std::mutex> locker(mtx_); // 使用独占锁保护队列数据
//     while(deq_.size() >= capacity_) {       // 如果队列已满
//         condProducer_.wait(locker);       // 生产者等待
//     }
//     deq_.push_front(item);               // 添加元素到队列头部
//     condConsumer_.notify_one();         // 通知一个消费者
// }

// 从队列头部移除元素
template<class T>
bool BlockDeque<T>::pop(T &item) {
    m_mutex.lock();         // 使用独占锁保护队列数据
    while(m_deq.empty()){                   // 如果队列为空
        m_cond.wait(m_mutex.get());       // 消费者等待
        if(isClose){                     // 如果队列已关闭
            return false;                // 返回失败
        }
    }
    item = m_deq.front();             // 获取队列头部元素
    m_deq.pop_front();                // 移除队列头部元素

    m_mutex.unlock();                 // 解锁
    //m_cond.signal();         // 通知一个或多个生产者

    return true;                      // 返回成功
}

// 从队列头部移除元素，带超时时间
template<class T>
bool BlockDeque<T>::pop(T &item, int timeout) {
    m_mutex.lock(); // 使用独占锁保护队列数据
    while(m_deq.empty()){                   // 如果队列为空
        
        // 将超时时间转换为 timespec 结构体
        struct timespec ts;
        clock_gettime(CLOCK_MONOTONIC, &ts);
        ts.tv_sec += timeout / 1000;//毫秒
        ts.tv_nsec += (timeout % 1000) * 1000000;//纳秒
        if (ts.tv_nsec >= 1000000000) {
            ts.tv_sec++;
            ts.tv_nsec -= 1000000000;
        }
        // 使用 timewait 函数进行超时等待
        if(!m_cond.timewait(m_mutex.get(), ts)){ // 超时返回失败
            return false;
        }
        if(isClose){                     // 如果队列已关闭
            return false;                // 返回失败
        }
    }
    item = m_deq.front();                 // 获取队列头部元素
    m_deq.pop_front();                  // 移除队列头部元素
    m_mutex.unlock();
    m_cond.signal();         // 通知一个生产者
    return true;                      // 返回成功
}

#endif // BLOCKQUEUE_H