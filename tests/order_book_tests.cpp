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

    assert(cancelled);
    assert(!order_book.best_bid().has_value());

    std::cout << "All tests passed" << std::endl;
    return 0;
}