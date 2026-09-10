#include "order_book.hpp"
#include "order_index.hpp"
#include "test_runner.hpp"

#include <iostream>
#include <map>
#include <random>

int main() {
    TestRunner runner;

    OrderBook order_book;

    //test 1: submit a buy order and check best bid
    Order buy_order{1, 100, Side::Buy, OrderType::Limit, 100, 10, 10};
    auto trades = order_book.submit(buy_order);
    runner.check(trades.empty(), "a buy order with an empty book to trade against does not trade");
    runner.check(order_book.best_bid().has_value() && order_book.best_bid().value() == 100,
                 "the resting buy order becomes the best bid");
    runner.check(!order_book.best_ask().has_value(), "the ask side is still empty");

    //test 2: submit a sell order that matches the buy order
    Order sell_order{2, 200, Side::Sell, OrderType::Limit, 90, 5, 20};
    trades = order_book.submit(sell_order);
    runner.check(trades.size() == 1, "a sell that crosses the bid produces one trade");
    runner.check(trades.size() == 1 && trades[0].quantity == 5,
                 "the trade is for the smaller of the two quantities");
    runner.check(trades.size() == 1 && trades[0].price == 100,
                 "the trade happens at the resting price, so the incoming order is improved");
    runner.check(order_book.best_bid().has_value() && order_book.best_bid().value() == 100,
                 "the partly filled buy order stays at the front of the book");
    runner.check(!order_book.best_ask().has_value(), "a fully filled sell order does not rest");

    //test 3: submit another sell order that partially matches the remaining buy order
    Order sell_order2{3, 300, Side::Sell, OrderType::Limit, 90, 10, 30};
    trades = order_book.submit(sell_order2);
    runner.check(trades.size() == 1, "the second sell trades against what is left of the buy order");
    runner.check(trades.size() == 1 && trades[0].quantity == 5,
                 "only the 5 remaining on the buy order can trade");
    runner.check(!order_book.best_bid().has_value(), "the exhausted buy order leaves the book");
    runner.check(order_book.best_ask().has_value() && order_book.best_ask().value() == 90,
                 "what the sell order could not fill rests as the best ask");

    //test 4: cancel a resting buy order
    Order cancel_buy_order{10, 100, Side::Buy, OrderType::Limit, 100, 10, 40};

    order_book.submit(cancel_buy_order);

    runner.check(order_book.best_bid().has_value() && order_book.best_bid().value() == 100,
                 "the unfilled part of the buy order rests as the best bid");

    bool cancelled = order_book.cancel_order(10);

    runner.check(cancelled, "a resting order can be cancelled");
    runner.check(!order_book.cancel_order(9999), "cancelling an unknown order id fails");
    runner.check(!order_book.best_bid().has_value(), "cancelling the only bid empties that side");

    //test 5: inspect prices and quantities at price levels
    OrderBook market_data_book;
    runner.check(!market_data_book.best_bid().has_value(), "an empty book has no best bid");
    runner.check(!market_data_book.best_ask().has_value(), "an empty book has no best ask");
    runner.check(!market_data_book.spread().has_value(), "an empty book has no spread");
    runner.check(!market_data_book.mid_price().has_value(), "an empty book has no mid price");
    runner.check(market_data_book.quantity_at_price(Side::Buy, 100) == 0,
                 "an empty book has no quantity at any bid price");
    runner.check(market_data_book.quantity_at_price(Side::Sell, 110) == 0,
                 "an empty book has no quantity at any ask price");

    market_data_book.submit(Order{11, 101, Side::Buy, OrderType::Limit, 100, 10, 50});
    market_data_book.submit(Order{12, 102, Side::Buy, OrderType::Limit, 105, 7, 60});
    market_data_book.submit(Order{13, 103, Side::Buy, OrderType::Limit, 105, 3, 70});
    market_data_book.submit(Order{14, 104, Side::Sell, OrderType::Limit, 110, 4, 80});
    market_data_book.submit(Order{15, 105, Side::Sell, OrderType::Limit, 110, 6, 90});
    market_data_book.submit(Order{16, 106, Side::Sell, OrderType::Limit, 115, 8, 100});

    runner.check(market_data_book.best_bid().has_value() && market_data_book.best_bid().value() == 105,
                 "the best bid is the highest bid price, not the lowest");
    runner.check(market_data_book.best_ask().has_value() && market_data_book.best_ask().value() == 110,
                 "the best ask is the lowest ask price");
    runner.check(market_data_book.spread().has_value() && market_data_book.spread().value() == 5,
                 "the spread is the distance between the two best prices");
    runner.check(market_data_book.mid_price().has_value() && market_data_book.mid_price().value() == 107.5,
                 "the mid price sits halfway between them");
    runner.check(market_data_book.quantity_at_price(Side::Buy, 105) == 10,
                 "two orders at the same bid price add up");
    runner.check(market_data_book.quantity_at_price(Side::Buy, 100) == 10,
                 "the quantity behind the touch is reported too");
    runner.check(market_data_book.quantity_at_price(Side::Sell, 110) == 10,
                 "two orders at the same ask price add up");
    runner.check(market_data_book.quantity_at_price(Side::Sell, 115) == 8,
                 "a single order at an ask price is reported on its own");

    //test 6: read the depth of each side of the book
    auto bid_levels = market_data_book.bid_depth(5);
    runner.check(bid_levels.size() == 2, "the bid side has two price levels");
    runner.check(bid_levels.size() == 2 && bid_levels[0].price == 105 && bid_levels[0].quantity == 10,
                 "bid depth starts at the best price");
    runner.check(bid_levels.size() == 2 && bid_levels[1].price == 100 && bid_levels[1].quantity == 10,
                 "bid depth then works down away from the touch");

    auto ask_levels = market_data_book.ask_depth(5);
    runner.check(ask_levels.size() == 2, "the ask side has two price levels");
    runner.check(ask_levels.size() == 2 && ask_levels[0].price == 110 && ask_levels[0].quantity == 10,
                 "ask depth starts at the best price");
    runner.check(ask_levels.size() == 2 && ask_levels[1].price == 115 && ask_levels[1].quantity == 8,
                 "ask depth then works up away from the touch");

    runner.check(market_data_book.bid_depth(1).size() == 1,
                 "asking for one bid level returns only one");
    runner.check(market_data_book.bid_depth(1).at(0).price == 105,
                 "the one bid level returned is the best one");
    runner.check(market_data_book.ask_depth(1).size() == 1,
                 "asking for one ask level returns only one");
    runner.check(market_data_book.ask_depth(1).at(0).price == 110,
                 "the one ask level returned is the best one");

    OrderBook empty_book;
    runner.check(empty_book.bid_depth(5).empty(), "an empty book has no bid depth");
    runner.check(empty_book.ask_depth(5).empty(), "an empty book has no ask depth");

    //test 7: market orders take the best available price and never rest in the book
    OrderBook market_order_book;
    market_order_book.submit(Order{20, 200, Side::Sell, OrderType::Limit, 110, 5, 110});
    market_order_book.submit(Order{21, 201, Side::Sell, OrderType::Limit, 112, 5, 120});

    auto market_trades = market_order_book.submit(
        Order{22, 202, Side::Buy, OrderType::Market, 0, 8, 130}
    );
    runner.check(market_trades.size() == 2, "a market buy keeps going until it is filled");
    runner.check(market_trades.size() == 2 && market_trades[0].price == 110 && market_trades[0].quantity == 5,
                 "the market buy clears the cheapest ask first");
    runner.check(market_trades.size() == 2 && market_trades[1].price == 112 && market_trades[1].quantity == 3,
                 "it then pays the next price up for the rest");
    runner.check(market_order_book.best_ask().has_value() && market_order_book.best_ask().value() == 112,
                 "the swept level is gone and the next one is the new best ask");
    runner.check(market_order_book.quantity_at_price(Side::Sell, 112) == 2,
                 "what the market buy did not take is still resting");

    market_order_book.submit(Order{23, 203, Side::Buy, OrderType::Market, 0, 100, 140});
    runner.check(!market_order_book.best_ask().has_value(), "an oversized market buy empties the ask side");
    runner.check(!market_order_book.best_bid().has_value(),
                 "the part it could not fill is discarded rather than resting as a bid");

    runner.check(market_order_book.submit(
                     Order{24, 204, Side::Sell, OrderType::Market, 0, 5, 150}
                 ).empty(),
                 "a market order against an empty book trades nothing");
    runner.check(!market_order_book.best_bid().has_value() && !market_order_book.best_ask().has_value(),
                 "and leaves the book empty behind it");

    market_order_book.submit(Order{25, 205, Side::Sell, OrderType::Limit, 110, 5, 160});
    auto limit_trades = market_order_book.submit(
        Order{26, 206, Side::Buy, OrderType::Limit, 105, 5, 170}
    );
    runner.check(limit_trades.empty(), "a limit order still refuses to pay more than its own price");
    runner.check(market_order_book.best_bid().has_value() && market_order_book.best_bid().value() == 105,
                 "and rests instead of crossing");

    //test 8: the order id index is a hand written hash table, so it is checked against a std::map asked
    //to do exactly the same work. the ids are drawn from a range far smaller than the number of steps so
    //that the same id is inserted, erased and reinserted constantly, which is what shakes out mistakes in
    //the removal path
    OrderIndex<std::size_t> index;
    std::map<OrderId, std::size_t> reference;
    std::mt19937_64 rng(12345);
    std::uniform_int_distribution<OrderId> id_distribution(1, 4000);

    bool erase_agreed = true;
    bool find_agreed = true;

    for (std::size_t step = 0; step < 200000; ++step) {
        OrderId id = id_distribution(rng);

        if (step % 3 == 0) {
            bool erased = index.erase(id);
            bool reference_erased = reference.erase(id) > 0;

            if (erased != reference_erased) {
                erase_agreed = false;
            }
        } else {
            index.insert(id, step);
            reference[id] = step;
        }

        const std::size_t* found = index.find(id);
        auto reference_found = reference.find(id);

        if ((found == nullptr) != (reference_found == reference.end())) {
            find_agreed = false;
        } else if (found != nullptr && *found != reference_found->second) {
            find_agreed = false;
        }
    }

    runner.check(erase_agreed, "the index agrees with a map about whether there was anything to erase");
    runner.check(find_agreed, "the index agrees with a map about what is present after every step");
    runner.check(index.size() == reference.size(), "and holds the same number of entries at the end");

    bool every_entry_found = true;
    for (const auto& entry : reference) {
        const std::size_t* found = index.find(entry.first);

        if (found == nullptr || *found != entry.second) {
            every_entry_found = false;
        }
    }

    runner.check(every_entry_found, "and every surviving entry is still reachable with the right value");

    //ids that were never inserted must not be found, however full the table is
    bool absent_stay_absent = true;
    for (OrderId id = 100000; id < 100100; ++id) {
        if (index.find(id) != nullptr) {
            absent_stay_absent = false;
        }
    }

    runner.check(absent_stay_absent, "an id that was never inserted is not found");
    runner.check(!index.erase(999999), "and erasing one that is not there reports that there was nothing");

    //emptying the table completely should leave nothing behind that a lookup can trip over
    for (const auto& entry : reference) {
        index.erase(entry.first);
    }

    runner.check(index.size() == 0, "erasing everything leaves the index empty");

    bool empty_finds_nothing = true;
    for (OrderId id = 1; id <= 4000; ++id) {
        if (index.find(id) != nullptr) {
            empty_finds_nothing = false;
        }
    }

    runner.check(empty_finds_nothing, "and nothing can be found in it afterwards");

    //test 9: immediate or cancel takes what it can and does not wait around for the rest
    OrderBook ioc_book;
    ioc_book.submit(Order{30, 300, Side::Sell, OrderType::Limit, 110, 5, 10});

    auto ioc_trades = ioc_book.submit(
        Order{31, 301, Side::Buy, OrderType::Limit, 115, 10, 20, TimeInForce::ImmediateOrCancel}
    );

    runner.check(ioc_trades.size() == 1, "an immediate or cancel order trades what is available");
    runner.check(ioc_trades.size() == 1 && ioc_trades[0].quantity == 5,
                 "for as much as the book could give it");
    runner.check(!ioc_book.best_bid().has_value(),
                 "and the part it could not fill is discarded rather than resting");
    runner.check(!ioc_book.best_ask().has_value(), "while the order it traded against is gone");

    //test 10: fill or kill either trades in full or does not trade at all
    OrderBook fok_book;
    fok_book.submit(Order{40, 400, Side::Sell, OrderType::Limit, 110, 3, 10});
    fok_book.submit(Order{41, 401, Side::Sell, OrderType::Limit, 112, 4, 20});

    runner.check(fok_book.fillable_quantity(
                     Order{42, 402, Side::Buy, OrderType::Limit, 112, 7, 30}
                 ) == 7,
                 "the book can report that it could fill an order across two levels");
    runner.check(fok_book.fillable_quantity(
                     Order{43, 403, Side::Buy, OrderType::Limit, 110, 7, 40}
                 ) == 3,
                 "and that a price that only reaches the first level would fill less");

    auto rejected = fok_book.submit(
        Order{44, 404, Side::Buy, OrderType::Limit, 110, 7, 50, TimeInForce::FillOrKill}
    );

    runner.check(rejected.empty(), "a fill or kill order that cannot be filled in full does not trade");
    runner.check(fok_book.quantity_at_price(Side::Sell, 110) == 3,
                 "and leaves the orders it would have traded against untouched");
    runner.check(fok_book.quantity_at_price(Side::Sell, 112) == 4, "on both levels");
    runner.check(!fok_book.best_bid().has_value(), "and does not rest either");

    auto accepted = fok_book.submit(
        Order{45, 405, Side::Buy, OrderType::Limit, 112, 7, 60, TimeInForce::FillOrKill}
    );

    runner.check(accepted.size() == 2, "one that can be filled in full sweeps both levels");
    runner.check(!fok_book.best_ask().has_value(), "leaving the ask side empty");

    //test 11: a trader is not allowed to trade with themselves
    OrderBook stp_book;
    stp_book.submit(Order{50, 500, Side::Sell, OrderType::Limit, 110, 5, 10});
    stp_book.submit(Order{51, 600, Side::Sell, OrderType::Limit, 110, 5, 20});

    auto stp_trades = stp_book.submit(Order{52, 500, Side::Buy, OrderType::Limit, 115, 8, 30});

    runner.check(stp_book.self_trade_cancellations() == 1,
                 "the resting order belonging to the incoming trader is pulled");
    runner.check(stp_trades.size() == 1, "so only the other trader's order is traded against");
    runner.check(stp_trades.size() == 1 && stp_trades[0].seller_id == 600,
                 "and the trade is with that other trader");
    runner.check(stp_trades.size() == 1 && stp_trades[0].quantity == 5,
                 "for what that trader had resting");
    runner.check(!stp_book.best_ask().has_value(), "the ask side is emptied between the two of them");
    runner.check(stp_book.best_bid().has_value() && stp_book.best_bid().value() == 115,
                 "and what the incoming order could not fill rests as a bid");
    runner.check(stp_book.quantity_at_price(Side::Buy, 115) == 3, "for the quantity left over");

    //a fill or kill order must not count its own resting quantity as something it could trade against,
    //or it would be accepted and then find there was nothing to fill it after all
    OrderBook stp_fok_book;
    stp_fok_book.submit(Order{53, 700, Side::Sell, OrderType::Limit, 110, 5, 10});

    runner.check(stp_fok_book.fillable_quantity(
                     Order{54, 700, Side::Buy, OrderType::Limit, 115, 5, 20}
                 ) == 0,
                 "quantity resting under the same trader counts for nothing");

    auto own_fok = stp_fok_book.submit(
        Order{55, 700, Side::Buy, OrderType::Limit, 115, 5, 30, TimeInForce::FillOrKill}
    );

    runner.check(own_fok.empty(), "so a fill or kill order facing only its own quantity does not trade");
    runner.check(stp_fok_book.quantity_at_price(Side::Sell, 110) == 5,
                 "and its own resting order is left alone, since the order was rejected before matching");

    //test 12: modifying a resting order
    OrderBook modify_book;
    modify_book.submit(Order{60, 800, Side::Buy, OrderType::Limit, 100, 10, 10});
    modify_book.submit(Order{61, 801, Side::Buy, OrderType::Limit, 100, 10, 20});

    auto reduced = modify_book.modify_order(60, 100, 4);

    runner.check(reduced.has_value(), "a resting order can be modified");
    runner.check(modify_book.quantity_at_price(Side::Buy, 100) == 14,
                 "reducing the quantity takes it off the level total");

    auto after_reduce = modify_book.submit(Order{62, 802, Side::Sell, OrderType::Limit, 100, 4, 30});

    runner.check(after_reduce.size() == 1 && after_reduce[0].resting_order_id == 60,
                 "and the reduced order keeps its place at the front of the queue");

    //raising the quantity is a different matter, because the extra was never queued
    OrderBook priority_book;
    priority_book.submit(Order{70, 810, Side::Buy, OrderType::Limit, 100, 10, 10});
    priority_book.submit(Order{71, 811, Side::Buy, OrderType::Limit, 100, 10, 20});
    priority_book.modify_order(70, 100, 20);

    auto after_raise = priority_book.submit(Order{72, 812, Side::Sell, OrderType::Limit, 100, 10, 30});

    runner.check(after_raise.size() == 1 && after_raise[0].resting_order_id == 71,
                 "raising the quantity sends the order to the back, so the one behind trades first");

    //a new price also gives up the order's place, and moves it to the level it now belongs on
    OrderBook reprice_book;
    reprice_book.submit(Order{80, 820, Side::Buy, OrderType::Limit, 100, 10, 10});
    reprice_book.modify_order(80, 105, 10);

    runner.check(reprice_book.best_bid().has_value() && reprice_book.best_bid().value() == 105,
                 "a repriced order shows up at its new price");
    runner.check(reprice_book.quantity_at_price(Side::Buy, 100) == 0, "and not at the old one");

    //and if the new price crosses, the modify trades on the way back in
    OrderBook crossing_book;
    crossing_book.submit(Order{90, 830, Side::Sell, OrderType::Limit, 110, 5, 10});
    crossing_book.submit(Order{91, 831, Side::Buy, OrderType::Limit, 100, 5, 20});

    auto crossed = crossing_book.modify_order(91, 110, 5);

    runner.check(crossed.has_value() && crossed.value().size() == 1,
                 "modifying a bid up onto the ask trades immediately");
    runner.check(!crossing_book.best_ask().has_value() && !crossing_book.best_bid().has_value(),
                 "and clears both sides");

    //modifying down to nothing is a cancel
    OrderBook zero_book;
    zero_book.submit(Order{95, 840, Side::Buy, OrderType::Limit, 100, 10, 10});

    runner.check(zero_book.modify_order(95, 100, 0).has_value(), "an order can be modified to nothing");
    runner.check(!zero_book.best_bid().has_value(), "which removes it from the book");
    runner.check(!zero_book.modify_order(95, 100, 5).has_value(),
                 "and it cannot be modified again afterwards");
    runner.check(!zero_book.modify_order(123456, 100, 5).has_value(),
                 "modifying an order that was never submitted reports nothing");

    //test 13: sizing the index and the book up front
    OrderIndex<std::size_t> sized_index;
    sized_index.reserve(10000);
    std::size_t sized_capacity = sized_index.capacity();

    for (OrderId id = 1; id <= 10000; ++id) {
        sized_index.insert(id, static_cast<std::size_t>(id));
    }

    runner.check(sized_index.capacity() == sized_capacity,
                 "an index sized up front does not grow while it fills to that size");

    bool sized_all_found = true;
    for (OrderId id = 1; id <= 10000; ++id) {
        const std::size_t* found = sized_index.find(id);

        if (found == nullptr || *found != id) {
            sized_all_found = false;
        }
    }

    runner.check(sized_all_found, "and every entry in it can still be found");

    //sizing a table that already has entries in it has to keep all of them
    OrderIndex<std::size_t> resized_index;
    for (OrderId id = 1; id <= 100; ++id) {
        resized_index.insert(id, static_cast<std::size_t>(id) * 7);
    }
    resized_index.reserve(100000);

    bool all_kept = resized_index.size() == 100;
    for (OrderId id = 1; id <= 100; ++id) {
        const std::size_t* found = resized_index.find(id);

        if (found == nullptr || *found != id * 7) {
            all_kept = false;
        }
    }

    runner.check(all_kept, "sizing an index that already holds entries keeps every one of them");

    OrderBook sized_book;
    sized_book.reserve(1000);

    for (std::size_t i = 0; i < 1000; ++i) {
        sized_book.submit(Order{static_cast<OrderId>(5000 + i), 900, Side::Buy, OrderType::Limit,
                                100 - static_cast<Price>(i % 10), 1, static_cast<Timestamp>(i)});
    }

    runner.check(sized_book.resting_order_count() == 1000, "a book sized up front holds what is put in it");
    runner.check(sized_book.cancel_order(5000) && !sized_book.cancel_order(5000),
                 "and cancelling in it behaves as it always does");

    //sizing a book that already has orders resting in it has to leave them exactly as they were
    OrderBook resized_book;
    resized_book.submit(Order{7000, 910, Side::Buy, OrderType::Limit, 100, 5, 1});
    resized_book.submit(Order{7001, 911, Side::Sell, OrderType::Limit, 105, 3, 2});
    resized_book.reserve(50000);

    runner.check(resized_book.resting_order_count() == 2, "sizing a book with orders already in it keeps them");
    runner.check(resized_book.quantity_at_price(Side::Buy, 100) == 5 &&
                 resized_book.quantity_at_price(Side::Sell, 105) == 3,
                 "at the prices and quantities they had");
    runner.check(resized_book.cancel_order(7001) && resized_book.best_bid().value() == 100,
                 "and they can still be cancelled and read afterwards");

    return runner.summary("order book tests");
}
