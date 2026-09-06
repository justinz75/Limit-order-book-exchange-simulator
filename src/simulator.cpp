#include "simulator.hpp"

#include <algorithm>
#include <cmath>

Simulator::Simulator(std::uint64_t seed)
    : rng_(seed) {
}

//the size of the random step the reference price takes each event
constexpr double reference_step_size = 0.03;

//the reference price is not allowed below this, since a price cannot be negative
constexpr double minimum_reference_price = 1.0;

//how tightly orders cluster around the reference price. a higher value keeps them closer to it
constexpr double offset_tightness = 0.55;

//moves the reference price by a small random step. letting it wander is what allows resting orders to
//be reached and traded rather than sitting in the book forever, so it is deliberately not pulled back
//toward any particular level
void Simulator::step_reference_price() {
    std::normal_distribution<double> noise(0.0, reference_step_size);
    reference_price_ += noise(rng_);

    if (reference_price_ < minimum_reference_price) {
        reference_price_ = minimum_reference_price;
    }
}

//most orders arrive at or near the reference price, with a thinner tail further out
Price Simulator::random_offset() {
    std::geometric_distribution<int> distribution(offset_tightness);
    return static_cast<Price>(distribution(rng_));
}

//a buy is placed at or below the reference price and a sell at or above it, so an order only trades
//when it is priced aggressively enough to reach the other side
Price Simulator::random_price(Side side) {
    Price reference = static_cast<Price>(std::llround(reference_price_));
    Price offset = random_offset();

    if (side == Side::Buy) {
        return reference - offset;
    }

    return reference + offset;
}

//returns a random quantity for an order, which is a random integer between 1 and 20
Quantity Simulator::random_quantity() {
    std::uniform_int_distribution<Quantity> distribution(1, 20);
    return distribution(rng_);
}

//returns a random trader ID for an order, which is a random integer between 1 and 1000
std::uint64_t Simulator::random_trader_id() {
    std::uniform_int_distribution<std::uint64_t> distribution(1, 1000);
    return distribution(rng_);
}

//chooses a random order ID from the known order IDs, or 0 if there are none
OrderId Simulator::choose_order_to_cancel() {

    if (known_order_ids_.empty()) {
        return 0;
    }
    std::uniform_int_distribution<std::size_t> distribution(
        0,
        known_order_ids_.size() - 1
    );
    return known_order_ids_[distribution(rng_)];
}

//the share of events that cancel a resting order rather than submitting a new one. orders arrive
//faster than they trade, so without a healthy cancel rate the book only ever grows
constexpr int cancel_percentage = 40;

//the share of new orders that arrive as market orders
constexpr int market_order_percentage = 10;

//returns true if the next event should cancel a resting order rather than submit a new one
bool Simulator::should_cancel() {
    std::uniform_int_distribution<int> distribution(1, 100);
    return distribution(rng_) <= cancel_percentage;
}

//returns true if the next generated order should be a market order
bool Simulator::should_be_market() {
    std::uniform_int_distribution<int> distribution(1, 100);
    return distribution(rng_) <= market_order_percentage;
}

//generates a random order with a unique ID, random trader ID, side, type, price and quantity
Order Simulator::generate_order() {
    std::uniform_int_distribution<int> side_distribution(0, 1);
    Side side;

    //randomly choose the side of the order (buy or sell) using a uniform distribution
    if (side_distribution(rng_) == 0) {
        side = Side::Buy;
    } else {
        side = Side::Sell;
    }

    //generate a unique order ID, random trader ID, price, and quantity for the order
    OrderId order_id = next_order_id_++;
    std::uint64_t trader_id = random_trader_id();
    Price price = random_price(side);
    Quantity quantity = random_quantity();

    //a market order takes whatever the book offers, so it carries no price of its own
    OrderType type = OrderType::Limit;
    if (should_be_market()) {
        type = OrderType::Market;
        price = 0;
    }

    //create and return the order object with the generated attributes
    Order order{
        order_id,
        trader_id,
        side,
        type,
        price,
        quantity,
        next_timestamp_++
    };
    return order;
}

//submits an order to the order book, records the resulting trades and updates the statistics
void Simulator::process_order(const Order& order, std::size_t event_number, DataWriter& writer) {
    auto trades = order_book_.submit(order);
    stats_.orders_submitted++;

    if (order.side == Side::Buy) {
        stats_.buy_orders++;
    } else {
        stats_.sell_orders++;
    }

    if (order.type == OrderType::Market) {
        stats_.market_orders++;
    }

    writer.write_event(
        event_number,
        order,
        order_book_
    );

    Quantity filled = 0;

    for (const auto& trade : trades) {
        stats_.trades++;
        stats_.traded_quantity += trade.quantity;
        stats_.total_traded_value +=
            static_cast<double>(trade.price) *
            static_cast<double>(trade.quantity);

        filled += trade.quantity;

        writer.write_trade(
            event_number,
            trade
        );
    }

    //only a limit order with quantity left over is resting in the book, so only those can be cancelled
    if (order.type == OrderType::Limit && filled < order.remaining_quantity) {
        known_order_ids_.push_back(order.id);
    }
}

//cancels a randomly chosen known order and updates the statistics
void Simulator::process_cancel() {
    //if there are no known order IDs, return early since there are no orders to cancel
    if (known_order_ids_.empty()) {
        return;
    }

    OrderId order_id = choose_order_to_cancel();
    stats_.cancel_attempts++;

    //attempt to cancel the order in the order book and update statistics accordingly
    if (order_book_.cancel_order(order_id)) {
        stats_.successful_cancels++;
    }

    //the order is forgotten either way, since a cancel only fails once the order has been filled
    known_order_ids_.erase(
        std::remove(
            known_order_ids_.begin(),
            known_order_ids_.end(),
            order_id
        ),
        known_order_ids_.end()
    );
}

double SimulationStats::average_trade_price() const {
    //calculate the average trade price by dividing the total traded value by the total traded quantity
    if (traded_quantity == 0) {
        return 0.0;
    }
    return total_traded_value / static_cast<double>(traded_quantity);
}

//runs the simulation for a specified number of events, generating random orders and processing them through the order book and collects statistics about the simulation
SimulationStats Simulator::run(std::size_t number_of_events, DataWriter& writer) {
    stats_ = SimulationStats{};
    for (std::size_t i = 0; i < number_of_events; ++i) {
        std::size_t event_number = i + 1;

        //the market drifts a little between events, whether or not an order arrives
        step_reference_price();

        if (should_cancel() && !known_order_ids_.empty()) {
            process_cancel();

        } else {
            Order order = generate_order();
            process_order(order, event_number, writer);
        }
    }
    return stats_;
}