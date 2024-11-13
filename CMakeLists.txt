cmake_minimum_required(VERSION 3.10.2)

project(server)

# 设置编译选项
set(CMAKE_CXX_FLAGS "-Wall") # 默认编译参数，会和下面两个之一组合
set(CMAKE_CXX_FLAGS_DEBUG "-g -Og") # 调试模式，-Og是GCC特有的，旨在提供合理的优化水平,-pg 性能分析
set(CMAKE_CXX_FLAGS_RELEASE "-O2") # 默认模式运行
set(CMAKE_CXX_STANDARD 14) # 指定CXX版本

# 额外的头文件目录
include_directories(./lock ./threadpool)

# 设置源文件
set(SOURCES
    main.cpp
    ./http/http_conn.cpp
    ./logs/log.cpp
    ./MySQL/sql_conn_pool.cpp
    ./Timer_lst/priorityTimer.cpp
)

# 添加可执行目标
add_executable(${PROJECT_NAME} ${SOURCES})

# 链接库
target_link_libraries(${PROJECT_NAME}
    mysqlclient
    pthread
)
