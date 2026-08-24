#pragma once

#include "order.hpp"
#include "trade.hpp"
#include "price.hpp"

#include <map>
#include <vector>
#include <optional>

#include <unordered_map>
#include <list>

class OrderBook {
    public:
        //submits an order to the order book and returns a vector of trades that occurred as a result of the submission.
        std::vector<Trade> submit(Order order);

        //returns the best ask price in the order book, or std::nullopt if there are no asks
        std::optional<Price> best_ask() const;
        std::optional<Price> best_bid() const;

        bool cancel_order(OrderId order_id);

    private:
        //the order book is represented as two separate maps: one for asks and one for bids.
        AskBook asks_;
        BidBook bids_;

        //match_buy and match_sell are private member functions that handle the matching of incoming orders with existing orders in the order book.
        std::vector<Trade> match_buy(Order& incoming);
        std::vector<Trade> match_sell(Order& incoming);

        void add_to_book(const Order& order);

    //OrderLocation is a struct that holds information about an order's location in the order book, including its side (buy or sell), price, and an iterator to its position in the list of orders at that price level.
    struct OrderLocation {
        Side side;
        Price price;
        std::list<Order>::iterator order_iterator;

};

//order_map_ is an unordered map that allows for quick lookup of orders by their ID. It maps an OrderId to an OrderLocation, which contains information about the order's side, price, and its position in the order book.
std::unordered_map<OrderId, OrderLocation> order_index_;
};

