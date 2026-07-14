#pragma once

#include "me/types.hpp"

#include <cstdint>

namespace me {

struct PriceLevel; // forward

// Exactly one cache line. Hot fields first.
struct alignas(64) Order {
    uint64_t id{0};
    uint64_t price{0};       // fixed-point: price * 10^4
    uint32_t quantity{0};
    uint32_t filled_qty{0};
    Side side{Side::Bid};
    OrderType type{OrderType::Limit};
    OrderStatus status{OrderStatus::New};
    uint8_t pad0{0};

    Order* next{nullptr};
    Order* prev{nullptr};
    PriceLevel* level{nullptr};

    [[nodiscard]] uint32_t remaining() const noexcept {
        return quantity - filled_qty;
    }

    [[nodiscard]] bool fully_filled() const noexcept {
        return filled_qty >= quantity;
    }
};

static_assert(sizeof(Order) == 64, "Order must be exactly one cache line");
static_assert(alignof(Order) == 64, "Order must be 64-byte aligned");

} // namespace me
