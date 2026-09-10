#pragma once

#include "order_book.hpp"
#include "data_writer.hpp"

#include <cstddef>
#include <cstdint>
#include <random>
#include <vector>

//decides what kind of market gets simulated. the defaults give the original model, which is the one
//data/simulation.csv comes from, so a Simulator built without a config behaves exactly as it always has
struct SimulationConfig {
    //how many events out of date the ordinary traders are about the reference price. at zero they all
    //know the true value as it is right now, which is the original model, and nobody has an edge on
    //anybody. above zero they are pricing off where the value was a while ago, which leaves something
    //for anyone who can see where it is now to trade on.
    //
    //an earlier version anchored them to the mid instead. that left the book anchored to nothing but
    //itself, and the price did not move once in a hundred thousand events
    std::size_t noise_view_lag = 0;

    //the chance, out of a hundred, that an ordinary trader sends a market order rather than a limit one.
    //real uninformed flow mostly takes liquidity instead of providing it. when the ordinary traders here
    //mostly rest limit orders they are acting as market makers themselves, and a dedicated one ends up
    //queueing behind all of them with almost nothing to do
    int market_order_percentage = 10;

    //the chance, out of a hundred, that an informed trader looks at the book on a given event. it only
    //trades if the book has drifted away from the reference price
    int informed_percentage = 0;

    //how far the book has to be from the reference, in ticks, before an informed trader will act
    double informed_threshold = 0.5;

    //whether there is a market maker quoting both sides of the book
    bool market_maker = false;

    //how far either side of the mid the maker quotes, in ticks. has to be above zero, or its bid and
    //offer could land on the same price
    double maker_half_spread = 0.5;

    //how much the maker shows on each side
    Quantity maker_quote_size = 10;

    //the maker stops quoting whichever side would take its position beyond this
    std::int64_t maker_inventory_limit = 200;

    //how far both quotes move per unit of position, in ticks. a maker that is long lowers both, so it
    //buys less and sells more, and drifts back toward flat. zero turns this off
    double maker_skew_per_unit = 0.02;

    //how often, in events, the maker's position and value are written down for plotting
    std::size_t maker_log_interval = 10;
};

//one fill the market maker received
struct MakerFill {
    std::size_t event;

    //the side the maker was on, not the side of the order that hit it
    Side side;

    Price price;
    Quantity quantity;

    //the mid just before the order that filled the maker arrived, which is what its edge is measured from
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

    //resting orders the book pulled because they would have traded against their own trader. these are
    //not cancel attempts, since nobody asked for them
    std::size_t self_trade_cancellations = 0;

    //statistics for trades executed during the simulation
    std::size_t trades = 0;
    Quantity traded_quantity = 0;

    double total_traded_value = 0.0;

    //orders sent by informed traders. they only send one when the book is mispriced, so this is also a
    //count of how often it was
    std::size_t informed_orders = 0;

    //the market maker, when there is one
    std::size_t maker_fills = 0;
    Quantity maker_volume = 0;
    std::int64_t maker_final_inventory = 0;
    std::int64_t maker_peak_inventory = 0;

    //the typical size of the maker's position over the run, the root mean square of it taken every event
    double maker_inventory_rms = 0.0;

    //cash plus whatever position is left, valued at the final mid
    double maker_pnl = 0.0;

    //what the maker made against the mid at the moment of each fill. this is the spread it earned, and
    //it is what it would have kept if the price had never moved after trading with it
    double maker_edge = 0.0;

    //everything else: what the maker made or lost from the price moving while it held a position. when
    //the traders it fills know something it does not, this is where that shows up
    double maker_inventory_pnl = 0.0;

    double average_trade_price() const;
};

class Simulator {
    public:
    //constructor for the Simulator class, which initializes the random number generator with a given seed (default is 50).
    //the config decides what kind of market it simulates, and leaving it out gives the original one
        Simulator(std::uint64_t seed = 50, SimulationConfig config = SimulationConfig{});
        SimulationStats run(std::size_t number_of_events, DataWriter& writer);

        //every fill the market maker received during the run, in order
        const std::vector<MakerFill>& maker_fills() const;

        //the maker's position and value, written down every config.maker_log_interval events
        const std::vector<MakerSnapshot>& maker_snapshots() const;

    private:
        SimulationConfig config_;

        OrderBook order_book_;

        std::mt19937_64 rng_;

        OrderId next_order_id_ = 1;
        Timestamp next_timestamp_ = 1;

        //the price the market is currently trading around
        double reference_price_ = 100.0;

        //the reference price at every event so far, kept only when the ordinary traders are working from
        //an out of date view of it
        std::vector<double> reference_history_;

        std::vector<OrderId> known_order_ids_;

        SimulationStats stats_;

        //the trader id the market maker uses. the ordinary traders are numbered from 1 and the informed
        //ones from 1001, so nobody can collide with it
        static constexpr TraderId maker_trader_id = 0;

        //the maker's two resting quotes, or 0 for a side it is not currently quoting
        OrderId maker_bid_id_ = 0;
        OrderId maker_ask_id_ = 0;

        //the prices those quotes rest at, and how much of each is still there to be filled
        Price maker_bid_price_ = 0;
        Price maker_ask_price_ = 0;
        Quantity maker_bid_remaining_ = 0;
        Quantity maker_ask_remaining_ = 0;

        std::int64_t maker_inventory_ = 0;
        double maker_cash_ = 0.0;

        //where the maker centres its quotes. it is the mid whenever the book has one, and otherwise the
        //last mid the maker saw
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

        //lets an informed trader trade against the book if it has drifted away from the reference price.
        void process_informed(std::size_t event_number, DataWriter& writer);

        //processes the cancellation of an order in the order book and updates the simulation statistics accordingly.
        void process_cancel();

        //takes down the market maker's quotes and puts up new ones around the current mid.
        void requote_market_maker();

        //keeps one side of the maker at the price it wants, and only touches the book if that changed.
        void update_maker_quote(Side side, bool show, Price wanted);

        //updates the maker's position, cash and edge if it was on one side of this trade.
        void record_maker_fill(const Trade& trade, std::size_t event_number, double mid_before);

        //returns the price for a limit order, placed a short way off the reference price on the
        //passive side for its direction.
        Price random_price(Side side);

        //returns how far from the reference price an order is placed.
        Price random_offset();

        //moves the reference price by a small random step, so that resting orders are eventually
        //overtaken by the market instead of sitting in the book forever.
        void step_reference_price();

        //generates a random quantity for an order within a specified range (e.g., 1 to 100).
        Quantity random_quantity();

        //generates a random trader ID for an order, ensuring that each trader has a unique ID.
        std::uint64_t random_trader_id();

        //generates a random trader ID for an informed trader, from a range the ordinary traders do not use.
        TraderId random_informed_trader_id();
    };
