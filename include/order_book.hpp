#pragma once

#include "order.hpp"
#include "trade.hpp"
#include "price.hpp"

#include <map>
#include <vector>
#include <optional>

class OrderBook {
public:
    //submits an order to the order book and returns a vector of trades that occurred as a result of the submission.
    std::vector<Trade> submit(Order order);

    //returns the best ask price in the order book, or std::nullopt if there are no asks
    std::optional<Price> best_ask() const;
    std::optional<Price> best_bid() const;

private:
    //the order book is represented as two separate maps: one for asks and one for bids.
    AskBook asks_;
    BidBook bids_;

    //match_buy and match_sell are private member functions that handle the matching of incoming orders with existing orders in the order book.
    std::vector<Trade> match_buy(Order& incoming);
    std::vector<Trade> match_sell(Order& incoming);
};
