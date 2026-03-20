#pragma once

#include <atomic>
#include <cstdint>
#include <vector>
#include <array>

// Lock-free Single-Producer Single-Consumer ring buffer
// Optimized for low-latency message passing
template<typename T, size_t Capacity>
class SPSCQueue {
    static_assert((Capacity & (Capacity - 1)) == 0, "Capacity must be power of 2");

public:
    SPSCQueue() : head_(0), tail_(0) {}

    // Producer: try to push an item (returns false if full)
    bool try_push(const T& item) {
        const size_t current_tail = tail_.load(std::memory_order_relaxed);
        const size_t next_tail = (current_tail + 1) & mask_;
        
        if (next_tail == head_.load(std::memory_order_acquire)) {
            return false; // Queue full
        }
        
        buffer_[current_tail] = item;
        tail_.store(next_tail, std::memory_order_release);
        return true;
    }

    // Consumer: try to pop an item (returns false if empty)
    bool try_pop(T& item) {
        const size_t current_head = head_.load(std::memory_order_relaxed);
        
        if (current_head == tail_.load(std::memory_order_acquire)) {
            return false; // Queue empty
        }
        
        item = buffer_[current_head];
        head_.store((current_head + 1) & mask_, std::memory_order_release);
        return true;
    }

    // Check if empty
    bool empty() const {
        return head_.load(std::memory_order_acquire) == 
               tail_.load(std::memory_order_acquire);
    }

    // Approximate size (may be slightly stale)
    size_t size() const {
        const size_t head = head_.load(std::memory_order_acquire);
        const size_t tail = tail_.load(std::memory_order_acquire);
        return (tail - head) & mask_;
    }

private:
    static constexpr size_t mask_ = Capacity - 1;
    
    // Cache line padding to avoid false sharing
    alignas(64) std::atomic<size_t> head_;
    alignas(64) std::atomic<size_t> tail_;
    alignas(64) std::array<T, Capacity> buffer_;
};

// Message envelope for passing ITCH messages between threads
struct MessageEnvelope {
    char type;
    uint16_t length;
    uint16_t padding;
    const uint8_t* data; // Pointer into mmap'd region (zero-copy)
    uint64_t seq;
    uint64_t t0_ns; // Timestamp when message entered pipeline (nanoseconds)
};

using ITCHQueue = SPSCQueue<MessageEnvelope, 65536>; // 64K message queue
