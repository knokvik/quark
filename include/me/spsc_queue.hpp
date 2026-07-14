#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <new>
#include <utility>

namespace me {

// Lock-free single-producer single-consumer ring buffer.
// Capacity must be power of two. Hot path: no locks, no syscalls.
template <typename T>
class SpscQueue {
public:
    explicit SpscQueue(size_t capacity_pow2)
        : capacity_(round_up_pow2(capacity_pow2)), mask_(capacity_ - 1) {
        buf_ = static_cast<T*>(
            ::operator new[](capacity_ * sizeof(T), std::align_val_t{alignof(T)}));
        for (size_t i = 0; i < capacity_; ++i) {
            new (&buf_[i]) T{};
        }
    }

    ~SpscQueue() {
        if (buf_) {
            for (size_t i = 0; i < capacity_; ++i) {
                buf_[i].~T();
            }
            ::operator delete[](buf_, std::align_val_t{alignof(T)});
        }
    }

    SpscQueue(const SpscQueue&) = delete;
    SpscQueue& operator=(const SpscQueue&) = delete;

    // Producer only
    [[nodiscard]] bool try_push(const T& item) noexcept {
        const size_t head = head_.load(std::memory_order_relaxed);
        const size_t next = (head + 1) & mask_;
        if (next == tail_.load(std::memory_order_acquire)) {
            return false; // full
        }
        buf_[head] = item;
        head_.store(next, std::memory_order_release);
        return true;
    }

    // Consumer only
    [[nodiscard]] bool try_pop(T& out) noexcept {
        const size_t tail = tail_.load(std::memory_order_relaxed);
        if (tail == head_.load(std::memory_order_acquire)) {
            return false; // empty
        }
        out = buf_[tail];
        tail_.store((tail + 1) & mask_, std::memory_order_release);
        return true;
    }

    [[nodiscard]] size_t capacity() const noexcept { return capacity_; }

    // Approximate size (racy — for stats only)
    [[nodiscard]] size_t size_approx() const noexcept {
        const size_t h = head_.load(std::memory_order_relaxed);
        const size_t t = tail_.load(std::memory_order_relaxed);
        return (h - t) & mask_;
    }

private:
    static size_t round_up_pow2(size_t n) noexcept {
        if (n < 2) return 2;
        --n;
        n |= n >> 1;
        n |= n >> 2;
        n |= n >> 4;
        n |= n >> 8;
        n |= n >> 16;
        n |= n >> 32;
        return n + 1;
    }

    alignas(64) std::atomic<size_t> head_{0};
    alignas(64) std::atomic<size_t> tail_{0};
    T* buf_{nullptr};
    size_t capacity_{0};
    size_t mask_{0};
};

// Engine event emitted to async consumer (exactly one cache line).
struct alignas(64) EngineEvent {
    EventType type{EventType::Ack};
    RejectReason reason{RejectReason::None};
    Side side{Side::Bid};
    uint8_t pad0{0};
    uint32_t quantity{0};
    uint64_t order_id{0};
    uint64_t match_id{0}; // contra order for fills
    uint64_t price{0};
    uint64_t seq{0};
    uint64_t pad1{0};
    uint64_t pad2{0};
    uint64_t pad3{0};
};

static_assert(sizeof(EngineEvent) == 64, "EngineEvent one cache line");

} // namespace me
