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

        //modifies a resting order and returns any trades, or std::nullopt if there is no such order
        std::optional<std::vector<Trade>> modify_order(
            OrderId order_id,
            Price new_price,
            Quantity new_quantity
        );

        //how much of an order the book could fill right now, not counting the same trader's orders
        Quantity fillable_quantity(const Order& incoming) const;

        //how many resting orders were pulled to stop a trader trading with themselves
        std::size_t self_trade_cancellations() const;

        Quantity quantity_at_price(Side side, Price price) const;

        //a price level: its price and the total quantity resting at it
        struct PriceLevelSnapshot {
            Price price;
            Quantity quantity;
        };

        //returns up to 'levels' price levels from the top of each side, best first
        std::vector<PriceLevelSnapshot> bid_depth(std::size_t levels) const;
        std::vector<PriceLevelSnapshot> ask_depth(std::size_t levels) const;

        //how many orders are resting in the book
        std::size_t resting_order_count() const;

        //sets aside room for this many resting orders so the book never has to grow
        void reserve(std::size_t orders);

    private:
        //the order book is represented as two separate maps: one for asks and one for bids.
        AskBook asks_;
        BidBook bids_;

        //resting orders live in one vector and are linked into their level's queue by index
        struct OrderNode {
            Order order;
            std::size_t previous = no_order;
            std::size_t next = no_order;
        };

        std::vector<OrderNode> arena_;

        //slots freed by filled or cancelled orders, reused before the arena grows
        std::vector<std::size_t> free_slots_;

        //takes a slot for an order, reusing a freed one if there is one
        std::size_t acquire_slot(const Order& order);

        //hands a slot back for reuse
        void release_slot(std::size_t slot);

        //adds an order to the back of a level's queue
        void link_into_level(PriceLevel& level, std::size_t slot);

        //takes an order out of its level's queue, from anywhere in it
        void unlink_from_level(PriceLevel& level, std::size_t slot);

        //match_buy and match_sell are private member functions that handle the matching of incoming orders with existing orders in the order book.
        std::vector<Trade> match_buy(Order& incoming);
        std::vector<Trade> match_sell(Order& incoming);

        //removes a resting order without a trade, used for self trade prevention
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
