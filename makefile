# 指定编译器
CC = g++
# 指定编译选项
CFLAGS = -std=c++14 -O2# #-Wall  -g 
# 目标文件
TARGET = server
# 源文件列表
SRCS = http/http_conn.cpp logs/log.cpp main.cpp MySQL/sql_conn_pool.cpp Timer_lst/priorityTimer.cpp
# 头文件目录
INCLUDES = -I ./lock -I ./http -I ./logs -I ./threadpool -I ./Timer_lst -I ./MySQL 

# 库文件和库文件路径
LIBS= -lmysqlclient -lpthread#-lmysqlclient 
#LDFLAGS = -L/usr/local/mysql/lib  # 如果需要，指定库文件路径

# 目标文件列表
OBJS = $(patsubst %.cpp,%.o,$(SRCS))

# 规则
all: $(TARGET)
# 模板
# $(TARGET): $(OBJS)
# 	$(CC) $(CFLAGS) $(OBJS) -o ./bin/$(TARGET) $(INCLUDES) $(LDFLAGS) $(LIBS)

$(TARGET):$(OBJS)
	$(CC) $(CFLAGS) $(OBJS) -o $(TARGET) $(INCLUDES) $(LIBS)

# 使用 $< 表示第一个依赖文件，$@ 表示目标文件
%.o:%.cpp
#确保目标文件所在的目录存在
#	@mkdir -p $(@D)  
	$(CC) $(CFLAGS) $(INCLUDES) -c $< -o $@

# 清理规则
clean:
	rm -f $(TARGET) $(OBJS)