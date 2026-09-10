#include "simulator.hpp"

#include <algorithm>
#include <cmath>

Simulator::Simulator(std::uint64_t seed, SimulationConfig config)
    : config_(config),
      rng_(seed) {
}

const std::vector<MakerFill>& Simulator::maker_fills() const {
    return maker_fills_;
}

const std::vector<MakerSnapshot>& Simulator::maker_snapshots() const {
    return maker_snapshots_;
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

    if (config_.noise_view_lag > 0) {
        reference_history_.push_back(reference_price_);
    }
}

//most orders arrive at or near the reference price, with a thinner tail further out
Price Simulator::random_offset() {
    std::geometric_distribution<int> distribution(offset_tightness);
    return static_cast<Price>(distribution(rng_));
}

//a buy is placed at or below the anchor and a sell at or above it, so an order only trades when it is
//priced aggressively enough to reach the other side
Price Simulator::random_price(Side side) {
    Price offset = random_offset();

    //an ordinary trader prices off the reference as it stood noise_view_lag events ago, which with no lag
    //is simply where it is now
    double view = reference_price_;

    if (config_.noise_view_lag > 0 && !reference_history_.empty()) {
        std::size_t back = std::min(config_.noise_view_lag, reference_history_.size() - 1);
        view = reference_history_[reference_history_.size() - 1 - back];
    }

    Price reference = static_cast<Price>(std::llround(view));

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

//informed traders are numbered apart from the ordinary ones, so the rule against trading with yourself
//never gets in the way of the two trading with each other
TraderId Simulator::random_informed_trader_id() {
    std::uniform_int_distribution<TraderId> distribution(1001, 1100);
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

//returns true if the next event should cancel a resting order rather than submit a new one
bool Simulator::should_cancel() {
    std::uniform_int_distribution<int> distribution(1, 100);
    return distribution(rng_) <= cancel_percentage;
}

//returns true if the next generated order should be a market order
bool Simulator::should_be_market() {
    std::uniform_int_distribution<int> distribution(1, 100);
    return distribution(rng_) <= config_.market_order_percentage;
}

//returns true if an informed trader should look at the book on this event
bool Simulator::should_be_informed() {
    std::uniform_int_distribution<int> distribution(1, 100);
    return distribution(rng_) <= config_.informed_percentage;
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
    //the mid the order saw when it arrived, which is what a market maker's fill is measured against
    double mid_before = order_book_.mid_price().value_or(maker_center_);

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

        if (config_.market_maker) {
            record_maker_fill(trade, event_number, mid_before);
        }
    }

    //only a good till cancelled limit order with quantity left over is resting in the book, so only
    //those can be cancelled later
    if (order.type == OrderType::Limit &&
        order.time_in_force == TimeInForce::GoodTillCancelled &&
        filled < order.remaining_quantity) {
        known_order_ids_.push_back(order.id);
    }
}

//an informed trader can see the reference price, which nobody else can. it buys whatever is offered
//below that and sells into whatever is bid above it, and when the book is fair it does nothing at all.
//its orders are immediate or cancel, because the edge is in what is already resting there, and once
//that is gone it has no reason to wait around at a price
void Simulator::process_informed(std::size_t event_number, DataWriter& writer) {
    auto best_bid = order_book_.best_bid();
    auto best_ask = order_book_.best_ask();

    Side side;
    Price price;

    if (best_ask.has_value() &&
        static_cast<double>(best_ask.value()) < reference_price_ - config_.informed_threshold) {
        side = Side::Buy;

        //willing to pay anything up to the reference price, less the margin it wants for bothering
        price = static_cast<Price>(std::floor(reference_price_ - config_.informed_threshold));
    } else if (best_bid.has_value() &&
               static_cast<double>(best_bid.value()) > reference_price_ + config_.informed_threshold) {
        side = Side::Sell;
        price = static_cast<Price>(std::ceil(reference_price_ + config_.informed_threshold));
    } else {
        return;
    }

    OrderId order_id = next_order_id_++;
    TraderId trader_id = random_informed_trader_id();
    Quantity quantity = random_quantity();

    Order order{
        order_id,
        trader_id,
        side,
        OrderType::Limit,
        price,
        quantity,
        next_timestamp_++,
        TimeInForce::ImmediateOrCancel
    };

    stats_.informed_orders++;
    process_order(order, event_number, writer);
}

//the maker leaves a quote exactly where it is if the price it wants has not changed. taking it down and
//putting it straight back up would send it to the back of the queue at that price, behind every order
//that had arrived there since, and it would almost never be the one to trade. the first version of this
//did exactly that, every event, and was filled a handful of times in a hundred thousand events
void Simulator::requote_market_maker() {
    auto best_bid = order_book_.best_bid();
    auto best_ask = order_book_.best_ask();

    if (best_bid.has_value() && best_ask.has_value()) {
        maker_center_ = (static_cast<double>(best_bid.value()) +
                         static_cast<double>(best_ask.value())) / 2.0;
    }

    //leaning both quotes the same way as the position is how the maker gets back toward flat
    double skew = -config_.maker_skew_per_unit * static_cast<double>(maker_inventory_);

    //rounding outward rather than to the nearest tick keeps the two quotes the same distance from the
    //centre. a centre ending in a half would otherwise always round the same way and push both quotes up
    Price wanted_bid = static_cast<Price>(std::floor(maker_center_ + skew - config_.maker_half_spread));
    Price wanted_ask = static_cast<Price>(std::ceil(maker_center_ + skew + config_.maker_half_spread));

    std::int64_t size = static_cast<std::int64_t>(config_.maker_quote_size);
    bool show_bid = maker_inventory_ + size <= config_.maker_inventory_limit;
    bool show_ask = maker_inventory_ - size >= -config_.maker_inventory_limit;

    //the bid is kept a tick short of the best offer, so it can only ever rest and never trade on arrival
    best_ask = order_book_.best_ask();
    if (best_ask.has_value()) {
        wanted_bid = std::min(wanted_bid, best_ask.value() - 1);
    }

    update_maker_quote(Side::Buy, show_bid, wanted_bid);

    //the offer is worked out after the bid has settled, so that it cannot land on or under the bid
    //the maker has just put up, which would have the maker trading with itself
    best_bid = order_book_.best_bid();
    if (best_bid.has_value()) {
        wanted_ask = std::max(wanted_ask, best_bid.value() + 1);
    }

    update_maker_quote(Side::Sell, show_ask, wanted_ask);
}

void Simulator::update_maker_quote(Side side, bool show, Price wanted) {
    OrderId& id = (side == Side::Buy) ? maker_bid_id_ : maker_ask_id_;
    Price& price = (side == Side::Buy) ? maker_bid_price_ : maker_ask_price_;
    Quantity& remaining = (side == Side::Buy) ? maker_bid_remaining_ : maker_ask_remaining_;

    //still resting at the right price, so it keeps its place in the queue
    if (show && id != 0 && price == wanted) {
        return;
    }

    if (id != 0) {
        order_book_.cancel_order(id);
        id = 0;
        remaining = 0;
    }

    if (!show) {
        return;
    }

    Order quote{
        next_order_id_++,
        maker_trader_id,
        side,
        OrderType::Limit,
        wanted,
        config_.maker_quote_size,
        next_timestamp_++
    };

    order_book_.submit(quote);

    id = quote.id;
    price = wanted;
    remaining = config_.maker_quote_size;
}

//the maker's edge on a fill is how far the price was from the mid in its favour: below the mid for a
//buy and above it for a sell. its cash and position move the ordinary way
void Simulator::record_maker_fill(const Trade& trade, std::size_t event_number, double mid_before) {
    Side maker_side;

    if (trade.buyer_id == maker_trader_id) {
        maker_side = Side::Buy;
    } else if (trade.seller_id == maker_trader_id) {
        maker_side = Side::Sell;
    } else {
        return;
    }

    double price = static_cast<double>(trade.price);
    double quantity = static_cast<double>(trade.quantity);

    if (maker_side == Side::Buy) {
        maker_inventory_ += static_cast<std::int64_t>(trade.quantity);
        maker_cash_ -= price * quantity;
        stats_.maker_edge += (mid_before - price) * quantity;
    } else {
        maker_inventory_ -= static_cast<std::int64_t>(trade.quantity);
        maker_cash_ += price * quantity;
        stats_.maker_edge += (price - mid_before) * quantity;
    }

    if (trade.resting_order_id == maker_bid_id_) {
        maker_bid_remaining_ -= trade.quantity;

        if (maker_bid_remaining_ == 0) {
            maker_bid_id_ = 0;
        }
    } else if (trade.resting_order_id == maker_ask_id_) {
        maker_ask_remaining_ -= trade.quantity;

        if (maker_ask_remaining_ == 0) {
            maker_ask_id_ = 0;
        }
    }

    stats_.maker_fills++;
    stats_.maker_volume += trade.quantity;

    maker_fills_.push_back({event_number, maker_side, trade.price, trade.quantity, mid_before});
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

        //the maker refreshes its quotes before anything else happens, so it is always showing a price
        //based on the book as it stands
        if (config_.market_maker) {
            requote_market_maker();
        }

        if (should_cancel() && !known_order_ids_.empty()) {
            process_cancel();

        } else {
            Order order = generate_order();
            process_order(order, event_number, writer);
        }

        //informed traders act on top of the ordinary flow rather than in place of some of it, so the
        //ordinary traders behave the same way whether or not there is anybody informed in the market.
        //the check on the percentage comes first so that the original model never draws the random
        //number, and its results stay exactly as they were
        if (config_.informed_percentage > 0 && should_be_informed()) {
            process_informed(event_number, writer);
        }

        if (config_.market_maker) {
            double inventory = static_cast<double>(maker_inventory_);
            inventory_square_sum_ += inventory * inventory;

            std::int64_t magnitude = maker_inventory_ < 0 ? -maker_inventory_ : maker_inventory_;
            stats_.maker_peak_inventory = std::max(stats_.maker_peak_inventory, magnitude);

            if (config_.maker_log_interval > 0 && event_number % config_.maker_log_interval == 0) {
                maker_snapshots_.push_back({
                    event_number,
                    maker_inventory_,
                    maker_cash_,
                    order_book_.mid_price().value_or(maker_center_),
                    reference_price_
                });
            }
        }
    }

    //the book counts these as it matches, so they are collected at the end rather than event by event
    stats_.self_trade_cancellations = order_book_.self_trade_cancellations();

    if (config_.market_maker && number_of_events > 0) {
        double final_mid = order_book_.mid_price().value_or(maker_center_);

        stats_.maker_final_inventory = maker_inventory_;
        stats_.maker_pnl = maker_cash_ + static_cast<double>(maker_inventory_) * final_mid;
        stats_.maker_inventory_pnl = stats_.maker_pnl - stats_.maker_edge;
        stats_.maker_inventory_rms =
            std::sqrt(inventory_square_sum_ / static_cast<double>(number_of_events));
    }

    return stats_;
}
