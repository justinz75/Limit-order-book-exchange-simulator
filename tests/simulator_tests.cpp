#include "simulator.hpp"
#include "test_runner.hpp"

#include <cmath>
#include <cstdint>
#include <iostream>

int main() {
    TestRunner runner;

    Simulator simulator(50);
    //an empty name gives a writer that records nothing, so running the tests leaves no file behind
    DataWriter writer("");

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
    DataWriter repeat_writer("");
    SimulationStats repeat_stats = repeat_simulator.run(1000, repeat_writer);

    runner.check(repeat_stats.orders_submitted == stats.orders_submitted,
                 "the same seed submits the same number of orders");
    runner.check(repeat_stats.trades == stats.trades, "and produces the same number of trades");
    runner.check(repeat_stats.traded_quantity == stats.traded_quantity,
                 "and moves the same quantity");

    //a different seed should not retrace the same run
    Simulator other_simulator(51);
    DataWriter other_writer("");
    SimulationStats other_stats = other_simulator.run(1000, other_writer);

    runner.check(other_stats.traded_quantity != stats.traded_quantity,
                 "a different seed gives a different run");

    //leaving the config out has to give exactly the original model, since data/simulation.csv and every
    //number in the readme come from it
    Simulator explicit_default(50, SimulationConfig{});
    DataWriter explicit_writer("");
    SimulationStats explicit_stats = explicit_default.run(1000, explicit_writer);

    runner.check(explicit_stats.trades == stats.trades &&
                 explicit_stats.traded_quantity == stats.traded_quantity,
                 "a default config gives exactly the same run as no config at all");
    runner.check(explicit_stats.informed_orders == 0 && explicit_stats.maker_fills == 0,
                 "and has no informed traders and no market maker in it");

    //informed traders only act when the book is away from the reference, but with ordinary traders
    //working from an out of date view it drifts often enough that they should find something to do
    SimulationConfig informed_config;
    informed_config.noise_view_lag = 300;
    informed_config.informed_percentage = 10;

    Simulator informed_simulator(50, informed_config);
    DataWriter informed_writer("");
    SimulationStats informed_stats = informed_simulator.run(5000, informed_writer);

    runner.check(informed_stats.informed_orders > 0, "informed traders find mispricings to trade on");

    //the books of the market maker have to balance: its position is what it bought less what it sold
    SimulationConfig maker_config;
    maker_config.noise_view_lag = 300;
    maker_config.informed_percentage = 10;
    maker_config.market_maker = true;

    Simulator maker_simulator(50, maker_config);
    DataWriter maker_writer("");
    SimulationStats maker_stats = maker_simulator.run(5000, maker_writer);

    std::int64_t bought = 0;
    std::int64_t sold = 0;

    for (const MakerFill& fill : maker_simulator.maker_fills()) {
        if (fill.side == Side::Buy) {
            bought += static_cast<std::int64_t>(fill.quantity);
        } else {
            sold += static_cast<std::int64_t>(fill.quantity);
        }
    }

    runner.check(maker_stats.maker_fills > 0, "the market maker gets filled");
    runner.check(bought - sold == maker_stats.maker_final_inventory,
                 "its final position is exactly what it bought less what it sold");
    runner.check(maker_stats.maker_peak_inventory <= maker_config.maker_inventory_limit,
                 "and it never goes past its position limit");

    //the maker only ever rests, so everything it buys should be at or below the mid it saw and
    //everything it sells at or above it. a fill on the wrong side would mean a quote crossed the book
    bool maker_always_passive = true;

    for (const MakerFill& fill : maker_simulator.maker_fills()) {
        double price = static_cast<double>(fill.price);

        if (fill.side == Side::Buy && price > fill.mid_before) {
            maker_always_passive = false;
        }

        if (fill.side == Side::Sell && price < fill.mid_before) {
            maker_always_passive = false;
        }
    }

    runner.check(maker_always_passive, "the maker never trades through the mid, since it only rests");

    //with history kept, the markout at zero events is the spread earned per unit, which is also what the
    //edge the maker adds up fill by fill works out at. the two are worked out separately, so each checks
    //the other
    SimulationConfig history_config = maker_config;
    history_config.record_history = true;

    Simulator history_simulator(50, history_config);
    DataWriter history_writer("");
    SimulationStats history_stats = history_simulator.run(5000, history_writer);

    double edge_per_unit = history_stats.maker_edge / static_cast<double>(history_stats.maker_volume);

    runner.check(history_simulator.mid_history().size() == 5000, "history keeps a mid for every event");
    runner.check(history_simulator.reference_history().size() == 5000,
                 "and a reference price for every event");
    runner.check(std::abs(history_stats.maker_markout_0 - edge_per_unit) < 1e-9,
                 "the markout at zero events is exactly the spread earned per unit");
    runner.check(history_stats.maker_fills == maker_stats.maker_fills &&
                 history_stats.maker_pnl == maker_stats.maker_pnl,
                 "and keeping history does not change anything that happens in the run");

    //the maker centred on the view of the ordinary traders still makes a market
    SimulationConfig their_view_config = maker_config;
    their_view_config.maker_uses_noise_view = true;

    Simulator their_view_simulator(50, their_view_config);
    DataWriter their_view_writer("");
    SimulationStats their_view_stats = their_view_simulator.run(5000, their_view_writer);

    runner.check(their_view_stats.maker_fills > 0, "a maker working from their view still gets filled");
    runner.check(their_view_stats.maker_peak_inventory <= their_view_config.maker_inventory_limit,
                 "and still keeps inside its position limit");

    return runner.summary("simulator tests");
}
