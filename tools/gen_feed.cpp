// Generates a deterministic synthetic order stream as binary feed messages.
//
// The stream mimics a busy single-instrument session: a random-walking mid
// price, limit orders priced around the mid, occasional market orders, and
// cancels drawn from the pool of live order ids. Same seed = same stream,
// so benchmark runs are comparable.
//
// Usage: gen_feed <num_messages> <seed> <output_file>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <vector>

#include "lob/feed.hpp"

namespace {

// splitmix64: small, fast, deterministic.
struct Rng {
    std::uint64_t state;
    std::uint64_t next() {
        std::uint64_t z = (state += 0x9e3779b97f4a7c15ULL);
        z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9ULL;
        z = (z ^ (z >> 27)) * 0x94d049bb133111ebULL;
        return z ^ (z >> 31);
    }
    std::uint64_t below(std::uint64_t n) { return next() % n; }
};

}  // namespace

int main(int argc, char** argv) {
    if (argc != 4) {
        std::fprintf(stderr, "usage: gen_feed <num_messages> <seed> <output>\n");
        return 1;
    }
    const std::uint64_t count = std::strtoull(argv[1], nullptr, 10);
    Rng rng{std::strtoull(argv[2], nullptr, 10)};
    FILE* out = std::fopen(argv[3], "wb");
    if (out == nullptr) {
        std::perror("fopen");
        return 1;
    }

    std::vector<lob::Message> batch;
    batch.reserve(1 << 16);
    std::vector<lob::OrderId> live_ids;
    lob::OrderId next_id = 1;
    lob::Price mid = 10000;  // $100.00

    auto flush = [&] {
        std::fwrite(batch.data(), sizeof(lob::Message), batch.size(), out);
        batch.clear();
    };

    for (std::uint64_t i = 0; i < count; ++i) {
        lob::Message msg{};
        std::uint64_t roll = rng.below(100);

        if (roll < 12 && !live_ids.empty()) {
            // Cancel a random live order.
            std::size_t idx = rng.below(live_ids.size());
            msg.type = 'C';
            msg.id = live_ids[idx];
            live_ids[idx] = live_ids.back();
            live_ids.pop_back();
        } else {
            msg.type = 'A';
            msg.id = next_id;
            live_ids.push_back(next_id);
            ++next_id;

            bool buy = rng.below(2) == 0;
            msg.side = buy ? 0 : 1;
            // Drift the mid slowly so the book stays centred.
            mid += static_cast<lob::Price>(rng.below(5)) - 2;
            // Offset from mid: most orders land near the touch, a few cross
            // it (immediate match), a few rest deep in the book.
            lob::Price offset = static_cast<lob::Price>(rng.below(21)) - 8;
            msg.price = buy ? mid + offset : mid - offset;
            msg.order_type = (rng.below(100) < 3) ? 1 : 0;  // 3% market orders
            msg.quantity = 1 + static_cast<lob::Quantity>(rng.below(500));
        }
        batch.push_back(msg);
        if (batch.size() == (1 << 16)) flush();
    }
    flush();
    std::fclose(out);
    std::fprintf(stderr, "wrote %llu messages to %s\n",
                 static_cast<unsigned long long>(count), argv[3]);
    return 0;
}
