#pragma once

#include <cstdio>
#include <cstdint>
#include <string>

// High-performance L3 CSV exporter for normalized event stream
// Emits: timestamp_ns,event_type,order_id,price_ticks,size,side
class L3CsvExporter {
public:
    // Constructor
    // file: FILE* where CSV will be written (stdout or file)
    // tick_size_1e4: divisor for price conversion (price_1e4 / tick_size_1e4)
    // emit_header: whether to write CSV header row
    L3CsvExporter(FILE* file, int32_t tick_size_1e4, bool emit_header);
    
    ~L3CsvExporter();
    
    // Event handlers (called from ITCH message processing)
    void on_add(uint64_t timestamp_ns, uint64_t order_id, uint32_t price_1e4, 
                uint32_t size, char side);
    
    void on_cancel(uint64_t timestamp_ns, uint64_t order_id, uint32_t price_1e4, 
                   uint32_t size, char side);
    
    void on_exec(uint64_t timestamp_ns, uint64_t order_id, uint32_t price_1e4, 
                 uint32_t size, char side);
    
    void on_trade(uint64_t timestamp_ns, uint32_t price_1e4, uint32_t size, char side);
    
    // Flush any remaining buffered data
    void flush();
    
    // Statistics
    uint64_t rows_written() const { return rows_written_; }

private:
    void write_row(uint64_t timestamp_ns, const char* event_type, 
                   uint64_t order_id, int64_t price_ticks, int32_t size, char side);
    
    void flush_buffer();
    
    FILE* file_;
    int32_t tick_size_1e4_;
    
    // Buffering for performance
    static constexpr size_t BUFFER_SIZE = 256 * 1024; // 256KB buffer
    char buffer_[BUFFER_SIZE];
    size_t buffer_pos_;
    
    uint64_t rows_written_;
};
