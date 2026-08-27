#include "simulator.hpp"

#include <iostream>

int main() {
    Simulator simulator(50); //initialize the simulator with a seed for the random number generator

    constexpr std::size_t number_of_events = 100000;

    //run the simulation for a specified number of events and collect statistics
    SimulationStats stats = simulator.run(number_of_events);

    std::cout << "Simulation complete\n";
    std::cout << "-------------------\n";

    std::cout << "Events: "
              << number_of_events << '\n';

    std::cout << "Orders submitted: "
              << stats.orders_submitted << '\n';

    std::cout << "Buy orders: "
              << stats.buy_orders << '\n';

    std::cout << "Sell orders: "
              << stats.sell_orders << '\n';

    std::cout << "Cancel attempts: "
              << stats.cancel_attempts << '\n';

    std::cout << "Successful cancels: "
              << stats.successful_cancels << '\n';

    std::cout << "Trades: "
              << stats.trades << '\n';

    std::cout << "Traded quantity: "
              << stats.traded_quantity << '\n';

    std::cout << "Average trade price: "
              << stats.average_trade_price() << '\n';

    return 0;
}