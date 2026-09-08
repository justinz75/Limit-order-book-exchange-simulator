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

    return runner.summary("order book tests");
}
