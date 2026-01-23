#include "engine.h"
#include "itch_parser.h"
#include "spsc_queue.h"
#include <iostream>
#include <chrono>
#include <thread>

Engine::Engine(const Config& config) 
    : config_(config), msg_count_(0) {
}

void Engine::run() {
    std::cout << "Starting ITCH 5.0 Engine...\n";
    std::cout << "Input file: " << config_.input_file << "\n";
    std::cout << "Print first: " << config_.print_first << " messages\n";
    std::cout << "Order book enabled: " << (config_.enable_book ? "yes" : "no") << "\n";
    std::cout << "Timing enabled: " << (config_.enable_timing ? "yes" : "no") << "\n";
    std::cout << "SPSC mode: " << (config_.use_spsc_mode ? "yes (multi-threaded)" : "no (single-threaded)") << "\n";
    if (!config_.symbol_filter.empty()) {
        std::cout << "Symbol filter: " << config_.symbol_filter << "\n";
    }
    std::cout << "\n";

    if (config_.enable_timing) {
        latency_tracker_.start_timing();
    }

    if (config_.use_spsc_mode) {
        parse_and_process_spsc();
    } else {
        parse_and_process();
    }

    if (config_.enable_timing) {
        latency_tracker_.stop_timing();
        latency_tracker_.compute_and_print_stats();
        
        if (!config_.latency_output.empty()) {
            latency_tracker_.dump_to_file(config_.latency_output);
        }
    }

    std::cout << "\nProcessing complete.\n";
    std::cout << "Total messages processed: " << msg_count_ << "\n";
    
    if (config_.enable_book) {
        std::cout << "Order books maintained: " << book_registry_.size() << " symbols\n";
    }
}

void Engine::parse_and_process() {
    MappedFile mfile(config_.input_file);
    
    const uint8_t* ptr = mfile.data();
    const uint8_t* end = ptr + mfile.size();
    
    uint64_t seq = 0;
    
    while (ptr + 2 <= end) {
        // Read 2-byte big-endian length
        uint16_t msg_len = be16_to_host(ptr);
        ptr += 2;
        
        // Validate length
        if (ptr + msg_len > end) {
            std::cerr << "Incomplete message at end of file (expected " 
                      << msg_len << " bytes, have " << (end - ptr) << ")\n";
            break;
        }
        
        if (msg_len < 1) {
            std::cerr << "Invalid message length: " << msg_len << "\n";
            break;
        }
        
        // Read message type
        char msg_type = static_cast<char>(*ptr);
        
        // Process message
        process_message(msg_type, ptr, msg_len, seq);
        
        ptr += msg_len;
        seq++;
        msg_count_++;
    }
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
                    Order order;
                    order.order_id = msg.order_ref_number;
                    order.price = msg.price;
                    order.qty = msg.shares;
                    order.stock_locate = msg.stock_locate;
                    order.side = msg.buy_sell;
                    order.stock = msg.stock;
                    
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
                    Order order;
                    order.order_id = msg.order_ref_number;
                    order.price = msg.price;
                    order.qty = msg.shares;
                    order.stock_locate = msg.stock_locate;
                    order.side = msg.buy_sell;
                    order.stock = msg.stock;
                    
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
                    if (config_.symbol_filter.empty() || order->stock == config_.symbol_filter) {
                        auto* book = book_registry_.get_or_create(order->stock);
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
                    if (config_.symbol_filter.empty() || order->stock == config_.symbol_filter) {
                        auto* book = book_registry_.get_or_create(order->stock);
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
                    if (config_.symbol_filter.empty() || order->stock == config_.symbol_filter) {
                        auto* book = book_registry_.get_or_create(order->stock);
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
                    if (config_.symbol_filter.empty() || order->stock == config_.symbol_filter) {
                        auto* book = book_registry_.get_or_create(order->stock);
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
                    if (config_.symbol_filter.empty() || old_order->stock == config_.symbol_filter) {
                        auto* book = book_registry_.get_or_create(old_order->stock);
                        
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
            
            MessageEnvelope envelope{msg_type, msg_len, 0, ptr, seq};
            
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
