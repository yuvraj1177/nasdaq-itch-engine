#pragma once

#include <string>
#include <cstdint>
#include <vector>
#include "itch/mapped_file.h"

// Toggle between basic and optimized data structures
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
#include "itch/l3_csv_exporter.h"

// Benchmark modes
enum class BenchmarkMode {
    PARSE,           // Parse only, no book
    PARSE_BOOK,      // Parse + book (single thread, symbol filtered)
    PIPELINE_BOOK,   // Parse + book (two threads with SPSC, symbol filtered)
    FULL_BOOK        // Parse + book (single thread, ALL symbols, stress test)
};

// Symbol key: 8-byte packed representation (space-padded)
using SymbolKey = uint64_t;

// Helper functions for symbol handling
inline SymbolKey parse_symbol(const std::string& s) {
    uint64_t key = 0;
    const size_t len = std::min(s.size(), size_t(8));
    for (size_t i = 0; i < len; ++i) {
        key |= (static_cast<uint64_t>(s[i]) << (i * 8));
    }
    // Pad with spaces for remaining bytes
    for (size_t i = len; i < 8; ++i) {
        key |= (static_cast<uint64_t>(' ') << (i * 8));
    }
    return key;
}

inline SymbolKey read_symbol_key(const uint8_t* p) {
    uint64_t key = 0;
    for (int i = 0; i < 8; ++i) {
        key |= (static_cast<uint64_t>(p[i]) << (i * 8));
    }
    return key;
}

// Helper to convert SymbolKey back to string
inline std::string symbol_key_to_string(SymbolKey key) {
    std::string s;
    s.reserve(8);
    for (int i = 0; i < 8; ++i) {
        char c = static_cast<char>((key >> (i * 8)) & 0xFF);
        if (c != ' ') s += c;
    }
    return s;
}

// Small symbol set for cache-friendly membership test
class SymbolSet {
public:
    void add(SymbolKey key) {
        if (!contains(key)) {
            keys_.push_back(key);
        }
    }
    
    bool contains(SymbolKey key) const {
        for (auto k : keys_) {
            if (k == key) return true;
        }
        return false;
    }
    
    bool empty() const { return keys_.empty(); }
    size_t size() const { return keys_.size(); }
    
    const std::vector<SymbolKey>& keys() const { return keys_; }
    
private:
    std::vector<SymbolKey> keys_; // Linear search, N is tiny (1-10)
};

class Engine {
public:
    struct Config {
        std::string input_file;
        uint64_t print_first = 0;
        BenchmarkMode mode = BenchmarkMode::PARSE;
        SymbolSet symbols; // Empty = all symbols
        std::string latency_output;
        bool show_book_updates = false;
        bool debug_symbols = false;
        
        // CSV export options
        bool csv_export = false;
        int32_t tick_size_1e4 = 1; // Default: 0.0001 tick size
        bool csv_header = false;
        
        // Deprecated (mapped to mode for backward compat)
        bool enable_book = false;
        bool enable_timing = false;
        std::string symbol_filter;
        bool use_spsc_mode = false;
    };

    explicit Engine(const Config& config);
    
    void run();

private:
    // Four benchmark modes
    void run_parse();
    void run_parse_book();
    void run_pipeline_book();
    void run_full_book();
    
    // Helper for debug symbol printing
    void debug_print_symbol(const std::string& stock, SymbolKey key, bool matches_filter);
    
    // Legacy methods (will be refactored)
    void parse_and_process();
    void parse_and_process_spsc();
    void process_message(char type, const uint8_t* data, size_t len, uint64_t seq);
    
    Config config_;
    OrderTable order_table_;
    OrderBookRegistry book_registry_;
    SymbolBookRegistry symbol_book_registry_; // For full_book mode
    LatencyTracker latency_tracker_;
    uint64_t msg_count_;
    
    // CSV exporter (optional)
    L3CsvExporter* csv_exporter_;
    
    // Counters for observability
    uint64_t adds_ = 0;
    uint64_t execs_ = 0;
    uint64_t deletes_ = 0;
    uint64_t replaces_ = 0;
    uint64_t cancels_ = 0;
    uint64_t unknown_order_events_ = 0;
    uint64_t skipped_symbols_ = 0;
    uint64_t debug_symbol_count_ = 0;
};
