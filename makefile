CXX ?= g++
#这一行定义了一个变量 CXX，它表示 C++ 编译器。
# ?= 是一个赋值操作符，表示如果 CXX 变量未定义，则将其赋值为 g++。
# 这样做是为了允许用户通过命令行参数来指定使用的编译器，否则默认使用 g++。

DEBUG ?= 1
# 这一行定义了一个变量 DEBUG，用于控制编译的调试模式。同样使用了 ?= 操作符，如果 DEBUG 未定义，则将其赋值为 1。
ifeq ($(DEBUG), 1)
    CXXFLAGS += -g
else
    CXXFLAGS += -O2
endif

# 这是一个条件语句，如果 DEBUG 的值为 1，则执行其后面的代码块。
# CXXFLAGS += -g: 如果在调试模式下编译，将编译选项 -g 添加到 CXXFLAGS 变量中。
# -g 选项用于生成调试信息，以便在调试程序时使用。

server: main.cpp  ./timer/lst_timer.cpp ./http/http_conn.cpp ./log/log.cpp ./CGImysql/sql_connection_pool.cpp  webserver.cpp config.cpp ./memorypool/memorypool.cpp ./LFU/LFUCache.cpp
	$(CXX) -o server  $^ $(CXXFLAGS) -lpthread -lmysqlclient

clean:
	rm  -r server


# server: main.cpp ...: 这一行指定了编译目标 server，并列出了编译 server 程序所需的所有源文件。在这里列出的源文件都是程序的依赖项。

# $(CXX) -o server $^ $(CXXFLAGS) -lpthread -lmysqlclient: 这是生成可执行文件 server 的命令。
# $^ 表示依赖项列表中的所有文件，$(CXXFLAGS) 包含了编译选项，-lpthread 和 -lmysqlclient 分别链接了 pthread 库和 MySQL 客户端库。


# clean: rm -r server: 这是一个清理目标，用于删除生成的可执行文件。执行 make clean 将会删除名为 server 的文件。