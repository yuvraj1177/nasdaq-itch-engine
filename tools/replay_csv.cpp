#include <iostream>
#include <fstream>
#include <sstream>
#include <string>
#include <map>
#include <cstdint>
#include <vector>
#include <algorithm>

// Simple L3 Order Book for validation
struct SimpleOrder {
    uint64_t order_id;
    int64_t price_ticks;
    int32_t size;
    char side;
};

class SimpleOrderBook {
public:
    void add_order(uint64_t order_id, int64_t price_ticks, int32_t size, char side) {
        orders_[order_id] = {order_id, price_ticks, size, side};
        
        if (side == 'B') {
            bid_levels_[price_ticks] += size;
        } else {
            ask_levels_[price_ticks] += size;
        }
    }
    
    void cancel_order(uint64_t order_id, int32_t size) {
        auto it = orders_.find(order_id);
        if (it == orders_.end()) return;
        
        SimpleOrder& order = it->second;
        
        if (order.side == 'B') {
            bid_levels_[order.price_ticks] -= size;
            if (bid_levels_[order.price_ticks] <= 0) {
                bid_levels_.erase(order.price_ticks);
            }
        } else {
            ask_levels_[order.price_ticks] -= size;
            if (ask_levels_[order.price_ticks] <= 0) {
                ask_levels_.erase(order.price_ticks);
            }
        }
        
        order.size -= size;
        if (order.size <= 0) {
            orders_.erase(it);
        }
    }
    
    void exec_order(uint64_t order_id, int32_t size) {
        cancel_order(order_id, size); // Same semantics as cancel
    }
    
    int64_t best_bid() const {
        if (bid_levels_.empty()) return 0;
        return bid_levels_.rbegin()->first; // Highest bid
    }
    
    int64_t best_ask() const {
        if (ask_levels_.empty()) return 0;
        return ask_levels_.begin()->first; // Lowest ask
    }
    
    int64_t total_bid_qty() const {
        int64_t total = 0;
        for (const auto& [price, qty] : bid_levels_) {
            total += qty;
        }
        return total;
    }
    
    int64_t total_ask_qty() const {
        int64_t total = 0;
        for (const auto& [price, qty] : ask_levels_) {
            total += qty;
        }
        return total;
    }
    
    size_t bid_levels() const { return bid_levels_.size(); }
    size_t ask_levels() const { return ask_levels_.size(); }
    size_t total_orders() const { return orders_.size(); }

private:
    std::map<uint64_t, SimpleOrder> orders_;
    std::map<int64_t, int64_t> bid_levels_; // price -> total_qty
    std::map<int64_t, int64_t> ask_levels_;
};

int main(int argc, char** argv) {
    if (argc < 2) {
        std::cerr << "Usage: " << argv[0] << " <simulation_input.csv>\n";
        return 1;
    }
    
    std::string csv_file = argv[1];
    std::ifstream infile(csv_file);
    
    if (!infile) {
        std::cerr << "Error: Cannot open " << csv_file << "\n";
        return 1;
    }
    
    SimpleOrderBook book;
    std::string line;
    uint64_t line_num = 0;
    uint64_t adds = 0, cancels = 0, execs = 0, trades = 0;
    uint64_t unknown_order_events = 0;
    
    // Skip header if present
    if (std::getline(infile, line)) {
        if (line.find("timestamp_ns") != std::string::npos) {
            line_num++;
            std::cout << "Skipping CSV header\n";
        } else {
            infile.seekg(0); // Rewind if no header
        }
    }
    
    while (std::getline(infile, line)) {
        line_num++;
        
        if (line.empty()) continue;
        
        // Parse CSV: timestamp_ns,event_type,order_id,price_ticks,size,side
        std::stringstream ss(line);
        std::string token;
        std::vector<std::string> tokens;
        
        while (std::getline(ss, token, ',')) {
            tokens.push_back(token);
        }
        
        if (tokens.size() != 6) {
            std::cerr << "Line " << line_num << ": Invalid CSV format (expected 6 columns)\n";
            continue;
        }
        
        uint64_t timestamp_ns = std::stoull(tokens[0]);
        std::string event_type = tokens[1];
        uint64_t order_id = std::stoull(tokens[2]);
        int64_t price_ticks = std::stoll(tokens[3]);
        int32_t size = std::stoi(tokens[4]);
        char side = tokens[5][0];
        
        (void)timestamp_ns; // unused for now
        
        if (event_type == "ADD") {
            book.add_order(order_id, price_ticks, size, side);
            adds++;
        } else if (event_type == "CANCEL") {
            if (order_id == 0) {
                unknown_order_events++;
            } else {
                book.cancel_order(order_id, size);
                cancels++;
            }
        } else if (event_type == "EXEC") {
            if (order_id == 0) {
                unknown_order_events++;
            } else {
                book.exec_order(order_id, size);
                execs++;
            }
        } else if (event_type == "TRADE") {
            trades++;
            // TRADE is informational only, does not mutate book
        } else {
            std::cerr << "Line " << line_num << ": Unknown event_type: " << event_type << "\n";
        }
        
        // Periodic snapshot (every 10M events)
        if (line_num % 10000000 == 0) {
            std::cout << "Processed " << line_num << " events\n";
            std::cout << "  Best bid: " << book.best_bid() << " (levels: " << book.bid_levels() << ")\n";
            std::cout << "  Best ask: " << book.best_ask() << " (levels: " << book.ask_levels() << ")\n";
            std::cout << "  Total orders: " << book.total_orders() << "\n";
        }
    }
    
    std::cout << "\n=== REPLAY VALIDATION COMPLETE ===\n";
    std::cout << "Total lines processed: " << line_num << "\n";
    std::cout << "Events: adds=" << adds << " cancels=" << cancels 
              << " execs=" << execs << " trades=" << trades << "\n";
    std::cout << "Unknown order events: " << unknown_order_events << "\n";
    std::cout << "\nFinal Book State:\n";
    std::cout << "  Best bid: " << book.best_bid() << " (qty: " << book.total_bid_qty() << ")\n";
    std::cout << "  Best ask: " << book.best_ask() << " (qty: " << book.total_ask_qty() << ")\n";
    std::cout << "  Bid levels: " << book.bid_levels() << "\n";
    std::cout << "  Ask levels: " << book.ask_levels() << "\n";
    std::cout << "  Live orders: " << book.total_orders() << "\n";
    
    return 0;
}
