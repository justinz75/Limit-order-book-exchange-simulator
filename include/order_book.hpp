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

        //returns the spread between the best ask and best bid prices in the order book, or std::nullopt if either side is empty
        std::optional<Price> spread() const;
        std::optional<double> mid_price() const;

        bool cancel_order(OrderId order_id);

        Quantity quantity_at_price(Side side, Price price) const;

        //a single price level of the book: the price and the total quantity resting at it
        struct PriceLevelSnapshot {
            Price price;
            Quantity quantity;
        };

        //return the price levels nearest the top of the book, at most 'levels' of them, best first
        std::vector<PriceLevelSnapshot> bid_depth(std::size_t levels) const;
        std::vector<PriceLevelSnapshot> ask_depth(std::size_t levels) const;

    private:
        //the order book is represented as two separate maps: one for asks and one for bids.
        AskBook asks_;
        BidBook bids_;

        //match_buy and match_sell are private member functions that handle the matching of incoming orders with existing orders in the order book.
        std::vector<Trade> match_buy(Order& incoming);
        std::vector<Trade> match_sell(Order& incoming);

        void add_to_book(const Order& order);

        struct OrderLocation {
            Side side;
            Price price;
            std::list<Order>::iterator order_iterator;
        };

        std::unordered_map<OrderId, OrderLocation> order_index_;
};
