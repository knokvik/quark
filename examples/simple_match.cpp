// Minimal usage sketch (link against me_engine).
#include "me/order_book.hpp"
#include <cstdio>

int main() {
    me::OrderBook book;
    book.insert(1, me::Side::Bid, me::OrderType::Limit, 100'0000, 10);
    book.insert(2, me::Side::Ask, me::OrderType::Limit, 100'0000, 10);
    std::printf("fills=%llu best_bid=%llu best_ask=%llu\n",
                (unsigned long long)book.fill_count(),
                (unsigned long long)book.best_bid(),
                (unsigned long long)book.best_ask());
    return 0;
}
