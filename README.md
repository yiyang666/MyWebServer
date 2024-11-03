MyWebServer
===============
Linux下C++轻量级Web服务器，自己入手的第一个c++项目，从牛客的视频项目开始，从最基础的GET一个网页的功能开始，
参考大量开源项目，添加定时器，同步/异步日志系统，数据库连接支持登录注册，多处原c实现用c++改写了，添加大量注释
最终压测可以实现接近3w的QPS（每秒请求数）。

**技术栈：**
C++，C++常用特性，Socket，Epoll，ThreadPool，MySQL

**主要工作：**
* 1.使用**线程池**+**epoll**(非阻塞，ET模式)，模拟**Proactor**事件处理模式的高并发模型；
*	2.使用**状态机**解析HTTP请求报文，支持解析**GET**和**POST**请求；
*	3.基于**小根堆**实现的定时器，并使用**智能指针**管理定时器和http连接，防止内存泄漏，不仅实现定时关闭超时连接，还对未超时的连接和定时器进行重复利用，节省系统资源；
*	4.利用**单例模式**与阻塞队列实现**同步/异步**日志系统，记录服务器运行状态；
*	5.利用**RAII机制**实现了**数据库连接池**，减少数据库连接建立与关闭的开销，同时实现了**用户注册登录**功能。

快速部署
------------
* 服务器测试环境
	* Ubuntu版本22.04
	* MySQL版本8.0.39
* 相关依赖库
  * g++编译器  sudo apt-get install g++
  * make编译工具  sudo apt install make/cmake
  * Mysql  sudo apt-get install mysql-server
  * C版本的mysql链接库
  ```bash
  yum install mysql-connector-c  # CentOS/Red Hat
  sudo apt-get install libmysqlclient-dev  # Debian/Ubuntu
  ```
* 测试前确认已安装MySQL数据库，修改main.cpp中的数据库初始化信息
  ```C++
  // 建立yourdb库
  create database yourdb;
  // 创建user表
  USE yourdb;
  CREATE TABLE user(
      username char(50) NULL,
      passwd char(50) NULL
  )ENGINE=InnoDB;
  // 添加数据
  INSERT INTO user(username, passwd) VALUES('name', 'passwd');
  ```
  * 编译+启动
    ```C++
    make
    ./server [port] [Log]
    ```
    * port 随机指定[1024~655535]
    * Log ：0/关闭 1/异步日志 2/同步日志

参考的开源项目:
------------
经典WebServer[WebServer](https://github.com/linyacool/WebServer)

TinyWebServer[TinyWebServer-Raw](https://github.com/qinguoyi/TinyWebServer/tree/raw_version)

TinyWebServer-C++14[TinyWebServer-C++14](https://github.com/markparticle/WebServer)
   


  
