#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>
#include <map>
#include <vector>
#include <optional>
#include <iostream>

// Order representation
struct Order {
    uint64_t order_id;
    uint16_t stock_locate;
    std::string stock;
    char side; // 'B' or 'S'
    uint32_t price;
    uint32_t qty;
};

// Custom open-addressing hash table for Order lookup
// Simple implementation for Step 2; will optimize in Step 4
class OrderTable {
public:
    OrderTable() {
        size_ = 1 << 24; // 16M capacity
        table_.resize(size_);
        mask_ = size_ - 1;
    }

    void insert(uint64_t order_id, const Order& order) {
        size_t idx = hash(order_id) & mask_;
        
        // Linear probing
        while (table_[idx].has_value() && table_[idx]->order_id != order_id) {
            idx = (idx + 1) & mask_;
        }
        
        table_[idx] = order;
    }

    Order* find(uint64_t order_id) {
        size_t idx = hash(order_id) & mask_;
        
        while (table_[idx].has_value()) {
            if (table_[idx]->order_id == order_id) {
                return &(*table_[idx]);
            }
            idx = (idx + 1) & mask_;
        }
        
        return nullptr;
    }

    void erase(uint64_t order_id) {
        size_t idx = hash(order_id) & mask_;
        
        while (table_[idx].has_value()) {
            if (table_[idx]->order_id == order_id) {
                table_[idx].reset();
                return;
            }
            idx = (idx + 1) & mask_;
        }
    }

private:
    size_t hash(uint64_t key) const {
        // Simple hash function
        key ^= key >> 33;
        key *= 0xff51afd7ed558ccdULL;
        key ^= key >> 33;
        key *= 0xc4ceb9fe1a85ec53ULL;
        key ^= key >> 33;
        return static_cast<size_t>(key);
    }

    std::vector<std::optional<Order>> table_;
    size_t size_;
    size_t mask_;
};

// Price level representation
struct PriceLevel {
    uint32_t price;
    uint64_t total_qty;
    uint32_t order_count;
};

// Order Book for a single symbol
// Uses simple sorted vectors for correctness; will optimize in Step 4
class OrderBook {
public:
    OrderBook(const std::string& symbol) : symbol_(symbol) {}

    void add_order(uint64_t /*order_id*/, char side, uint32_t price, uint32_t qty) {
        if (side == 'B') {
            add_to_side(bids_, price, qty);
        } else {
            add_to_side(asks_, price, qty);
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
        int count = 0;
        for (auto it = bids_.rbegin(); it != bids_.rend() && count < levels; ++it, ++count) {
            std::cout << "  " << (it->first / 10000.0) << " x " << it->second << "\n";
        }
        std::cout << "Asks:\n";
        count = 0;
        for (auto it = asks_.begin(); it != asks_.end() && count < levels; ++it, ++count) {
            std::cout << "  " << (it->first / 10000.0) << " x " << it->second << "\n";
        }
    }

    const std::string& symbol() const { return symbol_; }

private:
    void add_to_side(std::map<uint32_t, uint64_t>& side, uint32_t price, uint32_t qty) {
        side[price] += qty;
    }

    void remove_from_side(std::map<uint32_t, uint64_t>& side, uint32_t price, uint32_t qty) {
        auto it = side.find(price);
        if (it != side.end()) {
            if (it->second <= qty) {
                side.erase(it);
            } else {
                it->second -= qty;
            }
        }
    }

    std::string symbol_;
    std::map<uint32_t, uint64_t> bids_; // price -> total qty (sorted ascending)
    std::map<uint32_t, uint64_t> asks_; // price -> total qty (sorted ascending)
};

// Order Book registry managing multiple symbols
class OrderBookRegistry {
public:
    OrderBook* get_or_create(const std::string& symbol) {
        auto it = books_.find(symbol);
        if (it != books_.end()) {
            return &it->second;
        }
        
        auto [new_it, inserted] = books_.emplace(symbol, OrderBook(symbol));
        return &new_it->second;
    }

    size_t size() const { return books_.size(); }

private:
    std::unordered_map<std::string, OrderBook> books_;
};
