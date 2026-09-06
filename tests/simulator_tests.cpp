#include "simulator.hpp"
#include "test_runner.hpp"

#include <iostream>

int main() {
    TestRunner runner;

    Simulator simulator(50);
    DataWriter writer("simulator_tests_output.csv");

    SimulationStats stats = simulator.run(1000, writer);

    runner.check(stats.orders_submitted > 0, "the simulation submits orders");
    runner.check(stats.buy_orders > 0, "some of them are buys");
    runner.check(stats.sell_orders > 0, "some of them are sells");
    runner.check(stats.buy_orders + stats.sell_orders == stats.orders_submitted,
                 "every order is counted as exactly one of the two");
    runner.check(stats.trades > 0, "the orders trade against each other");
    runner.check(stats.traded_quantity > 0, "the trades move quantity");

    //market orders are meant to be about a tenth of the flow, so a run of this size should hold a few
    runner.check(stats.market_orders > 0, "some orders arrive as market orders");
    runner.check(stats.market_orders < stats.orders_submitted,
                 "but market orders are not the whole of the flow");

    runner.check(stats.successful_cancels <= stats.cancel_attempts,
                 "a cancel cannot succeed more often than it is attempted");
    runner.check(stats.average_trade_price() > 0.0, "the average trade price is a real price");

    //the run is seeded, so the same seed has to give the same answer twice
    Simulator repeat_simulator(50);
    DataWriter repeat_writer("simulator_tests_output.csv");
    SimulationStats repeat_stats = repeat_simulator.run(1000, repeat_writer);

    runner.check(repeat_stats.orders_submitted == stats.orders_submitted,
                 "the same seed submits the same number of orders");
    runner.check(repeat_stats.trades == stats.trades, "and produces the same number of trades");
    runner.check(repeat_stats.traded_quantity == stats.traded_quantity,
                 "and moves the same quantity");

    //a different seed should not retrace the same run
    Simulator other_simulator(51);
    DataWriter other_writer("simulator_tests_output.csv");
    SimulationStats other_stats = other_simulator.run(1000, other_writer);

    runner.check(other_stats.traded_quantity != stats.traded_quantity,
                 "a different seed gives a different run");

    return runner.summary("simulator tests");
}
