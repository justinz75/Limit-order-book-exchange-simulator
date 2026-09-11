#pragma once

#include "order_book.hpp"
#include "data_writer.hpp"

#include <cstddef>
#include <cstdint>
#include <random>
#include <vector>

//settings for the market being simulated, where the defaults are the original model
struct SimulationConfig {
    //how many events out of date the ordinary traders' view of the reference price is
    std::size_t noise_view_lag = 0;

    //the chance, out of a hundred, that an ordinary trader sends a market order
    int market_order_percentage = 10;

    //the chance, out of a hundred, that an informed trader looks at the book each event
    int informed_percentage = 0;

    //how far the book has to be from the reference, in ticks, before an informed trader will act
    double informed_threshold = 0.5;

    //whether there is a market maker quoting both sides of the book
    bool market_maker = false;

    //how far either side of the mid the maker quotes, in ticks, which must be above zero
    double maker_half_spread = 0.5;

    //how much the maker shows on each side
    Quantity maker_quote_size = 10;

    //the maker stops quoting whichever side would take its position beyond this
    std::int64_t maker_inventory_limit = 200;

    //how far the maker leans both quotes per unit of position, in ticks, or zero for none
    double maker_skew_per_unit = 0.02;

    //how often, in events, the maker's position is recorded
    std::size_t maker_log_interval = 10;

    //whether the maker centres its quotes on the ordinary traders' view instead of the mid
    bool maker_uses_noise_view = false;

    //whether to keep the mid and reference price at the end of every event
    bool record_history = false;
};

//one fill the market maker received
struct MakerFill {
    std::size_t event;

    //the side the maker was on, not the side of the order that hit it
    Side side;

    Price price;
    Quantity quantity;

    //the mid just before the order that filled the maker arrived
    double mid_before;
};

//where the maker stood at one moment in the run
struct MakerSnapshot {
    std::size_t event;
    std::int64_t inventory;
    double cash;
    double mid;
    double reference;
};

struct SimulationStats {
    //statistics for the simulation
    std::size_t orders_submitted = 0;
    std::size_t buy_orders = 0;
    std::size_t sell_orders = 0;
    std::size_t market_orders = 0;
    std::size_t cancel_attempts = 0;
    std::size_t successful_cancels = 0;

    //resting orders pulled to stop a trader trading with themselves
    std::size_t self_trade_cancellations = 0;

    //statistics for trades executed during the simulation
    std::size_t trades = 0;
    Quantity traded_quantity = 0;

    double total_traded_value = 0.0;

    //orders sent by informed traders, who only trade when the book is mispriced
    std::size_t informed_orders = 0;

    //the market maker, when there is one
    std::size_t maker_fills = 0;
    Quantity maker_volume = 0;
    std::int64_t maker_final_inventory = 0;
    std::int64_t maker_peak_inventory = 0;

    //root mean square of the maker's position, taken every event
    double maker_inventory_rms = 0.0;

    //cash plus whatever position is left, valued at the final mid
    double maker_pnl = 0.0;

    //what the maker made against the mid at each fill, which is the spread it earned
    double maker_edge = 0.0;

    //everything else, made or lost from the price moving while the maker held a position
    double maker_inventory_pnl = 0.0;

    //average markout per unit filled, in ticks, this many events after each fill
    double maker_markout_0 = 0.0;
    double maker_markout_1 = 0.0;
    double maker_markout_10 = 0.0;
    double maker_markout_100 = 0.0;
    double maker_markout_1000 = 0.0;

    double average_trade_price() const;
};

class Simulator {
    public:
    //constructor for the Simulator class, which initializes the random number generator with a given seed (default is 50).
    //and a config for the kind of market, which defaults to the original model
        Simulator(std::uint64_t seed = 50, SimulationConfig config = SimulationConfig{});
        SimulationStats run(std::size_t number_of_events, DataWriter& writer);

        //every fill the market maker received during the run, in order
        const std::vector<MakerFill>& maker_fills() const;

        //the maker's position and value, every config.maker_log_interval events
        const std::vector<MakerSnapshot>& maker_snapshots() const;

        //the mid and reference price at the end of every event, if config.record_history is set
        const std::vector<double>& mid_history() const;
        const std::vector<double>& reference_history() const;

    private:
        SimulationConfig config_;

        OrderBook order_book_;

        std::mt19937_64 rng_;

        OrderId next_order_id_ = 1;
        Timestamp next_timestamp_ = 1;

        //the price the market is currently trading around
        double reference_price_ = 100.0;

        //the reference price and mid at the end of every event, when they are being kept
        std::vector<double> reference_history_;
        std::vector<double> mid_history_;

        std::vector<OrderId> known_order_ids_;

        SimulationStats stats_;

        //the market maker's trader id, which no other trader uses
        static constexpr TraderId maker_trader_id = 0;

        //the maker's two resting quotes, or 0 for a side it is not currently quoting
        OrderId maker_bid_id_ = 0;
        OrderId maker_ask_id_ = 0;

        //the prices of those quotes and how much of each is left
        Price maker_bid_price_ = 0;
        Price maker_ask_price_ = 0;
        Quantity maker_bid_remaining_ = 0;
        Quantity maker_ask_remaining_ = 0;

        std::int64_t maker_inventory_ = 0;
        double maker_cash_ = 0.0;

        //where the maker centres its quotes, the mid or the last mid it saw
        double maker_center_ = 100.0;

        double inventory_square_sum_ = 0.0;

        std::vector<MakerFill> maker_fills_;
        std::vector<MakerSnapshot> maker_snapshots_;

        //generates a random order with a unique order ID, random trader ID, price, and quantity.
        Order generate_order();

        //returns true if the next event should cancel a resting order rather than submit a new one.
        bool should_cancel();

        //returns true if the next generated order should be a market order.
        bool should_be_market();

        //returns true if an informed trader should look at the book on this event.
        bool should_be_informed();

        //chooses a random order ID from the known_order_ids_ vector for cancellation, or returns 0 if there are no known orders.
        OrderId choose_order_to_cancel();

        //processes the submission of an order to the order book and updates the simulation statistics accordingly.
        void process_order(const Order& order, std::size_t event_number, DataWriter& writer);

        //lets an informed trader trade if the book has drifted away from the reference price.
        void process_informed(std::size_t event_number, DataWriter& writer);

        //processes the cancellation of an order in the order book and updates the simulation statistics accordingly.
        void process_cancel();

        //updates the market maker's quotes around the current mid.
        void requote_market_maker();

        //moves one of the maker's quotes to the price it wants, if it is not already there.
        void update_maker_quote(Side side, bool show, Price wanted);

        //updates the maker's position, cash and edge if it was on one side of this trade.
        void record_maker_fill(const Trade& trade, std::size_t event_number, double mid_before);

        //returns the reference price as it was noise_view_lag events ago.
        double noise_view() const;

        //returns the average markout of the maker's fills, this many events after each.
        double average_markout(std::size_t horizon) const;

        //returns the price for a limit order, a short way off the value on the passive side.
        Price random_price(Side side);

        //returns how far from the reference price an order is placed.
        Price random_offset();

        //moves the reference price by a small random step.
        void step_reference_price();

        //generates a random quantity for an order within a specified range (e.g., 1 to 100).
        Quantity random_quantity();

        //generates a random trader ID for an order, ensuring that each trader has a unique ID.
        std::uint64_t random_trader_id();

        //generates a random trader ID for an informed trader.
        TraderId random_informed_trader_id();
    };
