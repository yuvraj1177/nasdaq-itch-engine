#include "engine.h"
#include <iostream>
#include <string>
#include <cstring>

void print_usage(const char* prog) {
    std::cout << "Usage: " << prog << " [options]\n";
    std::cout << "Options:\n";
    std::cout << "  --file <path>           Path to decompressed ITCH 5.0 file (required)\n";
    std::cout << "  --print-first <N>       Print first N messages in human-readable form\n";
    std::cout << "  --enable-book           Enable order book reconstruction\n";
    std::cout << "  --enable-timing         Enable per-message latency tracking\n";
    std::cout << "  --symbol <SYM>          Filter book updates to single symbol\n";
    std::cout << "  --latency-out <file>    Dump latencies to file (one per line, ns)\n";
    std::cout << "  --show-book-updates     Show book state periodically\n";
    std::cout << "  --use-spsc              Enable multi-threaded mode (reader + processor threads)\n";
    std::cout << "  --help                  Show this help message\n";
    std::cout << "\nExample:\n";
    std::cout << "  " << prog << " --file data.bin --print-first 100\n";
    std::cout << "  " << prog << " --file data.bin --enable-book --enable-timing --symbol AAPL --latency-out latencies.csv\n";
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
        } else if (arg == "--print-first" && i + 1 < argc) {
            config.print_first = std::stoull(argv[++i]);
        } else if (arg == "--enable-book") {
            config.enable_book = true;
        } else if (arg == "--enable-timing") {
            config.enable_timing = true;
        } else if (arg == "--symbol" && i + 1 < argc) {
            config.symbol_filter = argv[++i];
        } else if (arg == "--latency-out" && i + 1 < argc) {
            config.latency_output = argv[++i];
        } else if (arg == "--show-book-updates") {
            config.show_book_updates = true;
        } else if (arg == "--use-spsc") {
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
