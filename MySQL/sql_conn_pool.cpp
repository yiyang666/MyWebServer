#include "sql_conn_pool.h"

sql_conn_pool::sql_conn_pool()
{
    this->CurConn = 0;
    this->FreeConn = 0;
}
// 析构函数
sql_conn_pool::~sql_conn_pool()
{
	DestroyPool();
}

// 构造初始化
void sql_conn_pool::init(string url,string User,string Passwd,string DBname,int Port,
                    unsigned int MaxConn=8)
{
    assert(MaxConn > 0);
    // 初始化数据库信息
    this->url = url;
    this->Port = Port;
    this->User = User;
    this->Passwd = Passwd;
    this->DBname = DBname;

    // 创建MaxConn条数据库连接
    for (int i = 0; i < MaxConn; i++)
    {
        MYSQL *con = nullptr;
        con = mysql_init(con);

        if(!con){
            LOG_ERROR("MySQL init error!");
            assert(con);
        }
        con = mysql_real_connect(con, url.c_str(), User.c_str(), Passwd.c_str(), 
                                DBname.c_str(), Port, nullptr, 0);
        if(!con){
            LOG_ERROR("MySQL Connect error!");
            cout << "MySQL Connect error! " << endl;
            assert(con);
        }
        // 更新连接池和空闲连接数量
        connList.push_back(con);
        ++FreeConn;
    }
    cout << "Connection pool initialization successful! Numbers: "<<MaxConn<<endl;
    LOG_INFO("Connection pool initialization successful! Numbers: %d",MaxConn);
    // 信号量初始化为最大连接次数
    reserve = sem(FreeConn);
    this->MaxConn = FreeConn;
}
// 当有连接请求时,从数据库连接池中返回一个可用连接,更新使用和空闲连接数
MYSQL *sql_conn_pool::GetConn()
{
    // 当线程数量大于数据库连接数量时，使用信号量进行同步，每次取出连接时，信号量减1，释放连接时，信号量加1，若连接池内没有连接了，则阻塞等待。
    // 另外，由于多线程操作连接池，会造成竞争，这里使用互斥锁完成同步，具体的同步机制均使用lock.h中封装好的类。

    MYSQL *con = nullptr;
    if(connList.empty()){
        LOG_WARN("SqlConnPool busy!");
        return nullptr;
    }
    // 取出连接，信号量原子减1，为0则等待
    reserve.wait();
    m_mtx.lock();

    con = connList.front();
    connList.pop_back();

    --FreeConn;
    ++CurConn;

    m_mtx.unlock();
    return con;
}
// 释放当前使用的连接
bool sql_conn_pool::ReleaseConn(MYSQL *con)
{
    if(!con)
        return false;

    m_mtx.lock();//加锁

    connList.push_back(con);

    ++FreeConn;
    --CurConn;

    m_mtx.unlock();//释放锁
    reserve.post();

    return true;
}
// 销毁数据库连接池
void sql_conn_pool::DestroyPool()
{
    // 通过迭代器遍历连接池链表，关闭对应数据库连接，清空链表并重置空闲连接和现有连接数量。
    m_mtx.lock();
    if (!connList.empty())
    {
        for(auto i:connList)
        {
            mysql_close(i);
        }
        CurConn = 0;
        FreeConn = 0;
        // 清空list
        connList.clear();
    }
    m_mtx.unlock();
}

// 当前空闲的连接数
int sql_conn_pool::GetFreeConn()
{
    return this->FreeConn;
}


// 不直接调用获取和释放连接的接口，将其封装起来，通过RAII机制进行获取和释放
connectionRAII::connectionRAII(MYSQL **SQL,sql_conn_pool *connPool)
{
    // 数据库连接本身是指针类型，所以参数需要通过双指针才能对其进行修改
    // 双指针对MYSQL *conn进行修改
    *SQL = connPool->GetConn();//指针作为左值,解引用右边赋值给左边

    conRAII = *SQL;     // 再把二级指针解引用的值,赋给conRAII
    poolRAII = connPool;// connPool传进来本身就是指针
}
// 操作与原连接池对象对应,只是封装成对应的RAII对象进行连接和释放
connectionRAII::~connectionRAII()
{
    poolRAII->ReleaseConn(conRAII);
}
