#pragma once

#include "order.hpp"
#include "trade.hpp"
#include "price.hpp"
#include "order_index.hpp"

#include <cstddef>
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

        //returns the spread between the best ask and best bid prices in the order book, or std::nullopt if either side is empty
        std::optional<Price> spread() const;
        std::optional<double> mid_price() const;

        bool cancel_order(OrderId order_id);

        //changes a resting order. reducing the quantity is done where the order already sits, so it keeps
        //its place in the queue, since letting it keep its turn takes nothing away from anyone behind it.
        //any other change, a new price or more quantity, goes to the back of the queue instead, which is
        //what a real venue does, and can trade on the way in if the new price crosses. returns the trades
        //that resulted, or nothing at all if there is no such order
        std::optional<std::vector<Trade>> modify_order(
            OrderId order_id,
            Price new_price,
            Quantity new_quantity
        );

        //how much of an order the book could fill right now, without changing anything. quantity resting
        //under the same trader id does not count, since that would be prevented from trading
        Quantity fillable_quantity(const Order& incoming) const;

        //how many resting orders have been pulled because they would have traded against their owner
        std::size_t self_trade_cancellations() const;

        Quantity quantity_at_price(Side side, Price price) const;

        //a single price level of the book: the price and the total quantity resting at it
        struct PriceLevelSnapshot {
            Price price;
            Quantity quantity;
        };

        //return the price levels nearest the top of the book, at most 'levels' of them, best first
        std::vector<PriceLevelSnapshot> bid_depth(std::size_t levels) const;
        std::vector<PriceLevelSnapshot> ask_depth(std::size_t levels) const;

        //how many orders are resting in the book
        std::size_t resting_order_count() const;

        //sets aside room for this many resting orders, so the book never has to stop and grow while it
        //fills up to that size. growing is where the worst latencies come from: the index rehashes every
        //entry and the arena copies every order, all inside a single submit
        void reserve(std::size_t orders);

    private:
        //the order book is represented as two separate maps: one for asks and one for bids.
        AskBook asks_;
        BidBook bids_;

        //every resting order lives in this one vector rather than in a node of its own. the queue at a
        //price level is threaded through it by index, which keeps the orders that are matched one after
        //another close together in memory and takes an allocation off the path of every submit
        struct OrderNode {
            Order order;
            std::size_t previous = no_order;
            std::size_t next = no_order;
        };

        std::vector<OrderNode> arena_;

        //slots left behind by orders that have traded or been cancelled, handed out again before the
        //arena is grown
        std::vector<std::size_t> free_slots_;

        //takes a slot for an order, reusing a freed one where there is one to reuse
        std::size_t acquire_slot(const Order& order);

        //hands a slot back for reuse
        void release_slot(std::size_t slot);

        //adds an order to the back of a level's queue, which is what gives arrival order priority
        void link_into_level(PriceLevel& level, std::size_t slot);

        //takes an order out of its level's queue, from anywhere in it
        void unlink_from_level(PriceLevel& level, std::size_t slot);

        //match_buy and match_sell are private member functions that handle the matching of incoming orders with existing orders in the order book.
        std::vector<Trade> match_buy(Order& incoming);
        std::vector<Trade> match_sell(Order& incoming);

        //drops a resting order without recording a trade, used when it would have traded against its
        //own trader. the level is erased too if that emptied it
        void remove_resting_order(Side side, Price price, std::size_t slot);

        std::size_t self_trade_cancellations_ = 0;

        void add_to_book(const Order& order);

        struct OrderLocation {
            Side side;
            Price price;
            std::size_t slot;
        };

        OrderIndex<OrderLocation> order_index_;
};
