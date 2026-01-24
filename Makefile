# Makefile for ITCH 5.0 Market Data Engine

CXX = c++
CXXFLAGS = -std=c++20 -Wall -Wextra -pedantic -O3 -march=native -DNDEBUG
CXXFLAGS_DEBUG = -std=c++20 -Wall -Wextra -pedantic -g -O0

TARGET = itch_engine
REPLAY_TARGET = replay_csv

SRCS = src/main.cpp src/engine.cpp src/itch_parser.cpp src/order_book.cpp src/l3_csv_exporter.cpp
OBJS = $(SRCS:.cpp=.o)

REPLAY_SRCS = tools/replay_csv.cpp
REPLAY_OBJS = $(REPLAY_SRCS:.cpp=.o)

.PHONY: all clean debug

all: $(TARGET) $(REPLAY_TARGET)

$(TARGET): $(OBJS)
	$(CXX) $(CXXFLAGS) -o $@ $^

$(REPLAY_TARGET): $(REPLAY_OBJS)
	$(CXX) $(CXXFLAGS) -o $@ $^

src/%.o: src/%.cpp
	$(CXX) $(CXXFLAGS) -Isrc -c $< -o $@

tools/%.o: tools/%.cpp
	$(CXX) $(CXXFLAGS) -c $< -o $@

debug: CXXFLAGS = $(CXXFLAGS_DEBUG)
debug: clean $(TARGET)

clean:
	rm -f $(TARGET) $(REPLAY_TARGET) $(OBJS) $(REPLAY_OBJS)

run-test: $(TARGET)
	@echo "To test, first decompress the ITCH file:"
	@echo "  gunzip -k 01302020.NASDAQ_ITCH50.gz"
	@echo "Then run:"
	@echo "  ./$(TARGET) --file 01302020.NASDAQ_ITCH50 --print-first 100"
