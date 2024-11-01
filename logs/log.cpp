#include "log.h"
#include <sys/stat.h>

int Log::m_log_flag = 0;// 类外初始化

Log::Log(){
    m_count = 0;
    m_log_flag=0;// 默认是关闭的
}
Log::~Log(){
    if(m_fp!=NULL){
        fclose(m_fp);
    }
}
// 异步需要设置阻塞队列的长度，同步不需要设置
bool Log::init(const char *file_name, int log_flag, int log_buf_size,
                 int max_lines, int max_deq_size)
{
    m_log_flag = log_flag;// 修改静态成员变量
    // 如果设置了max_deq_size,则设置为异步模式
    if(max_deq_size>=1)
    {
        m_log_deq = new BlockDeque<string>(max_deq_size);
        pthread_t tid;
        // //flush_log_thread为回调函数,这里表示创建线程异步写日志
        pthread_create(&tid, NULL, flush_log_thread, NULL);
    }

    // 创建日志缓冲区
    m_log_buf_size = log_buf_size;
    m_buf = new char[m_log_buf_size];
    memset(m_buf, '\0', m_log_buf_size);

    m_max_lines = max_lines; //日志最大行数

    // 获取当前时间，结构体tm
    time_t t = time(nullptr);
    struct tm *sys_tm = localtime(&t);
    struct tm my_tm = *sys_tm;// 避免悬空指针，代码更清晰，方便修改

    // 从后往前找到第一个 / 的位置
    const char *p = strrchr(file_name, '/');
    char log_full_name[256] = {0};

    // 若输入的文件名没有 /，则直接将时间+文件名作为日志名
    if(p==nullptr){
        snprintf(log_full_name, 255, "%d_%02d_%02d_%s", my_tm.tm_year + 1900,
                 my_tm.tm_mon + 1, my_tm.tm_mday, file_name);
    }
    else{
        // 将路径名复制到dir_name中,并创建这个目录
        strncpy(dir_name, file_name, p - file_name + 1);
        // cout << "dir name=" << log_name << endl;
        struct stat sb;
        if(stat(dir_name,&sb)!=0){// 判断目录是否存在，不存在则创建
            if (mkdir(dir_name, 0777) != 0) {
                perror("mkdir error");
                return false;
            }   
        }
        // 将文件名复制到log_name中，加1是因为 p 指向的是 /
        strcpy(log_name, p + 1);
        // cout << "log name=" << log_name << endl;
        snprintf(log_full_name, 255, "%s%d_%02d_%02d_%s", dir_name, my_tm.tm_year + 1900,
                 my_tm.tm_mon + 1, my_tm.tm_mday, log_name);
        // printf("%s\n", log_full_name);
    }

    m_today = my_tm.tm_mday;

    m_fp = fopen(log_full_name, "a");
    if (m_fp==nullptr)  return false;
    return true;
}

// 写日志
void Log::write_log(int level,const char *format,...)
{
    struct timeval now = {0, 0};
    gettimeofday(&now, NULL);
    time_t t = now.tv_sec;
    struct tm *sys_tm = localtime(&t);
    struct tm my_tm = *sys_tm;
    char s[16] = {0};

    // 日志分级
    switch (level)
    {
    case 0:
        strcpy(s, "[debug]:");
        break;
    case 1:
        strcpy(s, "[info]:");
        break;
    case 2:
        strcpy(s, "[warn]:");
        break;
    case 3:
        strcpy(s, "[erro]:");
        break;
    default:
        strcpy(s, "[info]:");
        break;
    }
    // 上个锁
    m_mutex.lock();

    // 更新现有行数
    m_count++;

    //如果日志不是今天的 或 写入的日志行数是最大行的倍数
    if(m_today!= my_tm.tm_mday || m_count % m_max_lines==0)
    {
        char new_log[256] = {0};
        fflush(m_fp);//将缓冲区中的内容强制刷新到磁盘，确保数据被写入到文件
        fclose(m_fp);
        char tail[16] = {0};

        // 格式化日志名中的时间部分
        snprintf(tail, 16, "%d_%02d_%02d_", my_tm.tm_year + 1900,
                 my_tm.tm_mon + 1, my_tm.tm_mday);
        //如果时间不是今天，则创建今天的日志，更新m_doday和m_count
        if(m_today!=my_tm.tm_mday)
        {
            snprintf(new_log, 255, "%s%s%s", dir_name, tail, log_name);
            m_today = my_tm.tm_mday;
            m_count = 0;
        }else{
        // 超过了最大行，在之前的日志名后面加上后缀，m_count/m_max_lines
            snprintf(new_log, 255, "%s%s%s_%lld", dir_name, tail, log_name, m_count / m_max_lines);
        }
        m_fp = fopen(new_log, "a");
    }
    // 解锁
    m_mutex.unlock();

    // 初始化可变参数列表format->valst
    va_list val;
    va_start(val, format);

    string log_str;
    //加锁构建字符串log_str
    m_mutex.lock();

    // 写入内容格式：时间 + 内容
    // 时间格式化，snprintf成功返回写字符的总数，不包括结尾的 \0 字符
    int n = snprintf(m_buf, 48, "%d-%02d-%02d %02d:%02d:%02d.%06ld %s ",
                     my_tm.tm_year + 1900, my_tm.tm_mon + 1, my_tm.tm_mday,
                     my_tm.tm_hour, my_tm.tm_min, my_tm.tm_sec, now.tv_usec, s);

    // 内容格式化，用于向字符串中打印数据，数据格式用户自定义，返回写入到字符数组str中的字符个数
    int m = vsnprintf(m_buf + n, m_log_buf_size - 1, format, val);
    m_buf[n + m] = '\n';
    m_buf[n + m + 1] = '\0';
    log_str = m_buf;
    // 解锁
    m_mutex.unlock();

    // 异步加入阻塞队列，同步加锁写到文件中
    if(m_log_flag==1 && !m_log_deq->full())
    {
        m_log_deq->push(log_str);
    }
    else{
        m_mutex.lock();
        // c_str() 是 C++ 字符串类的一个成员函数，用于返回一个指向字符串的 const char* 指针
        fputs(log_str.c_str(), m_fp);
        m_mutex.unlock();
    }
    va_end(val);
}

// 强制刷新缓冲区
void Log::flush(void)
{
    m_mutex.lock();
    fflush(m_fp);
    m_mutex.unlock();
}