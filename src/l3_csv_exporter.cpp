#include "l3_csv_exporter.h"
#include <cstring>

L3CsvExporter::L3CsvExporter(FILE* file, int32_t tick_size_1e4, bool emit_header)
    : file_(file)
    , tick_size_1e4_(tick_size_1e4)
    , buffer_pos_(0)
    , rows_written_(0)
{
    // Set up large buffer for FILE*
    setvbuf(file_, nullptr, _IOFBF, 256 * 1024);
    
    // Emit CSV header if requested
    if (emit_header) {
        const char* header = "timestamp_ns,event_type,order_id,price_ticks,size,side\n";
        size_t len = strlen(header);
        if (buffer_pos_ + len < BUFFER_SIZE) {
            memcpy(buffer_ + buffer_pos_, header, len);
            buffer_pos_ += len;
        } else {
            fwrite(header, 1, len, file_);
        }
    }
}

L3CsvExporter::~L3CsvExporter() {
    flush();
}

void L3CsvExporter::on_add(uint64_t timestamp_ns, uint64_t order_id, 
                           uint32_t price_1e4, uint32_t size, char side) {
    int64_t price_ticks = static_cast<int64_t>(price_1e4) / tick_size_1e4_;
    write_row(timestamp_ns, "ADD", order_id, price_ticks, size, side);
}

void L3CsvExporter::on_cancel(uint64_t timestamp_ns, uint64_t order_id, 
                              uint32_t price_1e4, uint32_t size, char side) {
    int64_t price_ticks = static_cast<int64_t>(price_1e4) / tick_size_1e4_;
    write_row(timestamp_ns, "CANCEL", order_id, price_ticks, size, side);
}

void L3CsvExporter::on_exec(uint64_t timestamp_ns, uint64_t order_id, 
                            uint32_t price_1e4, uint32_t size, char side) {
    int64_t price_ticks = static_cast<int64_t>(price_1e4) / tick_size_1e4_;
    write_row(timestamp_ns, "EXEC", order_id, price_ticks, size, side);
}

void L3CsvExporter::on_trade(uint64_t timestamp_ns, uint32_t price_1e4, 
                             uint32_t size, char side) {
    int64_t price_ticks = static_cast<int64_t>(price_1e4) / tick_size_1e4_;
    write_row(timestamp_ns, "TRADE", 0, price_ticks, size, side);
}

void L3CsvExporter::write_row(uint64_t timestamp_ns, const char* event_type,
                              uint64_t order_id, int64_t price_ticks, 
                              int32_t size, char side) {
    // Format: timestamp_ns,event_type,order_id,price_ticks,size,side
    char row[256];
    int len = snprintf(row, sizeof(row), "%llu,%s,%llu,%lld,%d,%c\n",
                      timestamp_ns, event_type, order_id, price_ticks, size, side);
    
    if (len < 0 || len >= static_cast<int>(sizeof(row))) {
        return; // Format error, skip
    }
    
    // If buffer has space, append; otherwise flush and write
    if (buffer_pos_ + len < BUFFER_SIZE) {
        memcpy(buffer_ + buffer_pos_, row, len);
        buffer_pos_ += len;
    } else {
        flush_buffer();
        if (len < static_cast<int>(BUFFER_SIZE)) {
            memcpy(buffer_ + buffer_pos_, row, len);
            buffer_pos_ += len;
        } else {
            // Row too large for buffer, write directly
            fwrite(row, 1, len, file_);
        }
    }
    
    rows_written_++;
}

void L3CsvExporter::flush_buffer() {
    if (buffer_pos_ > 0) {
        fwrite(buffer_, 1, buffer_pos_, file_);
        buffer_pos_ = 0;
    }
}

void L3CsvExporter::flush() {
    flush_buffer();
    fflush(file_);
}
