#pragma once

#include "order_book.hpp"

#include <cstddef>
#include <cstdint>
#include <random>
#include <vector>

struct SimulationStats {
    //statistics for the simulation
    std::size_t orders_submitted = 0;
    std::size_t buy_orders = 0;
    std::size_t sell_orders = 0;
    std::size_t cancel_attempts = 0;
    std::size_t successful_cancels = 0;

    //statistics for trades executed during the simulation
    std::size_t trades = 0;
    Quantity traded_quantity = 0;

    double total_traded_value = 0.0;

    double average_trade_price() const;
};

class Simulator {
    public:
    //constructor for the Simulator class, which initializes the random number generator with a given seed (default is 42).
        Simulator(std::uint64_t seed = 50);
        SimulationStats run(std::size_t number_of_events);

    private:
        OrderBook order_book_;

        std::mt19937_64 rng_;

        OrderId next_order_id_ = 1;
        std::uint64_t next_trader_id_ = 1;

        std::vector<OrderId> known_order_ids_;

        SimulationStats stats_;

        //generates a random order with a unique order ID, random trader ID, price, and quantity.
        Order generate_order();

        //returns true if the next event should be a cancel event, based on a 10% probability.
        bool should_cancel();

        //chooses a random order ID from the known_order_ids_ vector for cancellation, or returns 0 if there are no known orders.
        OrderId choose_order_to_cancel();

        //processes the submission of an order to the order book and updates the simulation statistics accordingly.
        void process_order(const Order& order);

        //processes the cancellation of an order in the order book and updates the simulation statistics accordingly.
        void process_cancel();

        //generates a random price for an order within a specified range (e.g., 1 to 100).
        int random_price();

        //generates a random quantity for an order within a specified range (e.g., 1 to 100).
        Quantity random_quantity();

        //generates a random trader ID for an order, ensuring that each trader has a unique ID.
        std::uint64_t random_trader_id();
    };