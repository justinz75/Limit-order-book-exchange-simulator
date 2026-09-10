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

//each measurement is repeated this many times. one run of this varies by more than the differences
//that are worth measuring, so a single timing says very little
constexpr int repetitions = 5;

//the orders are spread over this many ticks, which is what keeps the book to a realistic handful of
//price levels rather than one enormous queue
constexpr Price price_range = 20;

//the depth query is measured over fewer iterations than the rest, because a version of it that adds the
//quantities up by walking each level takes long enough that a million calls does not finish
constexpr std::size_t depth_iterations = 20000;

//builds the orders up front so that generating them is not counted in the measurement
std::vector<Order> build_orders(std::uint64_t seed) {
    std::mt19937_64 rng(seed);
    std::uniform_int_distribution<int> side_distribution(0, 1);
    //the two sides overlap around the reference price, so a good share of the orders cross and have
    //to be matched rather than simply coming to rest
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

//fills a book with orders that cannot trade against each other, so that every one of them is left
//resting and available to be cancelled or read
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

//the middle timing of the repetitions, which throws away both the unlucky run and the lucky one
double median(std::vector<double> timings) {
    std::sort(timings.begin(), timings.end());
    return timings[timings.size() / 2];
}

//prints one result line, with the rate worked out from the median time and the spread alongside it so
//it is clear how much of a difference is worth reading anything into
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

//std::chrono::steady_clock on windows ticks every 100 ns, which is about as long as a cancel takes, so
//timing one operation with it would read as nothing, one tick or two. the processor's timestamp counter
//ticks a few billion times a second instead. it has to be turned into time using a rate measured
//against the steady clock, and that relies on it ticking at a constant rate whatever the processor's
//clock speed is doing, which is true of any processor from roughly the last fifteen years
std::uint64_t cycles_now() {
    //the fences stop out of order execution from taking the reading early or letting the work being
    //timed drift outside it
    _mm_lfence();
    std::uint64_t cycles = __rdtsc();
    _mm_lfence();
    return cycles;
}

//how many counter ticks make a nanosecond, found by counting them over a stretch long enough that the
//steady clock's coarse ticks stop mattering
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

    //submitting orders, which is matching plus resting whatever does not trade
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

    //cancelling, measured against a book holding nothing but resting orders. filling it is done outside
    //the timed section
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

    //reading the book, which is what a strategy would be doing between events. one book is built and
    //then read repeatedly, since reading does not change it
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

    //latency, which is where the tail lives. every operation is timed on its own here, so that the slow
    //ones show up instead of being averaged away by the million fast ones around them
    std::cout << "\nLatency per operation, each timed on its own\n";
    std::cout << "--------------------------------------------\n";

    double per_nanosecond = cycles_per_nanosecond();

    //the cost of reading the counter twice with nothing in between. it is inside every figure below,
    //and is printed rather than subtracted, since taking it off can push the fastest readings below zero
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

    //the vectors holding the timings are sized up front, so that growing them is not what gets measured
    std::vector<std::uint64_t> submit_cycles;
    submit_cycles.reserve(order_count);

    {
        OrderBook book;

        for (const Order& order : orders) {
            std::uint64_t start = cycles_now();
            book.submit(order);
            std::uint64_t end = cycles_now();
            submit_cycles.push_back(end - start);
        }
    }

    report_latency("submit", submit_cycles, per_nanosecond);

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

    //printed so that none of the work above can be optimised away as unused
    std::cout << "\nTrades made: " << trades_made
              << " (" << std::setprecision(1) << (100.0 * trades_made / order_count)
              << "% of orders traded)\n";
    std::cout << "Orders cancelled: " << cancelled << "\n";
    std::cout << "Resting after the read book was filled: " << read_book.resting_order_count() << "\n";
    std::cout << "Checksums: " << checksum << " " << depth_checksum << "\n";

    return 0;
}
