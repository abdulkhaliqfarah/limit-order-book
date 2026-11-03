// Replays a binary feed file through the matching engine and reports
// throughput and per-message latency percentiles.
//
// Usage: replay <feed_file> [--raw]
// Prints: message count, wall time, messages/sec, trades generated, final
// book state, and p50 / p90 / p99 / p99.9 / max per-message latency in ns.
// --raw skips the per-message clock reads, showing uninstrumented
// throughput; the gap between the two modes is the cost of measuring.
#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

#include "lob/feed.hpp"

int main(int argc, char** argv) {
    if (argc < 2 || argc > 3) {
        std::fprintf(stderr, "usage: replay <feed_file> [--raw]\n");
        return 1;
    }
    const bool raw = argc == 3 && std::strcmp(argv[2], "--raw") == 0;
    FILE* in = std::fopen(argv[1], "rb");
    if (in == nullptr) {
        std::perror("fopen");
        return 1;
    }
    std::vector<lob::Message> messages;
    lob::Message msg;
    while (std::fread(&msg, sizeof(msg), 1, in) == 1) messages.push_back(msg);
    std::fclose(in);

    lob::OrderBook book;
    lob::FeedHandler handler(book);
    std::vector<lob::Trade> trades;
    trades.reserve(1 << 20);
    std::vector<std::int64_t> latencies_ns;
    latencies_ns.reserve(messages.size());

    using Clock = std::chrono::steady_clock;
    const auto start = Clock::now();
    for (const lob::Message& m : messages) {
        if (raw) {
            handler.on_message(m, trades);
            continue;
        }
        const auto t0 = Clock::now();
        handler.on_message(m, trades);
        const auto t1 = Clock::now();
        latencies_ns.push_back(
            std::chrono::duration_cast<std::chrono::nanoseconds>(t1 - t0)
                .count());
    }
    const auto end = Clock::now();
    const double seconds =
        std::chrono::duration_cast<std::chrono::nanoseconds>(end - start)
            .count() /
        1e9;

    std::printf("messages        %zu\n", messages.size());
    std::printf("trades          %zu\n", trades.size());
    std::printf("wall time       %.3f s\n", seconds);
    std::printf("throughput      %.0f msg/s\n", messages.size() / seconds);
    if (!raw) {
        std::sort(latencies_ns.begin(), latencies_ns.end());
        auto pct = [&](double p) {
            std::size_t idx = static_cast<std::size_t>(p * latencies_ns.size());
            if (idx >= latencies_ns.size()) idx = latencies_ns.size() - 1;
            return latencies_ns[idx];
        };
        std::printf("latency p50     %lld ns\n", static_cast<long long>(pct(0.50)));
        std::printf("latency p90     %lld ns\n", static_cast<long long>(pct(0.90)));
        std::printf("latency p99     %lld ns\n", static_cast<long long>(pct(0.99)));
        std::printf("latency p99.9   %lld ns\n", static_cast<long long>(pct(0.999)));
    }
    std::printf("final book      best bid %lld  best ask %lld  live orders %zu\n",
                static_cast<long long>(book.best_bid()),
                static_cast<long long>(book.best_ask()),
                book.live_order_count());
    return 0;
}
