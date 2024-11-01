#ifndef LOCKER_H
#define LOCKER_H

#include <exception>
#include <pthread.h>
#include <semaphore.h>

// 线程同步机制封装类

// 互斥锁类
class locker {
public:
    locker() {
        // 初始化互斥锁
        if(pthread_mutex_init(&m_mutex, NULL) != 0) {
            throw std::exception();// 抛出异常
        }
    }

    ~locker() {
        pthread_mutex_destroy(&m_mutex);// 销毁互斥锁
    }

    bool lock() {
        return pthread_mutex_lock(&m_mutex) == 0;
    }

    bool unlock() {
        return pthread_mutex_unlock(&m_mutex) == 0;
    }
    // 获取互斥锁
    pthread_mutex_t *get()
    {
        return &m_mutex;
    }

private:
    pthread_mutex_t m_mutex;
};


// 条件变量类
class cond {
public:
    cond(){
        // 初始化条件变量
        if (pthread_cond_init(&m_cond, NULL) != 0) {
            throw std::exception();
        }
    }
    ~cond() {
        pthread_cond_destroy(&m_cond);
    }

    bool wait(pthread_mutex_t *m_mutex) {
        // 阻塞等待条件成立，一旦收到信号，就加上锁
        int ret = 0;
        ret = pthread_cond_wait(&m_cond, m_mutex);
        return ret == 0;
    }
    bool timewait(pthread_mutex_t *m_mutex, struct timespec t) {
        int ret = 0;
        ret = pthread_cond_timedwait(&m_cond, m_mutex, &t);
        return ret == 0;
    }
    bool signal() {
        // 唤醒一个或多个线程
        return pthread_cond_signal(&m_cond) == 0;
    }
    bool broadcast() {
        // 唤醒全部线程
        return pthread_cond_broadcast(&m_cond) == 0;
    }

private:
    pthread_cond_t m_cond;
};


// 信号量类
class sem {
public:
    sem() {
        // 初始化信号量
        if( sem_init( &m_sem, 0, 0 ) != 0 ) {
            throw std::exception();
        }
    }
    sem(int num) {
        // 传入信号量的大小
        if( sem_init( &m_sem, 0, num ) != 0 ) {
            throw std::exception();
        }
    }
    ~sem() {
        sem_destroy( &m_sem );
    }
    // 等待信号量
    bool wait() {
        // 对信号量加锁，调用一次对信号量的值-1，如果值为0，就阻塞
        return sem_wait( &m_sem ) == 0;
    }
    // 增加信号量
    bool post() {
        // 对信号量解锁，调用一次对信号量的值+1
        return sem_post( &m_sem ) == 0;
    }
private:
    sem_t m_sem;
};

#endif