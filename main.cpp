#include <cstdio>
#include <stdlib.h>
#include <string.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <error.h>
#include <fcntl.h>
#include <sys/epoll.h>
#include <signal.h>
#include <errno.h>
#include <iostream>
#include <memory>

#include "./lock/locker.h"
#include "./http/http_conn.h"
#include "./threadpool/threadpool.h"
#include "./Timer_lst/priorityTimer.h"
#include "./MySQL/sql_conn_pool.h"
#include "./logs/log.h"


#define MAX_FD 65535 // 最大的文件描述符个数
#define MAX_EVENT_NUMBER 15000 // 监听的最大的事件数量

// 添加文件描述符到epoll中
extern void addfd(int epollfd,int fd,bool one_shot);
// 从epoll中删除文件描述符
extern void removefd(int epollfd,int fd);
// 修改文件描述符
extern void modfd(int epollfd,int fd,int flag);
// 设置非阻塞文件描述符
extern int setnonblocking( int fd );

static int pipefd[2]; //定义一个管道用于信号传输
static timerQueue timer_queue; // 定时器链表/超时队列
static int epfd; //epoll描述符

// 处理定时信号，发送信号到写管道，使其用epoll监听读管道
void sig_send( int sig )
{
    //为保证函数的可重入性，保留原来的errno
    int save_errno = errno;
    int msg = sig;
    send( pipefd[1], ( char* )&msg, 1, 0 );
    errno = save_errno;
}

// 注册信号捕捉：1.用于忽略管道破裂信号SIGPIPE，2.捕捉终止信号和定时信号
void addsig(int sig  ,void(handler)(int))
{
    struct sigaction sa;
    memset(&sa,'\0',sizeof(sa));
    sa.sa_handler=handler;//信号处理函数
    // SA_RESTART 在网络编程、文件I/O等场景下，经常会用到这个标志，
    // 这个操作可以提高程序的健壮性。
    // 使系统调用自动重启，从而避免由于信号中断导致的系统调用失败。
    sa.sa_flags |= SA_RESTART;
    sigfillset(&sa.sa_mask);//临时阻塞
    assert( sigaction( sig, &sa, NULL ) != -1 );//注册信号
}

// epoll接收到信号后，优先处理IO，再执行此函数调用tick()来删除过期用户
void timer_handler()
{
    LOG_INFO("%s, current client numbers are %d ", "The timer tick is working ...",http_conn::m_user_count);
    // printf("The timer_handler is working ..., current client numbers are %d \n",http_conn::m_user_count);

    timer_queue.tick();// 调用定时器的tick()函数，心搏函数
    alarm(TIMESLOT); // 重新发定时信号
}

void show_error(int connfd, const char *info)
{
    printf("%s", info);
    send(connfd, info, strlen(info), 0);
    close(connfd);
}

int main(int argc, char *argv[])
{
    if(argc<3){
        printf("按照如下格式运行：%s port_number log_flag\n",basename(argv[0]));
        exit(-1);
    }

    // 设置日志
    int log_flag = atoi(argv[2]);
    if (log_flag==1)
    { 
        // 异步日志
        Log::get_instance()->init("log_file/yb200ServerLog",log_flag, 2000, 800000, 200);
        LOG_INFO("异步日志开启！");
        printf("异步日志开启！\n");
    }
    else if (log_flag==2)
    {   
        // 同步日志
        Log::get_instance()->init("log_file/TesttbServerLog",log_flag, 2000, 800000, 0);
        LOG_INFO("同步日志开启！");
        printf("同步日志开启！\n");
    }
    else{
        log_flag = 0;
        printf("日志系统关闭！\n");
    }

    //获取端口号
    int port=atoi(argv[1]);;
    //int port=9999;

    // 对SIGPIE信号做处理，防止客户端意外断开连接，终止进程
    // 对SIGALRM、SIGTERM设置信号处理函数
    addsig(SIGPIPE,SIG_IGN);
    addsig(SIGALRM,sig_send);
    addsig(SIGTERM,sig_send);

    // 创建数据库连接池
    sql_conn_pool *connPool = sql_conn_pool::GetInstance();
    connPool->init("localhost", "young", "123456", "WebServer", 3366, 8);
    // 作为静态变量给连接类初始化
    http_conn::m_connPool = connPool;

    //创建线程池，初始化线程池
    threadpool<http_conn> *pool=nullptr;
    try{
        pool=new threadpool<http_conn>;
    }catch(...){
        exit(-1);
    }
    
    // V1：http_conn *users=new http_conn[MAX_FD];// 静态数组法
    // V2：std::vector<http_conn> users;// vector版本
    // users.reserve(MAX_FD);
    // V3：创建一个week智能指针数组来管理每一个连接对象
    //  SPHttp users[MAX_FD]; //并没有初始化，指针指向空
    // V4：智能指针数组和一个指向该数组的unique指针
    http_conn::users = std::make_unique<SPHttp[]>(MAX_FD);

    //  静态方法初始化数据库静态表
    http_conn::initmysql_table();

    // 创建监听套接字
    int listenfd= socket(PF_INET,SOCK_STREAM,0);
    if (listenfd == -1) {
        perror("socket");
        exit(-1);
    }

    // 设置端口复用
    int opt=1;
    setsockopt(listenfd,SOL_SOCKET,SO_REUSEPORT,&opt,sizeof(opt));

    // 绑定
    struct sockaddr_in address;
    memset(&address,0,sizeof(address));
    address.sin_family=AF_INET;
    address.sin_addr.s_addr=INADDR_ANY;
    address.sin_port=htons(port);
    int ret=0;
    ret=bind(listenfd, (struct sockaddr*)&address, sizeof(address));
    assert(ret!=-1);

    // 监听
    listen(listenfd,1024);

    // 创建epoll对象，事件数组，添加
    epoll_event events[MAX_EVENT_NUMBER];
    epfd=epoll_create(999);

    // 将监听的文件描述符添加到epoll,fasle为非阻塞+LT模式
    addfd(epfd,listenfd,false);
    http_conn::m_epollfd=epfd;

    // 创建发送信号的管道，一端用于发送，一端用于监听
    ret=socketpair(PF_UNIX,SOCK_STREAM,0,pipefd);
    assert(ret!=-1);
    setnonblocking(pipefd[1]);
    addfd(epfd,pipefd[0],false);

    alarm(TIMESLOT);
    bool timeout=false;
    bool stop_server=false;
    while (!stop_server)
    {
        // epoll持续监听
        int num=epoll_wait(epfd,events,MAX_EVENT_NUMBER,-1);
        // 忽略因信号引起的中断
        if((num<0)&&(errno!=EINTR)){
            LOG_ERROR("epoll failure!");
            break;
        }

        //循环遍历事件数组
        for(int i=0;i<num;i++) 
        {
            int curfd=events[i].data.fd;
            //1. 有客户端连接进来
            if(curfd==listenfd)
            {
                // 准备接收客户端信息
                struct sockaddr_in client_address;
                socklen_t client_addlen=sizeof(client_address);

                int connfd=accept(listenfd, (struct sockaddr*)&client_address, &client_addlen);

                if ( connfd < 0 ) {
                    LOG_ERROR("%s:errno is:%d", "accept error", errno);
                    continue;
                } 
                if(http_conn::m_user_count>=MAX_FD){
                    // 最大支持的连接数已满
                    // 给客户端写一个信息：服务器内部正忙
                    show_error(connfd, "Internal Server Busy!");
                    LOG_WARN("%s", "Internal server busy！");
                    continue;
                }
                /*
                判断智能指针管理的对象是否被销毁，
                    1.如果不存在：初始化一个连接对象，交给它管理。初始化，同时创建定时器。
                    2.如果存在：说明当前管理的这个连接对象已经被初始化了，但是还没被释放，只需要更新它的值
                */
                if (http_conn::users[connfd].get()==nullptr)
                {
                    // 让这个shared智能指针指向一个连接类对象
                    // 强引用赋值给强引用，原来的自动销毁
                    http_conn::users[connfd] = std::make_shared<http_conn>(); 
                    // 正式对成员初始化
                    http_conn::users[connfd]->init(connfd, client_address); 
                    // 创建定时器,用sharedptr管理，再把这个sharedptr传出赋值给users中的弱定时器引用（weekptr）
                    SPTNode temp_timer=timer_queue.add_timer( 3 * TIMESLOT);
                    http_conn::users[connfd]->timer = temp_timer;
                    // 再给定时器中的弱引用连接对象赋值，强引用赋值给弱引用
                    temp_timer->user_data = http_conn::users[connfd];
                    // 打印日志
                    LOG_INFO("Connecting to a new client(%s) cfd(%d) ", inet_ntoa(client_address.sin_addr),connfd);
                }
                else
                {
                    http_conn::users[connfd]->init(connfd, client_address);    // 重新初始化该连接对象
                    http_conn::users[connfd]->timer.lock()->upadte(3*TIMESLOT); // 更新该连接对象的定时器
                    http_conn::users[connfd]->timer.lock()->cancelDeleted(); // 重新连接就取消删除标记
                    LOG_INFO("Reconnecting to a new client(%s) cfd(%d) ", inet_ntoa(client_address.sin_addr), connfd);
                }
            
            }
            //2. 处理管道中的信号
            else if((curfd==pipefd[0]) && (events[i].events & EPOLLIN))
            {
                char signals[1024];
                int ret=recv(pipefd[0],signals,sizeof(signals),0);
                if(ret==-1 || ret==0){
                    continue;
                }else{
                    for (int i = 0; i <ret; ++i){
                    // switch中的是字符,case可以是字符,也可以是字符对应的ASCLL码
                        switch (signals[i])
                        {
                        // 用timeout变量标记有定时任务需要处理，但不立即处理定时任务
                        // 这是因为定时任务的优先级不是很高，我们优先处理其他更重要的任务。
                        case SIGALRM:
                            timeout = true; // IO优先，延时处理
                            break;
                        case SIGTERM:
                            stop_server = true;
                        }
                    }
                }

            }
            //3. 处理错误信息事件，客户端关闭连接，移除对应的定时器
            else if(events[i].events & (EPOLLRDHUP|EPOLLHUP|EPOLLERR))
            {
                LOG_ERROR("Eroor messages or FIN in client(%s) cfd(%d)", inet_ntoa(http_conn::users[curfd]->get_address()->sin_addr), curfd);
                // 对方异常断开或者错误事件
                http_conn::users[curfd]->close_conn();
            }
            //4. 主线程处理客户端读事件
            else if(events[i].events & EPOLLIN)
            {
                // 非阻塞IO，主线程一次性把所有数据都读完
                // 从通信缓冲区读到该对象的读缓冲区内
                if(http_conn::users[curfd]->read())
                {
                    LOG_INFO("Deal with the client(%s) cfd(%d)", inet_ntoa(http_conn::users[curfd]->get_address()->sin_addr),curfd);
                    // 有数据传输，更新该客户端的定时器
                    http_conn::users[curfd]->timer.lock()->upadte(3 * TIMESLOT);
                    // 线程池把这个已经读取到客户请求（get/post/...）的请求对象放入请求队列
                    // 交给工作线程去解析，工作线程解析请求后，把响应信息放到写缓冲区
                    pool->append(http_conn::users[curfd].get()); // 把原始指针传过去
                }
                // 对方异常断开或者错误事件，和处理错误事件一样
                else
                {
                    LOG_ERROR("Read error in client(%s) cfd(%d)", inet_ntoa(http_conn::users[curfd]->get_address()->sin_addr),curfd); 
                    // 断开连接，关闭掉这个curfd的事件监听，同时标记删除定时器
                    http_conn::users[curfd]->close_conn();
                }
            }
            //5. 主线程处理客户端写事件
            else if(events[i].events & EPOLLOUT)
            {
                // 非阻塞IO，主线程一次性写完所有数据，包括响应消息和html资源两部分
                // 从对象的写缓冲区发送到通信缓冲区
                if(http_conn::users[curfd]->write())
                {
                    // 写事件日志
                    LOG_INFO("Send data to client(%s) cfd(%d)",inet_ntoa(http_conn::users[curfd]->get_address()->sin_addr), curfd);
                    //更新该客户端的定时器
                    http_conn::users[curfd]->timer.lock()->upadte( 3 * TIMESLOT);
                }
                //如果发生写错误 或 对方已经关闭连接，则服务端也关闭连接，标记删除定时器
                else
                {
                    LOG_ERROR("Write error in client(%s) cfd(%d)", inet_ntoa(http_conn::users[curfd]->get_address()->sin_addr),curfd);
                    http_conn::users[curfd]->close_conn();
                    
                }
            }
        }
        // 最后处理alrm定时事件。这样做将导致定时任务不能精准的按照预定的时间执行
        if(timeout){
            // 执行定时清理
            timer_handler();
            timeout = false;
        }
    }
    close(epfd);
    close(listenfd);
    close(pipefd[0]);
    close(pipefd[1]);
    // delete[] users;
    // delete[] client_users;
    delete pool;
    return 0;
}