#pragma once

#include <cstdint>
#include <cstring>
#include <string>
#include <string_view>
#include <array>

// Big-endian conversion helpers
inline uint16_t be16_to_host(const uint8_t* p) {
    return (static_cast<uint16_t>(p[0]) << 8) | p[1];
}

inline uint32_t be32_to_host(const uint8_t* p) {
    return (static_cast<uint32_t>(p[0]) << 24) |
           (static_cast<uint32_t>(p[1]) << 16) |
           (static_cast<uint32_t>(p[2]) << 8) |
           p[3];
}

inline uint64_t be64_to_host(const uint8_t* p) {
    return (static_cast<uint64_t>(p[0]) << 56) |
           (static_cast<uint64_t>(p[1]) << 48) |
           (static_cast<uint64_t>(p[2]) << 40) |
           (static_cast<uint64_t>(p[3]) << 32) |
           (static_cast<uint64_t>(p[4]) << 24) |
           (static_cast<uint64_t>(p[5]) << 16) |
           (static_cast<uint64_t>(p[6]) << 8) |
           p[7];
}

// Read 6-byte timestamp (nanoseconds since midnight)
inline uint64_t read_timestamp_6(const uint8_t* p) {
    return (static_cast<uint64_t>(p[0]) << 40) |
           (static_cast<uint64_t>(p[1]) << 32) |
           (static_cast<uint64_t>(p[2]) << 24) |
           (static_cast<uint64_t>(p[3]) << 16) |
           (static_cast<uint64_t>(p[4]) << 8) |
           p[5];
}

// Read 4-byte price (in decimal form, divide by 10000 for actual price)
inline uint32_t read_price_4(const uint8_t* p) {
    return be32_to_host(p);
}

// Read 8-byte stock symbol (right-padded with spaces)
inline std::string read_stock_8(const uint8_t* p) {
    std::string s(reinterpret_cast<const char*>(p), 8);
    // Trim trailing spaces
    size_t end = s.find_last_not_of(' ');
    if (end != std::string::npos) {
        s.erase(end + 1);
    }
    return s;
}

// ITCH 5.0 Message Types
namespace itch {

// System Event (S) - Length 12
struct SystemEvent {
    static constexpr char TYPE = 'S';
    static constexpr size_t SIZE = 11; // excluding type byte
    
    uint16_t stock_locate;
    uint16_t tracking_number;
    uint64_t timestamp;
    char event_code; // O=start, S=start system hours, Q=start market hours, M=end market hours, E=end system hours, C=close
    
    void parse(const uint8_t* data) {
        stock_locate = be16_to_host(data);
        tracking_number = be16_to_host(data + 2);
        timestamp = read_timestamp_6(data + 4);
        event_code = static_cast<char>(data[10]);
    }
};

// Add Order (A) - Length 36
struct AddOrder {
    static constexpr char TYPE = 'A';
    static constexpr size_t SIZE = 35;
    
    uint16_t stock_locate;
    uint16_t tracking_number;
    uint64_t timestamp;
    uint64_t order_ref_number;
    char buy_sell; // B or S
    uint32_t shares;
    std::string stock; // 8 bytes
    uint32_t price;
    
    void parse(const uint8_t* data) {
        stock_locate = be16_to_host(data);
        tracking_number = be16_to_host(data + 2);
        timestamp = read_timestamp_6(data + 4);
        order_ref_number = be64_to_host(data + 10);
        buy_sell = static_cast<char>(data[18]);
        shares = be32_to_host(data + 19);
        stock = read_stock_8(data + 23);
        price = read_price_4(data + 31);
    }
};

// Add Order with MPID (F) - Length 40
struct AddOrderMPID {
    static constexpr char TYPE = 'F';
    static constexpr size_t SIZE = 39;
    
    uint16_t stock_locate;
    uint16_t tracking_number;
    uint64_t timestamp;
    uint64_t order_ref_number;
    char buy_sell;
    uint32_t shares;
    std::string stock;
    uint32_t price;
    std::string attribution; // 4 bytes
    
    void parse(const uint8_t* data) {
        stock_locate = be16_to_host(data);
        tracking_number = be16_to_host(data + 2);
        timestamp = read_timestamp_6(data + 4);
        order_ref_number = be64_to_host(data + 10);
        buy_sell = static_cast<char>(data[18]);
        shares = be32_to_host(data + 19);
        stock = read_stock_8(data + 23);
        price = read_price_4(data + 31);
        attribution = std::string(reinterpret_cast<const char*>(data + 35), 4);
    }
};

// Order Executed (E) - Length 31
struct OrderExecuted {
    static constexpr char TYPE = 'E';
    static constexpr size_t SIZE = 30;
    
    uint16_t stock_locate;
    uint16_t tracking_number;
    uint64_t timestamp;
    uint64_t order_ref_number;
    uint32_t executed_shares;
    uint64_t match_number;
    
    void parse(const uint8_t* data) {
        stock_locate = be16_to_host(data);
        tracking_number = be16_to_host(data + 2);
        timestamp = read_timestamp_6(data + 4);
        order_ref_number = be64_to_host(data + 10);
        executed_shares = be32_to_host(data + 18);
        match_number = be64_to_host(data + 22);
    }
};

// Order Executed With Price (C) - Length 36
struct OrderExecutedWithPrice {
    static constexpr char TYPE = 'C';
    static constexpr size_t SIZE = 35;
    
    uint16_t stock_locate;
    uint16_t tracking_number;
    uint64_t timestamp;
    uint64_t order_ref_number;
    uint32_t executed_shares;
    uint64_t match_number;
    char printable; // Y or N
    uint32_t execution_price;
    
    void parse(const uint8_t* data) {
        stock_locate = be16_to_host(data);
        tracking_number = be16_to_host(data + 2);
        timestamp = read_timestamp_6(data + 4);
        order_ref_number = be64_to_host(data + 10);
        executed_shares = be32_to_host(data + 18);
        match_number = be64_to_host(data + 22);
        printable = static_cast<char>(data[30]);
        execution_price = read_price_4(data + 31);
    }
};

// Order Cancel (X) - Length 23
struct OrderCancel {
    static constexpr char TYPE = 'X';
    static constexpr size_t SIZE = 22;
    
    uint16_t stock_locate;
    uint16_t tracking_number;
    uint64_t timestamp;
    uint64_t order_ref_number;
    uint32_t cancelled_shares;
    
    void parse(const uint8_t* data) {
        stock_locate = be16_to_host(data);
        tracking_number = be16_to_host(data + 2);
        timestamp = read_timestamp_6(data + 4);
        order_ref_number = be64_to_host(data + 10);
        cancelled_shares = be32_to_host(data + 18);
    }
};

// Order Delete (D) - Length 19
struct OrderDelete {
    static constexpr char TYPE = 'D';
    static constexpr size_t SIZE = 18;
    
    uint16_t stock_locate;
    uint16_t tracking_number;
    uint64_t timestamp;
    uint64_t order_ref_number;
    
    void parse(const uint8_t* data) {
        stock_locate = be16_to_host(data);
        tracking_number = be16_to_host(data + 2);
        timestamp = read_timestamp_6(data + 4);
        order_ref_number = be64_to_host(data + 10);
    }
};

// Order Replace (U) - Length 35
struct OrderReplace {
    static constexpr char TYPE = 'U';
    static constexpr size_t SIZE = 34;
    
    uint16_t stock_locate;
    uint16_t tracking_number;
    uint64_t timestamp;
    uint64_t original_order_ref_number;
    uint64_t new_order_ref_number;
    uint32_t shares;
    uint32_t price;
    
    void parse(const uint8_t* data) {
        stock_locate = be16_to_host(data);
        tracking_number = be16_to_host(data + 2);
        timestamp = read_timestamp_6(data + 4);
        original_order_ref_number = be64_to_host(data + 10);
        new_order_ref_number = be64_to_host(data + 18);
        shares = be32_to_host(data + 26);
        price = read_price_4(data + 30);
    }
};

// Trade (non-cross) (P) - Length 44
struct Trade {
    static constexpr char TYPE = 'P';
    static constexpr size_t SIZE = 43;
    
    uint16_t stock_locate;
    uint16_t tracking_number;
    uint64_t timestamp;
    uint64_t order_ref_number;
    char buy_sell;
    uint32_t shares;
    std::string stock;
    uint32_t price;
    uint64_t match_number;
    
    void parse(const uint8_t* data) {
        stock_locate = be16_to_host(data);
        tracking_number = be16_to_host(data + 2);
        timestamp = read_timestamp_6(data + 4);
        order_ref_number = be64_to_host(data + 10);
        buy_sell = static_cast<char>(data[18]);
        shares = be32_to_host(data + 19);
        stock = read_stock_8(data + 23);
        price = read_price_4(data + 31);
        match_number = be64_to_host(data + 35);
    }
};

// Print functions for human-readable output
void print_system_event(const SystemEvent& msg, uint64_t seq);
void print_add_order(const AddOrder& msg, uint64_t seq);
void print_add_order_mpid(const AddOrderMPID& msg, uint64_t seq);
void print_order_executed(const OrderExecuted& msg, uint64_t seq);
void print_order_executed_with_price(const OrderExecutedWithPrice& msg, uint64_t seq);
void print_order_cancel(const OrderCancel& msg, uint64_t seq);
void print_order_delete(const OrderDelete& msg, uint64_t seq);
void print_order_replace(const OrderReplace& msg, uint64_t seq);
void print_trade(const Trade& msg, uint64_t seq);

} // namespace itch
