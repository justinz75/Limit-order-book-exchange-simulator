#include "simulator.hpp"

#include <algorithm>
#include <cmath>
#include <iostream>
#include <stdexcept>

Simulator::Simulator(std::uint64_t seed)
    : rng_(seed) {
}


//runs the simulation for a specified number of events, generating random orders and processing them through the order book. It also collects statistics about the simulation
int Simulator::random_price() {
    std::uniform_int_distribution<int> distribution(95, 105);
    return distribution(rng_);
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

//returns true if the next event should be a cancel event, based on a 10% probability
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

//returns true if the next event should be a cancel event, based on a 10% probability
bool Simulator::should_cancel() {
    std::uniform_int_distribution<int> distribution(1, 10);
    return distribution(rng_) == 1;
}

//returns true if the next event should be a cancel event, based on a 10% probability
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
    Price price = random_price();
    Quantity quantity = random_quantity();

    //create and return the order object with the generated attributes
    Order order{
        order_id,
        trader_id,
        side,
        OrderType::Limit,
        price,
        quantity
    };
    return order;
}

//returns true if the next event should be a cancel event, based on a 10% probability
void Simulator::process_order(const Order& order, std::size_t event_number, DataWriter& writer) {
    auto trades = order_book_.submit(order);
    stats_.orders_submitted++;

    if (order.side == Side::Buy) {
        stats_.buy_orders++;
    } else {
        stats_.sell_orders++;
    }

    known_order_ids_.push_back(order.id);

    writer.write_event(
        event_number,
        order,
        order_book_
    );

    for (const auto& trade : trades) {
        stats_.trades++;
        stats_.traded_quantity += trade.quantity;
        stats_.total_traded_value +=
            static_cast<double>(trade.price) *
            static_cast<double>(trade.quantity);

        writer.write_trade(
            event_number,
            trade
        );
    }
}

//returns true if the next event should be a cancel event, based on a 10% probability
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

        //remove the order ID from the known_order_ids_ vector if the cancel was successful
        known_order_ids_.erase(
            std::remove(
                known_order_ids_.begin(),
                known_order_ids_.end(),
                order_id
            ),
            known_order_ids_.end()
        );
    } else {
        //the order may already have been completely filled
        known_order_ids_.erase(
            std::remove(
                known_order_ids_.begin(),
                known_order_ids_.end(),
                order_id
            ),
            known_order_ids_.end()
        );
    }
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

        if (should_cancel() && !known_order_ids_.empty()) {
            process_cancel();

        } else {
            Order order = generate_order();
            process_order(order, event_number, writer);
            writer.write_event(
                event_number,
                order,
                order_book_
            );
        }
    }
    return stats_;
}

