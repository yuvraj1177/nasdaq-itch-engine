#include "itch/engine.h"
#include "itch/itch_parser.h"
#include "itch/spsc_queue.h"
#include <iostream>
#include <chrono>
#include <thread>

Engine::Engine(const Config& config) 
    : config_(config), msg_count_(0), csv_exporter_(nullptr) {
    // Initialize CSV exporter if requested
    if (config_.csv_export) {
        csv_exporter_ = new L3CsvExporter(stdout, config_.tick_size_1e4, config_.csv_header);
    }
}

void Engine::debug_print_symbol(const std::string& stock, SymbolKey key, bool matches_filter) {
    if (!config_.debug_symbols || debug_symbol_count_ >= 20) return;
    
    std::cout << "DEBUG Symbol #" << debug_symbol_count_ + 1 << ": "
              << "\"" << stock << "\" "
              << "(key=0x" << std::hex << key << std::dec << ") "
              << "filter_match=" << (matches_filter ? "YES" : "NO") << "\n";
    debug_symbol_count_++;
}

void Engine::run() {
    // Use stderr for all logs when CSV export is enabled
    FILE* log_out = config_.csv_export ? stderr : stdout;
    
    fprintf(log_out, "Starting ITCH 5.0 Engine...\n");
    fprintf(log_out, "Input file: %s\n", config_.input_file.c_str());
    
    // Print mode
    const char* mode_str = "parse";
    if (config_.mode == BenchmarkMode::PARSE_BOOK) mode_str = "parse_book";
    else if (config_.mode == BenchmarkMode::PIPELINE_BOOK) mode_str = "pipeline_book";
    else if (config_.mode == BenchmarkMode::FULL_BOOK) mode_str = "full_book";
    fprintf(log_out, "Mode: %s\n", mode_str);
    
    if (!config_.symbols.empty()) {
        fprintf(log_out, "Tracking symbols: %zu\n", config_.symbols.size());
    }
    if (config_.print_first > 0) {
        fprintf(log_out, "Print first: %llu messages\n", config_.print_first);
    }
    if (config_.csv_export) {
        fprintf(log_out, "CSV export: enabled (tick_size_1e4=%d)\n", config_.tick_size_1e4);
    }
    fprintf(log_out, "\n");

    // Pre-create books for tracked symbols
    if (config_.mode != BenchmarkMode::PARSE && !config_.symbols.empty()) {
        for (SymbolKey key : config_.symbols.keys()) {
            // Convert key back to string for book registry
            std::string sym;
            for (int i = 0; i < 8; ++i) {
                char c = static_cast<char>((key >> (i * 8)) & 0xFF);
                if (c != ' ') sym += c;
            }
            book_registry_.get_or_create(sym);
        }
    }

    latency_tracker_.start_timing();

    // Dispatch to correct benchmark mode
    switch (config_.mode) {
        case BenchmarkMode::PARSE:
            run_parse();
            break;
        case BenchmarkMode::PARSE_BOOK:
            run_parse_book();
            break;
        case BenchmarkMode::PIPELINE_BOOK:
            run_pipeline_book();
            break;
        case BenchmarkMode::FULL_BOOK:
            run_full_book();
            break;
    }

    latency_tracker_.stop_timing();
    
    // Flush CSV before stats
    if (csv_exporter_) {
        csv_exporter_->flush();
    }
    
    // Print stats to stderr if CSV export enabled
    if (config_.csv_export) {
        fprintf(log_out, "\n========== PERFORMANCE STATISTICS ==========\n");
    }
    
    latency_tracker_.compute_and_print_stats();
    
    if (!config_.latency_output.empty()) {
        latency_tracker_.dump_to_file(config_.latency_output);
    }

    fprintf(log_out, "\nProcessing complete.\n");
    fprintf(log_out, "Total messages processed: %llu\n", msg_count_);
    fprintf(log_out, "Book operations: adds=%llu execs=%llu deletes=%llu replaces=%llu cancels=%llu\n",
            adds_, execs_, deletes_, replaces_, cancels_);
    fprintf(log_out, "Unknown order events: %llu\n", unknown_order_events_);
    fprintf(log_out, "Skipped symbols: %llu\n", skipped_symbols_);
    
    // Compute and print book-hit fraction
    uint64_t total_book_ops = adds_ + execs_ + deletes_ + replaces_ + cancels_;
    if (msg_count_ > 0) {
        double book_hit_fraction = static_cast<double>(total_book_ops) / static_cast<double>(msg_count_);
        fprintf(log_out, "Book hit fraction: %.2f (%llu / %llu)\n", 
                book_hit_fraction, total_book_ops, msg_count_);
    }
    
    if (config_.mode != BenchmarkMode::PARSE) {
        if (config_.mode == BenchmarkMode::FULL_BOOK) {
            fprintf(log_out, "\nOrder books maintained: %zu symbols\n", symbol_book_registry_.size());
        } else {
            fprintf(log_out, "\nOrder books maintained: %zu symbols\n", book_registry_.size());
        }
        fprintf(log_out, "Live orders in table: %zu\n", order_table_.size());
    }
    
    if (csv_exporter_) {
        fprintf(log_out, "CSV rows exported: %llu\n", csv_exporter_->rows_written());
        delete csv_exporter_;
        csv_exporter_ = nullptr;
    }
}

// Mode 1: Parse only (no book updates)
void Engine::run_parse() {
    MappedFile mfile(config_.input_file);
    
    const uint8_t* ptr = mfile.data();
    const uint8_t* end = ptr + mfile.size();
    
    uint64_t seq = 0;
    
    while (ptr + 2 <= end) {
        uint16_t msg_len = be16_to_host(ptr);
        ptr += 2;
        
        if (ptr + msg_len > end || msg_len < 1) break;
        
        char msg_type = static_cast<char>(*ptr);
        
        auto t0 = std::chrono::steady_clock::now();
        
        // Parse message but don't apply to book
        const uint8_t* payload = ptr + 1;
        bool should_print = seq < config_.print_first;
        
        switch (msg_type) {
            case 'S': {
                itch::SystemEvent msg;
                msg.parse(payload);
                if (should_print) itch::print_system_event(msg, seq);
                break;
            }
            case 'A': {
                itch::AddOrder msg;
                msg.parse(payload);
                if (should_print) itch::print_add_order(msg, seq);
                break;
            }
            case 'F': {
                itch::AddOrderMPID msg;
                msg.parse(payload);
                if (should_print) itch::print_add_order_mpid(msg, seq);
                break;
            }
            case 'E': {
                itch::OrderExecuted msg;
                msg.parse(payload);
                if (should_print) itch::print_order_executed(msg, seq);
                break;
            }
            case 'C': {
                itch::OrderExecutedWithPrice msg;
                msg.parse(payload);
                if (should_print) itch::print_order_executed_with_price(msg, seq);
                break;
            }
            case 'X': {
                itch::OrderCancel msg;
                msg.parse(payload);
                if (should_print) itch::print_order_cancel(msg, seq);
                break;
            }
            case 'D': {
                itch::OrderDelete msg;
                msg.parse(payload);
                if (should_print) itch::print_order_delete(msg, seq);
                break;
            }
            case 'U': {
                itch::OrderReplace msg;
                msg.parse(payload);
                if (should_print) itch::print_order_replace(msg, seq);
                break;
            }
            case 'P': {
                itch::Trade msg;
                msg.parse(payload);
                if (should_print) itch::print_trade(msg, seq);
                break;
            }
            default:
                break;
        }
        
        auto t1 = std::chrono::steady_clock::now();
        auto dt = std::chrono::duration_cast<std::chrono::nanoseconds>(t1 - t0).count();
        latency_tracker_.record(dt);
        
        ptr += msg_len;
        seq++;
        msg_count_++;
    }
}

// Mode 2: Parse + book (single-threaded)
void Engine::run_parse_book() {
    MappedFile mfile(config_.input_file);
    
    const uint8_t* ptr = mfile.data();
    const uint8_t* end = ptr + mfile.size();
    
    uint64_t seq = 0;
    
    while (ptr + 2 <= end) {
        uint16_t msg_len = be16_to_host(ptr);
        ptr += 2;
        
        if (ptr + msg_len > end || msg_len < 1) break;
        
        char msg_type = static_cast<char>(*ptr);
        
        auto t0 = std::chrono::steady_clock::now();
        
        // Parse AND apply to book
        const uint8_t* payload = ptr + 1;
        bool should_print = seq < config_.print_first;
        
        switch (msg_type) {
            case 'A': {
                itch::AddOrder msg;
                msg.parse(payload);
                if (should_print) itch::print_add_order(msg, seq);
                
                SymbolKey sym_key = read_symbol_key(payload + 23); // stock offset (was 19, FIXED)
                bool matches = config_.symbols.empty() || config_.symbols.contains(sym_key);
                debug_print_symbol(msg.stock, sym_key, matches);
                
                if (matches) {
                    Order order;
                    order.order_id = msg.order_ref_number;
                    order.symbol = sym_key;
                    order.price = msg.price;
                    order.qty = msg.shares;
                    order.stock_locate = msg.stock_locate;
                    order.side = msg.buy_sell;
                    
                    order_table_.insert(msg.order_ref_number, order);
                    auto* book = book_registry_.get_or_create(msg.stock);
                    book->add_order(msg.order_ref_number, msg.buy_sell, msg.price, msg.shares);
                    adds_++;
                } else {
                    skipped_symbols_++;
                }
                break;
            }
            case 'F': {
                itch::AddOrderMPID msg;
                msg.parse(payload);
                if (should_print) itch::print_add_order_mpid(msg, seq);
                
                SymbolKey sym_key = read_symbol_key(payload + 23); // stock offset (was 19, FIXED)
                bool matches = config_.symbols.empty() || config_.symbols.contains(sym_key);
                debug_print_symbol(msg.stock, sym_key, matches);
                
                if (matches) {
                    Order order;
                    order.order_id = msg.order_ref_number;
                    order.symbol = sym_key;
                    order.price = msg.price;
                    order.qty = msg.shares;
                    order.stock_locate = msg.stock_locate;
                    order.side = msg.buy_sell;
                    
                    order_table_.insert(msg.order_ref_number, order);
                    auto* book = book_registry_.get_or_create(msg.stock);
                    book->add_order(msg.order_ref_number, msg.buy_sell, msg.price, msg.shares);
                    adds_++;
                } else {
                    skipped_symbols_++;
                }
                break;
            }
            case 'E': {
                itch::OrderExecuted msg;
                msg.parse(payload);
                if (should_print) itch::print_order_executed(msg, seq);
                
                Order* order = order_table_.find(msg.order_ref_number);
                if (order) {
                    std::string stock_str = symbol_key_to_string(order->symbol);
                    auto* book = book_registry_.get_or_create(stock_str);
                    book->remove_order(order->side, order->price, msg.executed_shares);
                    order->qty -= msg.executed_shares;
                    if (order->qty == 0) {
                        order_table_.erase(msg.order_ref_number);
                    }
                    execs_++;
                } else {
                    unknown_order_events_++;
                }
                break;
            }
            case 'C': {
                itch::OrderExecutedWithPrice msg;
                msg.parse(payload);
                if (should_print) itch::print_order_executed_with_price(msg, seq);
                
                Order* order = order_table_.find(msg.order_ref_number);
                if (order) {
                    std::string stock_str = symbol_key_to_string(order->symbol);
                    auto* book = book_registry_.get_or_create(stock_str);
                    book->remove_order(order->side, order->price, msg.executed_shares);
                    order->qty -= msg.executed_shares;
                    if (order->qty == 0) {
                        order_table_.erase(msg.order_ref_number);
                    }
                    execs_++;
                } else {
                    unknown_order_events_++;
                }
                break;
            }
            case 'X': {
                itch::OrderCancel msg;
                msg.parse(payload);
                if (should_print) itch::print_order_cancel(msg, seq);
                
                Order* order = order_table_.find(msg.order_ref_number);
                if (order) {
                    std::string stock_str = symbol_key_to_string(order->symbol);
                    auto* book = book_registry_.get_or_create(stock_str);
                    book->remove_order(order->side, order->price, msg.cancelled_shares);
                    order->qty -= msg.cancelled_shares;
                    if (order->qty == 0) {
                        order_table_.erase(msg.order_ref_number);
                    }
                    cancels_++;
                } else {
                    unknown_order_events_++;
                }
                break;
            }
            case 'D': {
                itch::OrderDelete msg;
                msg.parse(payload);
                if (should_print) itch::print_order_delete(msg, seq);
                
                Order* order = order_table_.find(msg.order_ref_number);
                if (order) {
                    std::string stock_str = symbol_key_to_string(order->symbol);
                    auto* book = book_registry_.get_or_create(stock_str);
                    book->remove_order(order->side, order->price, order->qty);
                    order_table_.erase(msg.order_ref_number);
                    deletes_++;
                } else {
                    unknown_order_events_++;
                }
                break;
            }
            case 'U': {
                itch::OrderReplace msg;
                msg.parse(payload);
                if (should_print) itch::print_order_replace(msg, seq);
                
                Order* old_order = order_table_.find(msg.original_order_ref_number);
                if (old_order) {
                    std::string stock_str = symbol_key_to_string(old_order->symbol);
                    auto* book = book_registry_.get_or_create(stock_str);
                    book->remove_order(old_order->side, old_order->price, old_order->qty);
                    
                    Order new_order = *old_order;
                    new_order.order_id = msg.new_order_ref_number;
                    new_order.price = msg.price;
                    new_order.qty = msg.shares;
                    
                    order_table_.erase(msg.original_order_ref_number);
                    order_table_.insert(msg.new_order_ref_number, new_order);
                    book->add_order(msg.new_order_ref_number, new_order.side, msg.price, msg.shares);
                    replaces_++;
                } else {
                    unknown_order_events_++;
                }
                break;
            }
            case 'S': {
                itch::SystemEvent msg;
                msg.parse(payload);
                if (should_print) itch::print_system_event(msg, seq);
                break;
            }
            case 'P': {
                itch::Trade msg;
                msg.parse(payload);
                if (should_print) itch::print_trade(msg, seq);
                break;
            }
            default:
                break;
        }
        
        auto t1 = std::chrono::steady_clock::now();
        auto dt = std::chrono::duration_cast<std::chrono::nanoseconds>(t1 - t0).count();
        latency_tracker_.record(dt);
        
        ptr += msg_len;
        seq++;
        msg_count_++;
    }
}

// Mode 3: Pipeline + book (two-threaded with SPSC)
void Engine::run_pipeline_book() {
    MappedFile mfile(config_.input_file);
    
    ITCHQueue queue;
    std::atomic<bool> reader_done{false};
    
    // Reader thread: parse framing and timestamp
    std::thread reader_thread([&]() {
        const uint8_t* ptr = mfile.data();
        const uint8_t* end = ptr + mfile.size();
        uint64_t seq = 0;
        
        while (ptr + 2 <= end) {
            uint16_t msg_len = be16_to_host(ptr);
            ptr += 2;
            
            if (ptr + msg_len > end || msg_len < 1) break;
            
            char msg_type = static_cast<char>(*ptr);
            
            // Timestamp AFTER framing validation
            auto t0 = std::chrono::steady_clock::now();
            uint64_t t0_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
                t0.time_since_epoch()
            ).count();
            
            MessageEnvelope envelope{msg_type, msg_len, 0, ptr, seq, t0_ns};
            
            while (!queue.try_push(envelope)) {
                std::this_thread::yield();
            }
            
            ptr += msg_len;
            seq++;
        }
        
        reader_done.store(true, std::memory_order_release);
    });
    
    // Worker thread: apply to book and measure end-to-end latency
    uint64_t processed = 0;
    MessageEnvelope envelope;
    
    while (true) {
        if (queue.try_pop(envelope)) {
            const uint8_t* payload = envelope.data + 1;
            bool should_print = envelope.seq < config_.print_first;
            
            switch (envelope.type) {
                case 'A': {
                    itch::AddOrder msg;
                    msg.parse(payload);
                    if (should_print) itch::print_add_order(msg, envelope.seq);
                    
                    SymbolKey sym_key = read_symbol_key(payload + 23); // stock offset (was 19, FIXED)
                    bool matches = config_.symbols.empty() || config_.symbols.contains(sym_key);
                    debug_print_symbol(msg.stock, sym_key, matches);
                    
                    if (matches) {
                        Order order;
                        order.order_id = msg.order_ref_number;
                        order.symbol = sym_key;
                        order.price = msg.price;
                        order.qty = msg.shares;
                        order.stock_locate = msg.stock_locate;
                        order.side = msg.buy_sell;
                        
                        order_table_.insert(msg.order_ref_number, order);
                        auto* book = book_registry_.get_or_create(msg.stock);
                        book->add_order(msg.order_ref_number, msg.buy_sell, msg.price, msg.shares);
                        adds_++;
                    } else {
                        skipped_symbols_++;
                    }
                    break;
                }
                case 'F': {
                    itch::AddOrderMPID msg;
                    msg.parse(payload);
                    if (should_print) itch::print_add_order_mpid(msg, envelope.seq);
                    
                    SymbolKey sym_key = read_symbol_key(payload + 23); // stock offset (was 19, FIXED)
                    bool matches = config_.symbols.empty() || config_.symbols.contains(sym_key);
                    debug_print_symbol(msg.stock, sym_key, matches);
                    
                    if (matches) {
                        Order order;
                        order.order_id = msg.order_ref_number;
                        order.symbol = sym_key;
                        order.price = msg.price;
                        order.qty = msg.shares;
                        order.stock_locate = msg.stock_locate;
                        order.side = msg.buy_sell;
                        
                        order_table_.insert(msg.order_ref_number, order);
                        auto* book = book_registry_.get_or_create(msg.stock);
                        book->add_order(msg.order_ref_number, msg.buy_sell, msg.price, msg.shares);
                        adds_++;
                    } else {
                        skipped_symbols_++;
                    }
                    break;
                }
                case 'E': {
                    itch::OrderExecuted msg;
                    msg.parse(payload);
                    if (should_print) itch::print_order_executed(msg, envelope.seq);
                    
                    Order* order = order_table_.find(msg.order_ref_number);
                    if (order) {
                        std::string stock_str = symbol_key_to_string(order->symbol);
                        auto* book = book_registry_.get_or_create(stock_str);
                        book->remove_order(order->side, order->price, msg.executed_shares);
                        order->qty -= msg.executed_shares;
                        if (order->qty == 0) {
                            order_table_.erase(msg.order_ref_number);
                        }
                        execs_++;
                    } else {
                        unknown_order_events_++;
                    }
                    break;
                }
                case 'C': {
                    itch::OrderExecutedWithPrice msg;
                    msg.parse(payload);
                    if (should_print) itch::print_order_executed_with_price(msg, envelope.seq);
                    
                    Order* order = order_table_.find(msg.order_ref_number);
                    if (order) {
                        std::string stock_str = symbol_key_to_string(order->symbol);
                        auto* book = book_registry_.get_or_create(stock_str);
                        book->remove_order(order->side, order->price, msg.executed_shares);
                        order->qty -= msg.executed_shares;
                        if (order->qty == 0) {
                            order_table_.erase(msg.order_ref_number);
                        }
                        execs_++;
                    } else {
                        unknown_order_events_++;
                    }
                    break;
                }
                case 'X': {
                    itch::OrderCancel msg;
                    msg.parse(payload);
                    if (should_print) itch::print_order_cancel(msg, envelope.seq);
                    
                    Order* order = order_table_.find(msg.order_ref_number);
                    if (order) {
                        std::string stock_str = symbol_key_to_string(order->symbol);
                        auto* book = book_registry_.get_or_create(stock_str);
                        book->remove_order(order->side, order->price, msg.cancelled_shares);
                        order->qty -= msg.cancelled_shares;
                        if (order->qty == 0) {
                            order_table_.erase(msg.order_ref_number);
                        }
                        cancels_++;
                    } else {
                        unknown_order_events_++;
                    }
                    break;
                }
                case 'D': {
                    itch::OrderDelete msg;
                    msg.parse(payload);
                    if (should_print) itch::print_order_delete(msg, envelope.seq);
                    
                    Order* order = order_table_.find(msg.order_ref_number);
                    if (order) {
                        std::string stock_str = symbol_key_to_string(order->symbol);
                        auto* book = book_registry_.get_or_create(stock_str);
                        book->remove_order(order->side, order->price, order->qty);
                        order_table_.erase(msg.order_ref_number);
                        deletes_++;
                    } else {
                        unknown_order_events_++;
                    }
                    break;
                }
                case 'U': {
                    itch::OrderReplace msg;
                    msg.parse(payload);
                    if (should_print) itch::print_order_replace(msg, envelope.seq);
                    
                    Order* old_order = order_table_.find(msg.original_order_ref_number);
                    if (old_order) {
                        std::string stock_str = symbol_key_to_string(old_order->symbol);
                        auto* book = book_registry_.get_or_create(stock_str);
                        book->remove_order(old_order->side, old_order->price, old_order->qty);
                        
                        Order new_order = *old_order;
                        new_order.order_id = msg.new_order_ref_number;
                        new_order.price = msg.price;
                        new_order.qty = msg.shares;
                        
                        order_table_.erase(msg.original_order_ref_number);
                        order_table_.insert(msg.new_order_ref_number, new_order);
                        book->add_order(msg.new_order_ref_number, new_order.side, msg.price, msg.shares);
                        replaces_++;
                    } else {
                        unknown_order_events_++;
                    }
                    break;
                }
                case 'S': {
                    itch::SystemEvent msg;
                    msg.parse(payload);
                    if (should_print) itch::print_system_event(msg, envelope.seq);
                    break;
                }
                case 'P': {
                    itch::Trade msg;
                    msg.parse(payload);
                    if (should_print) itch::print_trade(msg, envelope.seq);
                    break;
                }
                default:
                    break;
            }
            
            // Measure end-to-end latency (after book update)
            auto t1 = std::chrono::steady_clock::now();
            uint64_t t1_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
                t1.time_since_epoch()
            ).count();
            uint64_t latency_ns = t1_ns - envelope.t0_ns;
            latency_tracker_.record(latency_ns);
            
            processed++;
        } else {
            if (reader_done.load(std::memory_order_acquire) && queue.empty()) {
                break;
            }
            std::this_thread::yield();
        }
    }
    
    reader_thread.join();
    msg_count_ = processed;
}

// Mode 4: Full book (single-threaded, ALL symbols)
void Engine::run_full_book() {
    MappedFile mfile(config_.input_file);
    
    const uint8_t* ptr = mfile.data();
    const uint8_t* end = ptr + mfile.size();
    
    uint64_t seq = 0;
    
    while (ptr + 2 <= end) {
        uint16_t msg_len = be16_to_host(ptr);
        ptr += 2;
        
        if (ptr + msg_len > end || msg_len < 1) break;
        
        char msg_type = static_cast<char>(*ptr);
        
        auto t0 = std::chrono::steady_clock::now();
        
        // Parse AND apply to book (NO symbol filtering)
        const uint8_t* payload = ptr + 1;
        bool should_print = seq < config_.print_first;
        
        switch (msg_type) {
            case 'A': {
                itch::AddOrder msg;
                msg.parse(payload);
                if (should_print) itch::print_add_order(msg, seq);
                
                SymbolKey sym_key = read_symbol_key(payload + 23);
                debug_print_symbol(msg.stock, sym_key, true);
                
                Order order;
                order.order_id = msg.order_ref_number;
                order.symbol = sym_key;
                order.price = msg.price;
                order.qty = msg.shares;
                order.stock_locate = msg.stock_locate;
                order.side = msg.buy_sell;
                
                order_table_.insert(msg.order_ref_number, order);
                auto* book = symbol_book_registry_.get_or_create(sym_key);
                if (book) {
                    book->add_order(msg.order_ref_number, msg.buy_sell, msg.price, msg.shares);
                    adds_++;
                    
                    // CSV export
                    if (csv_exporter_) {
                        csv_exporter_->on_add(msg.timestamp, msg.order_ref_number, 
                                             msg.price, msg.shares, msg.buy_sell);
                    }
                }
                break;
            }
            case 'F': {
                itch::AddOrderMPID msg;
                msg.parse(payload);
                if (should_print) itch::print_add_order_mpid(msg, seq);
                
                SymbolKey sym_key = read_symbol_key(payload + 23);
                debug_print_symbol(msg.stock, sym_key, true);
                
                Order order;
                order.order_id = msg.order_ref_number;
                order.symbol = sym_key;
                order.price = msg.price;
                order.qty = msg.shares;
                order.stock_locate = msg.stock_locate;
                order.side = msg.buy_sell;
                
                order_table_.insert(msg.order_ref_number, order);
                auto* book = symbol_book_registry_.get_or_create(sym_key);
                if (book) {
                    book->add_order(msg.order_ref_number, msg.buy_sell, msg.price, msg.shares);
                    adds_++;
                    
                    // CSV export
                    if (csv_exporter_) {
                        csv_exporter_->on_add(msg.timestamp, msg.order_ref_number, 
                                             msg.price, msg.shares, msg.buy_sell);
                    }
                }
                break;
            }
            case 'E': {
                itch::OrderExecuted msg;
                msg.parse(payload);
                if (should_print) itch::print_order_executed(msg, seq);
                
                Order* order = order_table_.find(msg.order_ref_number);
                if (order) {
                    auto* book = symbol_book_registry_.get_or_create(order->symbol);
                    if (book) {
                        book->remove_order(order->side, order->price, msg.executed_shares);
                        
                        // CSV export (before qty update)
                        if (csv_exporter_) {
                            csv_exporter_->on_exec(msg.timestamp, msg.order_ref_number, 
                                                  order->price, msg.executed_shares, order->side);
                        }
                        
                        order->qty -= msg.executed_shares;
                        if (order->qty == 0) {
                            order_table_.erase(msg.order_ref_number);
                        }
                        execs_++;
                    }
                } else {
                    unknown_order_events_++;
                }
                break;
            }
            case 'C': {
                itch::OrderExecutedWithPrice msg;
                msg.parse(payload);
                if (should_print) itch::print_order_executed_with_price(msg, seq);
                
                Order* order = order_table_.find(msg.order_ref_number);
                if (order) {
                    auto* book = symbol_book_registry_.get_or_create(order->symbol);
                    if (book) {
                        book->remove_order(order->side, order->price, msg.executed_shares);
                        
                        // CSV export (before qty update)
                        if (csv_exporter_) {
                            csv_exporter_->on_exec(msg.timestamp, msg.order_ref_number, 
                                                  order->price, msg.executed_shares, order->side);
                        }
                        
                        order->qty -= msg.executed_shares;
                        if (order->qty == 0) {
                            order_table_.erase(msg.order_ref_number);
                        }
                        execs_++;
                    }
                } else {
                    unknown_order_events_++;
                }
                break;
            }
            case 'X': {
                itch::OrderCancel msg;
                msg.parse(payload);
                if (should_print) itch::print_order_cancel(msg, seq);
                
                Order* order = order_table_.find(msg.order_ref_number);
                if (order) {
                    auto* book = symbol_book_registry_.get_or_create(order->symbol);
                    if (book) {
                        book->remove_order(order->side, order->price, msg.cancelled_shares);
                        
                        // CSV export (before qty update)
                        if (csv_exporter_) {
                            csv_exporter_->on_cancel(msg.timestamp, msg.order_ref_number, 
                                                    order->price, msg.cancelled_shares, order->side);
                        }
                        
                        order->qty -= msg.cancelled_shares;
                        if (order->qty == 0) {
                            order_table_.erase(msg.order_ref_number);
                        }
                        cancels_++;
                    }
                } else {
                    unknown_order_events_++;
                }
                break;
            }
            case 'D': {
                itch::OrderDelete msg;
                msg.parse(payload);
                if (should_print) itch::print_order_delete(msg, seq);
                
                Order* order = order_table_.find(msg.order_ref_number);
                if (order) {
                    auto* book = symbol_book_registry_.get_or_create(order->symbol);
                    if (book) {
                        book->remove_order(order->side, order->price, order->qty);
                        
                        // CSV export (before deletion)
                        if (csv_exporter_) {
                            csv_exporter_->on_cancel(msg.timestamp, msg.order_ref_number, 
                                                    order->price, order->qty, order->side);
                        }
                        
                        order_table_.erase(msg.order_ref_number);
                        deletes_++;
                    }
                } else {
                    unknown_order_events_++;
                }
                break;
            }
            case 'U': {
                itch::OrderReplace msg;
                msg.parse(payload);
                if (should_print) itch::print_order_replace(msg, seq);
                
                Order* old_order = order_table_.find(msg.original_order_ref_number);
                if (old_order) {
                    auto* book = symbol_book_registry_.get_or_create(old_order->symbol);
                    if (book) {
                        book->remove_order(old_order->side, old_order->price, old_order->qty);
                        
                        // CSV export: CANCEL old + ADD new
                        if (csv_exporter_) {
                            csv_exporter_->on_cancel(msg.timestamp, msg.original_order_ref_number, 
                                                    old_order->price, old_order->qty, old_order->side);
                            csv_exporter_->on_add(msg.timestamp, msg.new_order_ref_number, 
                                                 msg.price, msg.shares, old_order->side);
                        }
                        
                        Order new_order = *old_order;
                        new_order.order_id = msg.new_order_ref_number;
                        new_order.price = msg.price;
                        new_order.qty = msg.shares;
                        
                        order_table_.erase(msg.original_order_ref_number);
                        order_table_.insert(msg.new_order_ref_number, new_order);
                        book->add_order(msg.new_order_ref_number, new_order.side, msg.price, msg.shares);
                        replaces_++;
                    }
                } else {
                    unknown_order_events_++;
                }
                break;
            }
            case 'S': {
                itch::SystemEvent msg;
                msg.parse(payload);
                if (should_print) itch::print_system_event(msg, seq);
                break;
            }
            case 'P': {
                itch::Trade msg;
                msg.parse(payload);
                if (should_print) itch::print_trade(msg, seq);
                break;
            }
            default:
                break;
        }
        
        auto t1 = std::chrono::steady_clock::now();
        auto dt = std::chrono::duration_cast<std::chrono::nanoseconds>(t1 - t0).count();
        latency_tracker_.record(dt);
        
        ptr += msg_len;
        seq++;
        msg_count_++;
    }
}

// Legacy method (kept for backward compatibility)
void Engine::parse_and_process() {
    // Delegate to parse mode
    run_parse();
}

void Engine::process_message(char type, const uint8_t* data, size_t /*len*/, uint64_t seq) {
    auto t0 = std::chrono::steady_clock::now();
    
    // Print first N messages
    bool should_print = seq < config_.print_first;
    
    // Skip type byte for parsing
    const uint8_t* payload = data + 1;
    
    switch (type) {
        case 'S': { // System Event
            itch::SystemEvent msg;
            msg.parse(payload);
            if (should_print) {
                itch::print_system_event(msg, seq);
            }
            break;
        }
        
        case 'A': { // Add Order
            itch::AddOrder msg;
            msg.parse(payload);
            if (should_print) {
                itch::print_add_order(msg, seq);
            }
            
            if (config_.enable_book) {
                // Apply to symbol filter if specified
                if (config_.symbol_filter.empty() || msg.stock == config_.symbol_filter) {
                    SymbolKey sym_key = read_symbol_key(payload + 23);
                    Order order;
                    order.order_id = msg.order_ref_number;
                    order.symbol = sym_key;
                    order.price = msg.price;
                    order.qty = msg.shares;
                    order.stock_locate = msg.stock_locate;
                    order.side = msg.buy_sell;
                    
                    order_table_.insert(msg.order_ref_number, order);
                    
                    auto* book = book_registry_.get_or_create(msg.stock);
                    book->add_order(msg.order_ref_number, msg.buy_sell, msg.price, msg.shares);
                }
            }
            break;
        }
        
        case 'F': { // Add Order with MPID
            itch::AddOrderMPID msg;
            msg.parse(payload);
            if (should_print) {
                itch::print_add_order_mpid(msg, seq);
            }
            
            if (config_.enable_book) {
                if (config_.symbol_filter.empty() || msg.stock == config_.symbol_filter) {
                    SymbolKey sym_key = read_symbol_key(payload + 23);
                    Order order;
                    order.order_id = msg.order_ref_number;
                    order.symbol = sym_key;
                    order.price = msg.price;
                    order.qty = msg.shares;
                    order.stock_locate = msg.stock_locate;
                    order.side = msg.buy_sell;
                    
                    order_table_.insert(msg.order_ref_number, order);
                    
                    auto* book = book_registry_.get_or_create(msg.stock);
                    book->add_order(msg.order_ref_number, msg.buy_sell, msg.price, msg.shares);
                }
            }
            break;
        }
        
        case 'E': { // Order Executed
            itch::OrderExecuted msg;
            msg.parse(payload);
            if (should_print) {
                itch::print_order_executed(msg, seq);
            }
            
            if (config_.enable_book) {
                Order* order = order_table_.find(msg.order_ref_number);
                if (order) {
                    std::string stock_str = symbol_key_to_string(order->symbol);
                    if (config_.symbol_filter.empty() || stock_str == config_.symbol_filter) {
                        auto* book = book_registry_.get_or_create(stock_str);
                        book->remove_order(order->side, order->price, msg.executed_shares);
                        
                        // Update order quantity
                        order->qty -= msg.executed_shares;
                        if (order->qty == 0) {
                            order_table_.erase(msg.order_ref_number);
                        }
                    }
                }
            }
            break;
        }
        
        case 'C': { // Order Executed with Price
            itch::OrderExecutedWithPrice msg;
            msg.parse(payload);
            if (should_print) {
                itch::print_order_executed_with_price(msg, seq);
            }
            
            if (config_.enable_book) {
                Order* order = order_table_.find(msg.order_ref_number);
                if (order) {
                    std::string stock_str = symbol_key_to_string(order->symbol);
                    if (config_.symbol_filter.empty() || stock_str == config_.symbol_filter) {
                        auto* book = book_registry_.get_or_create(stock_str);
                        book->remove_order(order->side, order->price, msg.executed_shares);
                        
                        order->qty -= msg.executed_shares;
                        if (order->qty == 0) {
                            order_table_.erase(msg.order_ref_number);
                        }
                    }
                }
            }
            break;
        }
        
        case 'X': { // Order Cancel
            itch::OrderCancel msg;
            msg.parse(payload);
            if (should_print) {
                itch::print_order_cancel(msg, seq);
            }
            
            if (config_.enable_book) {
                Order* order = order_table_.find(msg.order_ref_number);
                if (order) {
                    std::string stock_str = symbol_key_to_string(order->symbol);
                    if (config_.symbol_filter.empty() || stock_str == config_.symbol_filter) {
                        auto* book = book_registry_.get_or_create(stock_str);
                        book->remove_order(order->side, order->price, msg.cancelled_shares);
                        
                        order->qty -= msg.cancelled_shares;
                        if (order->qty == 0) {
                            order_table_.erase(msg.order_ref_number);
                        }
                    }
                }
            }
            break;
        }
        
        case 'D': { // Order Delete
            itch::OrderDelete msg;
            msg.parse(payload);
            if (should_print) {
                itch::print_order_delete(msg, seq);
            }
            
            if (config_.enable_book) {
                Order* order = order_table_.find(msg.order_ref_number);
                if (order) {
                    std::string stock_str = symbol_key_to_string(order->symbol);
                    if (config_.symbol_filter.empty() || stock_str == config_.symbol_filter) {
                        auto* book = book_registry_.get_or_create(stock_str);
                        book->remove_order(order->side, order->price, order->qty);
                        order_table_.erase(msg.order_ref_number);
                    }
                }
            }
            break;
        }
        
        case 'U': { // Order Replace
            itch::OrderReplace msg;
            msg.parse(payload);
            if (should_print) {
                itch::print_order_replace(msg, seq);
            }
            
            if (config_.enable_book) {
                Order* old_order = order_table_.find(msg.original_order_ref_number);
                if (old_order) {
                    std::string stock_str = symbol_key_to_string(old_order->symbol);
                    if (config_.symbol_filter.empty() || stock_str == config_.symbol_filter) {
                        auto* book = book_registry_.get_or_create(stock_str);
                        
                        // Remove old order
                        book->remove_order(old_order->side, old_order->price, old_order->qty);
                        
                        // Add new order
                        Order new_order = *old_order;
                        new_order.order_id = msg.new_order_ref_number;
                        new_order.price = msg.price;
                        new_order.qty = msg.shares;
                        
                        order_table_.erase(msg.original_order_ref_number);
                        order_table_.insert(msg.new_order_ref_number, new_order);
                        book->add_order(msg.new_order_ref_number, new_order.side, msg.price, msg.shares);
                    }
                }
            }
            break;
        }
        
        case 'P': { // Trade
            itch::Trade msg;
            msg.parse(payload);
            if (should_print) {
                itch::print_trade(msg, seq);
            }
            // Note: Trade messages don't affect the displayed book by default
            break;
        }
        
        default:
            // Silently skip unsupported message types
            break;
    }
    
    if (config_.enable_timing) {
        auto t1 = std::chrono::steady_clock::now();
        auto dt = std::chrono::duration_cast<std::chrono::nanoseconds>(t1 - t0).count();
        latency_tracker_.record(dt);
    }
    
    // Optionally show book after updates
    if (config_.show_book_updates && config_.enable_book && !config_.symbol_filter.empty()) {
        if (seq > 0 && seq % 1000 == 0) {
            auto* book = book_registry_.get_or_create(config_.symbol_filter);
            book->print_top(5);
        }
    }
}

void Engine::parse_and_process_spsc() {
    MappedFile mfile(config_.input_file);
    
    ITCHQueue queue;
    std::atomic<bool> reader_done{false};
    std::atomic<uint64_t> messages_read{0};
    
    // Reader thread: parses mmap and pushes to queue
    std::thread reader_thread([&]() {
        const uint8_t* ptr = mfile.data();
        const uint8_t* end = ptr + mfile.size();
        uint64_t seq = 0;
        
        while (ptr + 2 <= end) {
            uint16_t msg_len = be16_to_host(ptr);
            ptr += 2;
            
            if (ptr + msg_len > end || msg_len < 1) {
                break;
            }
            
            char msg_type = static_cast<char>(*ptr);
            
            MessageEnvelope envelope{msg_type, msg_len, 0, ptr, seq, 0};
            
            // Spin until we can push (queue full backpressure)
            while (!queue.try_push(envelope)) {
                std::this_thread::yield();
            }
            
            ptr += msg_len;
            seq++;
            messages_read.store(seq, std::memory_order_relaxed);
        }
        
        reader_done.store(true, std::memory_order_release);
    });
    
    // Processor thread: pops from queue and applies
    uint64_t processed = 0;
    MessageEnvelope envelope;
    
    while (true) {
        if (queue.try_pop(envelope)) {
            process_message(envelope.type, envelope.data, envelope.length, envelope.seq);
            processed++;
        } else {
            if (reader_done.load(std::memory_order_acquire) && queue.empty()) {
                break; // Reader finished and queue drained
            }
            std::this_thread::yield();
        }
    }
    
    reader_thread.join();
    msg_count_ = processed;
}
