#include "engine.h"
#include <iostream>
#include <string>
#include <cstring>

void print_usage(const char* prog) {
    std::cout << "Usage: " << prog << " [options]\n";
    std::cout << "Options:\n";
    std::cout << "  --file <path>           Path to decompressed ITCH 5.0 file (required)\n";
    std::cout << "  --mode <mode>           Benchmark mode: parse|parse_book|pipeline_book|full_book (default: parse)\n";
    std::cout << "  --symbol <SYM>          Track single symbol (e.g., AAPL)\n";
    std::cout << "  --symbols <SYM,SYM,...> Track multiple symbols (e.g., AAPL,MSFT,AMZN)\n";
    std::cout << "  --print-first <N>       Print first N messages in human-readable form\n";
    std::cout << "  --latency-out <file>    Dump latencies to file (one per line, ns)\n";
    std::cout << "  --show-book-updates     Show book state periodically\n";
    std::cout << "  --debug-symbols         Print first 20 symbols encountered (with keys + filter status)\n";
    std::cout << "  --help                  Show this help message\n";
    std::cout << "\nDeprecated (for backward compatibility):\n";
    std::cout << "  --enable-book           Maps to --mode=parse_book\n";
    std::cout << "  --enable-timing         Enabled automatically in all modes\n";
    std::cout << "  --use-spsc              Maps to --mode=pipeline_book\n";
    std::cout << "\nExamples:\n";
    std::cout << "  " << prog << " --file data.bin --mode=parse\n";
    std::cout << "  " << prog << " --file data.bin --mode=parse_book --symbol AAPL\n";
    std::cout << "  " << prog << " --file data.bin --mode=pipeline_book --symbols AAPL,MSFT,AMZN --latency-out lat.csv\n";
}

int main(int argc, char** argv) {
    Engine::Config config;
    
    // Parse command-line arguments
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        
        if (arg == "--help") {
            print_usage(argv[0]);
            return 0;
        } else if (arg == "--file" && i + 1 < argc) {
            config.input_file = argv[++i];
        } else if (arg == "--mode" && i + 1 < argc) {
            std::string mode_str = argv[++i];
            if (mode_str == "parse") {
                config.mode = BenchmarkMode::PARSE;
            } else if (mode_str == "parse_book") {
                config.mode = BenchmarkMode::PARSE_BOOK;
            } else if (mode_str == "pipeline_book") {
                config.mode = BenchmarkMode::PIPELINE_BOOK;
            } else if (mode_str == "full_book") {
                config.mode = BenchmarkMode::FULL_BOOK;
            } else {
                std::cerr << "Invalid mode: " << mode_str << "\n";
                print_usage(argv[0]);
                return 1;
            }
        } else if (arg == "--symbol" && i + 1 < argc) {
            std::string sym = argv[++i];
            config.symbols.add(parse_symbol(sym));
            config.symbol_filter = sym; // For backward compat
        } else if (arg == "--symbols" && i + 1 < argc) {
            std::string syms = argv[++i];
            size_t pos = 0;
            while (pos < syms.size()) {
                size_t comma = syms.find(',', pos);
                if (comma == std::string::npos) {
                    config.symbols.add(parse_symbol(syms.substr(pos)));
                    break;
                }
                config.symbols.add(parse_symbol(syms.substr(pos, comma - pos)));
                pos = comma + 1;
            }
        } else if (arg == "--print-first" && i + 1 < argc) {
            config.print_first = std::stoull(argv[++i]);
        } else if (arg == "--latency-out" && i + 1 < argc) {
            config.latency_output = argv[++i];
        } else if (arg == "--show-book-updates") {
            config.show_book_updates = true;
        } else if (arg == "--debug-symbols") {
            config.debug_symbols = true;
        } else if (arg == "--enable-book") {
            // Deprecated: map to parse_book mode
            config.mode = BenchmarkMode::PARSE_BOOK;
            config.enable_book = true;
        } else if (arg == "--enable-timing") {
            // Deprecated: now always enabled
            config.enable_timing = true;
        } else if (arg == "--use-spsc") {
            // Deprecated: map to pipeline_book mode
            config.mode = BenchmarkMode::PIPELINE_BOOK;
            config.use_spsc_mode = true;
        } else {
            std::cerr << "Unknown option: " << arg << "\n";
            print_usage(argv[0]);
            return 1;
        }
    }
    
    // Validate required arguments
    if (config.input_file.empty()) {
        std::cerr << "Error: --file is required\n";
        print_usage(argv[0]);
        return 1;
    }
    
    try {
        Engine engine(config);
        engine.run();
    } catch (const std::exception& e) {
        std::cerr << "Fatal error: " << e.what() << "\n";
        return 1;
    }
    
    return 0;
}
