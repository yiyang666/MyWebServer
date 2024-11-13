# 指定编译器
CC = g++
# 指定编译参数
CFLAGS = -std=c++14 -O2# #-Wall  -g
# 指定要生成的目标
TARGET = server

# 源文件列表, 可指定当前目录所有*.cpp
SRCS = ./http/http_conn.cpp \
	 MySQL/sql_conn_pool.cpp \
	 Timer_lst/priorityTimer.cpp \
	 logs/log.cpp \
	 main.cpp

# 头文件目录,补充一下头文件（.h文件）目录,默认搜索路径是.cpp目录
INCLUDES = -I./lock -I./threadpool #-I ./Timer_lst -I ./MySQL ./http -I ./logs

# 库文件和库文件路径
LIBS= -lmysqlclient -lpthread

# 如果需要，指定库文件路径
#LDFLAGS = -L/usr/local/mysql/lib  

# 目标文件列表，指定源文件列表中的全部.cpp文件产生.o文件
OBJS = $(patsubst %.cpp,bin/%.o,$(SRCS))

# 规则：如何编译，目标文件：依赖文件，如果依赖文件找不到，会去找它的生成规则
all: $(TARGET)

# 这里找不到OBJS的.o文件，就去找怎么生成.o文件的生成规则
$(TARGET):$(OBJS)
	$(CC) $(CFLAGS) -o $(TARGET) $(OBJS) $(LIBS)


# 这里就是通过.cpp文件生成.o文件的规则，g++的-c参数编译而来
# 这里加了INCLUDES头文件，上面生成可执行文件就不需要了
# 使用 $< 表示第一个依赖文件，$@ 表示目标文件
# @mkdir -p $(@D) 创建一个临时文件夹存储中间产物，方便删除
# 确保目标文件所在的目录存在，不在则创建它

bin/%.o: %.cpp
	@mkdir -p $(@D)
	$(CC) $(CFLAGS) $(INCLUDES) -c $< -o $@

# 清理规则，清理中间产物
clean:
	rm -rf bin $(TARGET)

# 伪函数，用来执行一些操作，避免和文件重名，所以用伪函数声明
.PHONY: clean