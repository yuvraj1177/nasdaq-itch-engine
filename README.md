# NASDAQ ITCH 5.0 Market Data Engine

A production-style, high-performance C++20 market data engine for processing NASDAQ TotalView-ITCH 5.0 binary files.

## Features

- **Four benchmark modes**: Parse-only, parse+book (filtered), pipeline+book (dual-thread), full_book (all symbols)
- **L3 CSV export**: Normalized event stream (ADD/CANCEL/EXEC/TRADE) for microstructure simulation
- **Zero-copy parsing**: Memory-maps ITCH files for maximum throughput
- **Full message support**: Parses S, A, F, E, C, X, D, U, P message types
- **Order book reconstruction**: Maintains accurate limit order books per symbol
- **O(1) order lookup**: Custom Robin Hood hash table for order ID → order mapping
- **Performance metrics**: Per-message latency tracking with p50/p99/p99.9/max percentiles
- **Symbol filtering**: Track specific symbols for cache-friendly benchmarking
- **High throughput**: 4.5M msgs/sec full_book, 10.1M msgs/sec parse-only

## Project Structure

```
.
├── include/itch/              Public headers
│   ├── engine.h               Main processing engine
│   ├── engine_threaded.h      Dual-threaded SPSC pipeline
│   ├── itch_parser.h          ITCH 5.0 message decoding
│   ├── l3_csv_exporter.h      High-performance CSV writer
│   ├── latency_tracker.h      Percentile latency measurement
│   ├── mapped_file.h          RAII mmap wrapper
│   ├── order_book.h           Baseline order book (std::map)
│   ├── order_book_optimized.h Robin Hood hash + flat-array book
│   ├── spsc_queue.h           Lock-free SPSC queue (variant 1)
│   └── spsc_ring.h            Lock-free SPSC ring  (variant 2)
├── src/                       Implementation
│   ├── main.cpp               CLI entry point
│   ├── engine.cpp             Engine message dispatch loop
│   ├── itch_parser.cpp        Human-readable message printing
│   ├── l3_csv_exporter.cpp    Buffered CSV row writer
│   └── order_book.cpp         Order book stub
├── tools/
│   └── replay_csv.cpp         CSV replay & book validator
├── data/                      ITCH binary files (gitignored)
├── output/                    Generated CSVs, latency dumps (gitignored)
├── docs/
│   ├── IMPLEMENTATION.md      Detailed design notes
│   └── PERFORMANCE.md         Benchmarking methodology
├── CMakeLists.txt
├── Makefile
└── README.md
```

## Building

### Prerequisites
- C++20 compatible compiler (GCC 10+, Clang 12+)
- Make

### Build steps

```bash
make              # Build optimized release binary + replay tool
make clean        # Clean build artifacts
```

Compiled binaries are written to `build/bin/`:
- `build/bin/itch_engine` -- main engine
- `build/bin/replay_csv` -- CSV replay validator

## Benchmark Modes

The engine supports four benchmark modes to measure different performance characteristics:

### Mode 1: Parse Only
**Purpose**: Measure pure parsing throughput (no book updates)

```bash
./build/bin/itch_engine --mode parse --file data/01302020.NASDAQ_ITCH50
```

### Mode 2: Parse + Book (Single-Threaded, Filtered)
**Purpose**: Measure end-to-end latency including order book updates for selected symbols

```bash
./build/bin/itch_engine --mode parse_book --file data/01302020.NASDAQ_ITCH50 --symbols AAPL,MSFT,AMZN
```

### Mode 3: Pipeline + Book (Dual-Threaded, Filtered)
**Purpose**: Measure pipeline latency with reader/worker thread separation for selected symbols

```bash
./build/bin/itch_engine --mode pipeline_book --file data/01302020.NASDAQ_ITCH50 --symbol AAPL
```

### Mode 4: Full Book (Single-Threaded, ALL Symbols)
**Purpose**: Realistic stress test maintaining order books for ALL symbols (no filtering)

```bash
./build/bin/itch_engine --mode full_book --file data/01302020.NASDAQ_ITCH50
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
| **parse_book** | 8.5M/s | 49.9s | 0 ns | 83 ns | 2.8 µs | 1-10 | Parse + book (filtered) |
| **pipeline_book** | 9.8M/s | 43.4s | 14 µs | 228 µs | 8.8 ms | 1-10 | Pipeline (2 threads, SPSC) |
| **full_book** | **4.5M/s** | **93.8s** | **84 ns** | **750 ns** | **3.1 µs** | **8900** | **Full market stress test** |

### Full Book Mode - Production Validation

Processing 423,285,709 messages from a complete NASDAQ trading day:

```
Total messages:        423,285,709
Book operations:       417,219,234 (99% book hit rate)
  - adds:              186,610,705 (44%)
  - execs:               8,555,084 (2%)
  - deletes:           180,285,101 (43%)
  - replaces:           36,777,372 (9%)
  - cancels:             4,990,972 (1%)
Unknown order events:  0 ✅
Skipped symbols:       0 ✅
Symbols tracked:       8,900 ✅
Live orders at end:    0 ✅
```

**Key Achievements**:
- ✅ **4.5M msgs/sec** sustained throughput with full book maintenance
- ✅ **99% book hit rate** - Nearly all messages involve book operations
- ✅ **Zero unknown events** - Perfect order ID tracking across 186M adds
- ✅ **8,900 symbols** - Complete NASDAQ order book reconstruction
- ✅ **Clean book state** - Zero live orders at market close

## L3 CSV Export for Microstructure Simulation

The engine can export a normalized L3 event stream suitable for Python-based microstructure simulators.

### CSV Schema

```
timestamp_ns,event_type,order_id,price_ticks,size,side
```

**Columns**:
- `timestamp_ns`: Exchange timestamp (int64, nanoseconds since midnight)
- `event_type`: ADD | CANCEL | EXEC | TRADE
- `order_id`: uint64 (0 for TRADE events)
- `price_ticks`: int64 (integer price, no floats)
- `size`: int32 (shares)
- `side`: B (buy) or S (sell)

### Event Semantics

| ITCH Message | CSV Event | Behavior |
|--------------|-----------|----------|
| A, F (Add Order) | ADD | New order enters book |
| X (Cancel), D (Delete) | CANCEL | Partial/full order removal |
| E, C (Executed) | EXEC | Order execution (reduces size) |
| U (Replace) | CANCEL + ADD | Atomic replace (2 CSV rows) |
| P (Trade) | TRADE | Non-book print (order_id=0, informational) |

### Price Ticks (No Floats!)

ITCH prices are stored as `price × 10000` (e.g., $123.45 = 1234500).

Use `--tick-size-1e4 <divisor>` to convert to integer ticks:
```bash
# Default: tick_size = 0.0001, divisor = 1
price_ticks = price_1e4 / 1

# For 1-cent ticks: divisor = 100
price_ticks = price_1e4 / 100
```

### Usage

#### Generate CSV for full trading day
```bash
# Prerequisite: decompress ITCH file into data/
gunzip -k data/01302020.NASDAQ_ITCH50.gz

# Generate CSV (logs go to stderr, CSV to stdout)
./build/bin/itch_engine --file data/01302020.NASDAQ_ITCH50 \
  --mode full_book --csv --csv-header > output/full_book.csv 2> output/generation.log

# Check results
wc -l output/full_book.csv
# Expected: ~454M rows (99% book hit rate)
```

#### Validate CSV (Replay & Verify)
```bash
# Replay CSV and rebuild order book
./build/bin/replay_csv output/full_book.csv

# Output shows:
# - Total events processed
# - Book operations breakdown
# - Final best bid/ask
# - Live orders remaining
```

### CSV Generation Performance

Processing a full trading day (423M messages):
```
Wall time:         253 seconds
Throughput:        1.67M msgs/sec (with CSV writing)
CSV file size:     18GB
CSV rows exported: 454M events
```

**Why more CSV rows than ITCH messages?**
- Each Replace (U) message generates 2 CSV rows: CANCEL (old order) + ADD (new order)
- 36.8M replaces × 2 = 73.5M additional rows
- 423M ITCH messages → 454M CSV events ✅

**CSV Sample**:
```csv
timestamp_ns,event_type,order_id,price_ticks,size,side
14400000768178,ADD,8,198400,1500,B
14400000883067,ADD,40,198100,2200,B
14400000979447,ADD,80,198700,1500,S
14400001057567,ADD,112,199000,2200,S
```

### Step 0: Decompress ITCH file

```bash
gunzip -k data/01302020.NASDAQ_ITCH50.gz
```

### Step 1: Validate parsing (print first 100 messages)

```bash
./build/bin/itch_engine --mode parse --file data/01302020.NASDAQ_ITCH50 --print-first 100
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
./build/bin/itch_engine --mode parse_book --file data/01302020.NASDAQ_ITCH50 \
  --symbols AAPL,MSFT,AMZN,GOOGL,TSLA --latency-out output/latencies.csv
```

### Step 3: Test pipeline mode with single symbol

```bash
./build/bin/itch_engine --mode pipeline_book --file data/01302020.NASDAQ_ITCH50 \
  --symbol AAPL --latency-out output/pipeline_lat.csv
```

### Step 4: Run full book stress test

```bash
./build/bin/itch_engine --mode full_book --file data/01302020.NASDAQ_ITCH50 \
  --latency-out output/full_book_lat.csv
```

### Debug Symbol Parsing

To verify symbol extraction is correct, use `--debug-symbols` to print the first 20 encountered symbols:

```bash
./build/bin/itch_engine --mode parse_book --file data/01302020.NASDAQ_ITCH50 \
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
  --csv                   Export L3 event stream to stdout (logs go to stderr)
  --csv-header            Include CSV header row
  --tick-size-1e4 <int>   Tick size divisor for price_ticks (default: 1 = $0.0001)
  --print-first <N>       Print first N messages in human-readable form
  --latency-out <file>    Dump latencies to file (one per line, ns)
  --show-book-updates     Show book state periodically
  --debug-symbols         Print first 20 symbols encountered (with keys + filter status)
  --help                  Show this help message
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
- [x] **L3 CSV export**: Normalized event stream for microstructure simulation (ADD/CANCEL/EXEC/TRADE)
- [x] **Replay validator**: Tool to rebuild book from CSV and verify correctness

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

## Disclaimer

This project is provided for **educational and research purposes only**.

- It is **not affiliated with, endorsed by, or connected to NASDAQ**
- It is **not a production trading system**
- It must **not be used for live trading, regulatory, or compliance purposes**
- All processing is performed on **publicly available historical market data**

## License

Licensed under either of:

- Apache License, Version 2.0
- MIT License

at your option.
