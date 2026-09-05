#include "order_book.hpp"
#include <cassert>
#include <iostream>

int main() {
    OrderBook order_book;

    //test 1: submit a buy order and check best bid
    Order buy_order{1, 100, Side::Buy, OrderType::Limit, 100, 10, 10};
    auto trades = order_book.submit(buy_order);
    assert(trades.empty());
    assert(order_book.best_bid().has_value() && order_book.best_bid().value() == 100);
    assert(!order_book.best_ask().has_value());

    //test 2: submit a sell order that matches the buy order
    Order sell_order{2, 200, Side::Sell, OrderType::Limit, 90, 5, 20};
    trades = order_book.submit(sell_order);
    assert(trades.size() == 1);
    assert(trades[0].quantity == 5);
    assert(order_book.best_bid().has_value() && order_book.best_bid().value() == 100);
    assert(!order_book.best_ask().has_value());

    //test 3: submit another sell order that partially matches the remaining buy order
    Order sell_order2{3, 300, Side::Sell, OrderType::Limit, 90, 10, 30};
    trades = order_book.submit(sell_order2);
    assert(trades.size() == 1);
    assert(trades[0].quantity == 5); //only 5 remaining from the buy order
    assert(!order_book.best_bid().has_value());
    assert(order_book.best_ask().has_value() && order_book.best_ask().value() == 90);

    //test 4: cancel a resting buy order
    Order cancel_buy_order{10, 100, Side::Buy, OrderType::Limit, 100, 10, 40};

    order_book.submit(cancel_buy_order);

    assert(order_book.best_bid().has_value());
    assert(order_book.best_bid().value() == 100);

    bool cancelled = order_book.cancel_order(10);

    assert(!order_book.cancel_order(9999));
    assert(cancelled);
    assert(!order_book.best_bid().has_value());

    //test 5: inspect prices and quantities at price levels
    OrderBook market_data_book;
    assert(!market_data_book.best_bid().has_value());
    assert(!market_data_book.best_ask().has_value());
    assert(!market_data_book.spread().has_value());
    assert(!market_data_book.mid_price().has_value());
    assert(market_data_book.quantity_at_price(Side::Buy, 100) == 0);
    assert(market_data_book.quantity_at_price(Side::Sell, 110) == 0);

    market_data_book.submit(Order{11, 101, Side::Buy, OrderType::Limit, 100, 10, 50});
    market_data_book.submit(Order{12, 102, Side::Buy, OrderType::Limit, 105, 7, 60});
    market_data_book.submit(Order{13, 103, Side::Buy, OrderType::Limit, 105, 3, 70});
    market_data_book.submit(Order{14, 104, Side::Sell, OrderType::Limit, 110, 4, 80});
    market_data_book.submit(Order{15, 105, Side::Sell, OrderType::Limit, 110, 6, 90});
    market_data_book.submit(Order{16, 106, Side::Sell, OrderType::Limit, 115, 8, 100});

    assert(market_data_book.best_bid().has_value());
    assert(market_data_book.best_bid().value() == 105);
    assert(market_data_book.best_ask().has_value());
    assert(market_data_book.best_ask().value() == 110);
    assert(market_data_book.spread().has_value());
    assert(market_data_book.spread().value() == 5);
    assert(market_data_book.mid_price().has_value());
    assert(market_data_book.mid_price().value() == 107.5);
    assert(market_data_book.quantity_at_price(Side::Buy, 105) == 10);
    assert(market_data_book.quantity_at_price(Side::Buy, 100) == 10);
    assert(market_data_book.quantity_at_price(Side::Sell, 110) == 10);
    assert(market_data_book.quantity_at_price(Side::Sell, 115) == 8);

    //test 6: read the depth of each side of the book
    auto bid_levels = market_data_book.bid_depth(5);
    assert(bid_levels.size() == 2);
    assert(bid_levels[0].price == 105 && bid_levels[0].quantity == 10);
    assert(bid_levels[1].price == 100 && bid_levels[1].quantity == 10);

    auto ask_levels = market_data_book.ask_depth(5);
    assert(ask_levels.size() == 2);
    assert(ask_levels[0].price == 110 && ask_levels[0].quantity == 10);
    assert(ask_levels[1].price == 115 && ask_levels[1].quantity == 8);

    //asking for fewer levels than the book holds returns only the best ones
    assert(market_data_book.bid_depth(1).size() == 1);
    assert(market_data_book.bid_depth(1)[0].price == 105);
    assert(market_data_book.ask_depth(1).size() == 1);
    assert(market_data_book.ask_depth(1)[0].price == 110);

    //an empty book has no depth on either side
    OrderBook empty_book;
    assert(empty_book.bid_depth(5).empty());
    assert(empty_book.ask_depth(5).empty());

    std::cout << "All tests passed" << std::endl;
    return 0;
}