CC = g++

CFLAGS = -std=c++14 -O2# #-Wall  -g

TARGET = server

# 源文件列表, 可指定当前目录所有*.cpp
SRCS = ./http/http_conn.cpp \
	 MySQL/sql_conn_pool.cpp \
	 Timer_lst/priorityTimer.cpp \
	 logs/log.cpp \
	 main.cpp

# 头文件目录,补充一下头文件目录,默认搜索路径是.cpp目录
INCLUDES = -I./lock -I./threadpool #-I ./Timer_lst -I ./MySQL ./http -I ./logs

# 库文件和库文件路径
LIBS= -lmysqlclient -lpthread
#LDFLAGS = -L/usr/local/mysql/lib  # 如果需要，指定库文件路径

# 目标文件列表，处理中间产物，指定头文件生产.o文件
OBJS = $(patsubst %.cpp,bin/%.o,$(SRCS))

# 规则
all: $(TARGET)

$(TARGET):$(OBJS)
	$(CC) $(CFLAGS) -o $(TARGET) $(OBJS) $(LIBS)

# 使用 $< 表示第一个依赖文件，$@ 表示目标文件
# 创建一个临时文件夹存储中间产物，方便删除
# 确保目标文件所在的目录存在，不在则创建它
#	@mkdir -p $(@D) 
bin/%.o: %.cpp
	@mkdir -p $(@D)
	$(CC) $(CFLAGS) $(INCLUDES) -c $< -o $@

# 清理规则
clean:
	rm -rf bin $(TARGET)