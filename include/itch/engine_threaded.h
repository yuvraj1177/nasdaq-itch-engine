#pragma once

#include <string>
#include <cstdint>
#include <thread>
#include <atomic>
#include "itch/mapped_file.h"
#include "itch/spsc_ring.h"

#define USE_OPTIMIZED_DS 1

#if USE_OPTIMIZED_DS
#include "itch/order_book_optimized.h"
using OrderTable = OptimizedOrderTable;
using OrderBookRegistry = OptimizedOrderBookRegistry;
using OrderBook = OptimizedOrderBook;
#else
#include "itch/order_book.h"
#endif

#include "itch/latency_tracker.h"

// Dual-threaded engine with SPSC ring buffer
class ThreadedEngine {
public:
    struct Config {
        std::string input_file;
        uint64_t print_first = 0;
        bool enable_book = false;
        bool enable_timing = false;
        std::string symbol_filter;
        std::string latency_output;
        bool show_book_updates = false;
    };

    explicit ThreadedEngine(const Config& config);
    ~ThreadedEngine();
    
    void run();

private:
    void reader_thread();
    void processor_thread();
    void process_message(const MessageEnvelope& env);
    
    Config config_;
    
    // Shared between threads
    std::unique_ptr<MappedFile> mfile_;
    ITCHMessageRing ring_;
    std::atomic<bool> reader_done_;
    
    // Processor thread state
    OrderTable order_table_;
    OrderBookRegistry book_registry_;
    LatencyTracker latency_tracker_;
    uint64_t msg_count_;
    
    // Threads
    std::thread reader_;
    std::thread processor_;
};
