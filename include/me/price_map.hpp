#pragma once

#include "me/price_level.hpp"

#include <cstdint>
#include <cstring>
#include <new>
#include <utility>

namespace me {

// Open-addressing map: price → PriceLevel*. Power-of-two, no heap after ctor.
// Preferable to a 10M-slot flat array under sparse price distributions (TLB).
struct alignas(32) PriceMapEntry {
    uint64_t key{0}; // 0 = empty (prices are always >= kMinPrice >= 1)
    PriceLevel* value{nullptr};
    uint64_t dist{0};
};

class PriceMap {
public:
    explicit PriceMap(size_t capacity_pow2 = 4096)
        : capacity_(round_up_pow2(capacity_pow2)), mask_(capacity_ - 1), size_(0) {
        table_ = static_cast<PriceMapEntry*>(
            ::operator new[](capacity_ * sizeof(PriceMapEntry), std::align_val_t{32}));
        std::memset(table_, 0, capacity_ * sizeof(PriceMapEntry));
    }

    ~PriceMap() {
        if (table_) {
            ::operator delete[](table_, std::align_val_t{32});
            table_ = nullptr;
        }
    }

    PriceMap(const PriceMap&) = delete;
    PriceMap& operator=(const PriceMap&) = delete;

    PriceMap(PriceMap&& o) noexcept
        : table_(std::exchange(o.table_, nullptr))
        , capacity_(std::exchange(o.capacity_, 0))
        , mask_(std::exchange(o.mask_, 0))
        , size_(std::exchange(o.size_, 0)) {}

    bool insert(uint64_t key, PriceLevel* value) noexcept {
        if (key == 0) return false;
        if (size_ * 10 >= capacity_ * 7) return false;

        PriceMapEntry entr{key, value, 0};
        size_t idx = hash_key(key) & mask_;
        for (;;) {
            PriceMapEntry& slot = table_[idx];
            if (slot.key == 0) {
                slot = entr;
                ++size_;
                return true;
            }
            if (slot.key == key) {
                slot.value = value;
                return true;
            }
            if (entr.dist > slot.dist) std::swap(entr, slot);
            ++entr.dist;
            idx = (idx + 1) & mask_;
        }
    }

    [[nodiscard]] PriceLevel* find(uint64_t key) const noexcept {
        if (key == 0) return nullptr;
        size_t idx = hash_key(key) & mask_;
        uint64_t dist = 0;
        for (;;) {
            const PriceMapEntry& slot = table_[idx];
            if (slot.key == 0 || dist > slot.dist) return nullptr;
            if (slot.key == key) return slot.value;
            ++dist;
            idx = (idx + 1) & mask_;
        }
    }

    bool erase(uint64_t key) noexcept {
        if (key == 0) return false;
        size_t idx = hash_key(key) & mask_;
        uint64_t dist = 0;
        for (;;) {
            PriceMapEntry& slot = table_[idx];
            if (slot.key == 0 || dist > slot.dist) return false;
            if (slot.key == key) {
                for (;;) {
                    const size_t next = (idx + 1) & mask_;
                    PriceMapEntry& n = table_[next];
                    if (n.key == 0 || n.dist == 0) {
                        table_[idx] = PriceMapEntry{};
                        --size_;
                        return true;
                    }
                    table_[idx] = PriceMapEntry{n.key, n.value, n.dist - 1};
                    idx = next;
                }
            }
            ++dist;
            idx = (idx + 1) & mask_;
        }
    }

    void clear() noexcept {
        std::memset(table_, 0, capacity_ * sizeof(PriceMapEntry));
        size_ = 0;
    }

    [[nodiscard]] size_t size() const noexcept { return size_; }

private:
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

    PriceMapEntry* table_{nullptr};
    size_t capacity_{0};
    size_t mask_{0};
    size_t size_{0};
};

} // namespace me
