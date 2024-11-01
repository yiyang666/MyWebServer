#ifndef LOG_H
#define LOG_H

#include <stdio.h>
#include <iostream>
#include <string.h>
#include <stdarg.h>
#include <pthread.h>

#include "../lock/locker.h"
#include "block_deque.h"

using namespace std;

class Log
{
public:

    // 可选择的参数有日志文件、日志缓冲区大小、最大行数以及阻塞队列最大长度
    bool init(const char *file_name,int log_flag, int log_buf_size = 8192, 
        int split_lines = 5000000, int max_deque_size = 0);

    // C++11 规定了静态对象的初始化顺序，确保了在多线程环境下，静态对象只会被初始化一次。
    // C++11以后，使用局部静态变量懒汉不用加锁
    static Log* get_instance()
    {
        static Log inst;
        return &inst;
    }

    // 异步写日志公有方法，调用私有方法async_write_log
    static void *flush_log_thread(void *args)
    {
        Log::get_instance()->async_write_log();
    }

    // 将输出内容按照标准格式整理
    void write_log(int level,const char *format,...);

    // 强制刷新缓冲区
    void flush(void);

private:
    Log();
    virtual ~Log();

    // 异步写日志方法
    void *async_write_log()
    {
        string single_log;
        //从阻塞队列取一个日志文件string，写入文件
        while(m_log_deq->pop(single_log))
        {
            // 取成功，则加锁写入磁盘
            m_mutex.lock();
            fputs(single_log.c_str(), m_fp);
            m_mutex.unlock();
        }
    }
public:
    static int m_log_flag;               // 日志标记，0/关闭，1/异步，2/同步

private:

    char dir_name[128]; // 路径名
    char log_name[128]; // 日志文件名
    int m_max_lines;    // 日志最大行数
    int m_log_buf_size; // 日志缓冲区大小
    long long m_count;  // 日志当前行数
    int m_today;        // 按天分类，记录当前时间是哪一天
    FILE *m_fp;         // 打开的日志文件指针
    char *m_buf;        // 要输出的内容
    BlockDeque<string> *m_log_deq;// 阻塞队列
    locker m_mutex;               // 互斥锁
    
};

// 这四个宏定义在其他文件中使用，主要用于不同类型的日志输出
#define LOG_DEBUG(format, ...)if(Log::m_log_flag) {Log::get_instance()->write_log(0, format, ##__VA_ARGS__); Log::get_instance()->flush();}
#define LOG_INFO(format, ...) if(Log::m_log_flag) {Log::get_instance()->write_log(1, format, ##__VA_ARGS__); Log::get_instance()->flush();}
#define LOG_WARN(format, ...) if(Log::m_log_flag) {Log::get_instance()->write_log(2, format, ##__VA_ARGS__); Log::get_instance()->flush();}
#define LOG_ERROR(format, ...) if(Log::m_log_flag) {Log::get_instance()->write_log(3, format, ##__VA_ARGS__); Log::get_instance()->flush();}

#endif
