#pragma once

#include "me/types.hpp"

#include <cstdint>

namespace me {

struct Order; // forward

// Exactly one cache line. Intrusive FIFO head/tail for orders at this price.
struct alignas(64) PriceLevel {
    uint64_t price{0};       // 8
    uint32_t total_qty{0};   // 4
    uint32_t order_count{0}; // 4
    Order* head{nullptr};    // 8
    Order* tail{nullptr};    // 8
    // Optional tree links (unused with flat-array backend) — pack carefully.
    PriceLevel* left{nullptr};   // 8
    PriceLevel* right{nullptr};  // 8
    PriceLevel* parent{nullptr}; // 8
    // 8+4+4+8+8+8+8+8 = 56 → 8 bytes pad to 64
    uint32_t height{0};
    uint32_t pad{0};

    void reset(uint64_t p) noexcept {
        price = p;
        total_qty = 0;
        order_count = 0;
        head = tail = nullptr;
        left = right = parent = nullptr;
        height = 0;
        pad = 0;
    }

    [[nodiscard]] bool empty() const noexcept { return order_count == 0; }
};

static_assert(sizeof(PriceLevel) == 64, "PriceLevel must be exactly one cache line");
static_assert(alignof(PriceLevel) == 64, "PriceLevel must be 64-byte aligned");

} // namespace me
