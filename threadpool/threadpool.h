#ifndef THREADPOOL_H
#define THREADPOOL_H

#include <list>
#include <cstdio>
#include <exception>
#include <pthread.h>
#include "../lock/locker.h"
#include"../logs/log.h"

// 线程池类，将它定义为模板类是为了代码复用，模板参数T是任务类 
// 使用模板的好处:

// 1.代码复用: 通过使用模板，你可以创建一个通用的线程池，可以处理不同类型的任务，而无需为每种任务类型编写单独的线程池代码。

// 2.类型安全: 模板可以确保类型安全，避免在编译时出现类型错误。
template<typename T>
class threadpool {
public:
    /*thread_number是线程池中线程的数量，max_requests是请求队列中最多允许的、等待处理的请求的数量*/
    threadpool(int thread_number = 8, int max_requests = 15000);
    ~threadpool();
    bool append(T* request);

private:
    /*工作线程运行的函数，它不断从工作队列中取出任务并执行之*/
    // 注意这里worker是静态成员函数
    static void* worker(void* arg);
    void run();

private:
    // 线程的数量
    int m_thread_number;  
    
    // 描述线程池的数组，大小为m_thread_number    
    pthread_t * m_threads;

    // 请求队列中最多允许的、等待处理的请求的数量  
    int m_max_requests; 
    
    // 请求队列
    std::list< T* > m_workqueue;  

    // 保护请求队列的互斥锁
    locker m_queuelocker;   

    // 信号量是否有任务需要处理
    sem m_queuestat;

    // 是否结束线程          
    bool m_stop;                    
};

template< typename T >
threadpool< T >::threadpool(int thread_number, int max_requests) : 
        m_thread_number(thread_number), m_max_requests(max_requests), 
        m_stop(false), m_threads(NULL) 
{

    if((thread_number <= 0) || (max_requests <= 0) ) {
        throw std::exception();
    }

    m_threads = new pthread_t[m_thread_number];
    if(!m_threads) {
        throw std::exception();
    }

    // 创建thread_number 个线程，并将他们设置为脱离线程。
    for ( int i = 0; i < thread_number; ++i ) {
        if(pthread_create(m_threads + i, NULL, worker, this ) ) {
            delete [] m_threads;
            throw std::exception();
        }
        
        if( pthread_detach( m_threads[i] ) ) {
            delete [] m_threads;
            throw std::exception();
        }
    }
    std::cout << "Successfully created threads: " << thread_number << std::endl;
    LOG_INFO("Successfully created threads: %d", thread_number);
}

template< typename T >
threadpool< T >::~threadpool() {
    delete [] m_threads;
    m_stop = true;
}

template< typename T >
bool threadpool< T >::append( T* request )
{
    // 操作工作队列时一定要加锁，因为它被所有线程共享。
    m_queuelocker.lock();
    // 超出能处理的最大事件数量
    if ( m_workqueue.size() > m_max_requests ) {
        m_queuelocker.unlock();
        return false;
    }
    // 添加到事件队列
    m_workqueue.push_back(request);
    m_queuelocker.unlock();
    m_queuestat.post();//信号量++，此时会在wait()中唤醒一个或多个线程
    return true;
}

template< typename T >
void* threadpool< T >::worker( void* arg ) //线程被创建后开始执行
{
    // 将这个传入的void* 参数转换为线程池对象
    threadpool* pool = ( threadpool* )arg;
    pool->run();//线程执行任务
    return pool;
}

template< typename T >
void threadpool< T >::run() {

    while (!m_stop) {
        // 获取任务，如果信号量为0，则阻塞在此
        m_queuestat.wait();//信号量--
        m_queuelocker.lock();
        // 下面两个判断可省略
        if ( m_workqueue.empty() ) {
            m_queuelocker.unlock();
            continue;
        }
        T* request = m_workqueue.front();
        m_workqueue.pop_front();
        m_queuelocker.unlock();
        if ( !request ) {
            continue;
        }
        // 执行任务
        request->process();
    }

}

#endif
