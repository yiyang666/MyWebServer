#include "http_conn.h"

// 定义HTTP响应的一些状态信息
const char* ok_200_title = "OK";
const char* error_400_title = "Bad Request";
const char* error_400_form = "Your request has bad syntax or is inherently impossible to satisfy.\n";
const char* error_403_title = "Forbidden";
const char* error_403_form = "You do not have permission to get file from this server.\n";
const char* error_404_title = "Not Found";
const char* error_404_form = "The requested file was not found on this server.\n";
const char* error_500_title = "Internal Error";
const char* error_500_form = "There was an unusual problem serving the requested file.\n";

// 初始化静态成员变量
int http_conn::m_user_count = 0;
int http_conn::m_epollfd = -1;
const char *http_conn::doc_root = {};
sql_conn_pool *http_conn::m_connPool = nullptr;
map<string, string> http_conn::user_table={};
locker http_conn::m_lock=locker();
std::unique_ptr<SPHttp[]> http_conn::users=nullptr;

// 初始化数据库数据到本地
void http_conn::initmysql_table()
{
    // 取出一个mysql连接
    MYSQL *mysql = nullptr;
    // 通过RAII机制管理mysql的生存周期
    connectionRAII mysqlcon(&mysql, m_connPool);

    // 在user表中检索username，passwd数据，浏览器端输入
    if (mysql_query(mysql, "SELECT username,passwd FROM user"))
    {
        LOG_ERROR("MySQL SELECT error:%s", mysql_error(mysql));
    }
    LOG_INFO("Get the MySQL table success!");
    // 从表中检索完整的结果集
    MYSQL_RES *result = mysql_store_result(mysql);
    // 结果集列数
    int num_cols = mysql_num_fields(result);
    // 所有字段结构的数组
    MYSQL_FIELD *fields = mysql_fetch_fields(result);

    //从结果集中获取下一行，将对应的用户名和密码，存入map中
    while (MYSQL_ROW row=mysql_fetch_row(result))
    {
        string uname(row[0]);
        string pwd(row[1]);
        user_table[uname] = pwd;
    }
}
// lfd和cfd统一为非阻塞模式
int setnonblocking( int fd ) {
    int old_option = fcntl( fd, F_GETFL );
    int new_option = old_option | O_NONBLOCK;
    fcntl( fd, F_SETFL, new_option );
    return old_option;
}

// 向epoll中添加需要监听的文件描述符
// lfd设置为LT模式，cfd设置为ET模式，同时cfd需要oneshot处理
// 这里统一处理了,one_shot为true时说明是cfd，false为lfd
void addfd( int epollfd, int fd, bool one_shot ) {
    epoll_event event;
    event.data.fd = fd;
    event.events = EPOLLIN | EPOLLRDHUP;
    if(one_shot) 
    {
        // 防止同一个通信被不同的线程处理
        event.events |= EPOLLONESHOT;
        // cfd一律为ET模式
        event.events |= EPOLLET;
    }
    epoll_ctl(epollfd, EPOLL_CTL_ADD, fd, &event);
    // 设置文件描述符非阻塞
    setnonblocking(fd);  
}

// 从epoll中移除监听的文件描述符
void removefd( int epollfd, int fd ) {
    epoll_ctl( epollfd, EPOLL_CTL_DEL, fd, 0 );
    close(fd);
}

// 修改文件描述符，重置socket上的EPOLLONESHOT事件，以确保下一次可读时，EPOLLIN事件能被触发
void modfd(int epollfd, int fd, int ev) {
    epoll_event event;
    event.data.fd = fd;
    event.events = ev | EPOLLET | EPOLLONESHOT | EPOLLRDHUP;
    // event.events = ev | EPOLLONESHOT | EPOLLRDHUP;
    epoll_ctl( epollfd, EPOLL_CTL_MOD, fd, &event );
}

/* 
    关闭连接和释放连接对象的逻辑：
    首先，判断该定时器是否被标记删除
    1.被标记了：
        (1)连接已经断开过了，且过了容忍时间，列队pop定时器，同时数组释放连接对象
    2.未被标记：
        (1)客户端正常超时被清理，断开连接，标记删除，并不马上删除，
            给予2*TIMESHOT容忍时间，即定时器更新超时时间
        (2)读写错误，错误信号，客户端主动关闭以及短连接请求数据读完了，
            断开连接，标记删除，给予2*TIMERSHOT的容忍时间
*/
// 释放连接对象，清理数组空间，同时定时器也同时被删除
void http_conn::release_conn()
{
    LOG_INFO("Release client(%s) cfd(%d)connection and its timer......",inet_ntoa(m_address.sin_addr), m_sockfd);
    
    if(users[m_sockfd])
        users[m_sockfd].reset(); // 引用计数减为0，释放资源
}
// 断开连接+标记删除+更新容忍时间
void http_conn::close_conn() 
{
    removefd(m_epollfd, m_sockfd);// 先断开连接
    m_user_count--; // 关闭一个连接，将客户总数量-1

    // 统一标记删除，更新容忍时间2*TIMESHOT
    timer.lock()->setdeleted();
    timer.lock()->upadte(2 * TIMESLOT);
}

// 初始化连接,外部调用初始化套接字地址
void http_conn::init(int sockfd, const sockaddr_in& addr)
{

    m_sockfd = sockfd;
    m_address = addr;
    
    // 端口复用
    int reuse = 1;
    setsockopt( m_sockfd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof( reuse ) );
    addfd( m_epollfd, m_sockfd, true );
    m_user_count++;
    init();
}

void http_conn::init()
{

    bytes_to_send = 0;
    bytes_have_send = 0;

    m_check_state = CHECK_STATE_REQUESTLINE;    // 初始状态为检查请求行
    m_linger = false;       // 默认不保持链接  Connection : keep-alive保持连接

    m_method = GET;         // 默认请求方式为GET
    m_url = 0;              
    m_version = 0;
    m_content_length = 0;
    m_host = 0;
    m_start_line = 0;
    m_checked_idx = 0;
    m_read_idx = 0;
    m_write_idx = 0;
    cgi = 0;
    bzero(m_read_buf, READ_BUFFER_SIZE);
    bzero(m_write_buf, WRITE_BUFFER_SIZE);
    bzero(m_real_file, FILENAME_LEN);
}

// 循环读取客户数据，直到无数据可读或者对方关闭连接
bool http_conn::read() {

    if( m_read_idx >= READ_BUFFER_SIZE ) {
        return false;
    }
    int bytes_read = 0;
    //printf("初始m_read_idx：%d\n",m_read_idx);
    while(true) {
        // 从m_read_buf + m_read_idx索引出开始保存数据，大小是READ_BUFFER_SIZE - m_read_idx
        bytes_read = recv(m_sockfd, m_read_buf + m_read_idx,READ_BUFFER_SIZE - m_read_idx, 0 );
        if (bytes_read == -1) {
            if( errno == EAGAIN || errno == EWOULDBLOCK ) {
                // 没有数据
                break;
            }
            return false;   
        } else if (bytes_read == 0) {   // 对方关闭连接
            return false;
        }
        m_read_idx += bytes_read;
    }

    return true;
}

// 解析一行，判断依据\r\n
// TODO:这里可以用正则表达式优化
http_conn::LINE_STATUS http_conn::parse_line() {
    char temp;
    for ( ; m_checked_idx < m_read_idx; ++m_checked_idx ) {
        temp = m_read_buf[ m_checked_idx ];
        if ( temp == '\r' ) {
            if ( ( m_checked_idx + 1 ) == m_read_idx ) {
                return LINE_OPEN;
            } else if ( m_read_buf[ m_checked_idx + 1 ] == '\n' ) {
                m_read_buf[ m_checked_idx++ ] = '\0';
                m_read_buf[ m_checked_idx++ ] = '\0';
                return LINE_OK;
            }
            return LINE_BAD;
        } else if( temp == '\n' )  {
            if( ( m_checked_idx > 1) && ( m_read_buf[ m_checked_idx - 1 ] == '\r' ) ) {
                m_read_buf[ m_checked_idx-1 ] = '\0';
                m_read_buf[ m_checked_idx++ ] = '\0';
                return LINE_OK;
            }
            return LINE_BAD;
        }
    }
    return LINE_OPEN;
}

// 解析HTTP请求行，获得请求方法，目标URL,以及HTTP版本号
http_conn::HTTP_CODE http_conn::parse_request_line(char* text) {
    // GET /index.html HTTP/1.1
    m_url = strpbrk(text, " \t"); // 判断第二个参数中的字符哪个在text中最先出现
    if (! m_url) { 
        return BAD_REQUEST;
    }
    // GET\0/index.html HTTP/1.1
    *m_url++ = '\0';    // 置位空字符，字符串结束符
    char* method = text;
    if ( strcasecmp(method, "GET") == 0 ) { // 忽略大小写比较
        m_method = GET;
    } else if(strcasecmp(method,"POST")==0)
    {
        m_method = POST;
        cgi = 1;
    }
    else {
        return BAD_REQUEST;
    }
    // /index.html HTTP/1.1
    m_url += strspn(m_url, " \t");
    // 检索字符串 str1 中第一个不在字符串 str2 中出现的字符下标。
    m_version = strpbrk( m_url, " \t" );
    if (!m_version) {
        return BAD_REQUEST;
    }
    *m_version++ = '\0';
    m_version += strspn(m_version, " \t");
    if (strcasecmp(m_version, "HTTP/1.1") != 0){
        return BAD_REQUEST;
    }
    /**
     * http://192.168.110.129:10000/index.html
    */
    if (strncasecmp(m_url, "http://", 7) == 0 ) {   
        m_url += 7;
        // 在参数 str 所指向的字符串中搜索第一次出现字符 c（一个无符号字符）的位置。
        m_url = strchr( m_url, '/' );
    }
    if (strncasecmp(m_url, "https://", 8) == 0)
    {
        m_url += 8;
        m_url = strchr(m_url, '/');
    }
    if ( !m_url || m_url[0] != '/' ) {
        return BAD_REQUEST;
    }
    // 当url为/时,显示判断界面
    if(strlen(m_url)==1)
        strcat(m_url, "judge.html");
        
    m_check_state = CHECK_STATE_HEADER; // 检查状态变成检查头
    return NO_REQUEST;
}

// 解析HTTP请求的一个头部信息
//TODO：这里可以支持更多的头部信息
http_conn::HTTP_CODE http_conn::parse_headers(char* text) {   
    // 遇到空行，表示头部字段解析完毕
    if( text[0] == '\0' ) {
        // 如果HTTP请求有消息体，则还需要读取m_content_length字节的消息体，
        // 状态机转移到CHECK_STATE_CONTENT状态
        if ( m_content_length != 0 ) {
            m_check_state = CHECK_STATE_CONTENT;
            return NO_REQUEST;
        }
        // 否则说明我们已经得到了一个完整的HTTP请求
        return GET_REQUEST;
    } else if ( strncasecmp( text, "Connection:", 11 ) == 0 ) {
        // 处理Connection 头部字段  Connection: keep-alive
        text += 11;
        text += strspn( text, " \t" );
        if ( strcasecmp( text, "keep-alive" ) == 0 ) {
            m_linger = true;
        }
    } else if ( strncasecmp( text, "Content-Length:", 15 ) == 0 ) {
        // 处理Content-Length头部字段
        text += 15;
        text += strspn( text, " \t" );
        m_content_length = atol(text);
    } else if ( strncasecmp( text, "Host:", 5 ) == 0 ) {
        // 处理Host头部字段
        text += 5;
        text += strspn( text, " \t" );
        m_host = text;
    }
    else {
        // 不认识该字段(该字段未被处理)
        //printf( "oop! unknow header %s\n", text );
        LOG_INFO("oop! unknow header %s", text);
    }
    return NO_REQUEST;
}

// 解析HTTP请求的消息体，只是判断它是否被完整的读入了
http_conn::HTTP_CODE http_conn::parse_content( char* text ) {
    if ( m_read_idx >= ( m_content_length + m_checked_idx ) )
    {
        text[ m_content_length ] = '\0';
        //POST请求中最后为输入的用户名和密码  user=root&password=root
        std::string messages(text); // 转换为string类型
        m_string = messages;
        return GET_REQUEST;
    }
    return NO_REQUEST;
}

// 主状态机，解析请求
http_conn::HTTP_CODE http_conn::process_read() 
{
    LINE_STATUS line_status = LINE_OK;
    HTTP_CODE ret = NO_REQUEST;
    char* text = 0;

    while (((m_check_state == CHECK_STATE_CONTENT) && (line_status == LINE_OK))
        || ((line_status = parse_line()) == LINE_OK)) {
        // 获取一行数据
        text = get_line(); 
        m_start_line = m_checked_idx;
        //printf( "got 1 http line: %s\n", text );
        LOG_INFO("got 1 http line:%s", text);
        switch (m_check_state)
        {
        case CHECK_STATE_REQUESTLINE:
        {
            ret = parse_request_line(text);
            if (ret == BAD_REQUEST)
            {
                return BAD_REQUEST;
            }
            break;
            }
            case CHECK_STATE_HEADER: {
                ret = parse_headers( text );
                if ( ret == BAD_REQUEST ) {
                    return BAD_REQUEST;
                } else if ( ret == GET_REQUEST ) {
                    return do_request();
                }
                break;
            }
            case CHECK_STATE_CONTENT: {
                ret = parse_content( text );
                if ( ret == GET_REQUEST ) {
                    return do_request();
                }
                line_status = LINE_OPEN;
                break;
            }
            default: {
                return INTERNAL_ERROR;
            }
            }
    }
    return NO_REQUEST;
}

// 当得到一个完整、正确的HTTP请求时，我们就分析目标文件的属性，
// 如果目标文件存在、对所有用户可读，且不是目录，则使用mmap将其
// 映射到内存地址m_file_address处，并告诉调用者获取文件成功
http_conn::HTTP_CODE http_conn::do_request()
{
    // "/home/young/workspace/stay_linux/WebServer/resources"

    // 拼接成完整路径
    strcpy( m_real_file, doc_root );
    int len = strlen( doc_root );
    const char *p = strrchr(m_url, '/');
    /*
        根据标志判断是登录检测还是注册检测，即判断 / 后是 2 还是 3
        经过parse_request_line函数的处理，在此 m_url 指向 /2CGISQL.cgi 字符串
        而指针p为 const char *p = strrchr(m_url, '/');
    */
    // 处理cgi 2登录 3注册
    if (cgi==1 && (*(p+1)=='2'|| *(p+1)=='3'))
    {
        char flag = m_url[1];
        
        //  m_url 指向 /2CGISQL.cgi 字符串  处理c风格字符串
        char *m_url_real = (char *)malloc(sizeof(char) * 200);
        strcpy(m_url_real, "/");
        strcat(m_url_real, m_url + 2);// /CGISQL.cgi
        strncpy(m_real_file + len, m_url_real, FILENAME_LEN - len - 1);// root/CGISQL.cgi
        free(m_url_real);

        // got 1 http line: user=root&password=root   (std::string m_string)
        // 处理string类型
        std::string name, password;
        auto name_start = m_string.find("user=") + 5;
        auto name_end = m_string.find("&", name_start);
        name = m_string.substr(name_start, name_end - name_start);//从i取n  

        auto pw_start = m_string.find("password=") + 9;
        password = m_string.substr(pw_start);
        
        // 不优雅
        // char name[100],password[100];
        // int i=5;
        // for (; m_string[i] != '&'; ++i)
        // {
        //     name[i - 5] = m_string[i];
        // }
        // name[i - 5] = '\0';
        // int j = 0;
        // i = i + 10;
        // for (; m_string[i] != '\0'; ++i, ++j)
        // {
        //     password[j] = m_string[i];
        // }
        // password[j] = '\0';

        // 如果是注册: 先检查是否有重名,否则加一行数据
        //  m_url 指向 /3CGISQL.cgi 字符串
        if(*(p+1)=='3')
        {
            std::string sql_insert="INSERT INTO user(username, passwd) VALUES('";
            sql_insert += name;
            sql_insert += "','";
            sql_insert += password;
            sql_insert += "')";

            // std::cout << "sql_insert=" << sql_insert << std::endl;
            // std::cout << "name=" << name <<" passwd="<<password<< std::endl;

            if (user_table.find(name) == user_table.end())
            {
                MYSQL *mysql = nullptr;
                // // 获取连接池实例，取出一个mysql连接
                // sql_conn_pool *connPool = sql_conn_pool::GetInstance();
                // 通过RAII机制管理mysql的生存周期
                // 这里应该不用上锁,RAII获取一个mysql时调用GetConn()函数是上了锁的
                connectionRAII mysqlcon(&mysql, m_connPool);

                m_lock.lock();//向数据库中插入数据时，需要通过锁来同步数据

                int res = mysql_query(mysql, sql_insert.c_str());

                 //0： 表示查询执行成功。!0： 表示查询执行失败。
                if (res==0){
                    // 更新到本地结果集
                    user_table.insert(make_pair(name, password));
                    strcpy(m_url, "/log.html");
                    LOG_INFO("User registration successful!");
                }
                else
                    strcpy(m_url, "/registerError.html");

                m_lock.unlock();
            }
            // 已经注册过了
            else{
                strcpy(m_url, "/registerError.html");
            }
        }
        // 如果是登录,直接从map表中寻找账户密码对
        // 指向 /3CGISQL.cgi 字符串
        else if(*(p+1)=='2')
        {
            if (user_table.find(name) != user_table.end() && user_table[name] == password){

                strcpy(m_url, "/welcome.html");
                LOG_INFO("User login successful");
            }   
            else
                strcpy(m_url, "/logError.html");
        }
    }
    // 实现跳转功能
    const std::string urls[] = {
        "/register.html", // for '0'
        "/log.html",      // for '1'
        "",               // placeholder for '2'
        "",               // placeholder for '3'
        "",               // placeholder for '4'
        "/picture.html",  // for '5'
        "/video.html",    // for '6'
        "/fans.html"      // for '7'
    };
    char next_char = *(p + 1);
    if (next_char>='0' && next_char<='7' && !urls[next_char-'0'].empty())
    {
        std::string m_url_real = urls[next_char - '0'];
        strncpy(m_real_file + len, m_url_real.c_str(), m_url_real.length());
    }
    else{
        strncpy(m_real_file + len, m_url, FILENAME_LEN - len - 1);
    }

    // 获取m_real_file文件的相关的状态信息，-1失败，0成功
    if ( stat( m_real_file, &m_file_stat ) < 0 ) {
        return NO_RESOURCE;
    }

    // 判断访问权限
    if ( ! ( m_file_stat.st_mode & S_IROTH ) ) {
        return FORBIDDEN_REQUEST;
    }

    // 判断是否是目录
    if ( S_ISDIR( m_file_stat.st_mode ) ) {
        return BAD_REQUEST;
    }

    // 以只读方式打开文件
    int fd = open( m_real_file, O_RDONLY );
    // 创建内存映射
    m_file_address = ( char* )mmap( 0, m_file_stat.st_size, PROT_READ, MAP_PRIVATE, fd, 0 );
    close( fd );
    return FILE_REQUEST;
}

// 对内存映射区执行munmap操作
void http_conn::unmap() {
    if( m_file_address )
    {
        munmap( m_file_address, m_file_stat.st_size );
        m_file_address= nullptr;
    }
}

// 写HTTP响应
bool http_conn::write()
{
    int temp = 0;
    
    if ( bytes_to_send == 0 ) {
        // 将要发送的字节为0，这一次响应结束。
        modfd( m_epollfd, m_sockfd, EPOLLIN ); 
        init();
        return true;
    }

    while(1) {
        // 分散写
        temp = writev(m_sockfd, m_iv, m_iv_count);
        if ( temp < 0 ) {
            // 如果TCP写缓冲没有空间，则等待下一轮EPOLLOUT事件，虽然在此期间，
            // 服务器无法立即接收到同一客户的下一个请求，但可以保证连接的完整性。
            if( errno == EAGAIN ) {
                modfd( m_epollfd, m_sockfd, EPOLLOUT );
                return true;
            }
            unmap();
            return false;
        }

        bytes_have_send += temp;
        bytes_to_send -= temp;

        // 第一部分发完了
        if (bytes_have_send >= m_iv[0].iov_len)
        {
            m_iv[0].iov_len = 0;
            m_iv[1].iov_base = m_file_address + (bytes_have_send - m_write_idx);
            m_iv[1].iov_len = bytes_to_send;
        }
        else
        {
            m_iv[0].iov_base = m_write_buf + bytes_have_send;
            m_iv[0].iov_len = m_iv[0].iov_len - temp;
        }

        //发完了，没有数据要发送了
        if (bytes_to_send <= 0)
        {
            unmap();
            modfd(m_epollfd, m_sockfd, EPOLLIN);

            if (m_linger)
            {
                init();
                return true;
            }
            else
            {
                return false;
            }
        }
    }
}

// 往写缓冲中写入待发送的数据
bool http_conn::add_response( const char* format, ... ) {
    // 写满了
    if( m_write_idx >= WRITE_BUFFER_SIZE )
        return false;
    va_list arg_list;             // 可变参数列表
    va_start( arg_list, format ); // 初始化参数列表
    // vsnprintf 将格式化后的字符串写入到指定的缓冲区中
    int len = vsnprintf( m_write_buf + m_write_idx, WRITE_BUFFER_SIZE - 1 - m_write_idx, format, arg_list );
    if( len >= ( WRITE_BUFFER_SIZE - 1 - m_write_idx ) ) {
        return false;
    }
    m_write_idx += len;
    va_end( arg_list );
    // LOG_INFO("Request:%s", m_write_buf);
    // printf("reques: %s\n", m_write_buf);
    return true;
}

// 写响应行
bool http_conn::add_status_line( int status, const char* title ) {
    return add_response( "%s %d %s\r\n", "HTTP/1.1", status, title );
}

// 写响应头
bool http_conn::add_headers(int content_len) {
    add_content_length(content_len);
    add_content_type();
    add_linger();
    add_blank_line();
    return true;
}

bool http_conn::add_content_length(int content_len) {
    return add_response( "Content-Length: %d\r\n", content_len );
}

bool http_conn::add_content_type() {
    return add_response("Content-Type:%s\r\n", "text/html");
}

bool http_conn::add_linger()
{
    return add_response( "Connection: %s\r\n", ( m_linger == true ) ? "keep-alive" : "close" );
}

bool http_conn::add_blank_line()
{
    return add_response( "%s", "\r\n" );
}

// 写响应体（消息体）
bool http_conn::add_content( const char* content )
{
    return add_response( "%s", content );
}


// 根据服务器处理HTTP请求的结果，决定返回给客户端的内容
bool http_conn::process_write(HTTP_CODE ret) {
    switch (ret)
    {
        case INTERNAL_ERROR:
            add_status_line( 500, error_500_title );
            add_headers( strlen( error_500_form ) );
            if ( ! add_content( error_500_form ) ) {
                return false;
            }
            break;
        case BAD_REQUEST:
            add_status_line( 400, error_400_title );
            add_headers( strlen( error_400_form ) );
            if ( ! add_content( error_400_form ) ) {
                return false;
            }
            break;
        case NO_RESOURCE:
            add_status_line( 404, error_404_title );
            add_headers( strlen( error_404_form ) );
            if ( ! add_content( error_404_form ) ) {
                return false;
            }
            break;
        case FORBIDDEN_REQUEST:
            add_status_line( 403, error_403_title );
            add_headers(strlen( error_403_form));
            if ( ! add_content( error_403_form ) ) {
                return false;
            }
            break;
        case FILE_REQUEST:
            add_status_line(200, ok_200_title );
            // 根据文件大小判断
            //如果请求的文件存在且大小不为 0，则将文件内容作为响应内容发送给客户端。
            if (m_file_stat.st_size != 0)
            {
                add_headers(m_file_stat.st_size);
                m_iv[ 0 ].iov_base = m_write_buf;
                m_iv[ 0 ].iov_len = m_write_idx;
                m_iv[ 1 ].iov_base = m_file_address;
                m_iv[ 1 ].iov_len = m_file_stat.st_size;
                m_iv_count = 2;//响应消息和资源
                // 将要发送的数据字节数，两部分相加
                bytes_to_send = m_write_idx + m_file_stat.st_size;

                return true;
            }
            //如果请求的文件不存在或大小为 0，则发送一个空的 HTML 响应给客户端
            else{
                const char *ok_string = "<html><body></body></html>";
                add_headers(strlen(ok_string));
                if (!add_content(ok_string))
                    return false;
            }
        default:
            return false;
    }
    // 没有请求资源，只发送响应消息
    m_iv[ 0 ].iov_base = m_write_buf;
    m_iv[ 0 ].iov_len = m_write_idx;
    m_iv_count = 1;
    bytes_to_send = m_write_idx;
    return true;
}

// 主线程负责将数据从TCP读缓冲区读到用户缓冲区中，
// 再负责把请求响应内容从用户缓冲区写到TCP写缓冲区
// 而线程池中的工作线程只负责对用户缓冲区内容进行请求解析同时生成响应

// 由线程池中的工作线程调用，这是处理HTTP请求的入口函数
void http_conn::process() {

    // 解析HTTP请求
    HTTP_CODE read_ret = process_read();
    if ( read_ret == NO_REQUEST ) {
        modfd( m_epollfd, m_sockfd, EPOLLIN );
        return;
    }

    // 生成响应
    bool write_ret = process_write( read_ret );
    if ( !write_ret ) {
        close_conn();
        LOG_ERROR("Write error in client(%s) cfd(%d)", inet_ntoa(m_address.sin_addr),m_sockfd);
    }
    modfd( m_epollfd, m_sockfd, EPOLLOUT);
}