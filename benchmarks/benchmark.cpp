#include "order_book.hpp"

#include <chrono>
#include <cstddef>
#include <iomanip>
#include <iostream>
#include <random>
#include <vector>

//how many orders each measurement pushes through the book
constexpr std::size_t order_count = 1000000;

//the orders are spread over this many ticks, which is what keeps the book to a realistic handful of
//price levels rather than one enormous queue
constexpr Price price_range = 20;

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

//prints one result line, with the rate worked out from the elapsed time
void report(const std::string& name, std::size_t operations, double seconds) {
    double per_second = operations / seconds;
    double nanoseconds_each = (seconds * 1e9) / operations;

    std::cout << std::left << std::setw(22) << name
              << std::right << std::fixed << std::setprecision(2)
              << std::setw(9) << seconds << " s"
              << std::setw(12) << (per_second / 1e6) << " M ops/s"
              << std::setw(10) << nanoseconds_each << " ns/op\n";
}

int main() {
    std::cout << "Order book benchmark\n";
    std::cout << "--------------------\n";
    std::cout << "Orders per measurement: " << order_count << "\n\n";

    std::vector<Order> orders = build_orders(7);

    //submitting orders, which is matching plus resting whatever does not trade
    OrderBook submit_book;
    std::size_t trades_made = 0;

    auto submit_start = std::chrono::steady_clock::now();
    for (const Order& order : orders) {
        trades_made += submit_book.submit(order).size();
    }
    auto submit_end = std::chrono::steady_clock::now();

    double submit_seconds = std::chrono::duration<double>(submit_end - submit_start).count();
    report("submit", order_count, submit_seconds);

    //cancelling, measured against a book that holds nothing but resting orders. the orders are all
    //bids priced below every ask, so none of them trade and all of them are there to be cancelled
    OrderBook cancel_book;
    std::vector<OrderId> resting_ids;
    resting_ids.reserve(order_count);

    for (std::size_t i = 0; i < order_count; ++i) {
        OrderId id = static_cast<OrderId>(i + 1);
        Price price = 100 - static_cast<Price>(i % price_range);

        cancel_book.submit(Order{
            id,
            static_cast<TraderId>(i % 1000),
            Side::Buy,
            OrderType::Limit,
            price,
            10,
            static_cast<Timestamp>(i + 1)
        });

        resting_ids.push_back(id);
    }

    std::size_t cancelled = 0;

    auto cancel_start = std::chrono::steady_clock::now();
    for (OrderId id : resting_ids) {
        if (cancel_book.cancel_order(id)) {
            cancelled++;
        }
    }
    auto cancel_end = std::chrono::steady_clock::now();

    double cancel_seconds = std::chrono::duration<double>(cancel_end - cancel_start).count();
    report("cancel", order_count, cancel_seconds);

    //reading the top of the book, which is what a strategy would be doing between events
    OrderBook quote_book = std::move(submit_book);
    Price checksum = 0;

    auto quote_start = std::chrono::steady_clock::now();
    for (std::size_t i = 0; i < order_count; ++i) {
        auto bid = quote_book.best_bid();
        auto ask = quote_book.best_ask();

        if (bid.has_value()) {
            checksum += bid.value();
        }

        if (ask.has_value()) {
            checksum += ask.value();
        }
    }
    auto quote_end = std::chrono::steady_clock::now();

    double quote_seconds = std::chrono::duration<double>(quote_end - quote_start).count();
    report("best bid and ask", order_count, quote_seconds);

    //printed so that none of the work above can be optimised away as unused
    std::cout << "\nTrades made: " << trades_made
              << " (" << std::setprecision(1) << (100.0 * trades_made / order_count)
              << "% of orders traded)\n";
    std::cout << "Orders cancelled: " << cancelled << "\n";
    std::cout << "Quote checksum: " << checksum << "\n";

    return 0;
}
