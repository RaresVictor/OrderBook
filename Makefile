CXX = g++
CXXFLAGS = -std=c++2a -O2 -Wall

UNAME_S := $(shell uname -s)
ifeq ($(UNAME_S),Darwin)
	LDFLAGS = -lpthread
else
	LDFLAGS = -pthread
endif

SOURCES = src/main.cpp src/Orderbook.cpp
TARGET = OmniMatch

all: $(TARGET)

$(TARGET): $(SOURCES)
	$(CXX) $(CXXFLAGS) -o $(TARGET) $(SOURCES) $(LDFLAGS)

clean:
	rm -f $(TARGET)