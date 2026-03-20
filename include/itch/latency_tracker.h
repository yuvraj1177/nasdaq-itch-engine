#pragma once

#include <vector>
#include <chrono>
#include <algorithm>
#include <fstream>
#include <iostream>
#include <iomanip>
#include <cstdint>

class LatencyTracker {
public:
    LatencyTracker() : count_(0), min_ns_(UINT64_MAX), max_ns_(0) {
        latencies_ns_.reserve(50'000'000); // Reserve for ~50M messages
    }

    void record(uint64_t latency_ns) {
        latencies_ns_.push_back(latency_ns);
        count_++;
        if (latency_ns < min_ns_) min_ns_ = latency_ns;
        if (latency_ns > max_ns_) max_ns_ = latency_ns;
    }

    void start_timing() {
        wall_start_ = std::chrono::steady_clock::now();
    }

    void stop_timing() {
        wall_end_ = std::chrono::steady_clock::now();
    }

    void compute_and_print_stats() {
        if (latencies_ns_.empty()) {
            std::cout << "No latency data recorded." << std::endl;
            return;
        }

        auto wall_duration = std::chrono::duration_cast<std::chrono::milliseconds>(
            wall_end_ - wall_start_
        ).count();

        double throughput = (count_ * 1000.0) / wall_duration; // msgs/sec

        // Sort for percentile computation
        std::vector<uint64_t> sorted = latencies_ns_;
        std::sort(sorted.begin(), sorted.end());

        size_t p50_idx = sorted.size() / 2;
        size_t p99_idx = (sorted.size() * 99) / 100;
        size_t p999_idx = (sorted.size() * 999) / 1000;

        uint64_t p50 = sorted[p50_idx];
        uint64_t p99 = sorted[p99_idx];
        uint64_t p999 = sorted[p999_idx];

        std::cout << "\n========== PERFORMANCE STATISTICS ==========\n";
        std::cout << "Total messages:    " << count_ << "\n";
        std::cout << "Wall time:         " << wall_duration << " ms\n";
        std::cout << "Throughput:        " << std::fixed << std::setprecision(2) 
                  << throughput << " msgs/sec\n";
        std::cout << "\nLatency (nanoseconds):\n";
        std::cout << "  Min:   " << min_ns_ << " ns\n";
        std::cout << "  p50:   " << p50 << " ns\n";
        std::cout << "  p99:   " << p99 << " ns\n";
        std::cout << "  p99.9: " << p999 << " ns\n";
        std::cout << "  Max:   " << max_ns_ << " ns\n";
        std::cout << "============================================\n";
    }

    void dump_to_file(const std::string& filename) {
        std::ofstream ofs(filename);
        if (!ofs) {
            std::cerr << "Failed to open latency output file: " << filename << std::endl;
            return;
        }

        for (uint64_t lat : latencies_ns_) {
            ofs << lat << "\n";
        }

        std::cout << "Latency data dumped to: " << filename << std::endl;
    }

    uint64_t count() const { return count_; }

private:
    std::vector<uint64_t> latencies_ns_;
    uint64_t count_;
    uint64_t min_ns_;
    uint64_t max_ns_;
    std::chrono::steady_clock::time_point wall_start_;
    std::chrono::steady_clock::time_point wall_end_;
};
