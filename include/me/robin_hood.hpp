#pragma once

#include "me/order.hpp"

#include <cstdint>
#include <cstring>
#include <new>
#include <utility>

namespace me {

// Open-addressing Robin Hood hash map: OrderID → Order*.
// Power-of-two capacity. No heap after construction. Never resizes on hot path.
struct alignas(32) RobinHoodEntry {
    uint64_t key{0};  // 0 = empty
    uint64_t dist{0}; // probe sequence length
    Order* value{nullptr};
};

class RobinHoodIndex {
public:
    explicit RobinHoodIndex(size_t capacity_pow2)
        : capacity_(round_up_pow2(capacity_pow2)), mask_(capacity_ - 1), size_(0) {
        table_ = static_cast<RobinHoodEntry*>(
            ::operator new[](capacity_ * sizeof(RobinHoodEntry), std::align_val_t{32}));
        std::memset(table_, 0, capacity_ * sizeof(RobinHoodEntry));
    }

    ~RobinHoodIndex() {
        if (table_) {
            ::operator delete[](table_, std::align_val_t{32});
            table_ = nullptr;
        }
    }

    RobinHoodIndex(const RobinHoodIndex&) = delete;
    RobinHoodIndex& operator=(const RobinHoodIndex&) = delete;

    RobinHoodIndex(RobinHoodIndex&& o) noexcept
        : table_(std::exchange(o.table_, nullptr))
        , capacity_(std::exchange(o.capacity_, 0))
        , mask_(std::exchange(o.mask_, 0))
        , size_(std::exchange(o.size_, 0)) {}

    // Returns false if duplicate key or load factor would exceed 0.7.
    bool insert(uint64_t key, Order* value) noexcept {
        if (key == 0) [[unlikely]]
            return false;
        if (size_ * 10 >= capacity_ * 7) [[unlikely]]
            return false;

        RobinHoodEntry entr{key, 0, value};
        size_t idx = hash_key(key) & mask_;

        for (;;) {
            RobinHoodEntry& slot = table_[idx];
            if (slot.key == 0) {
                slot = entr;
                ++size_;
                return true;
            }
            if (slot.key == key) [[unlikely]]
                return false;
            if (entr.dist > slot.dist) {
                std::swap(entr, slot);
            }
            ++entr.dist;
            idx = (idx + 1) & mask_;
        }
    }

    [[nodiscard]] Order* find(uint64_t key) const noexcept {
        if (key == 0) [[unlikely]]
            return nullptr;
        size_t idx = hash_key(key) & mask_;
        uint64_t dist = 0;
        for (;;) {
            const RobinHoodEntry& slot = table_[idx];
            if (slot.key == 0 || dist > slot.dist) return nullptr;
            if (slot.key == key) return slot.value;
            ++dist;
            idx = (idx + 1) & mask_;
        }
    }

    // Backward-shift deletion (no tombstones).
    bool erase(uint64_t key) noexcept {
        if (key == 0) [[unlikely]]
            return false;
        size_t idx = hash_key(key) & mask_;
        uint64_t dist = 0;
        for (;;) {
            RobinHoodEntry& slot = table_[idx];
            if (slot.key == 0 || dist > slot.dist) return false;
            if (slot.key == key) {
                return erase_at(idx);
            }
            ++dist;
            idx = (idx + 1) & mask_;
        }
    }

    void clear() noexcept {
        std::memset(table_, 0, capacity_ * sizeof(RobinHoodEntry));
        size_ = 0;
    }

    [[nodiscard]] size_t size() const noexcept { return size_; }
    [[nodiscard]] size_t capacity() const noexcept { return capacity_; }

private:
    bool erase_at(size_t idx) noexcept {
        for (;;) {
            const size_t next = (idx + 1) & mask_;
            RobinHoodEntry& n = table_[next];
            if (n.key == 0 || n.dist == 0) {
                table_[idx] = RobinHoodEntry{};
                --size_;
                return true;
            }
            table_[idx] = RobinHoodEntry{n.key, n.dist - 1, n.value};
            idx = next;
        }
    }

    static size_t round_up_pow2(size_t n) noexcept {
        if (n < 16) return 16;
        --n;
        n |= n >> 1;
        n |= n >> 2;
        n |= n >> 4;
        n |= n >> 8;
        n |= n >> 16;
        n |= n >> 32;
        return n + 1;
    }

    static uint64_t hash_key(uint64_t key) noexcept {
        uint64_t z = key + 0x9e3779b97f4a7c15ULL;
        z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9ULL;
        z = (z ^ (z >> 27)) * 0x94d049bb133111ebULL;
        return z ^ (z >> 31);
    }

    RobinHoodEntry* table_{nullptr};
    size_t capacity_{0};
    size_t mask_{0};
    size_t size_{0};
};

} // namespace me
