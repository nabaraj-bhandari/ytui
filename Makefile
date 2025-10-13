CXX = g++
CXXFLAGS = -std=c++17 -Wall -Wextra -O2
LIBS = -lncurses

TARGET = ytui
SRC = main.cpp
BUILD_DIR = build

all: $(BUILD_DIR) $(BUILD_DIR)/$(TARGET)

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

$(BUILD_DIR)/$(TARGET): $(SRC)
	$(CXX) $(CXXFLAGS) -o $@ $(SRC) $(LIBS)

clean:
	rm -rf $(BUILD_DIR)

install: all
	cp $(BUILD_DIR)/$(TARGET) /usr/local/bin/

uninstall:
	rm -f /usr/local/bin/$(TARGET)

.PHONY: all clean install uninstall
