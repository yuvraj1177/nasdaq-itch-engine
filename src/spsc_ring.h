#pragma once

#include <atomic>
#include <array>
#include <cstdint>
#include <cstring>

// Single Producer Single Consumer lock-free ring buffer
// Optimized for low-latency message passing
template<typename T, size_t Capacity>
class SPSCRing {
    static_assert((Capacity & (Capacity - 1)) == 0, "Capacity must be power of 2");
    
public:
    SPSCRing() : head_(0), tail_(0) {}

    // Producer: try to push an item (returns false if full)
    bool try_push(const T& item) {
        const size_t current_tail = tail_.load(std::memory_order_relaxed);
        const size_t next_tail = (current_tail + 1) & mask_;
        
        if (next_tail == head_.load(std::memory_order_acquire)) {
            return false; // Full
        }
        
        buffer_[current_tail] = item;
        tail_.store(next_tail, std::memory_order_release);
        return true;
    }

    // Consumer: try to pop an item (returns false if empty)
    bool try_pop(T& item) {
        const size_t current_head = head_.load(std::memory_order_relaxed);
        
        if (current_head == tail_.load(std::memory_order_acquire)) {
            return false; // Empty
        }
        
        item = buffer_[current_head];
        head_.store((current_head + 1) & mask_, std::memory_order_release);
        return true;
    }

    // Check if empty (consumer side)
    bool empty() const {
        return head_.load(std::memory_order_relaxed) == 
               tail_.load(std::memory_order_acquire);
    }

    // Check if full (producer side)
    bool full() const {
        const size_t next_tail = (tail_.load(std::memory_order_relaxed) + 1) & mask_;
        return next_tail == head_.load(std::memory_order_acquire);
    }

    // Approximate size (can be stale due to concurrency)
    size_t size() const {
        const size_t h = head_.load(std::memory_order_relaxed);
        const size_t t = tail_.load(std::memory_order_relaxed);
        return (t - h) & mask_;
    }

private:
    static constexpr size_t mask_ = Capacity - 1;
    
    // Cache-line padding to avoid false sharing
    alignas(64) std::atomic<size_t> head_;
    alignas(64) std::atomic<size_t> tail_;
    alignas(64) std::array<T, Capacity> buffer_;
};

// Message envelope for passing ITCH messages between threads
struct MessageEnvelope {
    char type;
    uint16_t length;
    const uint8_t* data; // Points into mmap'd region (zero-copy)
    uint64_t seq;
};

using ITCHMessageRing = SPSCRing<MessageEnvelope, 65536>; // 64K message queue
