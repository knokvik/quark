#pragma once

#include "me/order.hpp"
#include "me/price_level.hpp"
#include "me/types.hpp"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <new>
#include <utility>

namespace me {

// Pre-allocated arena + free-list. No malloc after construction.
template <typename T>
class ObjectPool {
public:
    explicit ObjectPool(size_t capacity)
        : capacity_(static_cast<uint32_t>(capacity)), next_free_(0), free_list_(nullptr) {
        void* raw = ::operator new[](capacity * sizeof(T), std::align_val_t{alignof(T)});
        pool_ = static_cast<T*>(raw);
        // Touch pages without full construction cost (zero-fill is enough for POD-ish T)
        std::memset(pool_, 0, capacity * sizeof(T));
    }

    ~ObjectPool() {
        if (pool_) {
            ::operator delete[](pool_, std::align_val_t{alignof(T)});
            pool_ = nullptr;
        }
    }

    ObjectPool(const ObjectPool&) = delete;
    ObjectPool& operator=(const ObjectPool&) = delete;

    ObjectPool(ObjectPool&& other) noexcept
        : pool_(std::exchange(other.pool_, nullptr))
        , capacity_(std::exchange(other.capacity_, 0))
        , next_free_(std::exchange(other.next_free_, 0))
        , free_list_(std::exchange(other.free_list_, nullptr))
        , live_(std::exchange(other.live_, 0)) {}

    ObjectPool& operator=(ObjectPool&& other) noexcept {
        if (this != &other) {
            this->~ObjectPool();
            new (this) ObjectPool(std::move(other));
        }
        return *this;
    }

    [[nodiscard]] T* allocate() noexcept {
        if (free_list_) {
            T* o = free_list_;
            free_list_ = free_list_next(o);
            ++live_;
            return o;
        }
        if (next_free_ < capacity_) {
            ++live_;
            return &pool_[next_free_++];
        }
        return nullptr;
    }

    void deallocate(T* o) noexcept {
        if (!o) return;
        set_free_list_next(o, free_list_);
        free_list_ = o;
        --live_;
    }

    [[nodiscard]] uint32_t capacity() const noexcept { return capacity_; }
    [[nodiscard]] uint32_t live() const noexcept { return live_; }
    [[nodiscard]] uint32_t bump_used() const noexcept { return next_free_; }

    void reset() noexcept {
        free_list_ = nullptr;
        next_free_ = 0;
        live_ = 0;
        std::memset(pool_, 0, static_cast<size_t>(capacity_) * sizeof(T));
    }

    [[nodiscard]] T* data() noexcept { return pool_; }
    [[nodiscard]] const T* data() const noexcept { return pool_; }

private:
    static T* free_list_next(T* o) noexcept { return *reinterpret_cast<T**>(o); }
    static void set_free_list_next(T* o, T* n) noexcept { *reinterpret_cast<T**>(o) = n; }

    T* pool_{nullptr};
    uint32_t capacity_{0};
    uint32_t next_free_{0};
    T* free_list_{nullptr};
    uint32_t live_{0};
};

using OrderPool = ObjectPool<Order>;
using LevelPool = ObjectPool<PriceLevel>;

} // namespace me
