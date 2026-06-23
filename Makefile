# C++17 HTTP Web Server Makefile

CXX = g++
CXXFLAGS = -std=c++17 -Wall -Wextra -O3 -march=native
LDFLAGS = -pthread

#源文件
SRCS = server.cpp logger.cpp threadpool.cpp http.cpp router.cpp

#目标文件
OBJS = $(SRCS:.cpp=.o)

#可执行文件
TARGET = webserver

#头文件依赖
DEPS = server.h logger.h threadpool.h http.h router.h

.PHONY: all clean run debug

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CXX) $(CXXFLAGS) -o $@ $^ $(LDFLAGS)

%.o: %.cpp $(OBJS)
	$(CXX) $(CXXFLAGS) -c $< -o $@

clean:
	rm -f $(OBJS) $(TARGET) log.txt

run: $(TARGET)
	,/$(TARGET)

debug:
	$(CXX) $(CXXFLAGS) -g $(SRCS) $(LDFLAGS) -o $(TARGET)
	./$(TARGET)