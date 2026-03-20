# ── ITCH 5.0 Low-Latency Market Data Engine ──────────────────────────────────

CXX        = c++
CXXFLAGS   = -std=c++20 -Wall -Wextra -pedantic -O3 -march=native -DNDEBUG
DBG_FLAGS  = -std=c++20 -Wall -Wextra -pedantic -g -O0
INCLUDES   = -Iinclude

# Directories
BUILD_DIR  = build/bin
SRC_DIR    = src
TOOLS_DIR  = tools

# Targets
TARGET        = $(BUILD_DIR)/itch_engine
REPLAY_TARGET = $(BUILD_DIR)/replay_csv

# Sources
SRCS       = $(SRC_DIR)/main.cpp \
             $(SRC_DIR)/engine.cpp \
             $(SRC_DIR)/itch_parser.cpp \
             $(SRC_DIR)/order_book.cpp \
             $(SRC_DIR)/l3_csv_exporter.cpp

REPLAY_SRCS = $(TOOLS_DIR)/replay_csv.cpp

OBJS        = $(SRCS:.cpp=.o)
REPLAY_OBJS = $(REPLAY_SRCS:.cpp=.o)

# ── Rules ─────────────────────────────────────────────────────────────────────

.PHONY: all clean debug run-test

all: $(TARGET) $(REPLAY_TARGET)

$(BUILD_DIR):
	@mkdir -p $(BUILD_DIR)

$(TARGET): $(OBJS) | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) -o $@ $^

$(REPLAY_TARGET): $(REPLAY_OBJS) | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) -o $@ $^

$(SRC_DIR)/%.o: $(SRC_DIR)/%.cpp
	$(CXX) $(CXXFLAGS) $(INCLUDES) -c $< -o $@

$(TOOLS_DIR)/%.o: $(TOOLS_DIR)/%.cpp
	$(CXX) $(CXXFLAGS) $(INCLUDES) -c $< -o $@

debug: CXXFLAGS = $(DBG_FLAGS)
debug: clean all

clean:
	rm -rf $(BUILD_DIR) $(OBJS) $(REPLAY_OBJS)

run-test: $(TARGET)
	@echo "To test, first place the ITCH file in data/:"
	@echo "  gunzip -k data/01302020.NASDAQ_ITCH50.gz"
	@echo "Then run:"
	@echo "  $(TARGET) --file data/01302020.NASDAQ_ITCH50 --mode parse --print-first 100"
