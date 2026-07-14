#pragma once

#include <cstdint>
#include <cstddef>

namespace me {

enum class Side : uint8_t { Bid = 0, Ask = 1 };

enum class OrderType : uint8_t { Limit = 0, Market = 1 };

enum class OrderStatus : uint8_t {
    New = 0,
    Partial = 1,
    Filled = 2,
    Cancelled = 3,
    Rejected = 4
};

enum class EventType : uint8_t {
    Ack = 0,
    Fill = 1,
    CancelAck = 2,
    Reject = 3
};

enum class RejectReason : uint8_t {
    None = 0,
    PoolExhausted = 1,
    DuplicateId = 2,
    NotFound = 3,
    InvalidQuantity = 4,
    InvalidPrice = 5,
    BookEmpty = 6
};

// Fixed-point: real_price = price / kPriceScale (4 decimal places)
inline constexpr uint64_t kPriceScale = 10'000ULL;

// Supported absolute price range (fixed-point).
inline constexpr uint64_t kMinPrice = 1;
inline constexpr uint64_t kMaxPrice = 10'000'000ULL; // $1000.00

// Dense flat-array window for O(1) level lookup (equity-style band).
// $0.01 .. $1000.00 is too large; use a trading band of 200k ticks ($20.00 wide).
// Full range still accepted via overflow PriceMap (see OrderBook).
inline constexpr uint64_t kFlatMinPrice = 1;
inline constexpr uint64_t kFlatMaxPrice = 2'000'000ULL; // up to $200.00
inline constexpr size_t kFlatSlots =
    static_cast<size_t>(kFlatMaxPrice - kFlatMinPrice + 1); // ~2M → 16MB/side

inline constexpr size_t kDefaultOrderCapacity = 1 << 20; // 1M
inline constexpr size_t kDefaultLevelCapacity = 1 << 16; // 65K
inline constexpr size_t kDefaultIndexCapacity = 1 << 21; // 2M
inline constexpr size_t kDefaultEventCapacity = 1 << 16; // 64K

[[nodiscard]] inline bool valid_price(uint64_t price) noexcept {
    return price >= kMinPrice && price <= kMaxPrice;
}

[[nodiscard]] inline bool in_flat_window(uint64_t price) noexcept {
    return price >= kFlatMinPrice && price <= kFlatMaxPrice;
}

[[nodiscard]] inline size_t flat_index(uint64_t price) noexcept {
    return static_cast<size_t>(price - kFlatMinPrice);
}

} // namespace me
