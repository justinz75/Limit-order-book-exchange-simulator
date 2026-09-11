#include "order_book.hpp"

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <iomanip>
#include <iostream>
#include <random>
#include <string>
#include <vector>

#if defined(_MSC_VER)
#include <intrin.h>
#else
#include <x86intrin.h>
#endif

//how many orders each measurement pushes through the book
constexpr std::size_t order_count = 1000000;

//each measurement is repeated this many times and the median is reported
constexpr int repetitions = 5;

//the orders are spread over this many ticks either side of 100
constexpr Price price_range = 20;

//fewer iterations for depth, which is slower per call than the rest
constexpr std::size_t depth_iterations = 20000;

//builds the orders up front so generating them is not timed
std::vector<Order> build_orders(std::uint64_t seed) {
    std::mt19937_64 rng(seed);
    std::uniform_int_distribution<int> side_distribution(0, 1);
    //the two sides overlap so that some of the orders cross and trade
    std::uniform_int_distribution<int> offset_distribution(-3, static_cast<int>(price_range));
    std::uniform_int_distribution<Quantity> quantity_distribution(1, 20);

    std::vector<Order> orders;
    orders.reserve(order_count);

    for (std::size_t i = 0; i < order_count; ++i) {
        Side side = Side::Buy;
        Price price = 100 - offset_distribution(rng);

        if (side_distribution(rng) == 1) {
            side = Side::Sell;
            price = 100 + offset_distribution(rng);
        }

        orders.push_back(Order{
            static_cast<OrderId>(i + 1),
            static_cast<TraderId>(i % 1000),
            side,
            OrderType::Limit,
            price,
            quantity_distribution(rng),
            static_cast<Timestamp>(i + 1)
        });
    }

    return orders;
}

//fills a book with bids that cannot trade, so they are all left resting
void fill_with_resting_orders(OrderBook& book, std::vector<OrderId>& ids) {
    ids.clear();
    ids.reserve(order_count);

    for (std::size_t i = 0; i < order_count; ++i) {
        OrderId id = static_cast<OrderId>(i + 1);

        book.submit(Order{
            id,
            static_cast<TraderId>(i % 1000),
            Side::Buy,
            OrderType::Limit,
            100 - static_cast<Price>(i % price_range),
            10,
            static_cast<Timestamp>(i + 1)
        });

        ids.push_back(id);
    }
}

//returns the middle timing of the repetitions
double median(std::vector<double> timings) {
    std::sort(timings.begin(), timings.end());
    return timings[timings.size() / 2];
}

//prints one result line, with the median and the range of the timings
void report(const std::string& name, std::size_t operations, std::vector<double> timings) {
    double middle = median(timings);
    std::sort(timings.begin(), timings.end());

    double per_second = operations / middle;
    double nanoseconds_each = (middle * 1e9) / operations;
    double fastest = (timings.front() * 1e9) / operations;
    double slowest = (timings.back() * 1e9) / operations;

    std::cout << std::left << std::setw(20) << name
              << std::right << std::fixed
              << std::setprecision(2) << std::setw(10) << (per_second / 1e6) << " M ops/s"
              << std::setprecision(1) << std::setw(9) << nanoseconds_each << " ns/op"
              << "   (" << fastest << " to " << slowest << ")\n";
}

//reads the processor timestamp counter, which is finer than steady_clock on windows
std::uint64_t cycles_now() {
    //fences stop the processor moving the timed work outside the reading
    _mm_lfence();
    std::uint64_t cycles = __rdtsc();
    _mm_lfence();
    return cycles;
}

//how many counter ticks make a nanosecond, measured against steady_clock
double cycles_per_nanosecond() {
    auto wall_start = std::chrono::steady_clock::now();
    std::uint64_t cycles_start = cycles_now();

    while (std::chrono::steady_clock::now() - wall_start < std::chrono::milliseconds(250)) {
    }

    std::uint64_t cycles_end = cycles_now();
    auto wall_end = std::chrono::steady_clock::now();

    double nanoseconds = std::chrono::duration<double, std::nano>(wall_end - wall_start).count();
    return static_cast<double>(cycles_end - cycles_start) / nanoseconds;
}

//prints the percentiles of a set of individually timed operations
void report_latency(const std::string& name, std::vector<std::uint64_t>& cycles, double per_nanosecond) {
    std::sort(cycles.begin(), cycles.end());

    auto at = [&](double fraction) {
        std::size_t index = static_cast<std::size_t>(fraction * static_cast<double>(cycles.size() - 1));
        return static_cast<double>(cycles[index]) / per_nanosecond;
    };

    std::cout << std::left << std::setw(20) << name
              << std::right << std::fixed << std::setprecision(0)
              << std::setw(9) << at(0.50)
              << std::setw(9) << at(0.90)
              << std::setw(9) << at(0.99)
              << std::setw(10) << at(0.999)
              << std::setw(12) << at(1.0) << "\n";
}

int main() {
    std::cout << "Order book benchmark\n";
    std::cout << "--------------------\n";
    std::cout << "Orders per measurement: " << order_count << "\n";
    std::cout << "Repetitions: " << repetitions << ", median reported with the range in brackets\n\n";

    std::vector<Order> orders = build_orders(7);

    //time submitting the orders into a book that grows as it goes
    std::vector<double> submit_timings;
    std::size_t trades_made = 0;

    for (int repetition = 0; repetition < repetitions; ++repetition) {
        OrderBook book;
        std::size_t trades = 0;

        auto start = std::chrono::steady_clock::now();
        for (const Order& order : orders) {
            trades += book.submit(order).size();
        }
        auto end = std::chrono::steady_clock::now();

        submit_timings.push_back(std::chrono::duration<double>(end - start).count());
        trades_made = trades;
    }

    report("submit", order_count, submit_timings);

    //the same orders into books sized up front, so they never have to grow
    std::vector<double> sized_timings;

    for (int repetition = 0; repetition < repetitions; ++repetition) {
        OrderBook book;
        book.reserve(order_count);

        auto start = std::chrono::steady_clock::now();
        for (const Order& order : orders) {
            book.submit(order);
        }
        auto end = std::chrono::steady_clock::now();

        sized_timings.push_back(std::chrono::duration<double>(end - start).count());
    }

    report("submit, sized first", order_count, sized_timings);

    //time cancelling every order in a book full of resting orders
    std::vector<double> cancel_timings;
    std::size_t cancelled = 0;

    for (int repetition = 0; repetition < repetitions; ++repetition) {
        OrderBook book;
        std::vector<OrderId> ids;
        fill_with_resting_orders(book, ids);

        std::size_t removed = 0;

        auto start = std::chrono::steady_clock::now();
        for (OrderId id : ids) {
            if (book.cancel_order(id)) {
                removed++;
            }
        }
        auto end = std::chrono::steady_clock::now();

        cancel_timings.push_back(std::chrono::duration<double>(end - start).count());
        cancelled = removed;
    }

    report("cancel", order_count, cancel_timings);

    //time reading the top of the book and its depth
    OrderBook read_book;
    std::vector<OrderId> read_ids;
    fill_with_resting_orders(read_book, read_ids);

    std::vector<double> quote_timings;
    std::vector<double> depth_timings;
    Price checksum = 0;
    Quantity depth_checksum = 0;

    for (int repetition = 0; repetition < repetitions; ++repetition) {
        auto quote_start = std::chrono::steady_clock::now();
        for (std::size_t i = 0; i < order_count; ++i) {
            auto bid = read_book.best_bid();

            if (bid.has_value()) {
                checksum += bid.value();
            }
        }
        auto quote_end = std::chrono::steady_clock::now();

        quote_timings.push_back(std::chrono::duration<double>(quote_end - quote_start).count());

        auto depth_start = std::chrono::steady_clock::now();
        for (std::size_t i = 0; i < depth_iterations; ++i) {
            for (const auto& level : read_book.bid_depth(5)) {
                depth_checksum += level.quantity;
            }
        }
        auto depth_end = std::chrono::steady_clock::now();

        depth_timings.push_back(std::chrono::duration<double>(depth_end - depth_start).count());
    }

    report("best bid", order_count, quote_timings);
    report("bid depth, 5 deep", depth_iterations, depth_timings);

    //latency: time every operation on its own so the slow ones show up
    std::cout << "\nLatency per operation, each timed on its own\n";
    std::cout << "--------------------------------------------\n";

    double per_nanosecond = cycles_per_nanosecond();

    //cost of reading the counter twice, which is included in every figure below
    std::vector<std::uint64_t> overhead;
    overhead.reserve(100000);

    for (int i = 0; i < 100000; ++i) {
        std::uint64_t start = cycles_now();
        std::uint64_t end = cycles_now();
        overhead.push_back(end - start);
    }

    std::sort(overhead.begin(), overhead.end());

    std::cout << "Counter rate: " << std::setprecision(2) << per_nanosecond << " ticks per ns\n";
    std::cout << "Reading the counter costs about " << std::setprecision(0)
              << (static_cast<double>(overhead[overhead.size() / 2]) / per_nanosecond)
              << " ns, and that is included in every figure below\n\n";

    std::cout << std::left << std::setw(20) << "" << std::right
              << std::setw(9) << "p50"
              << std::setw(9) << "p90"
              << std::setw(9) << "p99"
              << std::setw(10) << "p99.9"
              << std::setw(12) << "max" << "   (ns)\n";

    //time the growing and sized books twice each, alternating so neither always runs first
    std::vector<std::uint64_t> growing_cycles;
    std::vector<std::uint64_t> sized_cycles;
    growing_cycles.reserve(2 * order_count);
    sized_cycles.reserve(2 * order_count);

    auto time_submits = [&](bool sized, std::vector<std::uint64_t>& out) {
        OrderBook book;

        if (sized) {
            book.reserve(order_count);
        }

        for (const Order& order : orders) {
            std::uint64_t start = cycles_now();
            book.submit(order);
            std::uint64_t end = cycles_now();
            out.push_back(end - start);
        }
    };

    time_submits(false, growing_cycles);
    time_submits(true, sized_cycles);
    time_submits(true, sized_cycles);
    time_submits(false, growing_cycles);

    //the second half of each run, when both books are full size
    auto second_half = [&](const std::vector<std::uint64_t>& cycles) {
        std::vector<std::uint64_t> part;

        for (std::size_t run = 0; run < 2; ++run) {
            auto run_start = cycles.begin() + static_cast<std::ptrdiff_t>(run * order_count);
            part.insert(part.end(),
                        run_start + static_cast<std::ptrdiff_t>(order_count / 2),
                        run_start + static_cast<std::ptrdiff_t>(order_count));
        }

        return part;
    };

    std::vector<std::uint64_t> growing_late = second_half(growing_cycles);
    std::vector<std::uint64_t> sized_late = second_half(sized_cycles);

    report_latency("submit", growing_cycles, per_nanosecond);
    report_latency("submit, sized first", sized_cycles, per_nanosecond);
    report_latency("submit, second half", growing_late, per_nanosecond);
    report_latency("sized, second half", sized_late, per_nanosecond);

    std::vector<std::uint64_t> cancel_cycles;
    cancel_cycles.reserve(order_count);

    {
        OrderBook book;
        std::vector<OrderId> ids;
        fill_with_resting_orders(book, ids);

        for (OrderId id : ids) {
            std::uint64_t start = cycles_now();
            book.cancel_order(id);
            std::uint64_t end = cycles_now();
            cancel_cycles.push_back(end - start);
        }
    }

    report_latency("cancel", cancel_cycles, per_nanosecond);

    //printed so the compiler cannot optimise the work above away
    std::cout << "\nTrades made: " << trades_made
              << " (" << std::setprecision(1) << (100.0 * trades_made / order_count)
              << "% of orders traded)\n";
    std::cout << "Orders cancelled: " << cancelled << "\n";
    std::cout << "Resting after the read book was filled: " << read_book.resting_order_count() << "\n";
    std::cout << "Checksums: " << checksum << " " << depth_checksum << "\n";

    return 0;
}
