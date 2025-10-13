# Makefile for ytui
# See config.h for configuration

CXX = g++
CXXFLAGS = -std=c++17 -Wall -Wextra -pedantic -O2
LDFLAGS = -lncurses -lpthread
TARGET = ytui
SRCS = ytui.cpp ui.cpp process.cpp youtube.cpp files.cpp
OBJS = $(SRCS:.cpp=.o)
PREFIX = /usr/local

.PHONY: all clean install uninstall

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CXX) $(OBJS) -o $(TARGET) $(LDFLAGS)

%.o: %.cpp
	$(CXX) $(CXXFLAGS) -c $< -o $@

clean:
	rm -f $(TARGET) $(OBJS)

install: all
	@echo installing executable to $(PREFIX)/bin
	@mkdir -p $(PREFIX)/bin
	@cp -f $(TARGET) $(PREFIX)/bin
	@chmod 755 $(PREFIX)/bin/$(TARGET)

uninstall:
	@echo removing executable from $(PREFIX)/bin
	@rm -f $(PREFIX)/bin/$(TARGET)
