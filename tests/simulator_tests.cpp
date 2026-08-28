#include "simulator.hpp"

#include <cassert>
#include <iostream>

int main() {

    Simulator simulator(50);

    SimulationStats stats = simulator.run(1000);

    assert(stats.orders_submitted > 0);
    assert(stats.buy_orders > 0);
    assert(stats.sell_orders > 0);
    assert(stats.trades > 0);
    assert(stats.traded_quantity > 0);

    std::cout << "Simulator tests passed!\n";
    
    return 0;
}