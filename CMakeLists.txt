# 指定最低版本
cmake_minimum_required(VERSION 3.10.2)
# 指定项目生成名称
project(server)

# 设置编译选项
add_compile_options(-std=c++14 -Wall -O2)# -g
# 分release和DeBug两种模式
# set(CMAKE_CXX_FLAGS "-Wall") # 默认编译参数，会和下面两个之一组合
# set(CMAKE_CXX_FLAGS_DEBUG "-g -Og") # 调试模式，-Og是GCC特有的，旨在提供合理的优化水平,-pg 性能分析
# set(CMAKE_CXX_FLAGS_RELEASE "-O2") # 默认模式运行
# set(CMAKE_CXX_STANDARD 14) # 指定CXX版本

# 额外的头文件目录，头文件分散在多个文件夹的情况
include_directories(./lock ./threadpool)

# 设置源文件
# aux_source_directory(dir SOURCES);# 单目录的情况，把dir中的所有源文件添加到SOURCES
# 多目录的情况，也可以采用递归的方式，在对应的目录下重新写一个CMakeLists.txt
set(SOURCES
    main.cpp
    ./http/http_conn.cpp
    ./logs/log.cpp
    ./MySQL/sql_conn_pool.cpp
    ./Timer_lst/priorityTimer.cpp
)

# 添加可执行目标
add_executable(${PROJECT_NAME} ${SOURCES})

# # 生成静态库或动态库，参数1：生成库名称，参数2：静态/动态，参数3：所需要的源文件
# add_library(lib_shared SHARED SOURCES)
# add_library(lib_static STATIC SOURCES)

# # 设置库的生成位置,PROJECT_BINARY_DIR是执行cmake命令的目录
# set(LIBRARY_OUTPUT_PATH ${PROJECT_BINARY_DIR}/../lib)

# 链接库
target_link_libraries(${PROJECT_NAME}
    mysqlclient
    pthread
)
