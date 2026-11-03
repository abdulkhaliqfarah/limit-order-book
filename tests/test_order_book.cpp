// Scenario tests for the matching engine. Each test pins one rule of a real
// continuous double auction: price priority, time priority, execution at the
// maker's price, partial fills, cancels. Run via ctest or directly; the
// process exits non-zero if any check fails.
#include <cstdio>
#include <vector>

#include "lob/order_book.hpp"

using namespace lob;

static int failures = 0;
static int checks = 0;

#define CHECK(cond)                                     \
    do {                                                \
        ++checks;                                       \
        if (!(cond)) {                                  \
            std::printf("FAIL %s:%d  %s\n", __FILE__,   \
                        __LINE__, #cond);               \
            ++failures;                                 \
        }                                               \
    } while (0)

static void test_resting_order_no_match() {
    OrderBook book;
    std::vector<Trade> trades;
    book.add_order(1, Side::Buy, OrderType::Limit, 10000, 100, trades);
    CHECK(trades.empty());
    CHECK(book.best_bid() == 10000);
    CHECK(book.best_ask() == 0);
    CHECK(book.quantity_at(Side::Buy, 10000) == 100);
    CHECK(book.live_order_count() == 1);
}

static void test_full_match_at_maker_price() {
    OrderBook book;
    std::vector<Trade> trades;
    book.add_order(1, Side::Sell, OrderType::Limit, 9900, 50, trades);
    // Buyer is willing to pay more; the trade still prints at the maker's
    // price (9900), not the taker's (10100).
    book.add_order(2, Side::Buy, OrderType::Limit, 10100, 50, trades);
    CHECK(trades.size() == 1);
    CHECK(trades[0].price == 9900);
    CHECK(trades[0].quantity == 50);
    CHECK(trades[0].maker_id == 1);
    CHECK(trades[0].taker_id == 2);
    CHECK(book.live_order_count() == 0);
    CHECK(book.empty(Side::Buy) && book.empty(Side::Sell));
}

static void test_partial_fill_remainder_rests() {
    OrderBook book;
    std::vector<Trade> trades;
    book.add_order(1, Side::Sell, OrderType::Limit, 10000, 30, trades);
    book.add_order(2, Side::Buy, OrderType::Limit, 10000, 100, trades);
    CHECK(trades.size() == 1);
    CHECK(trades[0].quantity == 30);
    CHECK(book.best_bid() == 10000);
    CHECK(book.quantity_at(Side::Buy, 10000) == 70);
    CHECK(book.live_order_count() == 1);
}

static void test_price_priority_across_levels() {
    OrderBook book;
    std::vector<Trade> trades;
    // Two ask levels; the cheaper one must be consumed first even though it
    // arrived second.
    book.add_order(1, Side::Sell, OrderType::Limit, 10005, 10, trades);
    book.add_order(2, Side::Sell, OrderType::Limit, 10001, 10, trades);
    book.add_order(3, Side::Buy, OrderType::Limit, 10005, 15, trades);
    CHECK(trades.size() == 2);
    CHECK(trades[0].maker_id == 2);
    CHECK(trades[0].price == 10001);
    CHECK(trades[1].maker_id == 1);
    CHECK(trades[1].quantity == 5);
    CHECK(book.quantity_at(Side::Sell, 10005) == 5);
}

static void test_time_priority_within_level() {
    OrderBook book;
    std::vector<Trade> trades;
    // Same price: first in, first matched (FIFO).
    book.add_order(1, Side::Sell, OrderType::Limit, 10000, 10, trades);
    book.add_order(2, Side::Sell, OrderType::Limit, 10000, 10, trades);
    book.add_order(3, Side::Buy, OrderType::Limit, 10000, 10, trades);
    CHECK(trades.size() == 1);
    CHECK(trades[0].maker_id == 1);
    CHECK(book.quantity_at(Side::Sell, 10000) == 10);
    // The remaining order is id 2; cancelling id 1 must now fail.
    CHECK(!book.cancel_order(1));
    CHECK(book.cancel_order(2));
}

static void test_no_cross_no_trade() {
    OrderBook book;
    std::vector<Trade> trades;
    book.add_order(1, Side::Sell, OrderType::Limit, 10010, 10, trades);
    book.add_order(2, Side::Buy, OrderType::Limit, 10000, 10, trades);
    CHECK(trades.empty());
    CHECK(book.best_bid() == 10000);
    CHECK(book.best_ask() == 10010);
}

static void test_market_order_sweeps_and_remainder_dies() {
    OrderBook book;
    std::vector<Trade> trades;
    book.add_order(1, Side::Sell, OrderType::Limit, 10000, 10, trades);
    book.add_order(2, Side::Sell, OrderType::Limit, 10050, 10, trades);
    book.add_order(3, Side::Buy, OrderType::Market, 0, 100, trades);
    CHECK(trades.size() == 2);
    CHECK(trades[0].price == 10000);
    CHECK(trades[1].price == 10050);
    // 80 unfilled market quantity must NOT rest on the book.
    CHECK(book.live_order_count() == 0);
    CHECK(book.empty(Side::Buy));
}

static void test_cancel() {
    OrderBook book;
    std::vector<Trade> trades;
    book.add_order(1, Side::Buy, OrderType::Limit, 9990, 10, trades);
    book.add_order(2, Side::Buy, OrderType::Limit, 9995, 10, trades);
    CHECK(book.best_bid() == 9995);
    CHECK(book.cancel_order(2));
    CHECK(book.best_bid() == 9990);
    CHECK(!book.cancel_order(2));     // already gone
    CHECK(!book.cancel_order(4242));  // never existed
    CHECK(book.live_order_count() == 1);
}

static void test_cancel_middle_of_level_keeps_fifo() {
    OrderBook book;
    std::vector<Trade> trades;
    book.add_order(1, Side::Sell, OrderType::Limit, 10000, 10, trades);
    book.add_order(2, Side::Sell, OrderType::Limit, 10000, 10, trades);
    book.add_order(3, Side::Sell, OrderType::Limit, 10000, 10, trades);
    CHECK(book.cancel_order(2));  // unlink the middle node
    book.add_order(4, Side::Buy, OrderType::Limit, 10000, 20, trades);
    CHECK(trades.size() == 2);
    CHECK(trades[0].maker_id == 1);
    CHECK(trades[1].maker_id == 3);
    CHECK(book.live_order_count() == 0);
}

static void test_partial_fill_of_maker_keeps_priority() {
    OrderBook book;
    std::vector<Trade> trades;
    book.add_order(1, Side::Sell, OrderType::Limit, 10000, 100, trades);
    book.add_order(2, Side::Sell, OrderType::Limit, 10000, 100, trades);
    book.add_order(3, Side::Buy, OrderType::Limit, 10000, 40, trades);
    CHECK(trades.size() == 1);
    CHECK(trades[0].maker_id == 1);
    CHECK(trades[0].quantity == 40);
    // Order 1 still has 60 left and is still first in the queue.
    book.add_order(4, Side::Buy, OrderType::Limit, 10000, 70, trades);
    CHECK(trades.size() == 3);
    CHECK(trades[1].maker_id == 1);
    CHECK(trades[1].quantity == 60);
    CHECK(trades[2].maker_id == 2);
    CHECK(trades[2].quantity == 10);
}

int main() {
    test_resting_order_no_match();
    test_full_match_at_maker_price();
    test_partial_fill_remainder_rests();
    test_price_priority_across_levels();
    test_time_priority_within_level();
    test_no_cross_no_trade();
    test_market_order_sweeps_and_remainder_dies();
    test_cancel();
    test_cancel_middle_of_level_keeps_fifo();
    test_partial_fill_of_maker_keeps_priority();
    std::printf("%d checks, %d failures\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
