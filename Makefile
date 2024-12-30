
# 定义变量
CC = g++
CFLAGS = -m32
LIBS = -L. -L.. -lpthread -lusbcan

# 手动指定源文件和头文件
MAIN_SRC = middleware.c util.c
HEADERS = controlcan.h util.h

# 输出目标文件名
TARGET = main

# 默认目标
all: $(TARGET)

# 编译规则
$(TARGET): $(MAIN_SRC) $(HEADERS)
	$(CC) $(CFLAGS) -o $(TARGET) $(MAIN_SRC) $(LIBS)

# 清理目标
clean:
	rm -vf $(TARGET)
