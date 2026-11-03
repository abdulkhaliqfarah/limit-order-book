// Differential-test driver: reads text operations on stdin, applies them to
// the engine, and prints every trade plus the final book state on stdout.
// scripts/model.py implements the same rules naively in Python and diffs
// this output against its own -- two independent implementations agreeing
// on thousands of random operations is strong evidence both are right.
//
// Input lines:   A <id> <B|S> <L|M> <price> <qty>     add order
//                C <id>                               cancel order
// Output lines:  T <maker_id> <taker_id> <price> <qty>
//                BOOK <side> <price> <total_qty>   (best-first, per side)
//                LIVE <count>
#include <cstdio>
#include <iostream>
#include <string>
#include <vector>

#include "lob/order_book.hpp"

int main() {
    lob::OrderBook book;
    std::vector<lob::Trade> trades;
    std::string op;
    while (std::cin >> op) {
        if (op == "A") {
            lob::OrderId id;
            char side_c, type_c;
            lob::Price price;
            lob::Quantity qty;
            std::cin >> id >> side_c >> type_c >> price >> qty;
            std::size_t before = trades.size();
            book.add_order(id, side_c == 'B' ? lob::Side::Buy : lob::Side::Sell,
                           type_c == 'L' ? lob::OrderType::Limit
                                         : lob::OrderType::Market,
                           price, qty, trades);
            for (std::size_t i = before; i < trades.size(); ++i) {
                std::printf("T %llu %llu %lld %lld\n",
                            static_cast<unsigned long long>(trades[i].maker_id),
                            static_cast<unsigned long long>(trades[i].taker_id),
                            static_cast<long long>(trades[i].price),
                            static_cast<long long>(trades[i].quantity));
            }
        } else if (op == "C") {
            lob::OrderId id;
            std::cin >> id;
            book.cancel_order(id);
        }
    }
    // Dump the resting book by probing both sides from the touch outward.
    for (int s = 0; s < 2; ++s) {
        lob::Side side = s == 0 ? lob::Side::Buy : lob::Side::Sell;
        // Walk prices: bids descending, asks ascending, over the tick range
        // the generator uses (9000..11000).
        for (lob::Price p = 9000; p <= 11000; ++p) {
            lob::Quantity q = book.quantity_at(side, p);
            if (q > 0) {
                std::printf("BOOK %c %lld %lld\n", s == 0 ? 'B' : 'S',
                            static_cast<long long>(p),
                            static_cast<long long>(q));
            }
        }
    }
    std::printf("LIVE %zu\n", book.live_order_count());
    return 0;
}
