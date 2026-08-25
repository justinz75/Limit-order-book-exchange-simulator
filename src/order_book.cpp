//include "order_book.hpp" and "trade.hpp"
#include "order_book.hpp"
#include "trade.hpp"

//returns the best ask price in the order book, or std::nullopt if there are no asks
std::optional<Price> OrderBook::best_ask() const {
    if (asks_.empty()) {
        return std::nullopt;
    }
    return asks_.begin()->first;
}

//returns the best bid price in the order book, or std::nullopt if there are no bids
std::optional<Price> OrderBook::best_bid() const {
    if (bids_.empty()) {
        return std::nullopt;
    }
    return bids_.begin()->first;
}

std::vector<Trade> OrderBook::match_buy(Order& incoming) {
    std::vector<Trade> trades;
    //while the incoming buy order has remaining quantity and there are asks in the order book
    while (incoming.remaining_quantity > 0 && !asks_.empty()) {
        //get the best ask price and the corresponding price level
        auto best_ask_it = asks_.begin();
        Price best_ask_price = best_ask_it->first;
        PriceLevel& best_ask_level = best_ask_it->second;

        //no more matching possible if the incoming buy order's price is less than the best ask price
        if (incoming.price < best_ask_price) {
            break; 
        }

        //match the incoming buy order with the resting sell order at the best ask price
        Order& resting_order = best_ask_level.front();
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

        //remove the resting order if fully filled
        if (resting_order.remaining_quantity == 0) {
            auto resting_order_id = resting_order.id;
            best_ask_level.pop_front();
            order_index_.erase(resting_order_id);
            if (best_ask_level.empty()) {
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

        //no more matching possible if the incoming sell order's price is less than the best bid price
        if (incoming.price > best_bid_price) {
            break; 
        }

        //match the incoming sell order with the resting buy order at the best bid price
        Order& resting_order = best_bid_level.front();
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

        //remove the resting order if fully filled
        if (resting_order.remaining_quantity == 0) {
            auto resting_order_id = resting_order.id;
            best_bid_level.pop_front();
            order_index_.erase(resting_order_id);
            if (best_bid_level.empty()) {
                bids_.erase(best_bid_it);
            }
        }
    }
    return trades;
}

//submits an order to the order book and returns a vector of trades that occurred as a result of the submission.
std::vector<Trade> OrderBook::submit(Order order) {
    std::vector<Trade> trades;
    if (order.side == Side::Buy) {
        trades = match_buy(order);
        //if buy order is not fully filled, add the remaining quantity to the bid book
        if (order.remaining_quantity > 0) {
            add_to_book(order);
        }
    } else if (order.side == Side::Sell) {
        trades = match_sell(order);
        //if sell order is not fully filled, add the remaining quantity to the ask book
        if (order.remaining_quantity > 0) {
            add_to_book(order);
        }
    }
    return trades;
}

//adds an order to the order book and updates the order index for quick lookup
void OrderBook::add_to_book(const Order& order) {
    //add the order to the appropriate book (bids or asks) based on its side
    if (order.side == Side::Buy) {
        auto& price_level = bids_[order.price];
        price_level.push_back(order);
        auto iterator = std::prev(price_level.end());
        order_index_[order.id] = {
            order.side,
            order.price,
            iterator
        };
    //if the order is a sell order, add it to the ask book
    } else {
        auto& price_level = asks_[order.price];
        price_level.push_back(order);
        auto iterator = std::prev(price_level.end());
        order_index_[order.id] = {
            order.side,
            order.price,
            iterator
        };
    }
}

bool OrderBook::cancel_order(OrderId order_id) {
    auto index_it = order_index_.find(order_id);
    if (index_it == order_index_.end()) {
        return false;
    }
    OrderLocation location = index_it->second;
    if (location.side == Side::Buy) {
        auto& price_level = bids_.at(location.price);
        price_level.erase(location.order_iterator);
        if (price_level.empty()) {
            bids_.erase(location.price);
        }

    } else {
        auto& price_level = asks_.at(location.price);
        price_level.erase(location.order_iterator);
        if (price_level.empty()) {
            asks_.erase(location.price);
        }
    }
    order_index_.erase(index_it);

    return true;
}