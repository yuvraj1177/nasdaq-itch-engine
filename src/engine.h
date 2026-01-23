#pragma once

#include <string>
#include <cstdint>
#include "mapped_file.h"

// Toggle between basic and optimized data structures
#define USE_OPTIMIZED_DS 1

#if USE_OPTIMIZED_DS
#include "order_book_optimized.h"
using OrderTable = OptimizedOrderTable;
using OrderBookRegistry = OptimizedOrderBookRegistry;
using OrderBook = OptimizedOrderBook;
#else
#include "order_book.h"
#endif

#include "latency_tracker.h"

class Engine {
public:
    struct Config {
        std::string input_file;
        uint64_t print_first = 0;
        bool enable_book = false;
        bool enable_timing = false;
        std::string symbol_filter; // empty = all symbols
        std::string latency_output;
        bool show_book_updates = false;
        bool use_spsc_mode = false; // Enable reader/processor threads
    };

    explicit Engine(const Config& config);
    
    void run();

private:
    void parse_and_process();
    void parse_and_process_spsc(); // Multi-threaded mode
    void process_message(char type, const uint8_t* data, size_t len, uint64_t seq);
    
    Config config_;
    OrderTable order_table_;
    OrderBookRegistry book_registry_;
    LatencyTracker latency_tracker_;
    uint64_t msg_count_;
};
