#ifndef SQL_CONN_POOL_H
#define SQL_CONN_POOL_H

#include <stdio.h>
#include<iostream>
#include <mysql/mysql.h>
#include<error.h>
#include <list>
#include <string>
#include<string.h>
#include<assert.h>
#include "../lock/locker.h"
#include"../logs/log.h"

using namespace std;

class sql_conn_pool
{
public:

    MYSQL *GetConn();               // 获取数据库连接
    bool ReleaseConn(MYSQL *conn);  // 释放连接
    int GetFreeConn();              // 获取空闲连接数
    void DestroyPool();             // 销毁所有连接

    // 单例模式
    static sql_conn_pool *GetInstance(){
        static sql_conn_pool connPool;
        return &connPool;
    }

    void init(string url, string User, string Passwd, string DBname, int Port, 
            unsigned int MaxConn);

    sql_conn_pool();
    ~sql_conn_pool();

private:
    unsigned int MaxConn;   // 最大连接数
    unsigned int CurConn;   // 当前已经使用的连接数
    unsigned int FreeConn;  // 当前空闲的连接数

    locker m_mtx;           // 互斥锁
    list<MYSQL *> connList; // 连接池
    sem reserve;            // 信号量

    string url;     // 主机地址
    int Port;    // 端口号
    string User;    // 用户名
    string Passwd;  // 数据库密码
    string DBname;  // 数据库名

};

//RAII机制是一种对资源申请、释放这种成对操作的封装，通过这种方式实现在局部作用域内申请资源，使用结束后，自动释放资源。
class  connectionRAII{
public:
    // 数据库连接本身是指针类型，所以参数需要通过双指针才能对其进行修改
    // 双指针对MYSQL *conn进行修改
    connectionRAII(MYSQL **con, sql_conn_pool *connPool);
    ~connectionRAII();

private:
    MYSQL *conRAII;
    sql_conn_pool *poolRAII;
};

#endif