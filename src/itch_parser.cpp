#include "itch_parser.h"
#include <iostream>
#include <iomanip>

namespace itch {

// Helper to format timestamp (nanoseconds since midnight)
std::string format_timestamp(uint64_t ts_ns) {
    uint64_t hours = ts_ns / 3600000000000ULL;
    uint64_t remainder = ts_ns % 3600000000000ULL;
    uint64_t minutes = remainder / 60000000000ULL;
    remainder = remainder % 60000000000ULL;
    uint64_t seconds = remainder / 1000000000ULL;
    uint64_t nanos = remainder % 1000000000ULL;
    
    char buf[32];
    snprintf(buf, sizeof(buf), "%02llu:%02llu:%02llu.%09llu", hours, minutes, seconds, nanos);
    return std::string(buf);
}

// Helper to format price (stored as integer, divide by 10000)
std::string format_price(uint32_t price_int) {
    double price = price_int / 10000.0;
    char buf[32];
    snprintf(buf, sizeof(buf), "%.4f", price);
    return std::string(buf);
}

void print_system_event(const SystemEvent& msg, uint64_t seq) {
    std::cout << std::setw(8) << seq << " | S | " 
              << format_timestamp(msg.timestamp) << " | "
              << "Event=" << msg.event_code << std::endl;
}

void print_add_order(const AddOrder& msg, uint64_t seq) {
    std::cout << std::setw(8) << seq << " | A | "
              << format_timestamp(msg.timestamp) << " | "
              << std::setw(8) << msg.stock << " | "
              << msg.buy_sell << " | "
              << "OID=" << msg.order_ref_number << " | "
              << "Qty=" << std::setw(8) << msg.shares << " | "
              << "Px=" << format_price(msg.price) << std::endl;
}

void print_add_order_mpid(const AddOrderMPID& msg, uint64_t seq) {
    std::cout << std::setw(8) << seq << " | F | "
              << format_timestamp(msg.timestamp) << " | "
              << std::setw(8) << msg.stock << " | "
              << msg.buy_sell << " | "
              << "OID=" << msg.order_ref_number << " | "
              << "Qty=" << std::setw(8) << msg.shares << " | "
              << "Px=" << format_price(msg.price) << " | "
              << "MPID=" << msg.attribution << std::endl;
}

void print_order_executed(const OrderExecuted& msg, uint64_t seq) {
    std::cout << std::setw(8) << seq << " | E | "
              << format_timestamp(msg.timestamp) << " | "
              << "OID=" << msg.order_ref_number << " | "
              << "ExecQty=" << msg.executed_shares << " | "
              << "Match=" << msg.match_number << std::endl;
}

void print_order_executed_with_price(const OrderExecutedWithPrice& msg, uint64_t seq) {
    std::cout << std::setw(8) << seq << " | C | "
              << format_timestamp(msg.timestamp) << " | "
              << "OID=" << msg.order_ref_number << " | "
              << "ExecQty=" << msg.executed_shares << " | "
              << "ExecPx=" << format_price(msg.execution_price) << " | "
              << "Match=" << msg.match_number << std::endl;
}

void print_order_cancel(const OrderCancel& msg, uint64_t seq) {
    std::cout << std::setw(8) << seq << " | X | "
              << format_timestamp(msg.timestamp) << " | "
              << "OID=" << msg.order_ref_number << " | "
              << "CxlQty=" << msg.cancelled_shares << std::endl;
}

void print_order_delete(const OrderDelete& msg, uint64_t seq) {
    std::cout << std::setw(8) << seq << " | D | "
              << format_timestamp(msg.timestamp) << " | "
              << "OID=" << msg.order_ref_number << std::endl;
}

void print_order_replace(const OrderReplace& msg, uint64_t seq) {
    std::cout << std::setw(8) << seq << " | U | "
              << format_timestamp(msg.timestamp) << " | "
              << "OldOID=" << msg.original_order_ref_number << " | "
              << "NewOID=" << msg.new_order_ref_number << " | "
              << "Qty=" << msg.shares << " | "
              << "Px=" << format_price(msg.price) << std::endl;
}

void print_trade(const Trade& msg, uint64_t seq) {
    std::cout << std::setw(8) << seq << " | P | "
              << format_timestamp(msg.timestamp) << " | "
              << std::setw(8) << msg.stock << " | "
              << msg.buy_sell << " | "
              << "OID=" << msg.order_ref_number << " | "
              << "Qty=" << msg.shares << " | "
              << "Px=" << format_price(msg.price) << " | "
              << "Match=" << msg.match_number << std::endl;
}

} // namespace itch
