# NASDAQ ITCH 5.0 Market Data Engine

A production-style, high-performance C++20 market data engine for processing NASDAQ TotalView-ITCH 5.0 binary files.

## Features

- **Zero-copy parsing**: Memory-maps ITCH files for maximum throughput
- **Full message support**: Parses S, A, F, E, C, X, D, U, P message types
- **Order book reconstruction**: Maintains accurate limit order books per symbol
- **O(1) order lookup**: Custom open-addressing hash table for order ID → order mapping
- **Performance metrics**: Per-message latency tracking with p50/p99/p99.9/max percentiles
- **High throughput**: Processes multi-GB files efficiently

## Architecture

```
src/
├── main.cpp              - CLI argument parsing and entry point
├── engine.{h,cpp}        - Main processing engine and message dispatcher
├── itch_parser.{h,cpp}   - ITCH 5.0 message parsing and decoding
├── order_book.{h,cpp}    - Order book and order table implementation
├── latency_tracker.h     - Performance measurement and statistics
└── mapped_file.h         - RAII wrapper for mmap
```

## Building

### Prerequisites
- C++20 compatible compiler (GCC 10+, Clang 12+)
- CMake 3.20+

### Build steps

```bash
mkdir build && cd build
cmake -DCMAKE_BUILD_TYPE=Release ..
make -j$(nproc)
```

The compiled binary will be `build/itch_engine`.

## Usage

### Step 0: Decompress ITCH file

```bash
gunzip -k 01302020.NASDAQ_ITCH50.gz
```

This produces `01302020.NASDAQ_ITCH50` which the engine will mmap.

### Step 1: Validate parsing (print first 100 messages)

```bash
./build/itch_engine --file 01302020.NASDAQ_ITCH50 --print-first 100
```

Sample output:
```
       0 | S | 04:00:00.000000000 | Event=O
       1 | A | 04:00:00.123456789 | AAPL     | B | OID=123456 | Qty=    100 | Px=150.5000
       2 | E | 04:00:01.234567890 | OID=123456 | ExecQty=50 | Match=7890
...
```

### Step 2: Full processing with order book

```bash
./build/itch_engine \
  --file 01302020.NASDAQ_ITCH50 \
  --enable-book \
  --enable-timing \
  --latency-out latencies_ns.csv
```

### Step 3: Single-symbol verification

```bash
./build/itch_engine \
  --file 01302020.NASDAQ_ITCH50 \
  --enable-book \
  --symbol AAPL \
  --show-book-updates
```

## Performance Metrics

The engine reports:
- **Total messages processed**
- **Wall-clock time**
- **Throughput** (messages/sec)
- **Latency percentiles**: min, p50, p99, p99.9, max (in nanoseconds)

Sample output:
```
========== PERFORMANCE STATISTICS ==========
Total messages:    45678912
Wall time:         12345 ms
Throughput:        3700000.00 msgs/sec

Latency (nanoseconds):
  Min:   45 ns
  p50:   120 ns
  p99:   450 ns
  p99.9: 1200 ns
  Max:   15000 ns
============================================
```

## Message Types Supported

| Type | Name | Description |
|------|------|-------------|
| S | System Event | Session state changes |
| A | Add Order | New order (no MPID) |
| F | Add Order (MPID) | New order with market participant ID |
| E | Order Executed | Partial/full execution |
| C | Order Executed (Price) | Execution with explicit price |
| X | Order Cancel | Partial cancel |
| D | Order Delete | Full cancel/delete |
| U | Order Replace | Modify order (cancel + add) |
| P | Trade | Non-cross trade message |

## Data Structures

### OrderTable (Custom Hash Table)
- Open-addressing with linear probing
- Power-of-two capacity (16M entries)
- O(1) insert/lookup/delete for order_id → Order

### OrderBook
- Per-symbol limit order book
- Bid/ask sides using `std::map` for correctness (optimized in Step 4)
- Tracks total quantity at each price level

## Development Roadmap

- [x] **Step 1**: Minimal ITCH parser with message printing
- [x] **Step 2**: Order book + order table with single-symbol verification
- [x] **Step 3**: Latency tracking and throughput measurement
- [ ] **Step 4**: Cache-friendly data structure optimizations
- [ ] **Step 5**: Optional SPSC ring buffer for multi-threaded processing

## File Format

The engine expects a **decompressed** ITCH 5.0 file with **2-byte length framing**:

```
[2-byte BE length] [1-byte type] [N-byte payload]
[2-byte BE length] [1-byte type] [N-byte payload]
...
```

## Notes

- Prices are stored as 4-byte integers; divide by 10,000 for actual price
- Timestamps are 6-byte nanoseconds since midnight
- Stock symbols are 8-byte right-padded with spaces
- The engine uses `MADV_SEQUENTIAL` for optimal mmap performance

## License

This is a demonstration project for low-latency trading system development.
