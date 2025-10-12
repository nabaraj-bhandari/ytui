# Makefile for ytui (tiny ncurses YouTube search player)
CXX = g++
CXXFLAGS = -std=c++17 -O2 -Wall -Wextra
LDFLAGS = -lncurses

TARGET = ytui
SRC = ytui.cpp

.PHONY: all clean

all: $(TARGET)

$(TARGET): $(SRC)
	$(CXX) $(CXXFLAGS) -o $@ $< $(LDFLAGS)

clean:
	-rm -f $(TARGET)

