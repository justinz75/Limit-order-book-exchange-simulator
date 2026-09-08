//include "order_book.hpp" and "trade.hpp"
#include "order_book.hpp"
#include "trade.hpp"

#include <algorithm>

//takes a slot in the arena for an order. freed slots are handed out again first, so a book that is
//busy but roughly the same size over time stops growing the arena and stops allocating altogether
std::size_t OrderBook::acquire_slot(const Order& order) {
    if (!free_slots_.empty()) {
        std::size_t slot = free_slots_.back();
        free_slots_.pop_back();

        arena_[slot].order = order;
        arena_[slot].previous = no_order;
        arena_[slot].next = no_order;

        return slot;
    }

    arena_.push_back(OrderNode{order, no_order, no_order});
    return arena_.size() - 1;
}

//hands a slot back. the order is left where it is rather than cleared, since acquire_slot overwrites it
void OrderBook::release_slot(std::size_t slot) {
    free_slots_.push_back(slot);
}

//joins the back of the queue at a price level, which is what gives orders arrival order priority
void OrderBook::link_into_level(PriceLevel& level, std::size_t slot) {
    arena_[slot].previous = level.tail;
    arena_[slot].next = no_order;

    if (level.tail == no_order) {
        level.head = slot;
    } else {
        arena_[level.tail].next = slot;
    }

    level.tail = slot;
    level.total_quantity += arena_[slot].order.remaining_quantity;
}

//takes an order out of its level's queue. matching only ever removes the head, but a cancel can come
//for an order anywhere in it, so both ends and the middle have to be handled
void OrderBook::unlink_from_level(PriceLevel& level, std::size_t slot) {
    std::size_t previous = arena_[slot].previous;
    std::size_t next = arena_[slot].next;

    if (previous == no_order) {
        level.head = next;
    } else {
        arena_[previous].next = next;
    }

    if (next == no_order) {
        level.tail = previous;
    } else {
        arena_[next].previous = previous;
    }

    level.total_quantity -= arena_[slot].order.remaining_quantity;

    arena_[slot].previous = no_order;
    arena_[slot].next = no_order;
}

std::size_t OrderBook::resting_order_count() const {
    return arena_.size() - free_slots_.size();
}

std::optional<Price> OrderBook::best_bid() const {
    if (bids_.empty()) {
        return std::nullopt;
    }
    //bids_ is sorted with std::greater, so the highest bid is the first element
    return bids_.begin()->first;
}

std::optional<Price> OrderBook::best_ask() const {
    if (asks_.empty()) {
        return std::nullopt;
    }
    return asks_.begin()->first;
}

std::optional<Price> OrderBook::spread() const {
    auto bid = best_bid();
    auto ask = best_ask();
    if (!bid.has_value() || !ask.has_value()) {
        return std::nullopt;
    }
    return ask.value() - bid.value();
}

std::optional<double> OrderBook::mid_price() const {
    auto bid = best_bid();
    auto ask = best_ask();
    if (!bid.has_value() || !ask.has_value()) {
        return std::nullopt;
    }
    return (static_cast<double>(bid.value()) +
            static_cast<double>(ask.value())) / 2.0;
}

//the level keeps its own running total, so this no longer has to walk the queue
Quantity OrderBook::quantity_at_price(Side side, Price price) const {
    if (side == Side::Buy) {
        auto it = bids_.find(price);
        if (it == bids_.end()) {
            return 0;
        }
        return it->second.total_quantity;
    }

    auto it = asks_.find(price);
    if (it == asks_.end()) {
        return 0;
    }
    return it->second.total_quantity;
}

//walks the bid book from the best price down and reports the quantity resting at each level
std::vector<OrderBook::PriceLevelSnapshot> OrderBook::bid_depth(std::size_t levels) const {
    std::vector<PriceLevelSnapshot> snapshots;

    //bids_ is sorted with std::greater, so iterating forward starts at the best bid
    for (const auto& price_level : bids_) {
        if (snapshots.size() >= levels) {
            break;
        }

        snapshots.push_back({price_level.first, price_level.second.total_quantity});
    }

    return snapshots;
}

//walks the ask book from the best price up and reports the quantity resting at each level
std::vector<OrderBook::PriceLevelSnapshot> OrderBook::ask_depth(std::size_t levels) const {
    std::vector<PriceLevelSnapshot> snapshots;

    //asks_ is sorted in ascending order, so iterating forward starts at the best ask
    for (const auto& price_level : asks_) {
        if (snapshots.size() >= levels) {
            break;
        }

        snapshots.push_back({price_level.first, price_level.second.total_quantity});
    }

    return snapshots;
}

std::vector<Trade> OrderBook::match_buy(Order& incoming) {
    std::vector<Trade> trades;
    //while the incoming buy order has remaining quantity and there are asks in the order book
    while (incoming.remaining_quantity > 0 && !asks_.empty()) {
        //get the best ask price and the corresponding price level
        auto best_ask_it = asks_.begin();
        Price best_ask_price = best_ask_it->first;
        PriceLevel& best_ask_level = best_ask_it->second;

        //a market order takes whatever the book offers, so only a limit order stops on price
        if (incoming.type == OrderType::Limit && incoming.price < best_ask_price) {
            break;
        }

        //match the incoming buy order with the resting sell order at the best ask price
        std::size_t resting_slot = best_ask_level.head;
        Order& resting_order = arena_[resting_slot].order;
        Quantity trade_quantity = std::min(incoming.remaining_quantity, resting_order.remaining_quantity);
        Price trade_price = resting_order.price;

        //create a trade record
        Trade trade{
            incoming.id,
            resting_order.id,
            incoming.trader_id,
            resting_order.trader_id,
            trade_price,
            trade_quantity,
            incoming.timestamp
        };
        //the trade is added to the trades vector, which will be returned to the caller of the submit function.
        trades.push_back(trade);

        //update quantities
        incoming.remaining_quantity -= trade_quantity;
        resting_order.remaining_quantity -= trade_quantity;
        best_ask_level.total_quantity -= trade_quantity;

        //remove the resting order if fully filled
        if (resting_order.remaining_quantity == 0) {
            auto resting_order_id = resting_order.id;
            unlink_from_level(best_ask_level, resting_slot);
            release_slot(resting_slot);
            order_index_.erase(resting_order_id);
            if (best_ask_level.head == no_order) {
                asks_.erase(best_ask_it);
            }
        }
    }
    return trades;
}

std::vector<Trade> OrderBook::match_sell(Order& incoming) {
    std::vector<Trade> trades;
    //while the incoming sell order has remaining quantity and there are bids in the order book
    while (incoming.remaining_quantity > 0 && !bids_.empty()) {
        //get the best bid price and the corresponding price level
        auto best_bid_it = bids_.begin();
        Price best_bid_price = best_bid_it->first;
        PriceLevel& best_bid_level = best_bid_it->second;

        //a market order takes whatever the book offers, so only a limit order stops on price
        if (incoming.type == OrderType::Limit && incoming.price > best_bid_price) {
            break;
        }

        //match the incoming sell order with the resting buy order at the best bid price
        std::size_t resting_slot = best_bid_level.head;
        Order& resting_order = arena_[resting_slot].order;
        Quantity trade_quantity = std::min(incoming.remaining_quantity, resting_order.remaining_quantity);
        Price trade_price = resting_order.price;

        //create a trade record
        Trade trade{
            incoming.id,
            resting_order.id,
            resting_order.trader_id,
            incoming.trader_id,
            trade_price,
            trade_quantity,
            incoming.timestamp
        };

        //the trade is added to the trades vector, which will be returned to the caller of the submit function.
        trades.push_back(trade);

        //update quantities
        incoming.remaining_quantity -= trade_quantity;
        resting_order.remaining_quantity -= trade_quantity;
        best_bid_level.total_quantity -= trade_quantity;

        //remove the resting order if fully filled
        if (resting_order.remaining_quantity == 0) {
            auto resting_order_id = resting_order.id;
            unlink_from_level(best_bid_level, resting_slot);
            release_slot(resting_slot);
            order_index_.erase(resting_order_id);
            if (best_bid_level.head == no_order) {
                bids_.erase(best_bid_it);
            }
        }
    }
    return trades;
}

//submits an order to the order book and returns a vector of trades that occurred as a result of the submission.
//an unfilled limit order rests in the book, while whatever a market order could not fill is discarded
std::vector<Trade> OrderBook::submit(Order order) {
    std::vector<Trade> trades;
    if (order.side == Side::Buy) {
        trades = match_buy(order);
        //if buy order is not fully filled, add the remaining quantity to the bid book
        if (order.remaining_quantity > 0 && order.type == OrderType::Limit) {
            add_to_book(order);
        }
    } else if (order.side == Side::Sell) {
        trades = match_sell(order);
        //if sell order is not fully filled, add the remaining quantity to the ask book
        if (order.remaining_quantity > 0 && order.type == OrderType::Limit) {
            add_to_book(order);
        }
    }
    return trades;
}

//adds an order to the order book and updates the order index for quick lookup
void OrderBook::add_to_book(const Order& order) {
    std::size_t slot = acquire_slot(order);

    //add the order to the appropriate book (bids or asks) based on its side
    if (order.side == Side::Buy) {
        link_into_level(bids_[order.price], slot);
    //if the order is a sell order, add it to the ask book
    } else {
        link_into_level(asks_[order.price], slot);
    }

    order_index_.insert(order.id, OrderLocation{
        order.side,
        order.price,
        slot
    });
}

bool OrderBook::cancel_order(OrderId order_id) {
    const OrderLocation* found = order_index_.find(order_id);
    if (found == nullptr) {
        return false;
    }
    OrderLocation location = *found;
    if (location.side == Side::Buy) {
        auto level_it = bids_.find(location.price);

        //the index should never point at a price the book has forgotten, but not checking would mean
        //walking off the end of the map if it ever did
        if (level_it == bids_.end()) {
            order_index_.erase(order_id);
            return false;
        }

        unlink_from_level(level_it->second, location.slot);
        if (level_it->second.head == no_order) {
            bids_.erase(level_it);
        }

    } else {
        auto level_it = asks_.find(location.price);

        if (level_it == asks_.end()) {
            order_index_.erase(order_id);
            return false;
        }

        unlink_from_level(level_it->second, location.slot);
        if (level_it->second.head == no_order) {
            asks_.erase(level_it);
        }
    }
    release_slot(location.slot);
    order_index_.erase(order_id);

    return true;
}
