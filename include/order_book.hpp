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

    //PriceLevelSnapshot is a struct that holds information about a price level in the order book, including the price and the total quantity of orders at that price level.
    struct PriceLevelSnapshot {
        Price price;
        Quantity quantity;
    };

    //bid_depth and ask_depth are member functions that return a vector of PriceLevelSnapshot objects representing the depth of the order book for bids and asks, respectively. The levels parameter specifies how many price levels to include in the snapshot.
    std::vector<PriceLevelSnapshot> bid_depth(std::size_t levels) const;
    std::vector<PriceLevelSnapshot> ask_depth(std::size_t levels) const;

};

//order_map_ is an unordered map that allows for quick lookup of orders by their ID. It maps an OrderId to an OrderLocation, which contains information about the order's side, price, and its position in the order book.
std::unordered_map<OrderId, OrderLocation> order_index_;
};

//returns the best bid price in the order book, or std::nullopt if there are no bids
std::optional<Price> OrderBook::best_bid() const {

    if (bids_.empty()) {
        return std::nullopt;
    }

    return std::prev(bids_.end())->first;
}

//returns the best ask price in the order book, or std::nullopt if there are no asks
std::optional<Price> OrderBook::best_ask() const {

    if (asks_.empty()) {
        return std::nullopt;
    }

    return asks_.begin()->first;
}

//returns the spread between the best ask and best bid prices in the order book, or std::nullopt if either side is empty
std::optional<Price> OrderBook::spread() const {

    auto bid = best_bid();
    auto ask = best_ask();

    if (!bid.has_value() || !ask.has_value()) {
        return std::nullopt;
    }

    return ask.value() - bid.value();
}

//returns the mid-price between the best ask and best bid prices in the order book, or std::nullopt if either side is empty
std::optional<double> OrderBook::mid_price() const {

    auto bid = best_bid();
    auto ask = best_ask();

    if (!bid.has_value() || !ask.has_value()) {
        return std::nullopt;
    }

    return (static_cast<double>(bid.value()) +
            static_cast<double>(ask.value())) / 2.0;
}

Quantity OrderBook::quantity_at_price(Side side, Price price) const {

    if (side == Side::Buy) {

        auto it = bids_.find(price);

        if (it == bids_.end()) {
            return 0;
        }

        Quantity total = 0;

        for (const auto& order : it->second) {
            total += order.remaining_quantity;
        }

        return total;

    } else {

        auto it = asks_.find(price);

        if (it == asks_.end()) {
            return 0;
        }

        Quantity total = 0;

        for (const auto& order : it->second) {
            total += order.remaining_quantity;
        }

        return total;
    }
}
