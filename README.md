# NASDAQ ITCH 5.0 Market Data Engine

A production-style, high-performance C++20 market data engine for processing NASDAQ TotalView-ITCH 5.0 binary files.

## Features

- **Three benchmark modes**: Parse-only, parse+book (single-thread), pipeline+book (dual-thread SPSC)
- **Zero-copy parsing**: Memory-maps ITCH files for maximum throughput
- **Full message support**: Parses S, A, F, E, C, X, D, U, P message types
- **Order book reconstruction**: Maintains accurate limit order books per symbol
- **O(1) order lookup**: Custom Robin Hood hash table for order ID → order mapping
- **Performance metrics**: Per-message latency tracking with p50/p99/p99.9/max percentiles
- **Symbol filtering**: Track specific symbols for cache-friendly benchmarking
- **High throughput**: Processes 423M+ messages in ~40-50 seconds

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

## Benchmark Modes

The engine supports four benchmark modes to measure different performance characteristics:

### Mode 1: Parse Only
**Purpose**: Measure pure parsing throughput (no book updates)

```bash
./itch_engine --mode parse --file 01302020.NASDAQ_ITCH50
```

### Mode 2: Parse + Book (Single-Threaded, Filtered)
**Purpose**: Measure end-to-end latency including order book updates for selected symbols

```bash
./itch_engine --mode parse_book --file 01302020.NASDAQ_ITCH50 --symbols AAPL,MSFT,AMZN
```

### Mode 3: Pipeline + Book (Dual-Threaded, Filtered)
**Purpose**: Measure pipeline latency with reader/worker thread separation for selected symbols

```bash
./itch_engine --mode pipeline_book --file 01302020.NASDAQ_ITCH50 --symbol AAPL
```

### Mode 4: Full Book (Single-Threaded, ALL Symbols)
**Purpose**: Realistic stress test maintaining order books for ALL symbols (no filtering)

```bash
./itch_engine --mode full_book --file 01302020.NASDAQ_ITCH50
```

**Expected behavior:**
- `full_book` processes all ~8,000+ NASDAQ symbols
- `skipped_symbols` should be ~0 in full_book mode
- Book operations (adds/execs/deletes/cancels/replaces) should total 100M+ on full-day file
- Throughput will be significantly lower than parse-only mode due to book maintenance overhead

## Benchmark Results

Tested on 423M messages from 01/30/2020 NASDAQ ITCH file (~12GB decompressed):

| Mode | Throughput | Wall Time | p50 | p99 | p99.9 | Symbols | Description |
|------|-----------|-----------|-----|-----|-------|---------|-------------|
| **parse** | 10.1M/s | 41.9s | 0 ns | 42 ns | 958 ns | N/A | Parse + decode only |
| **parse_book** | 9.4M/s | 45.2s | 0 ns | 83 ns | 2.8 µs | 1 | Parse + book (filtered) |
| **pipeline_book** | 9.8M/s | 43.4s | 14 µs | 228 µs | 8.8 ms | 1 | Pipeline (2 threads, SPSC) |
| **full_book** | **4.5M/s** | **93.8s** | **84 ns** | **750 ns** | **3.1 µs** | **8900** | **Realistic stress test** |

### Full Book Mode Results (Validation Metrics)

```
Total messages:        423,285,709
Book operations:
  - adds:              186,610,705 (44%)
  - execs:               8,555,084 (2%)
  - deletes:           180,285,101 (43%)
  - replaces:           36,777,372 (9%)
  - cancels:             4,990,972 (1%)
Unknown order events:  0 ✅
Skipped symbols:       0 ✅
Book hit fraction:     0.99 (99% of messages touched the book)
Symbols tracked:       8,900 ✅
Live orders at end:    0 ✅
```

**Key Observations**:
- ✅ **99% book hit rate** - Nearly all messages involve book operations
- ✅ **Zero unknown events** - All order IDs successfully tracked
- ✅ **Zero skipped symbols** - Full market coverage
- ✅ **8,900 symbols** - Complete NASDAQ order book reconstruction
- ⚡ **4.5M msgs/sec** - Realistic throughput with full book maintenance (55% slower than parse-only)
- 📊 **Clean book state** - 0 live orders at end indicates proper order lifecycle handling

## Usage Examples

### Step 0: Decompress ITCH file

```bash
gunzip -k 01302020.NASDAQ_ITCH50.gz
```

### Step 1: Validate parsing (print first 100 messages)

```bash
./itch_engine --mode parse --file 01302020.NASDAQ_ITCH50 --print-first 100
```

Sample output:
```
       0 | S | 04:00:00.000000000 | Event=O
       1 | A | 04:00:00.123456789 | AAPL     | B | OID=123456 | Qty=    100 | Px=150.5000
       2 | E | 04:00:01.234567890 | OID=123456 | ExecQty=50 | Match=7890
...
```

### Step 2: Benchmark with multiple symbols

```bash
./itch_engine --mode parse_book --file 01302020.NASDAQ_ITCH50 \
  --symbols AAPL,MSFT,AMZN,GOOGL,TSLA --latency-out latencies.csv
```

### Step 3: Test pipeline mode with single symbol

```bash
./itch_engine --mode pipeline_book --file 01302020.NASDAQ_ITCH50 \
  --symbol AAPL --latency-out pipeline_lat.csv
```

### Step 4: Run full book stress test

```bash
./itch_engine --mode full_book --file 01302020.NASDAQ_ITCH50 \
  --latency-out full_book_lat.csv
```

### Debug Symbol Parsing

To verify symbol extraction is correct, use `--debug-symbols` to print the first 20 encountered symbols:

```bash
./itch_engine --mode parse_book --file 01302020.NASDAQ_ITCH50 \
  --symbol AAPL --debug-symbols
```

This will output:
```
DEBUG Symbol #1: "VOD" (key=0x2020202020444f56) filter_match=NO
DEBUG Symbol #2: "ASML" (key=0x202020204c4d5341) filter_match=NO
...
DEBUG Symbol #20: "AAPL" (key=0x202020204c505041) filter_match=YES
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

## CLI Options

```
Options:
  --file <path>           Path to decompressed ITCH 5.0 file (required)
  --mode <mode>           Benchmark mode: parse|parse_book|pipeline_book|full_book (default: parse)
  --symbol <SYM>          Track single symbol (e.g., AAPL) [parse_book/pipeline_book only]
  --symbols <SYM,SYM,...> Track multiple symbols (e.g., AAPL,MSFT,AMZN) [parse_book/pipeline_book only]
  --print-first <N>       Print first N messages in human-readable form
  --latency-out <file>    Dump latencies to file (one per line, ns)
  --show-book-updates     Show book state periodically
  --debug-symbols         Print first 20 symbols encountered (with keys + filter status)
  --help                  Show this help message

Deprecated (for backward compatibility):
  --enable-book           Maps to --mode=parse_book
  --use-spsc              Maps to --mode=pipeline_book
```

## Development Roadmap

- [x] **Step 1**: Minimal ITCH parser with message printing
- [x] **Step 2**: Order book + order table with single-symbol verification
- [x] **Step 3**: Latency tracking and throughput measurement
- [x] **Step 4**: Cache-friendly data structure optimizations (Robin Hood hash, flat arrays)
- [x] **Step 5**: SPSC ring buffer for multi-threaded processing
- [x] **Benchmark modes**: Parse, parse+book, pipeline+book, full_book
- [x] **Symbol correctness**: Fixed SymbolKey extraction offset, added --debug-symbols
- [x] **Book hit metrics**: Counters for adds/execs/deletes/cancels/replaces, book-hit fraction
- [x] **Zero-allocation hot path**: Order stores SymbolKey instead of std::string

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
