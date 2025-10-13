# Makefile for ytui (intermediate structure)
# See config.h for configuration

# Compiler and flags
CXX = g++
CXXFLAGS = -std=c++17 -Wall -Wextra -pedantic -O2
LDFLAGS = -lncurses -lpthread

# Executable name
TARGET = ytui

# Source file
SRC = ytui.cpp

# Prefix for installation
PREFIX = /usr/local

.PHONY: all clean install uninstall

all: $(TARGET)

$(TARGET): $(SRC)
	$(CXX) $(CXXFLAGS) $(SRC) -o $(TARGET) $(LDFLAGS)

clean:
	rm -f $(TARGET)

install: all
	@echo installing executable to $(PREFIX)/bin
	@mkdir -p $(PREFIX)/bin
	@cp -f $(TARGET) $(PREFIX)/bin
	@chmod 755 $(PREFIX)/bin/$(TARGET)

uninstall:
	@echo removing executable from $(PREFIX)/bin
	@rm -f $(PREFIX)/bin/$(TARGET)
