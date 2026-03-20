CXX      = g++
CXXFLAGS = -Wall -Wextra -std=c++17 -O2
LDFLAGS  = -lncurses -ljpeg

TARGET = ytui
SRCS   = main.cpp globals.cpp utils.cpp youtube.cpp ui.cpp
OBJS   = $(SRCS:.cpp=.o)
HDRS   = config.h types.h globals.h utils.h youtube.h ui.h

.PHONY: all clean install run debug

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CXX) $(OBJS) -o $@ $(LDFLAGS)

%.o: %.cpp $(HDRS)
	$(CXX) $(CXXFLAGS) -c $< -o $@

clean:
	rm -f $(OBJS) $(TARGET)

install: $(TARGET)
	install -Dm755 $(TARGET) $(DESTDIR)/usr/local/bin/$(TARGET)

run: $(TARGET)
	./$(TARGET)

debug: CXXFLAGS += -g -DDEBUG
debug: clean $(TARGET)
