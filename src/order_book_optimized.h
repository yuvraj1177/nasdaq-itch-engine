#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include <optional>
#include <algorithm>
#include <cstring>
#include <iostream>

// Optimized Order representation with cache-friendly layout
struct Order {
    uint64_t order_id;
    uint32_t price;
    uint32_t qty;
    uint16_t stock_locate;
    char side; // 'B' or 'S'
    char padding[5]; // Align to 24 bytes
    std::string stock; // 24 bytes on most platforms
};

// High-performance open-addressing hash table with Robin Hood probing
class OptimizedOrderTable {
public:
    OptimizedOrderTable() {
        capacity_bits_ = 24; // 16M entries
        capacity_ = 1ULL << capacity_bits_;
        mask_ = capacity_ - 1;
        
        // Preallocate arrays
        keys_.resize(capacity_, 0);
        orders_.resize(capacity_);
        occupied_.resize(capacity_, false);
        psl_.resize(capacity_, 0); // Probe sequence length for Robin Hood
        
        size_ = 0;
    }

    void insert(uint64_t order_id, const Order& order) {
        if (size_ * 2 > capacity_) {
            // Would need rehashing; not implemented for simplicity
            return;
        }

        size_t idx = hash(order_id) & mask_;
        size_t probe_len = 0;
        
        Order to_insert = order;
        uint64_t key_to_insert = order_id;
        
        // Robin Hood insertion: swap with richer entries
        while (occupied_[idx]) {
            if (keys_[idx] == order_id) {
                // Update existing
                orders_[idx] = order;
                return;
            }
            
            // Robin Hood: steal from the rich
            if (probe_len > psl_[idx]) {
                std::swap(key_to_insert, keys_[idx]);
                std::swap(to_insert, orders_[idx]);
                size_t temp_psl = psl_[idx];
                psl_[idx] = static_cast<uint8_t>(probe_len);
                probe_len = temp_psl;
            }
            
            idx = (idx + 1) & mask_;
            probe_len++;
        }
        
        // Insert at empty slot
        keys_[idx] = key_to_insert;
        orders_[idx] = to_insert;
        occupied_[idx] = true;
        psl_[idx] = probe_len;
        size_++;
    }

    Order* find(uint64_t order_id) {
        size_t idx = hash(order_id) & mask_;
        size_t probe_len = 0;
        
        while (occupied_[idx]) {
            if (keys_[idx] == order_id) {
                return &orders_[idx];
            }
            
            // Robin Hood: if our PSL exceeds the slot's, key doesn't exist
            if (probe_len > psl_[idx]) {
                return nullptr;
            }
            
            idx = (idx + 1) & mask_;
            probe_len++;
        }
        
        return nullptr;
    }

    void erase(uint64_t order_id) {
        size_t idx = hash(order_id) & mask_;
        size_t probe_len = 0;
        
        while (occupied_[idx]) {
            if (keys_[idx] == order_id) {
                // Mark as deleted (tombstone approach simplified: just clear)
                occupied_[idx] = false;
                psl_[idx] = 0;
                size_--;
                
                // Backward shift to maintain Robin Hood invariant
                size_t next_idx = (idx + 1) & mask_;
                while (occupied_[next_idx] && psl_[next_idx] > 0) {
                    keys_[idx] = keys_[next_idx];
                    orders_[idx] = orders_[next_idx];
                    occupied_[idx] = true;
                    psl_[idx] = psl_[next_idx] - 1;
                    
                    occupied_[next_idx] = false;
                    psl_[next_idx] = 0;
                    
                    idx = next_idx;
                    next_idx = (idx + 1) & mask_;
                }
                
                return;
            }
            
            if (probe_len > psl_[idx]) {
                return;
            }
            
            idx = (idx + 1) & mask_;
            probe_len++;
        }
    }

    size_t size() const { return size_; }

private:
    size_t hash(uint64_t key) const {
        // FNV-1a style hash
        key ^= key >> 33;
        key *= 0xff51afd7ed558ccdULL;
        key ^= key >> 33;
        key *= 0xc4ceb9fe1a85ec53ULL;
        key ^= key >> 33;
        return static_cast<size_t>(key);
    }

    size_t capacity_;
    size_t capacity_bits_;
    size_t mask_;
    size_t size_;
    
    // SoA layout for better cache utilization
    std::vector<uint64_t> keys_;
    std::vector<Order> orders_;
    std::vector<bool> occupied_;
    std::vector<uint8_t> psl_; // Probe sequence length
};

// Price level for cache-friendly book
struct PriceLevel {
    uint32_t price;
    uint64_t total_qty;
    uint32_t order_count;
    
    PriceLevel() : price(0), total_qty(0), order_count(0) {}
    PriceLevel(uint32_t p, uint64_t q) : price(p), total_qty(q), order_count(1) {}
};

// Optimized Order Book using flat arrays and binary search
class OptimizedOrderBook {
public:
    OptimizedOrderBook(const std::string& symbol) : symbol_(symbol) {
        bids_.reserve(1000);
        asks_.reserve(1000);
    }

    void add_order(uint64_t /*order_id*/, char side, uint32_t price, uint32_t qty) {
        if (side == 'B') {
            add_to_side(bids_, price, qty, true);
        } else {
            add_to_side(asks_, price, qty, false);
        }
    }

    void remove_order(char side, uint32_t price, uint32_t qty) {
        if (side == 'B') {
            remove_from_side(bids_, price, qty);
        } else {
            remove_from_side(asks_, price, qty);
        }
    }

    void print_top(int levels = 5) const {
        std::cout << "\n=== Order Book: " << symbol_ << " ===\n";
        std::cout << "Bids:\n";
        
        // Bids are sorted descending (highest first)
        int count = 0;
        for (auto it = bids_.begin(); it != bids_.end() && count < levels; ++it, ++count) {
            std::cout << "  " << (it->price / 10000.0) << " x " << it->total_qty << "\n";
        }
        
        std::cout << "Asks:\n";
        count = 0;
        for (auto it = asks_.begin(); it != asks_.end() && count < levels; ++it, ++count) {
            std::cout << "  " << (it->price / 10000.0) << " x " << it->total_qty << "\n";
        }
    }

    const std::string& symbol() const { return symbol_; }

private:
    void add_to_side(std::vector<PriceLevel>& side, uint32_t price, uint32_t qty, bool is_bid) {
        auto comp = is_bid ? 
            [](const PriceLevel& a, uint32_t p) { return a.price > p; } :
            [](const PriceLevel& a, uint32_t p) { return a.price < p; };
        
        auto it = std::lower_bound(side.begin(), side.end(), price, comp);
        
        if (it != side.end() && it->price == price) {
            // Update existing level
            it->total_qty += qty;
            it->order_count++;
        } else {
            // Insert new level
            side.insert(it, PriceLevel(price, qty));
        }
    }

    void remove_from_side(std::vector<PriceLevel>& side, uint32_t price, uint32_t qty) {
        auto it = std::find_if(side.begin(), side.end(),
                               [price](const PriceLevel& l) { return l.price == price; });
        
        if (it != side.end()) {
            if (it->total_qty <= qty) {
                side.erase(it);
            } else {
                it->total_qty -= qty;
                if (it->order_count > 0) it->order_count--;
            }
        }
    }

    std::string symbol_;
    std::vector<PriceLevel> bids_; // Sorted descending (best bid first)
    std::vector<PriceLevel> asks_; // Sorted ascending (best ask first)
};

// Registry for optimized order books
class OptimizedOrderBookRegistry {
public:
    OptimizedOrderBook* get_or_create(const std::string& symbol) {
        // Simple linear search for now; could use hash map for many symbols
        for (auto& book : books_) {
            if (book.symbol() == symbol) {
                return &book;
            }
        }
        
        books_.emplace_back(symbol);
        return &books_.back();
    }

    size_t size() const { return books_.size(); }

private:
    std::vector<OptimizedOrderBook> books_;
};
